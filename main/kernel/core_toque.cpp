// CORE - o toque: o vigia do GT911 (anomalias e a lupa), a leitura continua
// enquanto se digita, os dedos da ultima amostra e a bancada que simula toque,
// gesto e abertura de app pelo console. Partido de core.cpp (o motor); o que
// as partes compartilham esta em core_interno.hpp.

#include "esp_log.h"
#include "driver/gpio.h"
#include "core_interno.hpp"

// ---------------------------------------------------------------------------
// VIGIA DO TOQUE (o toque ficava surdo por um tempo, voltava sozinho, e o
// log nao mostrava nada)
// ---------------------------------------------------------------------------
// O toque podia ficar surdo SEM erro nenhum: o driver do M5GFX nao loga falha
// de I2C, e quando a leitura falha ele devolve o ULTIMO dado lido, calado. Tres
// jeitos de ficar surdo, e a olho nao da para saber qual:
//   1. o I2C do GT911 falhando (barramento interno disputado ou preso);
//   2. o GT911 com um relatorio esperando que ninguem le (o pino INT nao
//      pulsa de novo enquanto o buffer dele nao for limpo);
//   3. pulsos chegando e a leitura voltando sem dedo.
// Uma vez por segundo, com a tela acesa, o vigia le o registrador de estado do
// GT911 DIRETO (0x814E - bit 7 "relatorio pronto", bits 0-3 dedos), cruza com
// o que o sistema viu e com os pulsos da ISR. Anomalia vira LOG (W, que vai
// para o cartao na hora) e a recuperacao e a mais barata que existe: forcar a
// proxima leitura pelo I2C, que limpa o buffer do GT911. Mesma tarefa que le
// o toque - nunca duas leituras dele ao mesmo tempo.
uint32_t s_vig_falhas = 0, s_vig_presos = 0, s_vig_surdos = 0, s_vig_forcados = 0;
uint32_t s_vig_reinicios = 0;
uint8_t  s_vig_status = 0;

// LUPA. Os avisos de "pulsos sem dedo" vieram so com a tela
// mudando sem parar (Camera, Tarefas) e com o dedo na tela - e o GT911 dizendo
// "zero dedos" com o I2C bom. Suspeita: ruido do painel, e o filtro do GT911
// jogando o toque fora. Para provar, a lupa olha o byte de estado a CADA
// volta por 3 s depois de uma anomalia (so le - quem limpa continua sendo o
// driver) e conta: relatorio com dedo, relatorio vazio, bit 6 ("toque grande"
// - o GT911 marca assim o que ele rejeita como palma/ruido), e sem relatorio.
static uint32_t s_lupa_ate = 0;
static uint32_t s_lupa_com = 0, s_lupa_vazio = 0, s_lupa_grande = 0, s_lupa_nada = 0, s_lupa_falha = 0;

void lupa_toque(uint32_t t, bool tocando)
{
  if (!s_lupa_ate || !tab5_painel_toque_gt911()) { return; }
  if ((int32_t)(t - s_lupa_ate) >= 0) {
    ESP_LOGW("toque", "lupa 3 s: %lu relatorio(s) com dedo, %lu vazio(s), %lu marcado(s) 'grande/ruido', "
                      "%lu volta(s) sem relatorio, %lu falha(s) de I2C",
             (unsigned long)s_lupa_com, (unsigned long)s_lupa_vazio, (unsigned long)s_lupa_grande,
             (unsigned long)s_lupa_nada, (unsigned long)s_lupa_falha);
    s_lupa_ate = 0;
    return;
  }
  auto* tp = M5.Display.touch();
  if (!tp) { return; }
  const auto c = tp->config();
  const uint8_t reg[2] = { 0x81, 0x4E };
  uint8_t st = 0;
  if (!lgfx::i2c::transactionWriteRead(c.i2c_port, c.i2c_addr, reg, 2, &st, 1, c.freq).has_value()) { ++s_lupa_falha; return; }
  if (!(st & 0x80))      { ++s_lupa_nada; }
  else if (st & 0x40)    { ++s_lupa_grande; }
  else if (st & 0x0F)    { ++s_lupa_com; }
  else                   { ++s_lupa_vazio; }
  (void)tocando;
}

