// Comandos do console - TESTES: concorrencia e simulacao (carga artificial para provar os servicos).
// (Partido de console.cpp; a tabela COMANDOS e a ajuda continuam la.)

#include "tab5_hora.hpp"   // tab5_agora_ms
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "tab5_gps.hpp"
#include "tab5_imu.hpp"
#include "tab5_tela.hpp"
#include "console/console_interno.hpp"

// ---------------------------------------------------------------------------
// SEGUNDO CONSUMIDOR DE MENTIRA (com um app so nao da para testar).
// ---------------------------------------------------------------------------
// O sistema so abre UM app por vez, entao nao da para provar que
// tab5_gps_posicao()/tab5_gps_tempo()/tab5_imu_ultima() aguentam DOIS
// leitores ao mesmo tempo so abrindo dois apps - nao existe "dois apps
// abertos" ainda. Esta tarefa e um segundo leitor de verdade, independente
// de qualquer app: martela as tres funcoes bem mais rapido que qualquer
// tela desenha, enquanto voce abre o Azimutal ou o IMU normalmente. Prova
// duas coisas: que a trava aguenta uso concorrente de verdade sem travar
// nem derrubar o sistema, e (comparando os numeros daqui com os da tela) que
// os dois leitores sempre veem o MESMO valor, nunca um "meio atualizado".
static bool s_conc_ativa = false;
static bool s_conc_parar = false;
static uint32_t s_conc_leituras = 0;

static void vTaskConcorrencia(void*)
{
  uint32_t ultimo_log = 0;
  while (!s_conc_parar) {
    nmea_posicao_t pos  = tab5_gps_posicao();
    nmea_tempo_t   hora = tab5_gps_tempo();
    tab5_imu_dados_t imu;
    tab5_imu_ultima(&imu);
    ++s_conc_leituras;
    (void)hora;

    const uint32_t agora = tab5_agora_ms();
    if (agora - ultimo_log >= 1000) {
      ultimo_log = agora;
      printf(tr("[concorrencia] %lu leituras | gps valida=%d lat=%.5f lon=%.5f "
             "| imu valida=%d ax=%.2f ay=%.2f az=%.2f\n"),
             (unsigned long)s_conc_leituras, (int)pos.valida, pos.lat, pos.lon,
             (int)imu.valida, imu.ax, imu.ay, imu.az);
    }
    // SEM vTaskDelay(0)/yield puro - 1 tick (o minimo real) ainda martela bem
    // mais rapido que qualquer app desenha (20ms de ciclo), sem virar um
    // busy-loop que rouba o core de quem esta desenhando de verdade.
    vTaskDelay(1);
  }
  s_conc_ativa = false;
  s_conc_parar = false;
  vTaskDelete(nullptr);
}

void cmd_concorrencia(int argc, char** argv)
{
  if (argc >= 2 && strcmp(argv[1], "iniciar") == 0) {
    if (s_conc_ativa) { printf(tr("ja esta rodando - %lu leituras ate agora\n"), (unsigned long)s_conc_leituras); return; }
    s_conc_ativa = true;
    s_conc_parar = false;
    s_conc_leituras = 0;
    xTaskCreatePinnedToCore(vTaskConcorrencia, "conc_teste", 4096, nullptr, 2, nullptr, tskNO_AFFINITY);
    printf("%s", tr("segundo leitor no ar - martelando GPS+IMU independente de qualquer app.\n"
           "abra o IMU (ou outro app que leia GPS/IMU) e compare os numeros com os daqui.\n"
           "'concorrencia parar' quando terminar.\n"));
    return;
  }
  if (argc >= 2 && strcmp(argv[1], "parar") == 0) {
    if (!s_conc_ativa) { printf("%s", tr("nao estava rodando\n")); return; }
    s_conc_parar = true;
    printf("%s", tr("pedido para parar - confirma na proxima volta (ate 1 tick)\n"));
    return;
  }
  printf(tr("rodando: %s | leituras ate agora: %lu\n"
         "uso: concorrencia iniciar | concorrencia parar\n"),
         s_conc_ativa ? tr("sim") : tr("nao"), (unsigned long)s_conc_leituras);
}

