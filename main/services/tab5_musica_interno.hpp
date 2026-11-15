// O que os arquivos do tocador de musica compartilham: o estado usado por mais
// de um deles (declarado aqui, definido no arquivo indicado) e as funcoes
// internas que cruzam arquivos. So para eles: quem usa o servico inclui
// tab5_musica.hpp.
//
// O servico foi partido por assunto (so mudou de arquivo, nada de logica):
//   tab5_musica.cpp          estado, controles, fila, laco de reproducao, parte de fundo
//   tab5_musica_nomes.cpp    nomes: UTF-8, CP437 antigo, titulo a partir do arquivo
//   tab5_musica_decod.cpp    arquivo aberto (MP3/WAV/MP4), ID3, posicao e decodificacao
//   tab5_musica_espectro.cpp FFT e anel de quadros do espectro
//   tab5_musica_pasta.cpp    pasta de musica, listagem, playlists .m3u, tocar pasta/playlist
//   tab5_musica_duracao.cpp  duracao sem decodificar
//   tab5_musica_capa.cpp     capa: ID3, pasta, cache, tarefa propria
//   tab5_musica_rede.cpp     capa pela internet (busca do iTunes)
//
// Todos os .cpp acima estao no linker.lf (m5tabos_extram_bss): ao criar um
// arquivo novo do tocador, acrescente a linha dele la.
#pragma once

#include "tab5_musica.hpp"
#include "tab5_mutex.hpp"
#include "tab5_hash.hpp"
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

// ---------------------------------------------------------------------------
// MEMORIA E LEITURA DO CARTAO (ver os comentarios em tab5_musica.cpp)
// ---------------------------------------------------------------------------
#define BLOCO_QUADROS  (1152 * 16)
#define LER_PEDACO     (32 * 1024)    // uma leitura do cartao
#define ARQ_BUF        (128 * 1024)   // o que o decodificador tem pela frente
#define SETOR          512
#define MP4_ARENA      (512 * 1024)   // arena do leitor de MP4 (tocar e medir)
#define LATENCIA_US    60000          // do enfileirar ao alto-falante: DMA do I2S + mixer
#define CAPA_MAX_BYTES (2 * 1024 * 1024)   // capa maior que isto nao e capa

typedef enum { FMT_NADA = 0, FMT_MP3, FMT_WAV, FMT_MP4 } formato_t;

// A fila: os caminhos (relativos a pasta de musica), copiados de quem pediu.
typedef char caminho_t[TAB5_MUSICA_CAMINHO_MAX];

// O pedido de capa (a musica que toca) e o resultado de uma busca na internet.
typedef struct {
  char caminho[TAB5_MUSICA_CAMINHO_MAX];
  char titulo[64], artista[48], album[48];
} capa_ped_t;
typedef enum { NET_ACHOU, NET_NAO_TEM, NET_FALHOU } net_t;

// ---------------------------------------------------------------------------
// Estado usado por mais de um arquivo
// ---------------------------------------------------------------------------
// tab5_musica.cpp: fila + estado (tudo sob a trava)
extern Trava                  g_musica_trava;
extern tab5_musica_estado_t   g_musica_est;
extern caminho_t*             g_musica_fila;
extern int16_t*               g_musica_ordem;   // a ordem em que toca (indices em g_musica_fila)
extern int                    g_musica_fila_n;
extern char                   g_musica_fila_raiz[160];   // a pasta de musica, caminho absoluto

// tab5_musica_decod.cpp: buffers e posicao do arquivo aberto
extern uint8_t*  g_musica_arq_buf;
extern uint8_t*  g_musica_ler_buf;       // alinhado, capaz de DMA (PSRAM)
extern mp3dec_t* g_musica_mp3;
extern int16_t*  g_musica_quadro;        // 1152 x 2 amostras (PSRAM)
extern formato_t g_musica_fmt;
extern long      g_musica_dados_ini, g_musica_dados_fim;   // bytes de audio [ini, fim)
extern uint32_t  g_musica_hz;
extern uint64_t  g_musica_quadros;       // ja enfileirados (posicao)
extern uint32_t  g_musica_ult_q;         // quadros do ultimo bloco enfileirado
extern uint32_t  g_musica_ler_us;        // tempo lendo o cartao no bloco atual
extern uint64_t  g_musica_total_quadros; // 0 = desconhecido
extern uint32_t  g_musica_mp3_kbps_soma, g_musica_mp3_quadros;   // media de bitrate (VBR sem Xing)
extern bool      g_musica_dur_exata;     // a duracao veio do Xing (ou e WAV)