// GT911 MUDO (13-14/11/2026): ele passou a pulsar o INT ~10x/s e o byte de
// estado NUNCA mostrava relatorio pronto (a lupa: 0 com dedo, 0 vazio, ~120
// voltas sem relatorio em 3 s). Forcar leitura nao adiantava; o aparelho
// entrou em repouso assim e o toque nao acordava mais a tela - so voltou
// reiniciando tudo. A recuperacao e a do boot: o pino de reset do chip, no HAL
// (tab5_toque_reiniciar). No maximo uma vez a cada 30 s, para um chip com
// defeito de verdade nao virar um laco de reinicios.
static void reiniciar_gt911(uint32_t t, const char* onde, uint32_t pulsos)
{
  static uint32_t ultimo = 0;
  if (ultimo && (t - ultimo) < 30000) { return; }
  ultimo = t;
  ++s_vig_reinicios;
  ESP_LOGW("toque", "GT911 mudo (%s): %lu pulso(s) sem relatorio nenhum - reiniciando o chip (%lu vez(es) desde o boot)",
           onde, (unsigned long)pulsos, (unsigned long)s_vig_reinicios);
  tab5_toque_reiniciar();
  s_toque_pulso = true;   // a proxima volta ja le o chip novo
}

// Pedido do console (outra tarefa): so a bandeira; quem reinicia e a
// interface, em vigiar_toque_repouso (roda em toda volta, acesa ou nao).
static std::atomic<bool> s_pedir_reinicio{false};
void sistema_pedir_reinicio_toque(void)
{
  s_pedir_reinicio = true;
  sistema_acordar_interface();
}

// RAIO-X DO GT911 (console 'toque regs'): o que o chip diz de si. Para
// separar "chip vivo mas sem sentir o dedo" de "configuracao estragada" e de
// "fio/painel". Le, nao escreve nada (a configuracao fica na flash do GT911).
//   0x8140..43 "911\0"  identificacao       0x8144..45 versao do firmware
//   0x8146..49 resolucao X/Y                0x814A     fornecedor do sensor
//   0x8047..0x80FE configuracao (184 bytes), 0x80FF a soma dela
//   0x814E estado: bit 7 relatorio pronto, bits 0-3 dedos
static std::atomic<bool> s_pedir_regs{false};
void sistema_pedir_regs_toque(void)
{
  s_pedir_regs = true;
  sistema_acordar_interface();
}

static bool gt911_ler(uint16_t reg, uint8_t* buf, size_t n)
{
  auto* tp = M5.Display.touch();
  if (!tp) { return false; }
  const auto c = tp->config();
  const uint8_t r[2] = { (uint8_t)(reg >> 8), (uint8_t)reg };
  return lgfx::i2c::transactionWriteRead(c.i2c_port, c.i2c_addr, r, 2, buf, n, c.freq).has_value();
}

static void gt911_raio_x(void)
{
  if (!tab5_painel_toque_gt911()) { ESP_LOGW("toque", "regs: a tela nao e GT911"); return; }
  const auto c = M5.Display.touch()->config();
  uint8_t id[11] = {0};
  if (!gt911_ler(0x8140, id, 11)) {
    ESP_LOGW("toque", "regs: GT911 NAO respondeu no I2C (endereco 0x%02X, pino INT=%d)",
             c.i2c_addr, gpio_get_level(GPIO_NUM_23));
    return;
  }
  ESP_LOGW("toque", "regs: id '%c%c%c%c' firmware 0x%02X%02X resolucao %ux%u fornecedor 0x%02X (endereco 0x%02X, pino INT=%d)",
           id[0] ? id[0] : '?', id[1] ? id[1] : '?', id[2] ? id[2] : '?', id[3] ? id[3] : '?',
           id[5], id[4], id[6] | (id[7] << 8), id[8] | (id[9] << 8), id[10],
           c.i2c_addr, gpio_get_level(GPIO_NUM_23));
  uint8_t cfg[185] = {0};
  if (!gt911_ler(0x8047, cfg, sizeof(cfg))) { ESP_LOGW("toque", "regs: a leitura da configuracao falhou"); return; }
  uint8_t soma = 0;
  for (int i = 0; i < 184; ++i) { soma += cfg[i]; }
  soma = (uint8_t)(~soma + 1);
  ESP_LOGW("toque", "regs: configuracao v%u, saida %ux%u, %u dedo(s), modulo 0x%02X, soma %s (lida 0x%02X, calculada 0x%02X)",
           cfg[0], cfg[1] | (cfg[2] << 8), cfg[3] | (cfg[4] << 8), cfg[5] & 0x0F, cfg[6],
           soma == cfg[184] ? "OK" : "ERRADA", cfg[184], soma);
  // A configuracao inteira, 16 bytes por linha (para comparar com outro Tab5).
  for (int i = 0; i < 184; i += 16) {
    char linha[16 * 3 + 1];
    int n = 0;
    for (int j = i; j < i + 16 && j < 184; ++j) { n += snprintf(linha + n, sizeof(linha) - n, "%02X ", cfg[j]); }
    ESP_LOGW("toque", "regs: 0x%04X: %s", 0x8047 + i, linha);
  }
  uint8_t st = 0;
  gt911_ler(0x814E, &st, 1);
  ESP_LOGW("toque", "regs: estado 0x%02X, %lu pulso(s) desde o boot", st, (unsigned long)s_pulsos);
}

