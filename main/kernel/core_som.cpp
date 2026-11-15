// CORE - o som: ociosidade do alto-falante (solta a trava de PM do I2S), os
// pinos que o alto-falante divide com o USB, o som em fluxo da parte de fundo
// dos apps e o bip de notificacao com o codec frio. Partido de core.cpp (o
// motor); o que as partes compartilham esta em core_interno.hpp.

#include "tab5_mutex.hpp"
#include "tab5_hora.hpp"
#include "notificacao.h"
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "core_interno.hpp"

static const char *TAG = "core";

// OCIOSIDADE DO ALTO-FALANTE - achado, investigando por que a
// trava de PM "i2s_driver" (APB_FREQ_MAX, ver comando "energia travas")
// nunca soltava, mesmo com o aparelho havia muito tempo em repouso.
//
// Causa raiz, achada lendo a biblioteca (managed_components/m5stack__
// m5unified/src/utility/Speaker_Class.cpp): a tarefa interna do alto-falante
// (spk_task) so chama i2s_stop() no ramo ocioso quando M5UNIFIED_I2S_ADC_DAC
// esta definido - isto e, quando o chip tem o DAC analogico interno da
// familia ESP32 classica. O Tab5 usa ESP32-P4 com o codec ES8388 externo via
// I2S "de verdade", nao tem esse DAC, entao esse ramo NUNCA compila aqui - e
// o canal I2S, uma vez aberto pelo primeiro tone()/playWav() da sessao, fica
// ligado (e a trava de clock junto) para sempre, tocando som ou nao.
//
// Nao vale a pena remendar a biblioteca vendorizada: um patch se perde na
// proxima atualizacao do componente. A correcao fica inteira do lado de ca,
// pela API publica: se o alto-falante esta ligado (isRunning()) mas ocioso
// (!isPlaying()) por SPEAKER_OCIOSO_MS seguidos, chamamos M5.Speaker.end() -
// que de fato deleta o canal I2S (i2s_del_channel), soltando a trava. O
// proximo tone()/playWav() reabre sozinho: begin() ja e preguicoso por
// natureza (mesmo mecanismo do "primeiro uso liga o ES8388", ver o aviso em
// ajustes.h/app_config.cpp), entao nao precisa de nenhum cuidado extra aqui.
//
// SEGURO chamar M5.Speaker.end()/isRunning()/isPlaying() AQUI: e a mesma
// tarefa de interface que ja e a unica a chamar M5.Speaker.tone() (ver o
// bloco "BEEP DE NOTIFICACAO NOVA" mais abaixo) - a regra e "so a tarefa de
// interface fala I2C", e end()/begin() tambem falam com o codec por I2C.
//
// A folga de 4s (bem mais que o passo BARRA_MS de 2s usado para conferir)
// existe para nao religar/desligar o canal entre dois beeps proximos (ex.:
// obturador da camera seguido de uma notificacao).
const uint32_t SPEAKER_OCIOSO_MS = 4000;
uint32_t       s_speaker_ocioso_desde = 0;  // 0 = tocando ou desligado

