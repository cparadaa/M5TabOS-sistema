// CORE - os radios: Bluetooth (o botao do usuario manda), Mesh pela internet
// quando o no nao esta e o liga/desliga de Wi-Fi, Grove e via do Mesh. Partido
// de core.cpp (o motor); o que as partes compartilham esta em core_interno.hpp.

#include "trabalho.hpp"
#include "ajustes.h"
#include "tab5_ble.hpp"
#include "tab5_wifi.hpp"
#include "tab5_grove.hpp"
#include "grove.hpp"
#include "tab5_mesh.hpp"
#include "tab5_mesh_ble.hpp"
#include "esp_log.h"
#include "core_interno.hpp"

static const char *TAG = "core";

// ---------------------------------------------------------------------------
// BLUETOOTH - o botao do usuario manda
// ---------------------------------------------------------------------------
// O boot ligava o Bluetooth se o botao estivesse ligado OU se o Mesh estivesse
// configurado para o Bluetooth - desligar o BT em Ajustes durava ate o
// proximo reinicio. Agora a escolha gravada (ajustes_bluetooth) e a unica
// que liga o radio, e o Mesh pelo Bluetooth sobe e desce junto com ela.
static void bt_mesh_subir(void*)
{
  if (!tab5_mesh_servico_ativo() || tab5_mesh_via() != AJUSTES_MESH_BLUETOOTH) { tab5_mesh_iniciar(); }
}

// ---------------------------------------------------------------------------
// MESH PELA INTERNET QUANDO O NO NAO ESTA
// ---------------------------------------------------------------------------
// Sem o C6L (cabo vazio, ou Bluetooth desligado) e com o Wi-Fi conectado, o
// servico Mesh sobe so pela internet (services/tab5_mesh_mqtt.hpp): as
// mensagens seguem chegando sem o radio ligado. Achar o no depois derruba
// este modo (tab5_mesh_iniciar). Com o Mesh no Bluetooth e o BT ligado, quem
// cobre a falta do no e o proprio servico, que abre a internet enquanto o
// BLE procura.
static void mesh_internet_subir(void*) { tab5_mesh_iniciar_internet(); }
static void mesh_internet_descer(void*)
{
  if (tab5_mesh_servico_ativo() && tab5_mesh_via() == AJUSTES_MESH_INTERNET) { tab5_mesh_parar(); }
}

void vigiar_mesh_internet(uint32_t t)
{
  static uint32_t s_ultima = 0;
  if (t - s_ultima < 15000) { return; }
  s_ultima = t;
  const bool ativo_net = tab5_mesh_servico_ativo() && tab5_mesh_via() == AJUSTES_MESH_INTERNET;
  // INTERNET ESCOLHIDA em Ajustes: e a via, nao o tapa-buraco - roda mesmo com
  // o C6L no cabo (a prioridade do Grove ja foi exercida no achado; ver
  // kernel/grove.cpp). So precisa do Wi-Fi.
  if (ajustes_mesh_via() == AJUSTES_MESH_INTERNET) {
    if (!tab5_mesh_servico_ativo() && !grove_sondando()
     && tab5_wifi_estado() == TAB5_WIFI_CONECTADO) {
      sistema_trabalho_solto(mesh_internet_subir, nullptr);
    }
    return;
  }
  // Internet como tapa-buraco: o Grove com o no a faz ceder, o cabo e o principal.
  if (!ajustes_mesh_internet() || tab5_grove_tipo() == GROVE_MESHTASTIC) {
    if (ativo_net) { sistema_trabalho_solto(mesh_internet_descer, nullptr); }
    return;
  }
  // Sem Wi-Fi o modo so-internet nao tem o que fazer: desce o servico. Duas
  // leituras seguidas (30 s) sem Wi-Fi, para nao derrubar numa piscada da rede.
  static int s_sem_wifi = 0;
  if (ativo_net && tab5_wifi_estado() != TAB5_WIFI_CONECTADO) {
    if (++s_sem_wifi >= 2) { s_sem_wifi = 0; sistema_trabalho_solto(mesh_internet_descer, nullptr); return; }
  } else { s_sem_wifi = 0; }
  const bool no_pelo_bt = ajustes_mesh_via() == AJUSTES_MESH_BLUETOOTH && ajustes_bluetooth();
  if (!tab5_mesh_servico_ativo() && !no_pelo_bt && !grove_sondando()
   && tab5_grove_tipo() != GROVE_MESHTASTIC
   && tab5_wifi_estado() == TAB5_WIFI_CONECTADO) {
    sistema_trabalho_solto(mesh_internet_subir, nullptr);
  }
}

