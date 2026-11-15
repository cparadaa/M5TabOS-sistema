// O DMA DW-GDMA DIVIDIDO ENTRE A TELA E A CAMERA - um canal so e apagado
// PARADO.
//
// O PROBLEMA (crash reproduzido em 05/11/2026: tela acordando com a camera do
// navegador transmitindo; core dump na tarefa 'ui', escrita em 0x50081128,
// dentro de dpi_panel_init -> dw_gdma_channel_use_link_list). O DW-GDMA tem um
// grupo de canais com contagem de quem usa: a tela (painel DPI, um canal em
// laco infinito levando o framebuffer ao DSI) e a camera (o CSI, outro canal).
// O esp_lcd_panel_del() do painel DPI apaga o canal (dw_gdma_del_channel) SEM
// PARA-LO antes - o canal continua habilitado no hardware. Sozinho isso nao
// aparece: sem outro usuario o grupo zera, e o driver desliga e RESETA o
// DW-GDMA, o que limpa o canal orfao. Com a camera ligada o grupo continua de
// pe; ao acordar, o painel novo ganha O MESMO numero de canal, ainda
// habilitado, e escrever a configuracao dele da falha de barramento.
//
// A SOLUCAO. O link desvia dw_gdma_del_channel() para ca
// (-Wl,--wrap=dw_gdma_del_channel, main/CMakeLists.txt): o canal e desabilitado
// e, se ainda estiver rodando, abortado - pela API do proprio driver - e so
// entao apagado. Vale para todo usuario do DW-GDMA (tela, camera e o que vier),
// sem editar o componente; o repouso da tela (hal/tab5_power, desmontar_tela)
// segue chamando so esp_lcd_panel_del.

#include "esp_private/dw_gdma.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" esp_err_t __real_dw_gdma_del_channel(dw_gdma_channel_handle_t chan);

extern "C" esp_err_t __wrap_dw_gdma_del_channel(dw_gdma_channel_handle_t chan)
{
  if (chan) {
    dw_gdma_channel_enable_ctrl(chan, false);
    // O canal em laco (a tela) nao termina o bloco sozinho: depois de uma
    // folga para a transferencia em curso, o aborto e o que garante a parada.
    esp_rom_delay_us(20);
    dw_gdma_channel_abort(chan);
  }
  return __real_dw_gdma_del_channel(chan);
}

// ---------------------------------------------------------------------------
// A INTERRUPCAO DO DMA DA TELA: NIVEL 3, SEMPRE NO NUCLEO 1 (a piscada azul)
// ---------------------------------------------------------------------------
// O PROBLEMA (provado em 14/11/2026). No IDF 5.5 o DMA que leva o
// framebuffer ao DSI NAO e circular: a lista tem um item so, marcado
// is_last, e a cada fim de quadro a ISR do DW-GDMA o religa
// (esp_lcd_panel_dpi.c, mipi_dsi_dma_trans_done_cb). A folga e o intervalo
// entre quadros (~0,5 ms). Se a ISR atrasa mais que isso, o painel passa UM
// QUADRO INTEIRO sem imagem e mostra AZUL - sem underrun na ponte (ela ficaria
// magenta), por isso o detector de texto nunca contou; o sinal e o contador
// de "atrasados" do tab5_quadro (sempre ~2 periodos).
//   - A ISR nascia no nivel 1 (o mais baixo): qualquer outra interrupcao do
//     nucleo (SDIO do C6, cartao, temporizadores) passava na frente. Com o
//     estresse, os atrasos vinham com o nucleo no IDLE esperando interrupcao
//     (outra ISR tinha segurado) e saindo de secao critica da tarefa Berry.
//   - E ela nascia no nucleo de QUEM CRIA o painel: o boot (nucleo 0) e cada
//     despertar do repouso (outra tarefa) - o nucleo mudava sozinho.
//   - Prova: 'tela atrasar 20 1' (segura o nucleo da ISR) = quadros
//     atrasados e a tela piscando azul; no outro nucleo, nada.
//
// A SOLUCAO, para todo canal do DW-GDMA (tela e camera dividem a mesma fonte
// de interrupcao e o mesmo nivel - dw_gdma.c: "all channels in the same group
// should use the same interrupt priority", o primeiro a chegar decide; um
// canal so com 3 daria "intr_priority conflict" e o painel nao subiria):
//   1. dw_gdma_new_channel: intr_priority = 3. O maior nivel que ainda pode
//      usar FreeRTOS na ISR (o callback do painel da um semaforo); passa na
//      frente das ISRs de nivel 1 e 2. Secao critica (mascara 1-3) ainda segura.
//   2. dw_gdma_channel_register_event_callbacks (onde o IDF aloca a
//      interrupcao, no nucleo de quem chama): sempre executada no NUCLEO 1,
//      longe das ISRs do SDIO/Wi-Fi e da tarefa Berry, que vivem no 0.
//      Chamada de outro nucleo, uma tarefa curta presa ao 1 faz a chamada e
//      devolve o resultado. Acontece so ao criar canal (boot, despertar,
//      camera) - nunca no caminho do quadro.

