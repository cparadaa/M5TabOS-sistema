#include "tab5_wifi_cochilo.hpp"
#include "tab5_hora.hpp"   // tab5_passou_s
#include "tab5_wifi.hpp"
#include "tab5_power.hpp"
#include "ajustes.h"
#include "fundo.h"
#include "app.h"
#include "tab5_mesh.hpp"

#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "cochilo";

static const int COCHILO_APOS_S = 3 * 60;    // tela apagada por isto antes de desligar
static const int JANELA_CADA_S  = 15 * 60;   // de quanto em quanto religa sozinho
static const int JANELA_S       = 60;        // conectado por isto em cada janela
static const int JANELA_MAX_S   = 120;       // sem conectar ate aqui, desiste da janela

typedef enum { C_NORMAL, C_COCHILANDO, C_JANELA } cochilo_t;
static cochilo_t s_estado = C_NORMAL;
static int       s_seg = 0;          // segundos no estado atual (ou em repouso, no NORMAL)
static int       s_conectado_s = 0;  // segundos conectado dentro da janela
static uint32_t  s_cochilos = 0;     // quantas vezes desligou (diagnostico)

// MESH PELA INTERNET E RECEBER: o MQTT nao guarda nada para quem esta fora -
// o que chegar com o Wi-Fi desligado se perde (aconteceu: 2026-10-17, uma noite
// de mensagens perdidas). Com o servico no ar pela internet, a rede fica.
static bool mesh_pela_internet(void)
{
  return tab5_mesh_servico_ativo() && tab5_mesh_via() == AJUSTES_MESH_INTERNET;
}

// BATIDA DO WI-FI. O SDIO do C6 nao tira o P4 do sono leve: com o Wi-Fi
// conectado e a tela apagada, em vez de travar o sono (72 mA medidos), o P4
// dorme e um esp_timer o acorda a cada BATIDA_MS para atender o que o C6
// juntou. O roteador e o TCP seguram esse atraso; nada se perde.
static const uint64_t BATIDA_MS       = 100;   // tela apagada
static const uint64_t BATIDA_DOZE_MS  = 300;   // tela apagada ha DOZE_APOS_S ("Doze" dos celulares)
static const int      DOZE_APOS_S     = 30 * 60;
static esp_timer_handle_t s_batida = nullptr;
static uint64_t s_batendo_ms = 0;   // o periodo em curso (0 = parada)
static void batida_cb(void*) { }   // so acordar ja e o trabalho

static void batida(uint64_t ms)
{
  if (ms == s_batendo_ms) { return; }
  if (!s_batida) {
    const esp_timer_create_args_t a = { .callback = batida_cb, .arg = nullptr,
                                        .dispatch_method = ESP_TIMER_TASK, .name = "batida_wifi" };
    if (esp_timer_create(&a, &s_batida) != ESP_OK) { return; }
  }
  if (s_batendo_ms) { esp_timer_stop(s_batida); }
  if (ms) { esp_timer_start_periodic(s_batida, ms * 1000); }
  s_batendo_ms = ms;
}

static bool alguem_precisa(void)
{
  if (tab5_wifi_receptor_pedido()) { return true; }
  if (tab5_wifi_precisada()) { return true; }   // o servidor web aberto (tab5_wifi_precisar)
  if (mesh_pela_internet()) { return true; }
  tab5_perfil_t p;               // nada ligado
  fundo_somar_perfil(p);
  return p.radio;
}

static void desligar(const char* por)
{
  tab5_wifi_desligar();
  s_estado = C_COCHILANDO; s_seg = 0; ++s_cochilos;
  ESP_LOGI(TAG, "Wi-Fi cochilando (%s)", por);
}

static void religar(const char* por)
{
  tab5_wifi_ligar();
  ESP_LOGI(TAG, "Wi-Fi religado (%s)", por);
}

void tab5_wifi_cochilo_vigiar(void)
{
  static int64_t ult = 0;
  const int dt = tab5_passou_s(&ult);
  // Conectado em repouso: o P4 dorme, mas com a batida. O receptor pedido
  // (ESP-NOW conversando, CSI, farejo) quer resposta na hora: ai segura.
  const bool conectado = tab5_wifi_estado() == TAB5_WIFI_CONECTADO;
  static int s_repouso_s = 0;   // ha quanto tempo a tela esta apagada
  s_repouso_s = tab5_power_em_repouso() ? s_repouso_s + dt : 0;
  batida(!(conectado && tab5_power_em_repouso()) ? 0
         : s_repouso_s >= DOZE_APOS_S ? BATIDA_DOZE_MS : BATIDA_MS);
  sistema_economia_segurar(ECONOMIA_SEGURA_WIFI, conectado && tab5_wifi_receptor_pedido());

  const bool vale = ajustes_wifi_cochilo() && ajustes_wifi();
  const bool repouso = tab5_power_em_repouso();

  // Desligaram o ajuste, acenderam a tela ou alguem precisa: volta ao normal.
  if (!vale || !repouso || alguem_precisa()) {
    if (s_estado == C_COCHILANDO && ajustes_wifi()) {
      religar(!vale ? "ajuste" : !repouso ? "tela acesa" : "pedido");
    }
    s_estado = C_NORMAL; s_seg = 0;
    return;
  }

  s_seg += dt;
  switch (s_estado) {
    case C_NORMAL: {
      // So desce o que esta de pe: com o Wi-Fi ja parado (o Radar pausa, a
      // senha errada desistiu) nao ha o que economizar nem o que devolver.
      const bool de_pe = tab5_wifi_radio_ativo() || tab5_wifi_estado() == TAB5_WIFI_CONECTANDO;
      if (s_seg >= COCHILO_APOS_S && de_pe) { desligar("tela apagada"); }
      break;
    }
    case C_COCHILANDO:
      if (s_seg >= JANELA_CADA_S) {
        religar("janela");
        s_estado = C_JANELA; s_seg = 0; s_conectado_s = 0;
      }
      break;
    case C_JANELA:
      if (tab5_wifi_estado() == TAB5_WIFI_CONECTADO) { s_conectado_s += dt; }
      if (s_conectado_s >= JANELA_S || s_seg >= JANELA_MAX_S) { desligar("fim da janela"); }
      break;
  }
}

void tab5_wifi_cochilo_resumo(char* out, size_t tam)
{
  if (!ajustes_wifi_cochilo()) {
    snprintf(out, tam, "conectado economico%s (economia maxima desligada em Ajustes)",
             s_batendo_ms ? (s_batendo_ms == BATIDA_DOZE_MS ? ", batida de 300 ms (doze)" : ", batida de 100 ms") : "");
    return;
  }
  switch (s_estado) {
    case C_NORMAL:
      snprintf(out, tam, "acordado%s%s (cochilou %lu vezes)",
               !tab5_power_em_repouso() ? "" : mesh_pela_internet() ? " - Mesh pela internet: nao cochila"
                                          : " - tela apagada, conta para cochilar",
               s_batendo_ms ? ", batida" : "", (unsigned long)s_cochilos);
      break;
    case C_COCHILANDO:
      snprintf(out, tam, "cochilando ha %d s (janela em %d s)", s_seg, JANELA_CADA_S - s_seg);
      break;
    case C_JANELA:
      snprintf(out, tam, "janela: %d s conectado de %d", s_conectado_s, JANELA_S);
      break;
  }
}
