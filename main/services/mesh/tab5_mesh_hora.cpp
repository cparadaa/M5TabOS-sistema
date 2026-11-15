// Servico Mesh - a HORA: o carimbo corrigido quando o relogio salta, a hora
// emprestada do radio e a hora ao vivo. Partido de tab5_mesh.cpp (o servico);
// o que as partes compartilham esta em mesh/tab5_mesh_interno.hpp.

#include "app.h"   // sistema_avisar
#include "esp_log.h"
#include <stdio.h>
#include "tab5_sd.hpp"
#include "tab5_mutex.hpp"
#include "tab5_hora.hpp"
#include "mesh/tab5_mesh_interno.hpp"

static const char *TAG = "mesh";

// ---------------------------------------------------------------------------
// CARIMBO CORRIGIDO - quando o relogio salta (ver tab5_hora.hpp)
// ---------------------------------------------------------------------------
// A mensagem enviada leva a hora do NOSSO relogio. Se ele estava errado e uma
// fonte o acerta depois, a mensagem ficava no dia errado para sempre: a linha
// dela ja esta no cartao. Reescrever o arquivo da conversa por isso seria
// caro; em vez disso vai uma linha "H,<id>,<quando>" para o envios.txt, o
// mesmo arquivo so de acrescentar dos desfechos, e o leitor do historico
// troca o carimbo de quem tem id la (ultima linha vence, como no "E").
//
// So as mensagens DESTA sessao: as de antes foram carimbadas por outro
// relogio (outro boot), que pode ter estado certo.
#define MESH_CARIMBADAS 16
typedef struct { uint32_t id; uint32_t quando; } carimbada_t;
static carimbada_t s_carimbadas[MESH_CARIMBADAS];
static int         s_prox_carimbada = 0;

// Com a trava tomada.
void lembrar_carimbo(uint32_t id, uint32_t quando)
{
  if (id == 0 || quando == 0) { return; }
  s_carimbadas[s_prox_carimbada].id     = id;
  s_carimbadas[s_prox_carimbada].quando = quando;
  s_prox_carimbada = (s_prox_carimbada + 1) % MESH_CARIMBADAS;
}

// Tarefa de interface, logo depois do acerto do relogio. So RAM e o gravador
// do sistema (tab5_sd_anexar copia e volta): nada de cartao aqui.
void ao_saltar_relogio(int32_t delta)
{
  int n = 0;
  travar();
  for (int i = 0; i < MESH_CARIMBADAS; ++i) {
    carimbada_t* c = &s_carimbadas[i];
    if (c->id == 0) { continue; }
    c->quando = (uint32_t)((int64_t)c->quando + delta);
    for (int k = 0; k < s_n_msgs; ++k) {
      if (s_msgs[k].id == c->id) { s_msgs[k].quando = c->quando; }
    }
    char linha[40];
    snprintf(linha, sizeof(linha), "H,%08lX,%lu\n", (unsigned long)c->id, (unsigned long)c->quando);
    tab5_sd_anexar(MESH_PASTA, MESH_ARQ_ENVIOS, linha);
    ++n;
  }
  // O app rele o historico quando este contador muda: e o que tira da tela
  // o balao no dia errado.
  if (n) { ++s_total_msgs; }
  destravar();
  if (n) {
    ESP_LOGI(TAG, "hora do aparelho corrigida em %+ld s: horario de %d mensagem(ns) enviada(s) "
                  "antes do acerto foi ajustado", (long)delta, n);
    sistema_avisar(AVISO_MESH_MSG);
  }
}

// ---------------------------------------------------------------------------
// QUE HORAS SAO - emprestadas do radio
// ---------------------------------------------------------------------------
// O Tab5 acorda sem hora certa, mas todo NodeInfo traz last_heard e todo pacote
// traz rx_time, os dois em segundos de epoch e vindos do relogio do C6L.
// Guardamos o maior carimbo ja visto junto com o nosso uptime naquele instante;
// dai em diante a hora anda com o nosso relogio, e a conta de "ha quanto tempo"
// fecha com erro de segundos.
static uint32_t s_epoch_ref    = 0;       // carimbo do radio
static uint32_t s_epoch_ref_ms = 0;       // nosso uptime quando ele chegou

uint32_t tab5_mesh_epoch(void)
{
  if (s_epoch_ref == 0) { return 0; }
  return s_epoch_ref + (tab5_agora_ms() - s_epoch_ref_ms) / 1000;
}

// So aceita carimbo que ADIANTE o relogio. Assim um last_heard antigo, do dump
// inicial, nunca puxa a referencia para tras.
void marcar_epoch(uint32_t carimbo)
{
  if (carimbo < EPOCH_MINIMO) { return; }
  const uint32_t agora = tab5_mesh_epoch();
  if (carimbo <= agora) { return; }
  s_epoch_ref    = carimbo;
  s_epoch_ref_ms = tab5_agora_ms();
}

// ---------------------------------------------------------------------------
// HORA AO VIVO - a unica deste arquivo que acerta o relogio do sistema
// ---------------------------------------------------------------------------
// tab5_mesh_epoch() acima serve para "ha quanto tempo", e so. Ancorada no
// MAIOR last_heard da lista de nos, ela fica ATRASADA quanto for o silencio
// da malha - o no mais recente pode ter falado ontem. Usada como "agora" ela
// acertava o relogio com esse atraso: a mensagem enviada saia carimbada no
// dia errado, e o "HOJE" da conversa pulava para o dia certo so quando uma
// resposta de verdade chegava.
//
// Aqui entra so carimbo que e "agora" por construcao:
//   - rx_time de pacote que chegou DEPOIS de o no terminar o dump e esvaziar
//     a queue do que guardou enquanto estavamos desconectados (esses tem o
//     rx_time de quando ELE recebeu, minutos ou horas atras);
//   - o campo 'time' da telemetria do nosso proprio no, pelo mesmo filtro.
// O rx_time e sempre do relogio do NOSSO no (ele carimba ao receber), mesmo
// para pacote que atravessou a malha ou veio pela ponte MQTT.
#define QUEUE_DRENADA_MS 10000u            // depois do fim do dump
// So vale enquanto fresca: estender a hora do no pelo esp_timer acumula o erro
// do RC interno no sono leve (~5%, ver NTP_VALIDO_MS em tab5_hora.cpp), e o
// acerto gravaria esse erro no RTC. Pacotes chegam a toda hora com o no ativo.
#define HORA_VIVA_VALE_MS (30u * 1000u)

uint32_t s_config_ok_ms = 0;        // fim do dump; 0 = ainda rodando
static uint32_t s_vivo_ref     = 0;
static uint32_t s_vivo_ref_ms  = 0;

bool trafego_ao_vivo(void)
{
  return s_config_ok_ms != 0 && (tab5_agora_ms() - s_config_ok_ms) > QUEUE_DRENADA_MS;
}

void marcar_hora_viva(uint32_t carimbo)
{
  if (carimbo < EPOCH_MINIMO || !trafego_ao_vivo()) { return; }
  s_vivo_ref    = carimbo;
  s_vivo_ref_ms = tab5_agora_ms();
}

uint32_t tab5_mesh_hora_viva(void)
{
  const uint32_t ref = s_vivo_ref, ms = s_vivo_ref_ms;
  if (ref == 0 || (tab5_agora_ms() - ms) > HORA_VIVA_VALE_MS) { return 0; }
  return ref + (tab5_agora_ms() - ms) / 1000;
}