static void lupa_ligar(uint32_t t)
{
  if (s_lupa_ate) { return; }
  s_lupa_ate = t + 3000;
  s_lupa_com = s_lupa_vazio = s_lupa_grande = s_lupa_nada = s_lupa_falha = 0;
}

void vigiar_toque(uint32_t t, bool tocando)
{
  // Os registradores lidos aqui sao do GT911. Numa tela TDDI (ST7123/ST7121)
  // o endereco do toque e outro chip, com outro mapa: ler 0x814E la seria
  // escrever um ponteiro de registrador sem sentido no controlador da tela.
  if (!tab5_painel_toque_gt911()) { return; }
  static uint32_t prox = 0, pulsos_ant = 0, dedo_desde = 0, log_falha_ms = 0;
  static bool viu_dedo = false, avisou_dedo_longo = false;
  static int presos_seguidos = 0;
  static int mudos_seguidos = 0;

  // CARENCIA DO BOOT. Na subida, o proprio driver do GT911 usa o
  // pino INT como SAIDA (o pulso de wakeup() que escolhe o endereco I2C) e o
  // painel e montado logo em seguida: a ISR contou 282 bordas num segundo sem
  // dedo nenhum, e a lupa confirmou - zero relatorios do GT911. Ruido de
  // partida, nao toque. O vigia so comeca 5 s depois da primeira volta, com a
  // contagem de pulsos zerada nesse instante.
  static bool armado = false;
  if (!armado) {
    if (!prox) { prox = t + 5000; }
    if ((int32_t)(t - prox) < 0) { return; }
    armado = true;
    pulsos_ant = s_pulsos;
    viu_dedo = tocando;
    prox = t + 1000;
    return;
  }

  if (tocando) { viu_dedo = true; if (!dedo_desde) { dedo_desde = t; } }
  else         { dedo_desde = 0; avisou_dedo_longo = false; }
  if ((int32_t)(t - prox) < 0) { return; }
  prox = t + 1000;

  const uint32_t total = s_pulsos;
  const uint32_t pulsos = total - pulsos_ant;
  pulsos_ant = total;
  const bool dedo_na_janela = viu_dedo;
  viu_dedo = tocando;

  auto* tp = M5.Display.touch();
  if (!tp) { return; }
  const auto c = tp->config();
  const uint8_t reg[2] = { 0x81, 0x4E };
  uint8_t st = 0;
  const bool ok = lgfx::i2c::transactionWriteRead(c.i2c_port, c.i2c_addr, reg, 2, &st, 1, c.freq).has_value();
  const int pino = gpio_get_level(GPIO_NUM_23);
  s_vig_status = st;

  if (!ok) {
    ++s_vig_falhas;
    s_toque_pulso = true;   // tenta pelo caminho do driver tambem
    if (s_vig_falhas == 1 || (t - log_falha_ms) >= 10000) {
      log_falha_ms = t;
      ESP_LOGW("toque", "I2C do GT911 NAO respondeu (%lu falha(s) desde o boot; pino INT=%d, %lu pulso(s)/s)",
               (unsigned long)s_vig_falhas, pino, (unsigned long)pulsos);
    }
    return;
  }

  // 2. Relatorio COM dedo parado no GT911, e o sistema sem dedo - duas sondas
  // seguidas (um toque que acabou de descer entre a leitura e a sonda e normal).
  if ((st & 0x80) && (st & 0x0F) && !tocando) {
    if (++presos_seguidos >= 2) {
      ++s_vig_presos; ++s_vig_forcados;
      s_toque_pulso = true;
      lupa_ligar(t);
      ESP_LOGW("toque", "GT911 com %u dedo(s) esperando e o sistema sem dedo ha %d s "
                    "(status %02X, pino INT=%d, %lu pulso(s)/s) - forcando leitura",
               (unsigned)(st & 0x0F), presos_seguidos, st, pino, (unsigned long)pulsos);
    }
  } else {
    presos_seguidos = 0;
  }

  // 3. Pulsos chegando (o GT911 esta vendo dedo) e nenhum dedo lido o segundo
  // inteiro. Um toque normal pulsa ~10-100x/s; uma soltura, poucas vezes.
  if (pulsos >= 10 && !dedo_na_janela) {
    ++s_vig_surdos; ++s_vig_forcados;
    s_toque_pulso = true;
    lupa_ligar(t);
    ESP_LOGW("toque", "%lu pulso(s) em 1 s e NENHUM dedo lido (status %02X, pino INT=%d) - forcando leitura",
             (unsigned long)pulsos, st, pino);
  }

  // 4. MUDO (ver reiniciar_gt911): pulsos, nenhum dedo e nenhum relatorio
  // pronto, tres segundos seguidos. O reinicio pulsa o INT: a contagem
  // recomeca dali, para os pulsos dele nao contarem na janela seguinte.
  if (pulsos >= 10 && !dedo_na_janela && !(st & 0x80)) {
    if (++mudos_seguidos >= 3) {
      reiniciar_gt911(t, "tela acesa", pulsos);
      mudos_seguidos = 0;
      pulsos_ant = s_pulsos;
    }
  } else {
    mudos_seguidos = 0;
  }

  // O avesso: dedo "na tela" sem parar - leitura velha repetida (o driver
  // devolve o ultimo dado quando falha). Um aviso por episodio.
  if (dedo_desde && !avisou_dedo_longo && (t - dedo_desde) > 15000) {
    avisou_dedo_longo = true;
    ESP_LOGW("toque", "dedo lido SEM PARAR ha 15 s (status %02X, pino INT=%d, %lu pulso(s)/s) - dedo fantasma?",
             st, pino, (unsigned long)pulsos);
  }
}

