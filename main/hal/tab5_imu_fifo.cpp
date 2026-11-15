#include "tab5_imu_fifo.hpp"
#include <M5Unified.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "imu_fifo";

// MODO COM CABECALHO: cada quadro comeca com um byte dizendo o que vem (giro,
// acelerometro ou os dois). O giroscopio roda a 200 Hz e o acelerometro a 100
// no padrao do chip, entao os quadros alternam "so giro" e "giro + acel" - o
// cabecalho e o que permite ler isso sem adivinhar.
#define BMI_CHIP_ID     0x24
#define BMI_REG_ID      0x00
#define BMI_FIFO_LEN    0x24
#define BMI_FIFO_DATA   0x26
#define BMI_ACC_RANGE   0x41
#define BMI_GYR_CONF    0x42
#define BMI_GYR_RANGE   0x43
#define BMI_FIFO_CFG0   0x48
#define BMI_FIFO_CFG1   0x49
#define BMI_PWR_CONF    0x7C
#define BMI_PWR_CTRL    0x7D
#define BMI_CMD         0x7E
#define BMI_FIFO_FLUSH  0xB0
#define BMI_I2C_HZ      400000
// FIFO_CONFIG_1: bit7 giro, bit6 acelerometro, bit4 cabecalho.
#define BMI_FIFO_LIGADO 0xD0
// A fila do chip tem 2 KB: o buffer do mesmo tamanho le tudo de uma vez, e
// quadro nenhum fica cortado no meio.
#define FIFO_BUF        2048
// GYR_CONF bit6: modo de baixo RUIDO do giroscopio (o padrao e o de baixo
// consumo). Custa uns decimos de mA, so com o FIFO ligado.
#define BMI_GYR_RUIDO   0x40

static uint8_t  s_bmi = 0;                     // endereco I2C; 0 = desligado
static uint8_t  s_fifo_cfg1_antes = 0;
static uint8_t* s_fifo_buf = nullptr;          // RAM interna (o I2C copia dali)
static float    s_gyr_lsb = 16.384f;           // contagens por grau/s
static float    s_acc_lsb = 4096.0f;           // contagens por g
static float    s_gyr_dt  = 0.005f;
static float    s_gyr_dt_nom = 0.005f;         // o que o registrador diz
static uint8_t  s_gyr_conf_antes = 0;
// A TAXA REAL. O relogio interno do BMI270 nao e o do P4: se ele roda 2% mais
// devagar que o nominal, cada amostra vale 2% a mais de tempo e TODO giro sai
// 2% curto - 7 graus por volta. Contando as amostras contra o esp_timer por
// alguns segundos, o passo verdadeiro sai com erro de centesimos de por cento.
static int64_t  s_odr_t0 = 0;
static uint32_t s_odr_n = 0;
#define ODR_MEDIR_S   4.0f
#define ODR_TOL       0.06f
// Ordem dentro do quadro "giro + acel". O datasheet diz giro primeiro; a
// calibracao parada confirma (o acelerometro marca ~1 g, o giro ~0) e corrige
// se o chip discordar - errar isto trocaria giro por gravidade em silencio.
static int      s_ordem_votos = 0;
static bool     s_acel_primeiro = false;
static tab5_imu_fifo_diag_t s_dg;              // contadores (so da tarefa de interface)

bool tab5_imu_fifo_ligado(void) { return s_bmi != 0; }

