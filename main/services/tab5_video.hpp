// TOCADOR DE VIDEO DO SISTEMA - toca do cartao (ou do pendrive):
//   - .mp4/.m4v/.mov com H.264 (o video do celular e da internet), com o som
//     AAC de dentro do arquivo;
//   - .avi MJPEG, com o som de um .mp3 de MESMO NOME ao lado
//     ("filme.avi" + "filme.mp3").
//
// QUEM USA E SO INTERFACE. O app Video (main/app_video) lista, pede para
// abrir, mostra o quadro e os botoes. Nenhum deles le o cartao, decodifica ou
// toca som: o servico faz tudo, numa tarefa propria ("video").
//
// COMO FUNCIONA
//   - ABRIR e rapido (so sobe a tarefa). A tarefa abre o arquivo pelo leitor
//     de tab5_volumes, le o indice do AVI (idx1; sem ele, varre o 'movi'),
//     liga o decodificador JPEG POR HARDWARE do P4 e, se houver o .mp3, pede
//     ao tocador de musica (services/tab5_musica) que o toque.
//   - O SOM MANDA NO TEMPO. O relogio do video e a posicao do som no
//     alto-falante (tab5_musica_estado.pos_ms): o quadro mostrado e o que
//     corresponde a ela. Se a decodificacao atrasa, PULA quadros (conta em
//     'perdidos') em vez de atrasar o resto. Sem .mp3, o relogio e o
//     esp_timer; se o som acaba antes do video, o video segue sozinho.
//   - O QUADRO PRONTO e RGB565 em dois buffers (duplo buffer). Quem mostra
//     recebe o aviso AVISO_VIDEO (quadro novo, pausa, fim, erro) e pede o
//     quadro; o app nao precisa de relogio.
//   - ESCALAR E COPIAR PARA A TELA e do servico (tab5_video_pintar, pelo PPA):
//     o app so diz em que retangulo da superficie de DMA (sistema_superficie)
//     o video deve aparecer.
//
// MP4: o decodificador H.264 e da CPU (services/tab5_h264): 360p fluido,
// 720p e 1080p aos pedacos (a politica de atraso pula quadros para o video
// nunca se atrasar do som). Um 1080p com muitos quadros de referencia nao
// cabe na PSRAM (erro "pesado demais para a memoria"). Ver "O MP4" no .cpp.
//
// LIMITES DO AVI: so MJPEG (fccHandler 'MJPG'); quadro ate TAB5_VIDEO_L_MAX x
// TAB5_VIDEO_A_MAX; o primeiro segmento RIFF (AVI OpenDML maior que 1 GB toca
// so o primeiro). O .avi bem feito (idx1) abre na hora; sem indice, a varredura
// le o arquivo todo (segundos a minutos).
//
// TAREFAS: tudo daqui pode ser chamado de qualquer tarefa (interface, console).
// Os controles so marcam um pedido; quem atende e a tarefa do video.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TAB5_VIDEO_L_MAX        1920          // maior largura aceita (pixels)
#define TAB5_VIDEO_A_MAX        1088          // maior altura aceita (pixels)
#define TAB5_VIDEO_QUADRO_MAX   (2u << 20)    // maior quadro JPEG aceito (bytes); maior que isso repete o anterior
#define TAB5_VIDEO_QUADROS_MAX  1000000u      // quadros na tabela (8 bytes cada, na PSRAM)

// As extensoes que o tocador abre, no formato do filtro de tab5_arq_listar.
#define TAB5_VIDEO_EXTENSOES    ".avi;.mp4;.m4v;.mov"

// A extensao e uma das de cima (maiuscula ou minuscula)? Nao toca no disco.
bool tab5_video_suportado(const char* nome);

// ABRE 'caminho' (absoluto: "/sdcard/Videos/filme.mp4") e comeca a tocar, com
// o som de dentro (MP4) ou de "filme.mp3" na mesma pasta (AVI). Fecha o que
// estava aberto. false = extensao que nao toca ou sem memoria; um arquivo que
// nao abre ou nao e de um formato aceito aparece depois em
// tab5_video_estado().erro.
bool tab5_video_abrir(const char* caminho);

void tab5_video_pausar_tocar(void);   // pausa; no fim do video, toca de novo do comeco
void tab5_video_ir_ms(uint32_t ms);   // vai para o ponto (o som vai junto)
void tab5_video_fechar(void);         // para tudo e solta o disco e a memoria; para o som

typedef struct {
  bool     aberto;      // ha um video aberto (inclusive abrindo ou com erro)
  bool     tocando;     // false = pausado, no fim ou com erro
  bool     com_som;     // o relogio e o do .mp3
  bool     fim;         // chegou ao fim
  bool     erro;        // nao deu (erro_txt diz por que)
  uint32_t pos_ms;      // onde esta
  uint32_t dur_ms;      // quanto dura (0 = ainda abrindo)
  uint32_t quadros;     // quadros no arquivo
  uint32_t mostrados;   // quadros decodificados e publicados
  uint32_t perdidos;    // quadros pulados porque o relogio ja passou deles
  int      l, a;        // tamanho do quadro (0 = ainda abrindo)
  float    fps;         // quadros mostrados por segundo (ultimo segundo)
  char     nome[64];    // nome do arquivo, sem a extensao
  char     erro_txt[64];
} tab5_video_estado_t;
void tab5_video_estado(tab5_video_estado_t* e);

// O QUADRO PRONTO MAIS NOVO (RGB565 do painel). 'passo' = pixels por linha do
// buffer (a largura arredondada para 16); a imagem util e l x a. false = nao ha
// quadro novo desde o ultimo pegar. O buffer fica do chamador ate
// tab5_video_soltar_quadro() (um por vez).
bool tab5_video_pegar_quadro(const uint16_t** px, int* l, int* a, int* passo);
void tab5_video_soltar_quadro(void);

// ESCALA o quadro para caber no retangulo (x, y, l, a) de uma superficie RGB565
// 'pic_l' x 'pic_a' (sistema_superficie) - mantendo a proporcao e centrado,
// pelo PPA, sem a CPU - e deixa preto o resto do retangulo. 'so_se_novo' =
// false repinta o ultimo quadro (voltou do repouso, a area foi limpa). Chamar
// da tarefa de interface. true = pintou (avise sistema_superficie_mudou).
bool tab5_video_pintar(void* destino, size_t destino_bytes, int pic_l, int pic_a,
                       int x, int y, int l, int a, bool so_se_novo);