// O MESMO MUDO EM REPOUSO. Ali o vigia de cima nao roda (a tela apagada nao
// pode ler o I2C a cada segundo a toa), e era justo em repouso que o defeito
// prendia o aparelho. Em repouso um toque de verdade acorda a tela na hora;
// entao pulsos chegando por 10 s sem dedo nenhum ja e o sinal. So nesse caso
// le o byte de estado (uma leitura a cada 10 s, no maximo) e, sem relatorio,
// reinicia o chip. Acordado, a janela fica zerada.
void vigiar_toque_repouso(uint32_t t, bool tocando, bool em_repouso)
{
  static uint32_t desde = 0, pulsos_ini = 0;
  static bool viu_dedo = false;
  if (s_pedir_regs.exchange(false)) { gt911_raio_x(); }
  if (s_pedir_reinicio.exchange(false)) {
    ESP_LOGW("toque", "reinicio do GT911 pedido pelo console");
    if (tab5_toque_reiniciar()) { ++s_vig_reinicios; }
    s_toque_pulso = true;
  }
  if (!em_repouso || !tab5_painel_toque_gt911()) { desde = 0; return; }
  if (!desde) { desde = t; pulsos_ini = s_pulsos; viu_dedo = tocando; return; }
  if (tocando) { viu_dedo = true; }
  if ((t - desde) < 10000) { return; }
  const uint32_t pulsos = s_pulsos - pulsos_ini;
  const bool teve_dedo = viu_dedo;
  desde = t; pulsos_ini = s_pulsos; viu_dedo = false;
  if (pulsos < 5 || teve_dedo) { return; }

  auto* tp = M5.Display.touch();
  if (!tp) { return; }
  const auto c = tp->config();
  const uint8_t reg[2] = { 0x81, 0x4E };
  uint8_t st = 0;
  if (!lgfx::i2c::transactionWriteRead(c.i2c_port, c.i2c_addr, reg, 2, &st, 1, c.freq).has_value()) { return; }
  if (!(st & 0x80)) {
    reiniciar_gt911(t, "repouso", pulsos);
    pulsos_ini = s_pulsos;
  }
}