// BEEP DE NOTIFICACAO NOVA + DESLIGAR O I2S OCIOSO,
// estendido para tambem valer EM REPOUSO, e para respeitar
// silenciar POR FONTE (a Trilha muda e o Mesh tocando, por exemplo). A pergunta "houve novidade que merece
// som?" mora inteira em notif_houve_novidade_sonora() (notificacao.cpp) -
// este arquivo nao sabe, e nao precisa saber, quantas fontes existem, os
// nomes delas, ou quais estao silenciadas. Isto comparava o
// TOTAL agregado contra um cache aqui mesmo (s_notif_total_beep) - tinha que
// mudar porque o total nao diz QUAL fonte subiu, e sem isso nao ha como
// silenciar uma sem silenciar todas.
//
// Isto so rodava com a tela acesa: tocar um som prendia a
// trava de PM do I2S para sempre (ver o aviso grande em s_speaker_ocioso_desde
// acima), o que destruiria o proprio motivo do repouso existir. Com o
// watchdog abaixo (M5.Speaker.end() apos SPEAKER_OCIOSO_MS sem tocar nada), o
// repouso deixou de ser incompativel com o beep: toca, e o proprio watchdog
// desliga de novo poucos segundos depois - mesmo com a tela apagada. E por
// isso que esta funcao virou uma funcao separada em vez de ficar inline no
// bloco de repintura: ela e chamada TAMBEM do ramo em repouso, onde nao ha
// tela para desenhar.
//
// NUNCA desenha nada aqui, de proposito - com o DSI desmontado (repouso),
// qualquer chamada a M5.Display seria, na melhor das hipoteses, inutil.
//
// SEGURO chamar M5.Speaker.tone()/isRunning()/isPlaying()/end() AQUI: esta e
// a tarefa de interface, a unica que pode falar I2C (liga o ES8388 no
// primeiro uso - ver o aviso grande em ajustes.h/app_config.cpp) tanto
// acordada quanto em repouso (o INA226 e o IMU ja fazem I2C em repouso, ver
// vTaskInterface mais abaixo).
// BIP COM O CODEC FRIO. Medido no aparelho: o primeiro bip depois de o
// alto-falante ter sido desligado (SPEAKER_OCIOSO_MS) saia mudo, mesmo no
// volume maximo; o segundo, dentro da janela de 4 s, tocava. O begin() liga o
// ES8388 e o amplificador, mas o DAC so fica audivel depois da rampa
// "click-free" do codec - um tom de 80-90 ms inteiro cabe dentro dela.
//
// Solucao sem bloquear a interface: com o codec frio, toca um bloco unico
// "silencio + tom" (o silencio cobre o aquecimento). Fica em PSRAM e so e
// alocado no primeiro uso. Dois blocos alternados, para um segundo pedido
// poder trocar o som em curso sem reescrever o que o alto-falante esta lendo.
// SOM_AQUECER_MS e o ajuste: se ainda perder o comeco, aumente.
static const uint32_t SOM_AQUECER_MS = 180;
static const uint32_t SOM_FS         = 8000;   // tons ate ~2 kHz: sobra
static const size_t   SOM_AMOSTRAS   = SOM_FS * 3 / 10;   // 300 ms
static int16_t*       s_som_bloco[2] = {nullptr, nullptr};
static int            s_som_vez      = 0;
static uint32_t       s_som_quente_em = 0;   // quando o codec esta pronto

// O ALTO-FALANTE E O USB DIVIDEM PINOS. O I2S do alto-falante sai no GPIO 26
// (dados) e 27 (BCK) - no P4, os pinos do PHY USB 1. O ESP-IDF supoe que esse
// PHY e sempre do OTG (hal/esp32p4/.../gpio_ll.h: gpio_ll_func_sel e
// gpio_ll_pullup_dis): ao configurar esses pinos ele DESLIGA o pad do OTG
// (usb_pad_enable = 0) e o pull-up do D+. Durante o pendrive o OTG esta no
// USB-C (PHY 0, trocado - ver tab5_pendrive.hpp), entao um bip no meio da
// sessao tirava o disco do computador: o Mac via o cabo sair (sem reset
// nenhum) e o Tab5 seguia esperando o proximo comando. O fim da tarefa do
// alto-falante (spk_task: pinMode no GPIO 26) faz o mesmo.
//
// Enquanto o pendrive pedir, o alto-falante fica desligado e mudo. Quem
// desliga e esta tarefa (a unica que fala com o codec); o pendrive espera a
// confirmacao antes de subir o USB.
static volatile bool s_som_reservado = false;   // o pendrive pediu os pinos
static volatile bool s_som_parado    = false;   // a interface ja desligou o alto-falante
// Dado pela interface quando o alto-falante parou: quem pediu ESPERA NELE, e
// acorda no instante da confirmacao (era um laco perguntando a cada 10 ms -
// o mesmo padrao que o kernel/parada.hpp aposentou nos servicos).
SemaphoreHandle_t s_som_parou = nullptr;   // criado em sistema_iniciar

// SOM EM FLUXO (ver app.h) - musica da parte de fundo de um app, no canal 0
// do alto-falante. O bip (tone/playRaw com canal -1) pega o canal livre MAIS
// ALTO, entao os dois nunca disputam o mesmo canal: a musica segue e o bip
// sai por cima dela.
//
// A REGRA "SO A INTERFACE FALA I2C" CONTINUA VALENDO: o playRaw() chama
// begin() por dentro, e begin() com o alto-falante desligado sobe o ES8388
// pelo I2C. Por isso o fundo so alimenta com s_fluxo_ativo (a interface ligou
// e ninguem desligou depois), e quem desliga - a reserva do pendrive e o
// vigia de ociosidade - desliga sob a MESMA trava que o fundo segura ao
// enfileirar: o end() nunca acontece no meio de um playRaw().
static const uint8_t     SOM_CANAL_FLUXO = 0;
static volatile bool     s_fluxo_pedido = false;   // o fundo quer o alto-falante ligado
static volatile bool     s_fluxo_ativo  = false;   // ligado para o fluxo (o fundo pode enfileirar)
SemaphoreHandle_t s_fluxo_pronto = nullptr; // a interface atendeu (criado em sistema_iniciar)
static Trava s_som_trava;   // playRaw do fluxo x end()

