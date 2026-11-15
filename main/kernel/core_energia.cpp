// CORE - a energia: tela acesa por pedido, contrato de economia (trava de sono
// leve), troca do relogio e do link do painel na tarefa de interface e o perfil
// de hardware (o que os apps e as preferencias mantem ligado). Partido de
// core.cpp (o motor); o que as partes compartilham esta em core_interno.hpp.

#include "tab5_camera.hpp"
#include "ajustes.h"
#include "tab5_wifi.hpp"
#include "tab5_usb.hpp"
#include "grove.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_pm.h"
#include "core_interno.hpp"

// TELA ACESA POR PEDIDO (app.h): um bit por motivo, de qualquer tarefa. A
// interface ve a mascara a cada volta; quando ela zera, o prazo de repouso
// conta DAQUELE instante - soltar depois de uma hora acesa nao pode apagar a
// tela na hora.
std::atomic<uint32_t> s_acordado{0};
volatile bool s_pedir_acordar = false;
void sistema_manter_acordado(uint32_t motivo, bool sim)
{
  if (sim) {
    // Pedido novo com a tela ja apagada: impedir o PROXIMO repouso nao basta -
    // o atual segue com o clock no piso e a pessoa sem ver nada. Acorda.
    const uint32_t antes = s_acordado.fetch_or(motivo);
    if (!(antes & motivo) && tab5_power_em_repouso()) { s_pedir_acordar = true; sistema_acordar_interface(); }
  } else {
    s_acordado.fetch_and(~motivo);
  }
}
// ---- CONTRATO DE ECONOMIA (app.h) -------------------------------------------
economia_t sistema_economia(void) { return tab5_power_em_repouso() ? ECONOMIA_REPOUSO : ECONOMIA_ACORDADO; }

static uint32_t s_seguros = 0;
static esp_pm_lock_handle_t s_trava_sono = nullptr;

void sistema_economia_segurar(uint32_t quem, bool sim)
{
#if CONFIG_PM_ENABLE
  // A trava nasce na primeira vez (de qualquer tarefa: esp_pm_lock_create e
  // seguro); o par OR/AND atomico decide quem a pega e quem a solta.
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  if (!s_trava_sono) {
    esp_pm_lock_handle_t h = nullptr;
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "economia", &h) != ESP_OK) { return; }
    taskENTER_CRITICAL(&mux);
    if (!s_trava_sono) { s_trava_sono = h; h = nullptr; }
    taskEXIT_CRITICAL(&mux);
    if (h) { esp_pm_lock_delete(h); }
  }
  const uint32_t antes = sim ? __atomic_fetch_or(&s_seguros, quem, __ATOMIC_ACQ_REL)
                             : __atomic_fetch_and(&s_seguros, ~quem, __ATOMIC_ACQ_REL);
  const uint32_t depois = sim ? (antes | quem) : (antes & ~quem);
  if (!antes && depois)      { esp_pm_lock_acquire(s_trava_sono); }
  else if (antes && !depois) { esp_pm_lock_release(s_trava_sono); }
#else
  (void)quem; (void)sim;
#endif
}

uint32_t sistema_economia_seguros(void) { return __atomic_load_n(&s_seguros, __ATOMIC_ACQUIRE); }
// TROCA DO RELOGIO DO PAINEL SO NA TAREFA DE INTERFACE. Trocar o relogio
// destroi e recria o painel - e as paginas junto. No modo direto a interface
// escreve NAS PAGINAS durante toda a passada de desenho: destruir o painel de
// outra tarefa (o console) no meio disso seria escrever em memoria liberada.
// Quem nao e a interface deixa o pedido, acorda a interface e ESPERA NO
// SEMAFORO que ela da ao terminar - nada de perguntar a cada 10 ms.
static std::atomic<int> s_relogio_pedido{0};
static std::atomic<int> s_relogio_resposta{0};   // 1 = ok, -1 = falhou
SemaphoreHandle_t s_relogio_feito = nullptr;   // criado com a interface (sistema_iniciar)

bool sistema_trocar_relogio_painel(int mhz)
{
  if (sistema_na_interface() || !s_tarefa_interface) { return tab5_power_relogio_dpi(mhz); }
  if (!s_relogio_feito) { return false; }
  xSemaphoreTake(s_relogio_feito, 0);   // sobra de um pedido que desistiu de esperar
  s_relogio_resposta.store(0);
  s_relogio_pedido.store(mhz);
  sistema_acordar_interface();
  if (xSemaphoreTake(s_relogio_feito, pdMS_TO_TICKS(5000)) != pdTRUE) { return false; }
  return s_relogio_resposta.load() == 1;
}

// O mesmo pedido, para a velocidade do link DSI (teste: 'tela link').
static std::atomic<int> s_link_pedido{0};

bool sistema_trocar_link_painel(int mbps)
{
  if (sistema_na_interface() || !s_tarefa_interface) { return tab5_power_link_dsi(mbps); }
  if (!s_relogio_feito) { return false; }
  xSemaphoreTake(s_relogio_feito, 0);
  s_relogio_resposta.store(0);
  s_link_pedido.store(mbps);
  sistema_acordar_interface();
  if (xSemaphoreTake(s_relogio_feito, pdMS_TO_TICKS(5000)) != pdTRUE) { return false; }
  return s_relogio_resposta.load() == 1;
}