// LEITURA CONTINUA ENQUANTO SE DIGITA ('continuo'). O driver do GT911 tem
// dois caminhos: lido ha menos de 128 ms, uma leitura simples; lido ha mais,
// ele DESCARTA o relatorio pendente e espera o seguinte (ate ~24 ms parado,
// com a tarefa de interface presa). O relatorio descartado e o do DEDO
// DESCENDO: num toque curto o seguinte ja e o da soltura, e a tecla some sem
// rastro. Era o Enter das Notas que "nao pegava" (04/11/2026): as letras saem
// em rajada, mas o Enter vem depois de uma pausa - e com mais de 500 ms sem
// dedo o loop dormia esperando o pino, a leitura passava dos 128 ms e o toque
// seguinte caia no descarte.
// Agora, COM O TECLADO ABERTO (navegacao travada), a leitura vai ao I2C em
// toda volta e a volta nunca dorme mais que TOQUE_FRESCO_MS: o driver fica
// sempre no caminho simples. Sem dedo, cada volta e uma leitura de status (o
// GT911 diz "nada novo" e o driver nao espera). Fora do teclado, como antes:
// a tela parada dorme ate o pino.
void ajustar_leitura_do_toque(bool continuo)
{
  auto* tp = M5.Display.touch();
  if (!tp || !tab5_painel_toque_gt911()) { return; }   // so o driver do GT911 olha o pino
  const bool pulso = s_toque_pulso;
  s_toque_pulso = false;   // antes da leitura: um pulso durante ela vale para a proxima
  const int pino = (s_dedo_na_tela || pulso || continuo) ? -1 : (int)GPIO_NUM_23;
  auto c = tp->config();
  if (c.pin_int != pino) { c.pin_int = pino; tp->config(c); }
}
// Os dedos da ultima amostra (ver sistema_toques em app.h). So a tarefa de
// interface escreve e so ela le (dentro do ao_tocar_nivel): sem trava.
sistema_toque_t s_toques[SISTEMA_TOQUES_MAX];
int             s_n_toques = 0;

// O toque em curso ja foi tratado por nivel (sistema_consumir_toque, app.h).
// Lido e zerado pelo reconhecedor do clique, na soltura.
bool s_toque_consumido = false;
void sistema_consumir_toque(void) { s_toque_consumido = true; }

int sistema_toques(sistema_toque_t* out, int max)
{
  const int n = s_n_toques < max ? s_n_toques : max;
  for (int i = 0; i < n; ++i) { out[i] = s_toques[i]; }
  return n;
}
// BANCADA: toque e abertura de app pedidos pelo console (outra tarefa). A
// interface consome na volta seguinte, como se o dedo tivesse tocado - para
// testar uma tela pela serial sem ninguem no aparelho.
volatile int s_sim_x = -1, s_sim_y = -1;
const app_t* volatile s_sim_abrir = nullptr;
volatile bool s_sim_fechar = false;
void sistema_simular_toque(int x, int y) { s_sim_y = y; s_sim_x = x; }
volatile int s_simg[4];
volatile bool s_simg_pedido = false;
void sistema_simular_gesto(int x0, int y0, int x1, int y1)
{
  s_simg[0] = x0; s_simg[1] = y0; s_simg[2] = x1; s_simg[3] = y1;
  s_simg_pedido = true;
  sistema_acordar_interface();
}
void sistema_simular_abrir(const app_t* app) { if (app) { s_sim_abrir = app; } else { s_sim_fechar = true; } }
