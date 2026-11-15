// H264 - o decodificador H.264 do sistema (components/h264_dec) com a politica
// do M5tabOS: onde fica a memoria de trabalho, em que tarefas ele roda e o
// ciclo de vida. Quem toca video (services/tab5_video) so pede "abre para esta
// resolucao, decodifica este pacote, me da o quadro".
//
// O DECODIFICADOR: Main e High (CABAC, quadros B, transformada 8x8), progressivo,
// 4:2:0, 8 bits, ate 1920 de lado - confere bit a bit com o ffmpeg
// (testes/rodar_h264.sh). Os quadros saem no formato YUV420 empacotado que o
// PPA le direto (O_UYY_E_VYY), na PSRAM: a conversao para RGB e a escala sao
// do PPA, nao da CPU.
//
// A MEMORIA (o ponto delicado do P4): a "arena" de trabalho e o que o
// decodificador toca o tempo todo (tabelas, linhas de macrobloco, janela de
// referencia). Na SRAM interna ele e bem mais rapido, mas a SRAM e disputada
// pelo Wi-Fi (o esp_hosted precisa do pool DMA dele), Mesh e MQTT. Medido:
// com a cache L2 de 256 KB a arena na PSRAM rende o mesmo, entao o normal
// (AUTO) e PSRAM - nunca derruba nada. A SRAM fica para a bancada, e mesmo
// la so se sobrar o piso (H264_PISO_INTERNA). A arena e reservada ao abrir e
// devolvida ao fechar: nada fica preso sem video.
//
// AS TAREFAS: quem chama tab5_h264_decodificar() faz a analise e a predicao;
// um trabalhador no nucleo 1 ("h264_pos") faz o deblocking e grava o quadro.
// Os dois usam a extensao PIE (SIMD): as pilhas sao internas e alinhadas
// (MALLOC_CAP_SIMD) - quem chama tem que ter sido criado com
// tab5_h264_criar_tarefa().
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "h264_dec.h"

// A SRAM interna que precisa sobrar depois da arena. Abaixo disso, a arena vai
// para a PSRAM. 80 KB: o minimo medido em uso normal e ~98 KB, e o Wi-Fi
// reconectando pede picos de dezenas de KB.
#define H264_PISO_INTERNA   (80 * 1024)

typedef enum { TAB5_H264_AUTO = 0, TAB5_H264_SRAM, TAB5_H264_PSRAM } tab5_h264_onde_t;

typedef struct tab5_h264 tab5_h264_t;

typedef struct {
  int               max_l, max_a;   // maior quadro aceito, ate 1920 (o arquivo diz o tamanho real)
  tab5_h264_onde_t  onde;           // AUTO = PSRAM (o normal)
  uint8_t           presos;         // quadros que quem chama segura de uma vez (0 = 2)
} tab5_h264_cfg_t;

typedef struct {
  const uint8_t* px;      // YUV420 empacotado (O_UYY_E_VYY), linhas de l_cod * 3 / 2 bytes
  int     l_cod, a_cod;   // tamanho codificado (multiplo de 16)
  int     x, y, l, a;     // a parte visivel (o recorte do SPS)
  bool    bt709;          // matriz de cor (senao BT.601)
  bool    faixa_cheia;    // 0-255 (senao 16-235)
  int64_t marca;          // o que foi passado em tab5_h264_decodificar (o pts)
  uint8_t id;             // para tab5_h264_soltar
} tab5_h264_quadro_t;

// Cria o decodificador (arena + trabalhador). nullptr = sem memoria.
tab5_h264_t* tab5_h264_abrir(const tab5_h264_cfg_t* cfg);
void         tab5_h264_fechar(tab5_h264_t* d);

// Um pacote (uma "access unit"): Annex-B (nal_tam = 0, com 00 00 01) ou com
// tamanho na frente (nal_tam = 1, 2 ou 4, como no MP4). 'marca' volta com o quadro.
// Devolve o resultado do decodificador (H264_DEC_OK / NO_PICTURE = tudo bem).
h264_dec_result_t tab5_h264_decodificar(tab5_h264_t* d, const uint8_t* dados, size_t n,
                                        uint8_t nal_tam, int64_t marca);
// O proximo quadro na ordem de exibicao, se houver. O quadro fica PRESO (o
// decodificador nao o reescreve) ate tab5_h264_soltar(id).
bool tab5_h264_quadro(tab5_h264_t* d, tab5_h264_quadro_t* q);
void tab5_h264_soltar(tab5_h264_t* d, uint8_t id);
void tab5_h264_fim(tab5_h264_t* d);       // fim do arquivo: solta os que estavam na fila
void tab5_h264_recomecar(tab5_h264_t* d); // antes de saltar (seek): esquece o que estava no meio
bool tab5_h264_descartavel(const uint8_t* dados, size_t n, uint8_t nal_tam);   // ninguem usa como referencia
const char* tab5_h264_erro(tab5_h264_t* d);
bool tab5_h264_na_sram(const tab5_h264_t* d);   // algum pedaco da arena ficou na SRAM
size_t tab5_h264_sram_bytes(const tab5_h264_t* d); // quanto da arena e SRAM
size_t tab5_h264_arena_bytes(const tab5_h264_t* d);

// Cria uma tarefa que pode chamar o decodificador (pilha interna com o
// alinhamento da PIE). Mesmo contrato de xTaskCreatePinnedToCore.
BaseType_t tab5_h264_criar_tarefa(TaskFunction_t f, const char* nome, uint32_t pilha, void* arg,
                                  UBaseType_t prio, BaseType_t nucleo, TaskHandle_t* h);
void tab5_h264_encerrar_tarefa(void);   // o fim de uma tarefa criada acima

// BANCADA (console 'video h264'): decodifica um .h264 cru (Annex-B) inteiro,
// sem tela, e mede. Com hash, imprime o FNV-1a de cada quadro - o mesmo que o
// teste do Mac (H264_HASH=1) imprime: as duas listas tem que ser iguais.
typedef struct {
  bool     ok;
  char     erro[64];
  uint32_t quadros, com_erro;
  uint32_t ms;               // so a decodificacao (o arquivo ja esta na memoria)
  int      l, a;
  bool     na_sram;
  uint32_t arena_kb, sram_kb;  // a arena inteira e quanto dela e SRAM
  uint32_t interna_livre_kb, interna_min_kb;   // a SRAM durante a bancada
  uint32_t cpu_mhz;          // o clock no fim da medida
} tab5_h264_bancada_t;
bool tab5_h264_bancada(const char* caminho, tab5_h264_onde_t onde, bool hash, tab5_h264_bancada_t* r);
// O mesmo, sem esperar: o resultado sai no log ("h264: bancada: ..."). Da para
// olhar 'tarefas' enquanto mede.
bool tab5_h264_bancada_fundo(const char* caminho, tab5_h264_onde_t onde);