// A FOLGA DO ALTO-FALANTE (06/11/2026, medido no Tab5). O M5Unified entrega o
// som ao I2S numa tarefa propria ('spk_task') com prioridade 2 e um DMA de
// 256 x 8 quadros a 48 kHz = ~43 ms de folga. Ao abrir um app a interface
// redesenha tudo e o barramento da PSRAM fica com a tela (prioridade ALTA,
// hal/tab5_barramento): medido, a tarefa da Musica (prio 5) levou 396 ms num
// bloco que leva ~44 ms, com 0 us de cartao - a CPU quase parou para o audio.
// A fila de blocos (~0,8 s) aguentou ('fila secou 0'); quem nao aguenta e a
// spk_task com 43 ms: o engasgo ao sair e voltar ao app. Aqui:
//   prioridade 6  acima da interface (4) e da Musica (5): ela so copia e mistura
//   DMA 512 x 8   ~85 ms de folga (+~9 KB de RAM interna: DMA e pilha)
// Antes de qualquer begin(), uma vez; o end()/begin() do vigia mantem a config.
void som_configurar(void)
{
  auto c = M5.Speaker.config();
  c.task_priority = 6;
  c.dma_buf_len   = 512;
  c.dma_buf_count = 8;
  M5.Speaker.config(c);
  ESP_LOGI(TAG, "alto-falante: tarefa prio %u, DMA %u x %u (~%u ms de folga)",
           (unsigned)c.task_priority, (unsigned)c.dma_buf_len, (unsigned)c.dma_buf_count,
           (unsigned)(c.dma_buf_len * c.dma_buf_count * 1000u / (c.sample_rate ? c.sample_rate : 48000u)));
}

void som_atender_fluxo(void)
{
  if (!s_fluxo_pedido) { return; }
  s_fluxo_pedido = false;
  if (!s_som_reservado) {
    const bool frio = !M5.Speaker.isRunning();
    if (M5.Speaker.begin()) {
      if (frio) { s_som_quente_em = tab5_agora_ms() + SOM_AQUECER_MS; }
      s_speaker_ocioso_desde = 0;
      s_fluxo_ativo = true;
    }
  }
  if (s_fluxo_pronto) { xSemaphoreGive(s_fluxo_pronto); }
}

bool sistema_som_fluxo_abrir(uint32_t espera_ms)
{
  if (s_som_reservado || !s_fluxo_pronto) { return false; }
  if (!s_fluxo_ativo) {
    xSemaphoreTake(s_fluxo_pronto, 0);   // sobra de um pedido anterior
    s_fluxo_pedido = true;
    sistema_acordar_interface();
    xSemaphoreTake(s_fluxo_pronto, pdMS_TO_TICKS(espera_ms));
    if (!s_fluxo_ativo) { return false; }
  }
  // Codec recem-ligado: o comeco do primeiro bloco sairia mudo.
  const int32_t falta = (int32_t)(s_som_quente_em - tab5_agora_ms());
  if (falta > 0) { vTaskDelay(pdMS_TO_TICKS(falta)); }
  return true;
}

bool sistema_som_fluxo_livre(void)
{
  return s_fluxo_ativo && M5.Speaker.isPlaying(SOM_CANAL_FLUXO) < 2;
}

int sistema_som_fluxo_na_fila(void)
{
  return s_fluxo_ativo ? (int)M5.Speaker.isPlaying(SOM_CANAL_FLUXO) : 0;
}

bool sistema_som_fluxo_tocar(const int16_t* pcm, size_t quadros, uint32_t hz, bool estereo)
{
  s_som_trava.travar();
  const bool ok = s_fluxo_ativo && !s_som_reservado;
  if (ok) {
    M5.Speaker.playRaw(pcm, quadros * (estereo ? 2 : 1), hz, estereo, 1, SOM_CANAL_FLUXO, false);
  }
  s_som_trava.destravar();
  return ok;
}

void sistema_som_fluxo_calar(void)
{
  s_som_trava.travar();
  if (s_fluxo_ativo) { M5.Speaker.stop(SOM_CANAL_FLUXO); }
  s_som_trava.destravar();
}