bool tab5_imu_fifo_ligar(tab5_imu_fifo_info_t* info)
{
  s_bmi = 0;
  for (uint8_t addr : { (uint8_t)0x68, (uint8_t)0x69 }) {
    if (M5.In_I2C.readRegister8(addr, BMI_REG_ID, BMI_I2C_HZ) == BMI_CHIP_ID) { s_bmi = addr; break; }
  }
  if (!s_bmi) { return false; }

  s_fifo_buf = (uint8_t*)heap_caps_malloc(FIFO_BUF, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_fifo_buf) { s_bmi = 0; return false; }

  // Giro e acelerometro ligados (alguem pode ter deixado so o acelerometro
  // para economizar - ver tab5_power_imu_so_acelerometro).
  const uint8_t pwr = M5.In_I2C.readRegister8(s_bmi, BMI_PWR_CTRL, BMI_I2C_HZ);
  if ((pwr & 0x06) != 0x06) { M5.In_I2C.writeRegister8(s_bmi, BMI_PWR_CTRL, pwr | 0x06, BMI_I2C_HZ); }
  // Economia avancada (PWR_CONF bit0) desligada: com ela o chip pode
  // cochilar entre amostras. O M5Unified ja deixa 0; aqui so garante.
  const uint8_t pconf = M5.In_I2C.readRegister8(s_bmi, BMI_PWR_CONF, BMI_I2C_HZ);
  if (pconf & 0x01) {
    M5.In_I2C.writeRegister8(s_bmi, BMI_PWR_CONF, pconf & ~0x01, BMI_I2C_HZ);
    vTaskDelay(pdMS_TO_TICKS(2));   // o datasheet pede 450 us antes da proxima escrita
  }

  // Escalas e taxa: lidas do chip, nao supostas - o M5Unified nao as muda
  // hoje, mas se mudar o rumo continua certo.
  const uint8_t ar = M5.In_I2C.readRegister8(s_bmi, BMI_ACC_RANGE, BMI_I2C_HZ) & 0x03;
  const uint8_t gr = M5.In_I2C.readRegister8(s_bmi, BMI_GYR_RANGE, BMI_I2C_HZ) & 0x07;
  s_gyr_conf_antes = M5.In_I2C.readRegister8(s_bmi, BMI_GYR_CONF, BMI_I2C_HZ);
  if (!(s_gyr_conf_antes & BMI_GYR_RUIDO)) {
    M5.In_I2C.writeRegister8(s_bmi, BMI_GYR_CONF, s_gyr_conf_antes | BMI_GYR_RUIDO, BMI_I2C_HZ);
  }
  const uint8_t gc = s_gyr_conf_antes & 0x0F;
  s_acc_lsb = (float)(16384 >> ar);                          // 2/4/8/16 g
  s_gyr_lsb = 16.384f * (float)(1 << (gr > 4 ? 4 : gr));     // 2000/1000/500/250/125 dps
  const float odr = 100.0f * powf(2.0f, (float)gc - 8.0f);   // codigo 8 = 100 Hz, 9 = 200 Hz
  s_gyr_dt = (odr >= 25.0f && odr <= 3200.0f) ? 1.0f / odr : 0.005f;
  s_gyr_dt_nom = s_gyr_dt;
  s_odr_t0 = 0;
  s_odr_n = 0;
  memset(&s_dg, 0, sizeof(s_dg));

  s_fifo_cfg1_antes = M5.In_I2C.readRegister8(s_bmi, BMI_FIFO_CFG1, BMI_I2C_HZ);
  M5.In_I2C.writeRegister8(s_bmi, BMI_FIFO_CFG0, 0x00, BMI_I2C_HZ);   // fila corrente, sem relogio
  M5.In_I2C.writeRegister8(s_bmi, BMI_FIFO_CFG1, BMI_FIFO_LIGADO, BMI_I2C_HZ);
  M5.In_I2C.writeRegister8(s_bmi, BMI_CMD, BMI_FIFO_FLUSH, BMI_I2C_HZ);
  s_ordem_votos = 0;
  s_acel_primeiro = false;
  ESP_LOGI(TAG, "FIFO do BMI270 (0x%02x): giro %.0f Hz, %.1f LSB/dps, acel %.0f LSB/g", s_bmi,
           (double)(1.0f / s_gyr_dt), (double)s_gyr_lsb, (double)s_acc_lsb);
  if (info) {
    info->gyr_hz  = 1.0f / s_gyr_dt;
    info->gyr_lsb = s_gyr_lsb;
    info->acc_lsb = s_acc_lsb;
  }
  return true;
}