// tab5_musica_espectro.cpp:
extern int64_t   g_musica_fim_prev_us;   // quando acaba o som ja enfileirado (0 = fila vazia)

// ---------------------------------------------------------------------------
// Pequenas funcoes puras (inline: so leem bytes e texto)
// ---------------------------------------------------------------------------
static inline uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static inline uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static inline uint32_t syncsafe(const uint8_t* p) { return ((p[0] & 0x7F) << 21) | ((p[1] & 0x7F) << 14) | ((p[2] & 0x7F) << 7) | (p[3] & 0x7F); }

static inline bool termina_com(const char* s, const char* ext)
{
  const size_t a = strlen(s), b = strlen(ext);
  return a >= b && strcasecmp(s + a - b, ext) == 0;
}

// Conteiner MP4 (.mp4, .m4a, .m4v, .mov): o som sai daqui.
static inline bool e_mp4(const char* nome)
{
  return termina_com(nome, ".mp4") || termina_com(nome, ".m4a") || termina_com(nome, ".m4v")
      || termina_com(nome, ".mov");
}

// UM CAMINHO DE MUSICA e relativo a pasta de musica do cartao ("Rock/x.mp3")
// ou ABSOLUTO num disco ("/usb/Rock/x.mp3" - o pendrive). Os dois convivem
// numa mesma fila e numa mesma playlist.
static inline bool absoluto(const char* c) { return c && c[0] == '/'; }

// Hash do texto sem diferenciar maiuscula ("Queen" e "queen" sao a mesma
// capa): o FNV-1a do sistema (tab5_hash.hpp), com 'sem_caixa'.
static inline uint32_t fnv(const char* s, uint32_t h = TAB5_FNV1A_INICIO) { return tab5_fnv1a_txt(s, h, true); }

// ---------------------------------------------------------------------------
// Funcoes que cruzam arquivos
// ---------------------------------------------------------------------------
// tab5_musica_nomes.cpp
bool musica_utf8_por(char* out, size_t tam, size_t* k, uint32_t c);   // false = nao coube
bool musica_utf8_valido(const char* s);

// tab5_musica_decod.cpp
bool     musica_arquivo_aberto(void);
void     musica_fechar_arquivo(void);
bool     musica_abrir_item(int pos);     // false = arquivo ruim (o laco pula para a proxima)
void     musica_ir_para_ms(uint32_t ms);
size_t   musica_decodificar_bloco(int16_t* saida, uint32_t* hz, bool* estereo);

// tab5_musica_espectro.cpp
bool musica_espectro_alocar(void);
void musica_espectro_soltar(void);
void musica_espectro_esquecer(void);
void musica_espectro_bloco(const int16_t* pcm, size_t q, uint32_t hz, bool estereo, int64_t ini_us);

// tab5_musica_pasta.cpp
void musica_raiz_montado(char* out, size_t tam);   // a pasta de musica (lembrada por 5 s), com o cartao montado
bool musica_montar_rel(const char* rel);           // monta o disco onde 'rel' mora
void musica_desmontar_rel(const char* rel);

// tab5_musica.cpp
bool musica_fila_tocar(const caminho_t* itens, int n, int inicio, const char* raiz,
                       const char* pasta_rel, const char* playlist, const char* app);

// tab5_musica_capa.cpp
void musica_capa_pedir(const char* caminho, const char* titulo, const char* artista, const char* album);

// tab5_musica_rede.cpp
net_t musica_capa_internet(const capa_ped_t* p, uint8_t** img, size_t* n);