static void bt_desligar(void*)
{
  // O Mesh sai primeiro: a stack nao pode sumir debaixo de uma conexao aberta.
  if (tab5_mesh_servico_ativo() && tab5_mesh_via() == AJUSTES_MESH_BLUETOOTH) { tab5_mesh_parar(); }
  // E confere: se o enlace ainda esta de pe (parar falhou, ou subiu fora do
  // servico), insiste - derrubar a pilha com ele dentro de um ble_gap_* e o
  // reinicio do aparelho que se via ao desligar o Bluetooth.
  if (mesh_ble_rodando()) { mesh_ble_parar(); }
  if (mesh_ble_rodando()) {
    ESP_LOGE(TAG, "o enlace do Mesh nao saiu - o Bluetooth fica ligado");
    return;
  }
  tab5_ble_desligar();
}

void bt_aplicar(bool ligar)
{
  if (ligar) {
    tab5_ble_ligar();   // trilho do C6 (I2C): tarefa de interface
    // O iniciar do Mesh le o historico do cartao: no worker.
    if (ajustes_mesh_via() == AJUSTES_MESH_BLUETOOTH) { sistema_trabalho_solto(bt_mesh_subir, nullptr); }
  } else {
    sistema_trabalho_solto(bt_desligar, nullptr);
  }
}

void sistema_bluetooth(bool ligar)
{
  if (ligar != ajustes_bluetooth()) { ajustes_definir_bluetooth(ligar); }
  bt_aplicar(ligar);
}

// ---- LIGAR UM SERVICO: a escolha e o efeito num lugar so (app.h) ----------
// Antes o app Ajustes, o app Mesh e o ensaio de energia faziam os dois passos
// (gravar a vontade + mexer no hardware/servico) cada um por conta propria -
// e cada um de um jeito (o Grove por perfil num, pelo trilho no outro).
void sistema_wifi(bool ligar)
{
  if (ligar == ajustes_wifi()) { return; }
  ajustes_definir_wifi(ligar);
  if (ligar) { tab5_wifi_ligar(); } else { tab5_wifi_desligar(); }
}

void sistema_grove(bool ligar)
{
  if (ligar != ajustes_grove()) { ajustes_definir_grove(ligar); }
  sistema_reaplicar_perfil();     // os 5V seguem o perfil (sistema_impor_preferencias)
  sistema_grove_redetectar();     // o que esta no cabo pode ter mudado
}

// O Mesh de novo, pela via gravada. No worker: parar e subir leem o cartao.
static void mesh_reiniciar_trabalho(void*)
{
  tab5_mesh_parar();
  // Bluetooth e internet sobem direto (a internet espera o Wi-Fi por conta
  // propria). O cabo: a sondagem acha o C6L e sobe a serial.
  if (ajustes_mesh_via() != AJUSTES_MESH_SERIAL) { tab5_mesh_iniciar(); }
  else { sistema_grove_redetectar(); }
}

void sistema_mesh_reiniciar(void) { sistema_trabalho_solto(mesh_reiniciar_trabalho, nullptr); }

void sistema_mesh_via(uint8_t via)
{
  if (via != ajustes_mesh_via()) { ajustes_definir_mesh_via(via); }
  // A via Bluetooth precisa do Bluetooth: liga, e fica gravado como no botao.
  if (via == AJUSTES_MESH_BLUETOOTH) {
    if (!ajustes_bluetooth()) { ajustes_definir_bluetooth(true); }
    tab5_ble_ligar();   // I2C do trilho: tarefa de interface
  }
  sistema_mesh_reiniciar();
}