void sistema_som_fluxo_fechar(void)
{
  s_som_trava.travar();
  if (s_fluxo_ativo) { M5.Speaker.stop(SOM_CANAL_FLUXO); }
  s_fluxo_ativo = false;   // o vigia de ociosidade desliga depois de SPEAKER_OCIOSO_MS
  s_som_trava.destravar();
}

void som_atender_reserva(void)
{
  if (!s_som_reservado || s_som_parado) { return; }
  s_som_trava.travar();
  s_fluxo_ativo = false;   // o fundo ve no proximo bloco e para
  if (M5.Speaker.isRunning()) { M5.Speaker.end(); }
  s_som_trava.destravar();
  s_speaker_ocioso_desde = 0;
  s_som_parado = true;
  if (s_som_parou) { xSemaphoreGive(s_som_parou); }
}

bool sistema_som_ceder_pinos(uint32_t espera_ms)
{
  if (!s_som_parou) { return false; }
  xSemaphoreTake(s_som_parou, 0);   // sobra de um pedido anterior nao vale para este
  s_som_parado = false;
  s_som_reservado = true;
  sistema_acordar_interface();
  return xSemaphoreTake(s_som_parou, pdMS_TO_TICKS(espera_ms)) == pdTRUE || s_som_parado;
}

void sistema_som_devolver_pinos(void)
{
  s_som_reservado = false;
  s_som_parado = false;
}

void sistema_beep(float hz, uint32_t ms)
{
  if (s_som_reservado) { return; }   // pinos com o USB (ver acima)
  const uint32_t t = tab5_agora_ms();
  if (!M5.Speaker.isRunning()) { s_som_quente_em = t + SOM_AQUECER_MS; }
  const int32_t falta = (int32_t)(s_som_quente_em - t);

  if (falta <= 0) {
    M5.Speaker.tone(hz, ms);
    return;
  }

  // Codec ainda subindo. begin() explicito: o silencio tem de comecar a
  // contar do instante em que o codec foi ligado, nao de um begin() preguicoso
  // la dentro do playRaw.
  if (!M5.Speaker.begin()) { M5.Speaker.tone(hz, ms); return; }
  const size_t lead = (size_t)falta * SOM_FS / 1000;
  size_t       tom  = (size_t)ms * SOM_FS / 1000;
  if (lead >= SOM_AMOSTRAS) { M5.Speaker.tone(hz, ms); return; }
  if (lead + tom > SOM_AMOSTRAS) { tom = SOM_AMOSTRAS - lead; }

  s_som_vez ^= 1;
  if (s_som_bloco[s_som_vez] == nullptr) {
    s_som_bloco[s_som_vez] = (int16_t*)heap_caps_malloc(SOM_AMOSTRAS * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  }
  int16_t* b = s_som_bloco[s_som_vez];
  if (b == nullptr) { M5.Speaker.tone(hz, ms); return; }

  memset(b, 0, lead * sizeof(int16_t));
  const size_t rampa = SOM_FS / 400;   // 2,5 ms de entrada/saida: sem estalo
  for (size_t i = 0; i < tom; ++i) {
    float g = 1.0f;
    if (i < rampa)            { g = (float)i / (float)rampa; }
    else if (tom - i < rampa) { g = (float)(tom - i) / (float)rampa; }
    b[lead + i] = (int16_t)(30000.0f * g * sinf(2.0f * (float)M_PI * hz * (float)i / (float)SOM_FS));
  }
  M5.Speaker.playRaw(b, lead + tom, SOM_FS, false, 1, -1, true);
}

void atualizar_beep_e_ociosidade_do_som(uint32_t t)
{
  if (notif_houve_novidade_sonora()) {
    sistema_beep(1568.0f, 90);   // Sol5 - curto e agudo, "notificacao", nao "confirmacao"
  }

  // So entra aqui se algum tone()/playWav() ja rodou nesta sessao
  // (isRunning() == false antes disso, nada a fazer).
  if (M5.Speaker.isRunning()) {
    if (M5.Speaker.isPlaying()) {
      s_speaker_ocioso_desde = 0;
    } else if (s_speaker_ocioso_desde == 0) {
      s_speaker_ocioso_desde = t;
    } else if (t - s_speaker_ocioso_desde >= SPEAKER_OCIOSO_MS && !s_fluxo_ativo) {
      // Com o fluxo aberto (musica entre dois blocos, ou carregando a
      // proxima) o alto-falante fica: e o fundo que diz quando acabou.
      s_som_trava.travar();
      if (!s_fluxo_ativo) { M5.Speaker.end(); }
      s_som_trava.destravar();
      s_speaker_ocioso_desde = 0;
    }
  }
}