extern "C" esp_err_t __real_dw_gdma_new_channel(const dw_gdma_channel_alloc_config_t* config,
                                                 dw_gdma_channel_handle_t* ret_chan);

extern "C" esp_err_t __wrap_dw_gdma_new_channel(const dw_gdma_channel_alloc_config_t* config,
                                                 dw_gdma_channel_handle_t* ret_chan)
{
  if (!config) { return __real_dw_gdma_new_channel(config, ret_chan); }
  dw_gdma_channel_alloc_config_t c = *config;
  c.intr_priority = 3;
  return __real_dw_gdma_new_channel(&c, ret_chan);
}

extern "C" esp_err_t __real_dw_gdma_channel_register_event_callbacks(dw_gdma_channel_handle_t chan,
                                                                      dw_gdma_event_callbacks_t* cbs,
                                                                      void* user_data);

static constexpr int NUCLEO_DA_TELA = 1;

// O pedido levado a tarefa do nucleo 1, e a resposta.
struct registro_t {
  dw_gdma_channel_handle_t   chan;
  dw_gdma_event_callbacks_t* cbs;
  void*                      user_data;
  esp_err_t                  resultado;
  TaskHandle_t               quem_pediu;
};

static void tarefa_registro(void* arg)
{
  registro_t* r = (registro_t*)arg;
  r->resultado = __real_dw_gdma_channel_register_event_callbacks(r->chan, r->cbs, r->user_data);
  xTaskNotifyGive(r->quem_pediu);
  vTaskDelete(nullptr);
}

extern "C" esp_err_t __wrap_dw_gdma_channel_register_event_callbacks(dw_gdma_channel_handle_t chan,
                                                                      dw_gdma_event_callbacks_t* cbs,
                                                                      void* user_data)
{
  if (xPortGetCoreID() == NUCLEO_DA_TELA) {
    return __real_dw_gdma_channel_register_event_callbacks(chan, cbs, user_data);
  }
  registro_t r = { chan, cbs, user_data, ESP_FAIL, xTaskGetCurrentTaskHandle() };
  // Pilha na RAM interna (xTaskCreatePinnedToCore usa a padrao): o
  // esp_intr_alloc mexe em estruturas que nao podem estar na PSRAM.
  if (xTaskCreatePinnedToCore(tarefa_registro, "gdma_irq", 3072, &r,
                              uxTaskPriorityGet(nullptr), nullptr, NUCLEO_DA_TELA) != pdPASS) {
    // Sem memoria para a tarefa: registra aqui mesmo (a tela funciona, so
    // fica sem a protecao do nucleo 1) - melhor que nao ter tela.
    ESP_EARLY_LOGW("tab5_dma", "sem memoria para mover a ISR do DMA ao nucleo %d", NUCLEO_DA_TELA);
    return __real_dw_gdma_channel_register_event_callbacks(chan, cbs, user_data);
  }
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  return r.resultado;
}
