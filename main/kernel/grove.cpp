// VIGIA DO GROVE - quem esta no cabo, e quando perguntar de novo.
//
// Perguntar "quem esta ai?" (a sondagem) custa ate ~10 s: parar o Mesh ou o
// GPS (ate 1 s cada), ligar o trilho, esperar o modulo bootar e escutar os
// dois baud. Por isso ela roda numa tarefa do pool, nunca na interface (o
// toque ficava surdo enquanto alguem trocava o modulo).
//
// "Tem alguem ai?" e barato, e um timer pergunta a cada 2 s (10 s em
// repouso). Cada estado tem quem o observe:
//   - porta LIVRE: o pino RX com pull-down (tab5_grove_presenca). Duas
//     leituras iguais antes de acreditar - um cabo sendo encaixado toca antes
//     de assentar;
//   - porta do MESH: o radio calado ha mais de 60 s. Folga larga de proposito:
//     o servico pede apresentacao a cada 10 s enquanto esta mudo, e um alarme
//     cedo demais derrubaria um radio so ocupado. Prazo PROPRIO, e nao o
//     SILENCIO_MS do servico - somados, a troca de modulo levava 150 s;
//   - porta do GPS: 5 s calado. O GPS fala sozinho a ~1 Hz, entao silencio
//     ali e cabo puxado com muito mais certeza.
//
// NOVAS TENTATIVAS: a primeira sondagem e refem do instante. O C6L leva alguns
// segundos para responder, e pode devolver ruido ("desconhecido") em vez de
// silencio enquanto termina de bootar. Sem nada conclusivo, tenta de novo aos
// 20 s e aos 50 s - e so. Uma troca de modulo de verdade (pedido, ou o pino
// mudou) reabre essas duas tentativas; a tentativa automatica nao.
#include "grove.hpp"
#include "internal.hpp"
#include "app.h"
#include "ajustes.h"
#include "pool_tarefas.h"
#include "tab5_grove.hpp"
#include "tab5_hora.hpp"
#include "tab5_mesh.hpp"
#include "tab5_gps.hpp"

#include "esp_timer.h"
#include "esp_log.h"
#include <atomic>

static const char* TAG = "grove";

static const uint32_t VIGIA_MS     = 2000;    // olhar o pino (acordado)
static const uint32_t VIGIA_REP_MS = 10000;   // idem, em repouso
static const uint32_t MESH_MUDO_MS = 60000;
static const uint32_t GPS_MUDO_MS  = 5000;
static const uint32_t PRIMEIRA_MS  = 1000;    // a do boot: depois do primeiro desenho

static esp_timer_handle_t s_timer = nullptr;
static std::atomic<bool>  s_sondando{false};   // do disparo ate a tarefa terminar
static std::atomic<bool>  s_pedido{false};     // sondagem pedida, ainda nao disparada
// So o timer e a tarefa de sondagem (uma de cada vez) mexem nestes:
static int      s_extras = 0;         // tentativas automaticas ja feitas
static uint32_t s_desde  = 0;         // de quando contam as tentativas
static bool     s_havia  = false;     // ultima leitura do pino
static int      s_iguais = 0;         // leituras seguidas iguais

static void sondar(void* reabrir)
{
  if (reabrir) { s_extras = 0; s_desde = tab5_agora_ms(); }
  const bool mesh_ok = tab5_mesh_soltar_grove();   // via Bluetooth o no nao esta no cabo
  const bool gps_ok  = tab5_gps_parar();
  if (!mesh_ok || !gps_ok) {
    ESP_LOGE(TAG, "nao consegui liberar o Grove - sondagem cancelada");
  } else if (!ajustes_grove()) {
    ESP_LOGI(TAG, "Grove desligado no ajuste - parei o servico e nao sondei");
  } else {
    const grove_tipo_t achado = tab5_grove_detectar();
    if (achado == GROVE_MESHTASTIC) {
      // O GROVE TEM PRIORIDADE: achar o C6L no cabo leva o Mesh para ele, de
      // qualquer via. So aqui, no achado - depois, a escolha manual em Ajustes
      // vale ate o proximo achado.
      if (ajustes_mesh_via() != AJUSTES_MESH_SERIAL) {
        ESP_LOGI(TAG, "Meshtastic no Grove: o cabo tem prioridade - Mesh vai para ele");
        ajustes_definir_mesh_via(AJUSTES_MESH_SERIAL);
      }
      tab5_mesh_iniciar();
    } else if (achado == GROVE_GPS) {
      tab5_gps_iniciar();
    }
  }
  // A leitura seguinte do pino se compara com o que a sondagem acabou de ver.
  s_havia  = (tab5_grove_tipo() != GROVE_NADA);
  s_iguais = 0;
  s_sujo_layout = true;
  s_sondando = false;   // POR ULTIMO - so agora a proxima pode comecar
  sistema_acordar_interface();
}

