#include "tab5_imu.hpp"
#include "tab5_hora.hpp"   // tab5_agora_ms
#include "tab5_mutex.hpp"
#include <M5Unified.h>
#include "utility/imu/BMI270_Class.hpp"   // DEFAULT_ADDRESS (chip_responde)
#include "tab5_power.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "imu";

static tab5_imu_dados_t s_cache = { };

// Lazy-create, nunca destruida - mesmo molde de services/notificacao.cpp e
// tab5_gps.cpp: o primeiro uso sempre vem antes de qualquer disputa de
// verdade (o boot chama tab5_imu_medir() antes de qualquer app poder ler).
static Trava s_trava;

static inline void travar(void)   { s_trava.travar(); }
static inline void destravar(void) { s_trava.destravar(); }

// A IMU QUE NAO SUBIU NO BOOT. O M5.begin() inicia o BMI270 uma vez so, e
// esse inicio carrega um firmware de 8 KB no chip pelo I2C; de vez em quando
// ele falha (o chip ainda saindo do reset, uma transacao perdida) e a IMU
// ficava fora a sessao inteira - o app mostrava "IMU nao disponivel neste
// boot" ate reiniciar. Aqui o sistema tenta de novo, algumas vezes,
// espacado: cada tentativa ocupa o barramento por uns 200 ms.
#define IMU_TENTATIVAS   5
#define IMU_ESPERA_MS 3000

// A IMU QUE PAROU DEPOIS DE SUBIR: esta quantidade de medidas seguidas sem
// amostra nova (1 por segundo) conta como "parou de responder" e dispara a
// mesma nova tentativa. O limite de tentativas vale por episodio: zera
// quando a IMU volta a responder.
#define IMU_FALHAS_SEGUIDAS 5
static int s_tentativas = 0;
static int s_falhas = 0;

// O BMI270 responde no I2C? Le o CHIP_ID (0x00) pelo endereco padrao do
// driver (BMI270_Class::DEFAULT_ADDRESS). So para distinguir "sem amostra
// nova" de "chip mudo" - ver tab5_imu_medir.
static bool chip_responde(void)
{
  uint8_t id = 0;
  return M5.In_I2C.readRegister(m5::BMI270_Class::DEFAULT_ADDRESS, 0x00, &id, 1, 400000) && id != 0;
}

static bool subir_de_novo(void)
{
  static uint32_t s_proxima_ms = 0;
  if (!tab5_power_imu_pedida() || s_tentativas >= IMU_TENTATIVAS) { return false; }
  const uint32_t agora = tab5_agora_ms();
  if ((int32_t)(agora - s_proxima_ms) < 0) { return false; }
  s_proxima_ms = agora + IMU_ESPERA_MS;
  ++s_tentativas;
  const bool ok = M5.Imu.begin(&M5.In_I2C, M5.getBoard());
  if (ok) { ESP_LOGI(TAG, "IMU subiu na tentativa %d depois do boot", s_tentativas); }
  else    { ESP_LOGW(TAG, "IMU ainda fora (tentativa %d de %d)", s_tentativas, IMU_TENTATIVAS); }
  return ok;
}

// O GIROSCOPIO (tab5_imu.hpp): os pedidos (bits) e o estado que o chip tem.
// -1 = nao se sabe - o boot e cada M5.Imu.begin() deixam o chip em desempenho
// com giroscopio, entao a primeira medida sempre aplica o que foi pedido.
#define GIRO_PRAZO_BERRY_MS 5000
static uint32_t          s_giro_pedidos = 0;
static uint32_t          s_giro_berry_ms = 0;
static int               s_giro_no_chip = -1;

void tab5_imu_giroscopio(uint32_t quem, bool sim)
{
  if (sim) { __atomic_fetch_or(&s_giro_pedidos, quem, __ATOMIC_ACQ_REL); }
  else     { __atomic_fetch_and(&s_giro_pedidos, ~quem, __ATOMIC_ACQ_REL); }
}