void tab5_imu_fifo_desligar(void)
{
  if (s_bmi) {
    M5.In_I2C.writeRegister8(s_bmi, BMI_FIFO_CFG1, s_fifo_cfg1_antes, BMI_I2C_HZ);
    M5.In_I2C.writeRegister8(s_bmi, BMI_GYR_CONF, s_gyr_conf_antes, BMI_I2C_HZ);
    M5.In_I2C.writeRegister8(s_bmi, BMI_CMD, BMI_FIFO_FLUSH, BMI_I2C_HZ);
  }
  s_bmi = 0;
  if (s_fifo_buf) { heap_caps_free(s_fifo_buf); s_fifo_buf = nullptr; }
}

static inline void tripla(const uint8_t* p, float out[3], float escala)
{
  for (int i = 0; i < 3; ++i) { out[i] = (float)(int16_t)(p[2 * i] | (p[2 * i + 1] << 8)) / escala; }
}

// Confere a ordem giro/acel num quadro com os dois, so parado (calibrando): o
// que marca ~1 g e o acelerometro. 5 votos seguidos contra o datasheet trocam.
static void votar_ordem(const float g[3], const float a[3])
{
  const float ng = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]) * s_gyr_lsb / s_acc_lsb;
  const float na = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
  if (na > 0.7f && na < 1.3f && ng < 0.2f) {
    if (s_ordem_votos < 5) { ++s_ordem_votos; }
  } else if (ng > 0.7f && ng < 1.3f && na < 0.2f) {
    if (--s_ordem_votos <= -5) {
      s_acel_primeiro = !s_acel_primeiro;
      s_ordem_votos = 0;
      ESP_LOGW(TAG, "FIFO: acelerometro antes do giro neste chip - corrigido");
    }
  }
}

// Percorre os quadros. Com 'cb' nulo so conta as amostras de giro (para saber
// o horario de cada uma: a ultima e "agora", as anteriores um passo antes
// cada). Devolve quantas havia, ou -1 se achou um byte que nao entende.
static int fifo_percorrer(const uint8_t* b, int n, bool parado, tab5_imu_fifo_cb cb, void* ctx,
                          int64_t t_fim, int total)
{
  const bool aplicar = cb != nullptr;
  int i = 0, k = 0;
  while (i < n) {
    const uint8_t h = b[i] & 0xFC;             // os 2 bits de baixo sao marcas de interrupcao
    if (h == 0x80) { if (aplicar) { s_dg.vazios += 1; } break; }   // fila vazia (leitura alem do fim)
    if ((h & 0xC0) == 0x80) {                  // quadro de dados: aux? giro? acel?
      const int  aux   = (h & 0x10) ? 8 : 0;
      const bool tem_g = (h & 0x08) != 0;
      const bool tem_a = (h & 0x04) != 0;
      const int  tam   = 1 + aux + (tem_g ? 6 : 0) + (tem_a ? 6 : 0);
      if (i + tam > n) { if (aplicar) { ++s_dg.cortes; } break; }   // cortado: o chip o repete na proxima leitura
      if (tem_g) {
        if (aplicar) {
          const uint8_t* p1 = b + i + 1 + aux;
          const bool troca = tem_a && s_acel_primeiro;
          float g[3], a[3];
          s_dg.quadro_tam = tam;
          memcpy(s_dg.quadro, b + i, tam < (int)sizeof(s_dg.quadro) ? tam : (int)sizeof(s_dg.quadro));
          tripla(troca ? p1 + 6 : p1, g, s_gyr_lsb);
          if (tem_a) {
            tripla(troca ? p1 : p1 + 6, a, s_acc_lsb);
            if (parado) { votar_ordem(g, a); }
          }
          const int64_t t = t_fim - (int64_t)((float)(total - 1 - k) * s_gyr_dt * 1e6f);
          cb(g, tem_a ? a : nullptr, s_gyr_dt, t, ctx);
        }
        ++k;
      }
      i += tam;
      continue;
    }
    // Quadros de controle: pulo (1), relogio (3), mudanca de configuracao (4),
    // amostra perdida (1).
    int resto = -1;
    switch (h) {
      case 0x40: resto = 1; if (aplicar) { ++s_dg.pulos; } break;   // pulo: a fila encheu
      case 0x44: resto = 3; break;
      case 0x48: resto = 4; break;
      case 0x50: resto = 1; break;
      default: break;
    }
    if (aplicar && resto >= 0) { ++s_dg.ctrl[h == 0x40 ? 0 : h == 0x44 ? 1 : h == 0x48 ? 2 : 3]; }
    if (resto < 0) { return -1; }
    i += 1 + resto;
  }
  return k;
}