// ---------------------------------------------------------------------------
// TRES "APPS" DE MENTIRA, CADA UM NA SUA TAREFA
// (teste mais rigoroso: os 3 apps aparecem no console pedindo o servico).
// ---------------------------------------------------------------------------
// A diferenca para "concorrencia" acima: aquele e UM leitor generico,
// martelando o mais rapido possivel. Isto aqui e tres CONSUMIDORES
// DIFERENTES, cada um com o padrao de acesso de um app de verdade -
// cadencia propria, log com o proprio nome - rodando ao mesmo tempo, na
// MESMA tarefa de interface que qualquer app real usaria (nao, so cada um na
// SUA tarefa, independente das outras duas e de qualquer app aberto na
// tela). E o mais perto de "3 apps abertos ao mesmo tempo" que da para
// simular sem o sistema realmente suportar isso ainda.
//
// App-Mapa   : le so GPS, a cada 200ms - como uma tela de posicao/mapa.
// App-Nivel  : le so IMU, o mais rapido possivel - como um nivel-bolha.
// App-Log    : le GPS E IMU juntos, a cada 1s - como um logger periodico.
//              Este e o que mais interessa: prova que um consumidor lendo
//              DOIS servicos em sequencia nao trava (as travas de GPS e IMU
//              sao independentes, nunca uma dentro da outra - sem isso
//              haveria risco de deadlock se um dia alguem invertesse a
//              ordem em outro consumidor).
static bool     s_sim_ativa = false;
static bool     s_sim_parar = false;
static uint32_t s_sim_leituras[3] = { 0, 0, 0 };

static void vTaskSimMapa(void*)
{
  uint32_t ultimo_log = 0;
  while (!s_sim_parar) {
    nmea_posicao_t pos = tab5_gps_posicao();
    ++s_sim_leituras[0];

    const uint32_t agora = tab5_agora_ms();
    if (agora - ultimo_log >= 1000) {
      ultimo_log = agora;
      printf(tr("[app-mapa]  pedido #%-6lu -> gps valida=%d lat=%.5f lon=%.5f\n"),
             (unsigned long)s_sim_leituras[0], (int)pos.valida, pos.lat, pos.lon);
    }
    vTaskDelay(pdMS_TO_TICKS(200));   // cadencia de uma tela de posicao
  }
  vTaskDelete(nullptr);
}

static void vTaskSimNivel(void*)
{
  uint32_t ultimo_log = 0;
  while (!s_sim_parar) {
    tab5_imu_dados_t d;
    tab5_imu_ultima(&d);
    ++s_sim_leituras[1];

    const uint32_t agora = tab5_agora_ms();
    if (agora - ultimo_log >= 1000) {
      ultimo_log = agora;
      printf(tr("[app-nivel] pedido #%-6lu -> imu valida=%d ax=%.2f ay=%.2f az=%.2f\n"),
             (unsigned long)s_sim_leituras[1], (int)d.valida, d.ax, d.ay, d.az);
    }
    vTaskDelay(1);   // o mais rapido possivel, como um nivel-bolha precisa
  }
  vTaskDelete(nullptr);
}

static void vTaskSimLog(void*)
{
  while (!s_sim_parar) {
    nmea_posicao_t pos = tab5_gps_posicao();
    nmea_tempo_t   hora = tab5_gps_tempo();
    tab5_imu_dados_t imu;
    tab5_imu_ultima(&imu);
    ++s_sim_leituras[2];

    printf(tr("[app-log]   pedido #%-6lu -> gps(valida=%d) + hora(valida=%d) + imu(valida=%d, az=%.2f)\n"),
           (unsigned long)s_sim_leituras[2], (int)pos.valida, (int)hora.valida, (int)imu.valida, imu.az);
    vTaskDelay(pdMS_TO_TICKS(1000));   // cadencia de um logger periodico
  }
  vTaskDelete(nullptr);
}

void cmd_simulacao(int argc, char** argv)
{
  if (argc >= 2 && strcmp(argv[1], "iniciar") == 0) {
    if (s_sim_ativa) { printf("%s", tr("ja esta rodando\n")); return; }
    s_sim_ativa = true;
    s_sim_parar = false;
    s_sim_leituras[0] = s_sim_leituras[1] = s_sim_leituras[2] = 0;
    xTaskCreatePinnedToCore(vTaskSimMapa,  "sim_mapa",  4096, nullptr, 2, nullptr, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vTaskSimNivel, "sim_nivel", 4096, nullptr, 2, nullptr, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(vTaskSimLog,   "sim_log",   4096, nullptr, 2, nullptr, tskNO_AFFINITY);
    printf("%s", tr("3 apps de mentira no ar: app-mapa (GPS/200ms), app-nivel (IMU/o mais rapido\n"
           "possivel), app-log (GPS+IMU/1s). Abra apps de verdade agora e compare.\n"
           "'simulacao parar' quando terminar.\n"));
    return;
  }
  if (argc >= 2 && strcmp(argv[1], "parar") == 0) {
    if (!s_sim_ativa) { printf("%s", tr("nao estava rodando\n")); return; }
    s_sim_parar = true;
    s_sim_ativa = false;
    printf("%s", tr("pedido para parar os 3 - confirma nas proximas voltas de cada um\n"));
    return;
  }
  printf(tr("rodando: %s | leituras: mapa=%lu nivel=%lu log=%lu\n"
         "uso: simulacao iniciar | simulacao parar\n"),
         s_sim_ativa ? tr("sim") : tr("nao"),
         (unsigned long)s_sim_leituras[0], (unsigned long)s_sim_leituras[1], (unsigned long)s_sim_leituras[2]);
}