void atender_pedido_de_relogio(void)
{
  const int mbps = s_link_pedido.exchange(0);
  if (mbps != 0) {
    s_relogio_resposta.store(tab5_power_link_dsi(mbps) ? 1 : -1);
    xSemaphoreGive(s_relogio_feito);
  }
  const int mhz = s_relogio_pedido.exchange(0);
  if (mhz == 0) { return; }
  s_relogio_resposta.store(tab5_power_relogio_dpi(mhz) ? 1 : -1);
  xSemaphoreGive(s_relogio_feito);
}

// ---------------------------------------------------------------------------
// Perfil de boot: a uniao do que os apps precisam que EXISTA
// ---------------------------------------------------------------------------
tab5_perfil_t sistema_perfil_de_boot(void)
{
  tab5_perfil_t u;
  u.brilho = 80;

  // So as flags de FASE 1 - as que decidem se o driver chega a ser
  // inicializado. Se UM app precisa do IMU, o IMU existe para todos; nao ha
  // como inicializa-lo depois do M5.begin().
  for (int i = 0; i < SISTEMA_NUM_APPS; ++i) {
    const tab5_perfil_t& p = SISTEMA_APPS[i]->perfil;
    u.imu          = u.imu          || p.imu;
    u.microfone    = u.microfone    || p.microfone;
    u.alto_falante = u.alto_falante || p.alto_falante;
    u.rtc          = u.rtc          || p.rtc;
  }
  return u;
}

// TODA preferencia salva entra no perfil AQUI, e em nenhum outro lugar.
//
// Nasceu de um bug: o Grove ficava desligado no boot mesmo com a
// preferencia gravada como ligada, e so acendia quando algum app era aberto -
// porque a regra morava no caminho de abrir app e o boot nao passava por ele.
// A tela mostrava "ON" e o hardware estava OFF, que e a pior combinacao.
//
// Regra: o perfil do app diz o que ELE precisa; esta funcao impoe o que o
// USUARIO pediu. O app so pode SOMAR - por isso tudo aqui e "||", nunca "=".
void sistema_impor_preferencias(tab5_perfil_t& p)
{
  p.brilho   = ajustes_brilho();                          // preferencia vence sempre
  // "necessario" = a stack ja subiu neste boot (tab5_wifi_pilha_de_pe, HAL)
  // OU o usuario quer WiFi ligado (ajustes_wifi, servico) - a conta mora
  // AQUI, porque so o kernel pode ler as duas sem inverter
  // a dependencia (ver docs/ESCREVENDO_APPS.md secao 8).
  p.radio    = p.radio    || tab5_wifi_pilha_de_pe() || ajustes_wifi()
             || ajustes_bluetooth();   // o C6 e o radio do Bluetooth
  p.grove_5v = p.grove_5v || ajustes_grove();
  // O USB-A (services/tab5_usb): o pendrive tem que ter energia para ser
  // visto quando for espetado - nao da para esperar alguem pedir.
  // Em repouso sem ninguem usando, services/tab5_usb os corta (tab5_usb_repouso):
  // um perfil reaplicado no meio nao pode religar.
  p.usb_a_5v = p.usb_a_5v || (ajustes_usb_a() && !tab5_usb_cortado());

  // CAMERA VIROU SERVICO (contagem de referencia, ver o aviso
  // grande em tab5_camera.hpp) - MESMA RAZAO do Grove acima: um app aberto
  // AGORA nao pode decidir sozinho se desliga CAM_RST, porque quem esta
  // usando a camera pode ser outra coisa (o comando de console 'foto', por
  // exemplo) que nao passa pelo perfil de app nenhum. tab5_camera_ativa()
  // OR'd aqui garante que reaplicar o perfil de QUALQUER app, a qualquer
  // momento, nunca corta a energia de um consumidor que segue vivo.
  p.camera   = p.camera   || tab5_camera_ativa();

  // PARTES DE FUNDO (kernel/fundo.h): o que cada uma declarou precisar entra
  // aqui - pelo MESMO motivo da camera acima: quem esta na tela nao pode
  // desligar o WiFi de um envio que roda em fundo.
  fundo_somar_perfil(p);
}

tab5_perfil_t perfil_da_home(void)
{
  tab5_perfil_t p;                    // nada ligado
  sistema_impor_preferencias(p);      // o que o usuario pediu entra aqui
  return p;
}

// Declarado em app.h. Monta o perfil de QUEM ESTA NA TELA agora - o app aberto
// ou a home - e aplica. E o unico jeito de um ajuste valer imediatamente sem o
// app mexer no hardware por fora, por cima da regra de que ele so pode somar.
// PEDE uma re-sondagem do Grove. Nao bloqueia: o trabalho acontece no proximo
// ciclo do loop de interface, que PARA o dono atual (Mesh ou GPS, os dois sao
// servico agora - ver o bloco de resondagem la embaixo) antes de sondar de
// novo. Isto recusava quando o dono era GPS, porque o
// Azimutal lia a UART direto e uma sondagem por cima quebraria a leitura;
// GPS virou servico com o mesmo desligamento limpo que o Mesh ja tinha, e a
// recusa deixou de fazer sentido.
bool sistema_grove_redetectar(void)
{
  grove_pedir_sondagem();
  s_sujo_layout = true;
  return true;
}

bool sistema_grove_sondando(void) { return grove_sondando(); }

void sistema_reaplicar_perfil(void)
{
  tab5_perfil_t p = app_vivo() ? s_app->perfil : tab5_perfil_t();
  sistema_impor_preferencias(p);
  tab5_power_aplicar(p);
}