void tab5_imu_fifo_ler(bool parado, tab5_imu_fifo_cb cb, void* ctx)
{
  if (!s_bmi || !cb) { return; }
  const int64_t agora = esp_timer_get_time();
  uint8_t l[2];
  if (!M5.In_I2C.readRegister(s_bmi, BMI_FIFO_LEN, l, 2, BMI_I2C_HZ)) { return; }
  int n = (l[0] | (l[1] << 8)) & 0x3FFF;
  if (n == 0) { return; }
  // Mais que o buffer so acontece depois de a interface parar ~0,5 s: le o
  // que cabe agora, o resto na proxima chamada (o horario dessa leva sai
  // adiantado, o giro integrado nao).
  const bool cheio = n > FIFO_BUF;
  if (cheio) { n = FIFO_BUF; }
  ++s_dg.lidas;
  s_dg.bytes += (uint32_t)n;
  if (cheio) { ++s_dg.cheios; }
  if (!M5.In_I2C.readRegister(s_bmi, BMI_FIFO_DATA, s_fifo_buf, n, BMI_I2C_HZ)) { s_odr_t0 = 0; return; }
  const int total = fifo_percorrer(s_fifo_buf, n, parado, nullptr, nullptr, 0, 0);
  if (total < 0) {
    ESP_LOGW(TAG, "FIFO: quadro desconhecido - esvaziando");
    ++s_dg.desconhecidos;
    M5.In_I2C.writeRegister8(s_bmi, BMI_CMD, BMI_FIFO_FLUSH, BMI_I2C_HZ);
    s_odr_t0 = 0;
    return;
  }
  const uint32_t pulos = s_dg.pulos;
  fifo_percorrer(s_fifo_buf, n, parado, cb, ctx, agora, total);
  if (s_dg.pulos != pulos) {
    ESP_LOGW(TAG, "FIFO: o chip descartou amostras (a interface parou?) - giro perdido");
  }

  // A taxa real: amostras desde t0 sobre o tempo do P4. Qualquer leitura
  // anormal (fila cheia, pulo) recomeca a conta.
  if (cheio || s_dg.pulos != pulos || s_odr_t0 == 0) {
    s_odr_t0 = agora;
    s_odr_n = 0;
    return;
  }
  s_odr_n += (uint32_t)total;
  const float dec = (float)(agora - s_odr_t0) * 1e-6f;
  if (dec >= ODR_MEDIR_S && s_odr_n > 0) {
    const float dt = dec / (float)s_odr_n;
    if (fabsf(dt / s_gyr_dt_nom - 1.0f) < ODR_TOL) {
      if (fabsf(dt / s_gyr_dt - 1.0f) > 0.002f) {
        ESP_LOGI(TAG, "taxa real do giro %.2f Hz (nominal %.0f)", (double)(1.0f / dt),
                 (double)(1.0f / s_gyr_dt_nom));
      }
      s_gyr_dt = dt;
    }
  }
}

void tab5_imu_fifo_diag(tab5_imu_fifo_diag_t* d, bool zerar)
{
  if (!d) { return; }
  *d = s_dg;
  d->acel_primeiro = s_acel_primeiro;
  d->dt = s_gyr_dt;
  if (zerar) {
    const uint32_t pulos = s_dg.pulos;   // acumulado: nao zera
    memset(&s_dg, 0, sizeof(s_dg));
    s_dg.pulos = pulos;
  }
}