// 6 KB, a vaga grande: a sondagem sobe o Mesh, que le o cartao (montagem do
// FATFS, NodeDB, diario) nesta pilha - o mesmo caminho que estourou o ESP-NOW
// com 4 KB. Com 4 KB o monitor registrou 316 B livres (25/10/2026).
static void disparar(bool reabrir)
{
  bool livre = false;
  if (!s_sondando.compare_exchange_strong(livre, true)) { return; }   // ja ha uma correndo
  s_pedido = false;
  if (!pool_criar(nullptr, sondar, reabrir ? (void*)1 : nullptr, "grove_sonda", 6144, 1)) {
    s_sondando = false;
  }
}

// Algo mudou no cabo? So a pergunta barata; a cara vai para disparar().
static bool cabo_mudou(void)
{
  const grove_dono_t dono = tab5_grove_dono();
  if (dono == GROVE_DONO_MESH) {
    if (tab5_mesh_silencio_ms() <= MESH_MUDO_MS) { return false; }
    ESP_LOGW(TAG, "radio mudo ha %us - devolvendo o Grove e sondando", (unsigned)(MESH_MUDO_MS / 1000));
    return true;
  }
  if (dono == GROVE_DONO_GPS) {
    if (tab5_gps_silencio_ms() <= GPS_MUDO_MS) { return false; }
    ESP_LOGW(TAG, "GPS mudo ha %us - devolvendo o Grove e sondando", (unsigned)(GPS_MUDO_MS / 1000));
    return true;
  }
  const bool ha = tab5_grove_presenca();
  if (ha != s_havia) { s_havia = ha; s_iguais = 1; return false; }
  if (++s_iguais != 2) { return false; }   // age so na segunda igual, uma vez
  const bool tinha = (tab5_grove_tipo() != GROVE_NADA);
  if (ha && !tinha) { ESP_LOGI(TAG, "algo foi plugado no Grove - sondando"); return true; }
  if (!ha && tinha) {
    // Nao se paga sondagem para concluir "nada": o pino ja disse. Com a porta
    // livre nao ha servico a parar - so esquecer o tipo.
    ESP_LOGI(TAG, "o Grove esvaziou");
    tab5_grove_esquecer();
    s_sujo_layout = true;
    sistema_acordar_interface();
  }
  return false;
}

static void ao_timer(void*)
{
  if (s_pedido) {
    disparar(true);
  } else if (ajustes_grove() && !s_sondando) {
    if (cabo_mudou()) {
      disparar(true);
    } else if (s_extras < 2 && tab5_grove_dono() == GROVE_DONO_NINGUEM
            && (tab5_grove_tipo() == GROVE_NADA || tab5_grove_tipo() == GROVE_DESCONHECIDO)
            && tab5_agora_ms() - s_desde > 20000u + (uint32_t)s_extras * 30000u) {
      ++s_extras;
      ESP_LOGI(TAG, "Grove inconclusivo (%s) - tentando de novo (%d/2)",
               tab5_grove_tipo() == GROVE_NADA ? "nada" : "desconhecido", s_extras);
      disparar(false);
    }
  }
  // GROVE DESLIGADO (ajuste): sem energia no cabo nao ha o que vigiar, e o
  // timer so acordaria o P4 do sono leve a cada 10 s para nada. Ele para
  // aqui; quem liga o Grove (sistema_grove, a volta do ensaio) passa por
  // sistema_grove_redetectar -> grove_pedir_sondagem, que o religa.
  if (!ajustes_grove() && !s_pedido && !s_sondando) { return; }
  esp_timer_start_once(s_timer, (uint64_t)sistema_ritmo_ms(VIGIA_MS, VIGIA_REP_MS) * 1000u);
}

void grove_iniciar(void)
{
  const esp_timer_create_args_t a = { .callback = ao_timer, .arg = nullptr,
                                      .dispatch_method = ESP_TIMER_TASK, .name = "grove",
                                      .skip_unhandled_events = true };
  if (esp_timer_create(&a, &s_timer) != ESP_OK) { ESP_LOGE(TAG, "sem timer - Grove sem vigia"); return; }
  if (ajustes_grove()) { s_pedido = true; }
  else { ESP_LOGI(TAG, "Grove sem energia (ajuste desligado) - nao vou sondar"); }
  esp_timer_start_once(s_timer, (uint64_t)PRIMEIRA_MS * 1000u);
}

void grove_pedir_sondagem(void)
{
  s_pedido = true;
  if (s_timer) { esp_timer_stop(s_timer); esp_timer_start_once(s_timer, 0); }
}

bool grove_sondando(void) { return s_pedido || s_sondando; }