void tab5_imu_giroscopio_berry(void)
{
  __atomic_store_n(&s_giro_berry_ms, tab5_agora_ms(), __ATOMIC_RELEASE);
  tab5_imu_giroscopio(TAB5_GIRO_BERRY, true);
}

// Na tarefa de interface, antes de medir: o pedido do Berry vencido cai, e o
// chip passa ao estado pedido se ainda nao estiver nele.
static void aplicar_giroscopio(void)
{
  uint32_t p = __atomic_load_n(&s_giro_pedidos, __ATOMIC_ACQUIRE);
  if ((p & TAB5_GIRO_BERRY)
   && tab5_agora_ms() - __atomic_load_n(&s_giro_berry_ms, __ATOMIC_ACQUIRE) > GIRO_PRAZO_BERRY_MS) {
    tab5_imu_giroscopio(TAB5_GIRO_BERRY, false);
    p &= ~TAB5_GIRO_BERRY;
  }
  const int quer = p ? 1 : 0;
  if (quer == s_giro_no_chip) { return; }
  if (tab5_power_imu_giroscopio(quer)) { s_giro_no_chip = quer; }
}

void tab5_imu_medir(void)
{
  // IMU fora do ar (nao subiu no boot) ou muda depois de ter subido: tenta
  // o begin() de novo, espacado e com limite por episodio.
  if (!M5.Imu.isEnabled() || s_falhas >= IMU_FALHAS_SEGUIDAS) {
    if (subir_de_novo()) { s_giro_no_chip = -1; }   // o begin() voltou o chip ao desempenho
    else if (!M5.Imu.isEnabled()) { return; }
  }
  aplicar_giroscopio();

  // update() == 0 quer dizer "sem amostra nova" - e o driver devolve isso
  // TAMBEM quando a leitura I2C falha (e deixa a ultima amostra). Nao da para
  // tratar todo zero como falha: um app que le a IMU depressa (app_imu,
  // orientacao) pode ter acabado de consumir a amostra. Entao, no zero,
  // pergunta-se o CHIP_ID (registrador 0x00): respondeu = so nao havia
  // amostra nova, o cache de 1 s atras segue valendo; nao respondeu = falha
  // de verdade, e o cache vai invalido e zerado em vez de repetir um valor
  // velho como se fosse atual.
  if (M5.Imu.update() == 0 && !chip_responde()) {
    if (++s_falhas == IMU_FALHAS_SEGUIDAS) {
      ESP_LOGW(TAG, "IMU parou de responder - reiniciando");   // uma vez por episodio
    }
    const tab5_imu_dados_t invalido = { };
    travar();
    s_cache = invalido;
    destravar();
    return;
  }
  if (s_falhas > 0) { ESP_LOGI(TAG, "IMU voltou a responder"); }
  s_falhas = 0;
  s_tentativas = 0;   // o episodio acabou: um novo ganha tentativas novas

  tab5_imu_dados_t d = { };
  d.valida = true;
  M5.Imu.getAccel(&d.ax, &d.ay, &d.az);
  if (s_giro_no_chip == 1) { M5.Imu.getGyro(&d.gx, &d.gy, &d.gz); }   // desligado: 0, e nao um valor velho

  travar();
  s_cache = d;
  destravar();
}

void tab5_imu_repouso(void)
{
  if (M5.Imu.isEnabled()) { aplicar_giroscopio(); }
}

bool tab5_imu_ultima(tab5_imu_dados_t* saida)
{
  if (!saida) { return false; }
  travar();
  *saida = s_cache;
  destravar();
  return saida->valida;
}

bool tab5_imu_disponivel(void)
{
  return M5.Imu.isEnabled();
}

bool tab5_imu_amostra(float a[3], float g[3])
{
  if (!M5.Imu.isEnabled()) { return false; }
  if (M5.Imu.update() == 0) { return false; }   // sem amostra nova
  M5.Imu.getAccel(&a[0], &a[1], &a[2]);
  M5.Imu.getGyro(&g[0], &g[1], &g[2]);
  return true;
}
