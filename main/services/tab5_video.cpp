// TOCADOR DE VIDEO DO SISTEMA - ver tab5_video.hpp (o contrato) e o fim deste
// arquivo (a API publica). Aqui: o leitor de AVI, a tarefa que decodifica e o
// PPA que escala para a tela.

#include "tab5_video.hpp"
#include "tab5_volumes.hpp"
#include "tab5_musica.hpp"
#include "tab5_mutex.hpp"
#include "tab5_h264.hpp"              // o decodificador H.264 (MP4)
#include "tab5_midia.hpp"             // o leitor de MP4 (components/medf)
#include "medf_mp4.h"
#include "app.h"                  // sistema_avisar, AVISO_VIDEO
#include "idioma.h"                // N_: textos de erro que o app traduz

#include "driver/jpeg_decode.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"   // tarefa com pilha na PSRAM
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <strings.h>

static const char* TAG = "video";

// A ORDEM DAS CORES SAIDA DO DECODIFICADOR. BGR = o 565 do painel (vermelho nos
// bits altos). Se no aparelho vermelho e azul vierem trocados, e so inverter aqui.
#define VIDEO_ORDEM_RGB  JPEG_DEC_RGB_ELEMENT_ORDER_BGR

#define VIDEO_PILHA        12288  // pilha da tarefa (PSRAM)
#define VIDEO_CAMINHO_MAX  256    // caminho absoluto do video e do mp3
#define VIDEO_PRIO         3      // acima do worker (2), abaixo dos servicos de radio
#define VIDEO_ESPERA_SOM_MS 3000  // quanto espera o som comecar antes de seguir sem ele
#define VIDEO_FOLGA_SOM_MS  1500  // quanto o relogio do video pode andar a frente da ultima posicao da musica
#define VIDEO_FALHAS_MAX   30     // quadros seguidos sem decodificar = desiste

// ---------------------------------------------------------------------------
// O ARQUIVO AVI
// ---------------------------------------------------------------------------
// Um AVI e um RIFF: uma sequencia de pedacos [id 4 bytes][tamanho 4 bytes
// little endian][dados][1 byte de alinhamento se o tamanho e impar]. Os que
// importam: LIST 'hdrl' (cabecalhos), LIST 'movi' (os quadros: pedacos
// "00dc") e 'idx1' (o indice: onde esta cada quadro).
#define FCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))
#define FCC_RIFF FCC('R', 'I', 'F', 'F')
#define FCC_AVI  FCC('A', 'V', 'I', ' ')
#define FCC_LIST FCC('L', 'I', 'S', 'T')
#define FCC_HDRL FCC('h', 'd', 'r', 'l')
#define FCC_STRL FCC('s', 't', 'r', 'l')
#define FCC_MOVI FCC('m', 'o', 'v', 'i')
#define FCC_REC  FCC('r', 'e', 'c', ' ')
#define FCC_AVIH FCC('a', 'v', 'i', 'h')
#define FCC_STRH FCC('s', 't', 'r', 'h')
#define FCC_STRF FCC('s', 't', 'r', 'f')
#define FCC_IDX1 FCC('i', 'd', 'x', '1')
#define FCC_VIDS FCC('v', 'i', 'd', 's')
#define FCC_MJPG FCC('M', 'J', 'P', 'G')
#define FCC_MJPG_MIN FCC('m', 'j', 'p', 'g')

// Onde esta um quadro: a posicao dos DADOS no arquivo e o tamanho. tam = 0 =
// "repita o quadro anterior" (quadro vazio, grande demais ou fora do arquivo):
// o tempo continua certo.
typedef struct { uint32_t off, tam; } quadro_t;

// O que os cabecalhos dizem.
typedef struct {
  uint32_t us_quadro;     // microssegundos entre quadros (0 = nao disse)
  uint32_t l, a;          // do cabecalho (o JPEG do primeiro quadro manda)
  int      v;             // numero do fluxo de video (o "00" de "00dc")
  bool     tem_video;
  bool     mjpeg;
} cab_t;

static uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static bool ler_em(tab5_leitor_t* r, uint64_t pos, void* dst, size_t n)
{
  return tab5_leitor_ir(r, pos) && tab5_leitor_ler(r, dst, n) == (int)n;
}

// Le o cabecalho (id e tamanho) do pedaco em 'pos'. false = nao cabe ate 'fim'.
static bool pedaco(tab5_leitor_t* r, uint64_t pos, uint64_t fim, uint32_t* id, uint32_t* tam)
{
  uint8_t c[8];
  if (pos + 8 > fim || !ler_em(r, pos, c, 8)) { return false; }
  *id = u32(c);
  *tam = u32(c + 4);
  return true;
}

// Onde comeca o proximo pedaco depois do que esta em 'pos' (com o alinhamento).
static uint64_t depois(uint64_t pos, uint32_t tam) { return pos + 8 + (uint64_t)tam + (tam & 1); }

// Um LIST 'strl' (os dados de UM fluxo): strh diz o tipo, strf o formato.
static void ler_strl(tab5_leitor_t* r, uint64_t ini, uint64_t fim, int numero, cab_t* c)
{
  uint32_t tipo = 0, handler = 0, escala = 0, taxa = 0, compressao = 0, l = 0, a = 0;
  uint32_t id, tam;
  int n = 0;
  for (uint64_t p = ini; n < 32 && pedaco(r, p, fim, &id, &tam); p = depois(p, tam), ++n) {
    uint8_t b[40];
    if (id == FCC_STRH && tam >= 28 && ler_em(r, p + 8, b, 28)) {
      tipo = u32(b); handler = u32(b + 4); escala = u32(b + 20); taxa = u32(b + 24);
    } else if (id == FCC_STRF && tam >= 20 && ler_em(r, p + 8, b, 20)) {
      l = u32(b + 4); a = (uint32_t)(int32_t)u32(b + 8); compressao = u32(b + 16);
      if ((int32_t)a < 0) { a = (uint32_t)(-(int32_t)a); }   // altura negativa = de cima para baixo
    }
  }
  if (tipo != FCC_VIDS || c->tem_video) { return; }   // so o primeiro fluxo de video
  c->tem_video = true;
  c->v = numero;
  c->l = l;
  c->a = a;
  c->mjpeg = handler == FCC_MJPG || handler == FCC_MJPG_MIN || compressao == FCC_MJPG;
  if (taxa && escala) { c->us_quadro = (uint32_t)(((uint64_t)escala * 1000000u) / taxa); }
}

// O LIST 'hdrl': avih (o tempo entre quadros) e um strl por fluxo.
static void ler_hdrl(tab5_leitor_t* r, uint64_t ini, uint64_t fim, cab_t* c)
{
  uint32_t id, tam;
  int n = 0, fluxo = 0;
  for (uint64_t p = ini; n < 32 && pedaco(r, p, fim, &id, &tam); p = depois(p, tam), ++n) {
    uint8_t b[4];
    if (id == FCC_AVIH && tam >= 4 && ler_em(r, p + 8, b, 4)) {
      const uint32_t us = u32(b);
      if (!c->us_quadro) { c->us_quadro = us; }
    } else if (id == FCC_LIST && tam >= 4 && ler_em(r, p + 8, b, 4) && u32(b) == FCC_STRL) {
      ler_strl(r, p + 12, p + 8 + (uint64_t)tam, fluxo++, c);
    }
  }
}

// A tabela de quadros (cresce se preciso).
typedef struct {
  quadro_t* v;
  uint32_t  n, cap;
} tabela_t;

static bool tabela_cabe(tabela_t* t, uint32_t quer)
{
  if (quer > TAB5_VIDEO_QUADROS_MAX) { return false; }
  if (quer <= t->cap) { return true; }
  uint32_t cap = t->cap ? t->cap * 2 : 4096;
  while (cap < quer) { cap *= 2; }
  if (cap > TAB5_VIDEO_QUADROS_MAX) { cap = TAB5_VIDEO_QUADROS_MAX; }
  quadro_t* v = (quadro_t*)heap_caps_realloc(t->v, sizeof(quadro_t) * cap, MALLOC_CAP_SPIRAM);
  if (!v) { return false; }
  t->v = v;
  t->cap = cap;
  return true;
}

// Poe um quadro cujo pedaco comeca em 'cab' (o cabecalho de 8 bytes) e tem
// 'tam' bytes de dados. Fora do arquivo ou grande demais = quadro vazio.
static bool tabela_por(tabela_t* t, uint64_t cab, uint32_t tam, uint64_t limite)
{
  if (!tabela_cabe(t, t->n + 1)) { return false; }
  quadro_t q = { 0, 0 };
  if (tam && tam <= TAB5_VIDEO_QUADRO_MAX && cab + 8 + tam <= limite) {
    q.off = (uint32_t)(cab + 8);
    q.tam = tam;
  }
  t->v[t->n++] = q;
  return true;
}

static bool eh_quadro(uint32_t id, int v)
{
  return id == FCC('0' + v / 10, '0' + v % 10, 'd', 'c');
}

// O INDICE 'idx1': entradas de 16 bytes [id][flags][offset][tamanho]. O offset
// e relativo ao 'movi' em quase todo arquivo, mas alguns gravadores poem o
// absoluto: o primeiro quadro diz qual e (o pedaco tem que estar la mesmo).
static bool ler_idx1(tab5_leitor_t* r, uint64_t pos, uint64_t tam_idx, uint64_t movi_ini,
                     uint64_t limite, int v, tabela_t* t)
{
  const uint32_t total = (uint32_t)(tam_idx / 16);
  uint8_t bloco[16 * 256];
  bool base_ok = false;
  uint64_t base = 0;
  for (uint32_t i = 0; i < total;) {
    const uint32_t n = (total - i) < 256 ? (total - i) : 256;
    if (!ler_em(r, pos + (uint64_t)i * 16, bloco, (size_t)n * 16)) { return false; }
    for (uint32_t k = 0; k < n; ++k) {
      const uint8_t* e = bloco + k * 16;
      if (!eh_quadro(u32(e), v)) { continue; }
      const uint32_t off = u32(e + 8), tam = u32(e + 12);
      if (!base_ok) {
        // Relativo ao 4o byte antes do primeiro pedaco (o 'movi'); senao absoluto.
        const uint64_t cand[2] = { movi_ini - 4, 0 };
        for (int c = 0; c < 2 && !base_ok; ++c) {
          uint32_t id2, tam2;
          if (pedaco(r, cand[c] + off, limite, &id2, &tam2) && id2 == u32(e) && tam2 == tam) {
            base = cand[c];
            base_ok = true;
          }
        }
        if (!base_ok) { return false; }   // o indice nao bate com o arquivo: varre
      }
      if (!tabela_por(t, base + off, tam, limite)) { return false; }
    }
    i += n;
  }
  return t->n > 0;
}

// SEM INDICE: percorre o 'movi' pedaco por pedaco (le o arquivo todo).
// "LIST rec " (gravacao agrupada) e entrado, os outros pedacos pulados.
static bool varrer_movi(tab5_leitor_t* r, uint64_t ini, uint64_t fim, uint64_t limite, int v, tabela_t* t)
{
  uint32_t id, tam;
  for (uint64_t p = ini; pedaco(r, p, fim, &id, &tam);) {
    if (id == FCC_LIST) { p += 12; continue; }
    if (eh_quadro(id, v) && !tabela_por(t, p, tam, limite)) { return false; }
    p = depois(p, tam);
  }
  return t->n > 0;
}

// Le o arquivo todo: cabecalho + tabela. false = nao e um AVI MJPEG utilizavel
// ('erro' diz por que).
static bool ler_avi(tab5_leitor_t* r, cab_t* c, tabela_t* t, char* erro, size_t erro_tam)
{
  const uint64_t arq = tab5_leitor_tamanho(r);
  uint8_t h[12];
  if (arq < 12 || !ler_em(r, 0, h, 12) || u32(h) != FCC_RIFF || u32(h + 8) != FCC_AVI) {
    snprintf(erro, erro_tam, N_("não é um AVI"));
    return false;
  }
  uint64_t limite = arq;
  const uint64_t riff = (uint64_t)u32(h + 4) + 8;
  if (riff >= 12 && riff < arq) { limite = riff; }   // lixo depois do RIFF nao vale

  uint64_t movi_ini = 0, movi_fim = 0, idx_pos = 0, idx_tam = 0;
  bool movi_ok = false, idx_ok = false;
  uint32_t id, tam;
  int n = 0;
  for (uint64_t p = 12; n < 4096 && pedaco(r, p, limite, &id, &tam); p = depois(p, tam), ++n) {
    const uint64_t fim_pedaco = p + 8 + (uint64_t)tam;
    uint8_t b[4];
    if (id == FCC_LIST && tam >= 4 && ler_em(r, p + 8, b, 4)) {
      if (u32(b) == FCC_HDRL) {
        ler_hdrl(r, p + 12, fim_pedaco < limite ? fim_pedaco : limite, c);
      } else if (u32(b) == FCC_MOVI && !movi_ok) {
        movi_ok = true;
        movi_ini = p + 12;
        movi_fim = fim_pedaco < limite ? fim_pedaco : limite;   // gravacao cortada: vai ate onde ha arquivo
      }
    } else if (id == FCC_IDX1 && !idx_ok) {
      idx_ok = true;
      idx_pos = p + 8;
      idx_tam = fim_pedaco <= limite ? tam : (limite > p + 8 ? limite - (p + 8) : 0);
    }
  }
  if (!c->tem_video || !movi_ok) { snprintf(erro, erro_tam, N_("AVI sem vídeo")); return false; }
  if (!c->mjpeg) { snprintf(erro, erro_tam, N_("só vídeo MJPEG")); return false; }
  if (c->us_quadro < 5000 || c->us_quadro > 1000000) { c->us_quadro = 40000; }   // 25 quadros/s se o arquivo nao diz

  bool ok = idx_ok && ler_idx1(r, idx_pos, idx_tam, movi_ini, limite, c->v, t);
  if (!ok) {
    ESP_LOGW(TAG, "sem indice utilizavel: varrendo o movi");
    t->n = 0;
    ok = varrer_movi(r, movi_ini, movi_fim, limite, c->v, t);
  }
  if (!ok) { snprintf(erro, erro_tam, N_("AVI sem quadros")); return false; }
  return true;
}

// ---------------------------------------------------------------------------
// A SESSAO - um video por vez. Tudo daqui e da tarefa do video, menos o que a
// trava protege (o estado publicado e os quadros prontos).
// ---------------------------------------------------------------------------
static Trava s_ctrl;    // abrir/fechar: um de cada vez
static Trava s_trava;   // estado publicado e quadros

static tab5_video_estado_t s_est;           // sob s_trava
// Tres quadros decodificados (RGB565): o publicado, o que o app pode estar
// escalando e o proximo, ja preparado de antemao (ver mostrar).
#define VIDEO_BUFS 3
static uint8_t*  s_buf[VIDEO_BUFS];
static size_t    s_buf_bytes;
static int       s_passo, s_ql, s_qa;       // pixels por linha, largura e altura uteis
static int       s_ultimo = -1;             // o buffer do quadro mais novo publicado
static bool      s_novo = false;            // esse quadro ainda nao foi pego
static int       s_preso = -1;              // o buffer que o app esta usando

static TaskHandle_t       s_tarefa = nullptr;
static SemaphoreHandle_t  s_saiu = nullptr;   // a tarefa deu: vou sair (fechar espera)
static std::atomic<bool>    s_ped_fechar{false};
static std::atomic<bool>    s_ped_alternar{false};
static std::atomic<int64_t> s_ped_ir{-1};

static char s_caminho[VIDEO_CAMINHO_MAX];
static char s_mp3[VIDEO_CAMINHO_MAX];

static tab5_musica_estado_t s_mus;          // copia do estado da musica (so a tarefa usa)

// ---------------------------------------------------------------------------
// PUBLICAR
// ---------------------------------------------------------------------------
static void publicar_estado(const tab5_video_estado_t& e)
{
  s_trava.travar();
  const bool aberto = s_est.aberto;
  if (aberto) {
    s_est.tocando = e.tocando; s_est.com_som = e.com_som; s_est.fim = e.fim; s_est.erro = e.erro;
    s_est.pos_ms = e.pos_ms; s_est.dur_ms = e.dur_ms; s_est.quadros = e.quadros;
    s_est.mostrados = e.mostrados; s_est.perdidos = e.perdidos; s_est.l = e.l; s_est.a = e.a;
    s_est.fps = e.fps;
    snprintf(s_est.erro_txt, sizeof(s_est.erro_txt), "%s", e.erro_txt);
  }
  s_trava.destravar();
  sistema_avisar(AVISO_VIDEO);
}

// ---------------------------------------------------------------------------
// O SOM
// ---------------------------------------------------------------------------
// O som e a musica "<video sem .avi>.mp3" na mesma pasta; so conta se existe.
static bool tem_mp3(void)
{
  snprintf(s_mp3, sizeof(s_mp3), "%s", s_caminho);
  char* ponto = strrchr(s_mp3, '.');
  if (!ponto) { return false; }
  snprintf(ponto, sizeof(s_mp3) - (size_t)(ponto - s_mp3), ".mp3");
  tab5_leitor_t* t = tab5_leitor_abrir(s_mp3);   // so para saber se existe
  if (!t) { return false; }
  tab5_leitor_fechar(t);
  return true;
}

static bool som_nosso(void)
{
  tab5_musica_estado(&s_mus);
  return s_mus.ativo && !strcmp(s_mus.caminho, s_mp3);
}

// ---------------------------------------------------------------------------
// A TAREFA
// ---------------------------------------------------------------------------
typedef struct {
  tab5_leitor_t*          r;
  tabela_t                tab;
  cab_t                   cab;
  uint8_t*                entrada;       // o JPEG do quadro (memoria do decodificador)
  size_t                  entrada_bytes;
  jpeg_decoder_handle_t   dec;
  uint32_t                us_quadro;
  uint32_t                dur_ms;
  // O RELOGIO
  bool                    com_som;       // o relogio e o do mp3
  bool                    som_visto;     // o som ja tocou algum instante
  uint32_t                som_ultimo_ms; // a ultima posicao que a musica informou (ver relogio_ms)
  bool                    som_pausado;   // fomos nos que pausamos o som
  int64_t                 t_som;         // quando pediu o som (esp_timer, us)
  int64_t                 base_us;       // relogio proprio: pos = agora - base
  uint32_t                pos_ms;        // ultima posicao conhecida
  // O QUE JA MOSTROU
  int64_t                 ultimo;        // indice do ultimo quadro tratado (-1 = nenhum)
  bool                    pulo_livre;    // depois de um ir: o salto nao conta como perdido
  int64_t                 adiantado;     // indice do quadro ja decodificado de antemao (-1 = nenhum)
  int                     adiantado_buf; // o buffer onde ele esta
  int                     falhas;        // quadros seguidos sem decodificar
  uint32_t                mostrados, perdidos;
  // DIAGNOSTICO (vai ao log no fim): onde os quadros foram pulados e quanto
  // custou o pior quadro - a leitura do cartao e a decodificacao separadas.
  uint32_t                perdidos_1s;   // pulados no primeiro segundo (o som ainda abrindo)
  uint32_t                pior_ler_us, pior_dec_us;
  uint32_t                lentos;        // quadros que passaram de um periodo (leitura + decodificacao)
  uint64_t                soma_dec_us;   // para a media da decodificacao
  uint32_t                n_dec;         // quantas decodificacoes (a media divide por ela)
  int64_t                 t_fps;
  uint32_t                n_fps;
  float                   fps;
  // O MP4 (H.264) - ver "O MP4" mais abaixo
  bool                    e_mp4;
  medf_arena_t            arena;         // a leitura do arquivo (medf)
  medf_mp4_demux_t*       mp4;
  tab5_h264_t*            h264;
  uint8_t                 nal_tam;       // bytes do tamanho na frente de cada NAL (do avcC)
  bool                    acabou_leitura;// o arquivo nao tem mais pacotes de video
  bool                    drenado;       // e o decodificador ja soltou os que segurava
  bool                    tem_q;         // ha um quadro decodificado esperando a hora
  tab5_h264_quadro_t      q;
  bool                    alcancar;      // atrasado demais: so quadros-chave ate alcancar o som
  int64_t                 chave_us;      // o pts do ultimo quadro-chave decodificado
  bool                    mostrar_ja;    // depois de um salto: mostra o primeiro quadro na hora
  int                     escala16;      // o PPA reduz o quadro na conversao (16 = tamanho original)
  bool                    sem_memoria;   // o video nao cabe na memoria (o decodificador disse)
} sessao_t;

static sessao_t s_v;

static void liberar_sessao(void)
{
  // O app pode estar escalando um quadro agora (tab5_video_pintar): espera soltar.
  for (int i = 0; i < 40; ++i) {
    s_trava.travar();
    const bool preso = s_preso >= 0;
    s_trava.destravar();
    if (!preso) { break; }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  if (s_v.dec) { jpeg_del_decoder_engine(s_v.dec); s_v.dec = nullptr; }
  if (s_v.h264) {
    if (s_v.tem_q) { tab5_h264_soltar(s_v.h264, s_v.q.id); }
    tab5_h264_fechar(s_v.h264);
  }
  if (s_v.mp4) { medf_mp4_demux_close(s_v.mp4); }
  tab5_midia_arena_soltar(&s_v.arena);
  if (s_v.entrada) { heap_caps_free(s_v.entrada); s_v.entrada = nullptr; }
  s_trava.travar();
  for (int i = 0; i < VIDEO_BUFS; ++i) { if (s_buf[i]) { heap_caps_free(s_buf[i]); s_buf[i] = nullptr; } }
  s_ultimo = -1; s_novo = false; s_preso = -1;
  s_trava.destravar();
  if (s_v.tab.v) { heap_caps_free(s_v.tab.v); }
  if (s_v.r) { tab5_leitor_fechar(s_v.r); }
  memset(&s_v, 0, sizeof(s_v));
}

static void falhar(const char* txt)
{
  ESP_LOGE(TAG, "%s", txt);
  tab5_video_estado_t e;
  tab5_video_estado(&e);
  e.tocando = false;
  e.erro = true;
  snprintf(e.erro_txt, sizeof(e.erro_txt), "%s", txt);
  publicar_estado(e);
}

// Le o quadro 'i' da tabela para a entrada. false = leitura falhou.
static bool ler_quadro(uint32_t i)
{
  const quadro_t q = s_v.tab.v[i];
  return ler_em(s_v.r, q.off, s_v.entrada, q.tam);
}

// Abre o arquivo, o indice, o decodificador e os buffers. false = ja publicou o erro.
static bool preparar(void)
{
  char erro[64] = "";
  s_v.r = tab5_leitor_abrir(s_caminho);
  if (!s_v.r) { falhar(N_("não abriu o arquivo")); return false; }
  if (!ler_avi(s_v.r, &s_v.cab, &s_v.tab, erro, sizeof(erro))) { falhar(erro); return false; }

  uint32_t maior = 0, primeiro = UINT32_MAX;
  for (uint32_t i = 0; i < s_v.tab.n; ++i) {
    if (s_v.tab.v[i].tam > maior) { maior = s_v.tab.v[i].tam; }
    if (s_v.tab.v[i].tam && primeiro == UINT32_MAX) { primeiro = i; }
  }
  if (primeiro == UINT32_MAX) { falhar(N_("AVI sem quadros")); return false; }

  const jpeg_decode_memory_alloc_cfg_t cfg_in = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
  s_v.entrada = (uint8_t*)jpeg_alloc_decoder_mem(maior, &cfg_in, &s_v.entrada_bytes);
  if (!s_v.entrada) { falhar(N_("sem memória")); return false; }

  // O tamanho de verdade vem do JPEG do primeiro quadro.
  jpeg_decode_picture_info_t info = { };
  if (!ler_quadro(primeiro) || jpeg_decoder_get_info(s_v.entrada, s_v.tab.v[primeiro].tam, &info) != ESP_OK) {
    falhar(N_("JPEG ilegível"));
    return false;
  }
  if (info.width < 16 || info.height < 16 || info.width > TAB5_VIDEO_L_MAX || info.height > TAB5_VIDEO_A_MAX) {
    falhar(N_("quadro grande demais"));
    return false;
  }
  const int passo = ((int)info.width + 15) & ~15, alt = ((int)info.height + 15) & ~15;
  const jpeg_decode_memory_alloc_cfg_t cfg_out = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
  size_t bytes = 0;
  uint8_t* b[VIDEO_BUFS] = { };
  bool tem_todos = true;
  for (int k = 0; k < VIDEO_BUFS; ++k) {
    b[k] = (uint8_t*)jpeg_alloc_decoder_mem((size_t)passo * alt * 2, &cfg_out, &bytes);
    if (!b[k]) { tem_todos = false; }
  }
  const jpeg_decode_engine_cfg_t eng = { .intr_priority = 0, .timeout_ms = 200 };
  const char* falta = !tem_todos ? N_("sem memória")
                    : jpeg_new_decoder_engine(&eng, &s_v.dec) != ESP_OK ? N_("decodificador JPEG ocupado") : nullptr;
  if (falta) {
    for (int k = 0; k < VIDEO_BUFS; ++k) { heap_caps_free(b[k]); }
    falhar(falta);
    return false;
  }
  s_trava.travar();
  for (int k = 0; k < VIDEO_BUFS; ++k) { s_buf[k] = b[k]; }
  s_buf_bytes = bytes;
  s_passo = passo; s_ql = (int)info.width; s_qa = (int)info.height;
  s_trava.destravar();

  s_v.us_quadro = s_v.cab.us_quadro;
  s_v.dur_ms = (uint32_t)(((uint64_t)s_v.tab.n * s_v.us_quadro) / 1000);
  s_v.ultimo = -1;
  s_v.adiantado = -1;
  ESP_LOGI(TAG, "%s: %dx%d, %u quadros, %.1f qps, %u s", s_caminho, s_ql, s_qa, (unsigned)s_v.tab.n,
           1e6f / (float)s_v.us_quadro, (unsigned)(s_v.dur_ms / 1000));
  return true;
}

// Um buffer livre: nem o publicado, nem o que o app esta usando, nem 'fora'
// (o quadro adiantado que ainda vale). Com tres buffers sempre sobra um.
static int buffer_livre(int fora)
{
  Trava::Guarda g(s_trava);
  for (int k = 0; k < VIDEO_BUFS; ++k) {
    if (k != s_ultimo && k != s_preso && k != fora) { return k; }
  }
  return -1;
}

// Le e decodifica o quadro 'i' no buffer 'b'. Mede o custo (diagnostico).
static bool decodificar(uint32_t i, int b)
{
  const jpeg_decode_cfg_t cfg = { .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
                                  .rgb_order = VIDEO_ORDEM_RGB,
                                  .conv_std = JPEG_YUV_RGB_CONV_STD_BT601 };
  uint32_t saiu = 0;
  const int64_t t0 = esp_timer_get_time();
  const bool lido = ler_quadro(i);
  const int64_t t1 = esp_timer_get_time();
  esp_err_t err = lido ? jpeg_decoder_process(s_v.dec, &cfg, s_v.entrada, s_v.tab.v[i].tam,
                                              s_buf[b], (uint32_t)s_buf_bytes, &saiu)
                       : ESP_FAIL;
  const int64_t t2 = esp_timer_get_time();
  if ((uint32_t)(t1 - t0) > s_v.pior_ler_us) { s_v.pior_ler_us = (uint32_t)(t1 - t0); }
  if ((uint32_t)(t2 - t1) > s_v.pior_dec_us) { s_v.pior_dec_us = (uint32_t)(t2 - t1); }
  if (t2 - t0 > (int64_t)s_v.us_quadro) { ++s_v.lentos; }
  s_v.soma_dec_us += (uint64_t)(t2 - t1);
  s_v.n_dec++;
  if (err != ESP_OK) {
    if (s_v.falhas++ < 3) { ESP_LOGW(TAG, "quadro %u: %s", (unsigned)i, lido ? esp_err_to_name(err) : "leitura falhou"); }
    return false;
  }
  s_v.falhas = 0;
  return true;
}

// Publica o quadro 'i' e ja prepara o seguinte. Sempre deixa s_v.ultimo = i
// (um quadro que falha nao e tentado de novo).
//
// Por que adiantar: o custo de um quadro varia - a media e de poucos ms, mas
// de vez em quando o cartao ou o JPEG por hardware levam 40-70 ms (disputa da
// PSRAM com o som e a tela). Decodificado na hora de mostrar, esse quadro
// passava do seu periodo e era pulado. Preparado logo depois do anterior, ele
// tem quase dois periodos de folga.
static void mostrar(uint32_t i)
{
  if (s_v.ultimo >= 0 && (int64_t)i > s_v.ultimo + 1 && !s_v.pulo_livre) {
    s_v.perdidos += (uint32_t)((int64_t)i - s_v.ultimo - 1);
    if ((uint64_t)i * s_v.us_quadro < 1000000) { s_v.perdidos_1s += (uint32_t)((int64_t)i - s_v.ultimo - 1); }
  }
  s_v.pulo_livre = false;
  s_v.ultimo = i;

  // O quadro desse instante: o adiantado, se for ele; senao, decodifica agora.
  int b = -1;
  if (s_v.adiantado == (int64_t)i) {
    b = s_v.adiantado_buf;
  } else if (s_v.tab.v[i].tam != 0) {              // quadro vazio: o anterior continua na tela
    b = buffer_livre(-1);
    if (b >= 0 && !decodificar(i, b)) { b = -1; }
  }
  s_v.adiantado = -1;                              // o adiantado foi usado ou nao vale mais

  if (b >= 0) {
    s_trava.travar();
    s_ultimo = b;
    s_novo = true;
    s_trava.destravar();

    ++s_v.mostrados;
    ++s_v.n_fps;
    const int64_t agora = esp_timer_get_time();
    if (agora - s_v.t_fps >= 1000000) {
      s_v.fps = (float)s_v.n_fps * 1e6f / (float)(agora - s_v.t_fps);
      s_v.t_fps = agora;
      s_v.n_fps = 0;
    }
  }

  // Prepara o proximo enquanto este fica na tela.
  const uint32_t j = i + 1;
  if (j < s_v.tab.n && s_v.tab.v[j].tam != 0) {
    const int bj = buffer_livre(-1);
    if (bj >= 0 && decodificar(j, bj)) { s_v.adiantado = j; s_v.adiantado_buf = bj; }
  }
}

// Espera o pedido ou o tempo. Os controles acordam a tarefa por notificacao.
static void esperar(uint32_t ms)
{
  ulTaskNotifyTake(pdTRUE, ms ? pdMS_TO_TICKS(ms) : 1);
}

// ---- o relogio -----------------------------------------------------------
// A posicao de AGORA, em ms. Com som, e a do alto-falante; sem, e o esp_timer.
static uint32_t relogio_ms(void)
{
  const int64_t agora = esp_timer_get_time();
  if (s_v.com_som) {
    if (som_nosso()) {
      if (!s_mus.tocando) { return s_v.pos_ms; }   // carregando ou pausado de fora: o video espera
      // A POSICAO DA MUSICA ANDA AOS SALTOS: ela so muda quando um bloco de
      // audio sai (centenas de ms). Seguida ao pe da letra, o video mostrava
      // um quadro por salto e pulava o resto (1 qps com som, 25 sem). Entao o
      // relogio anda sozinho (esp_timer) entre os saltos e se REALINHA a cada
      // posicao nova da musica (o som manda). O quadro mostrado nunca volta
      // (s_v.pos_ms so cresce: se o relogio adiantou, a imagem espera o som) e
      // nunca passa da musica mais que um bloco e pouco (som engasgou: espera).
      if (!s_v.som_visto || s_mus.pos_ms != s_v.som_ultimo_ms) {
        s_v.som_ultimo_ms = s_mus.pos_ms;
        s_v.base_us = agora - (int64_t)s_mus.pos_ms * 1000;
        s_v.som_visto = true;
      }
      uint32_t pos = (uint32_t)((agora - s_v.base_us) / 1000);
      if (pos > s_mus.pos_ms + VIDEO_FOLGA_SOM_MS) { pos = s_mus.pos_ms + VIDEO_FOLGA_SOM_MS; }
      if (pos > s_v.pos_ms) { s_v.pos_ms = pos; }
      return s_v.pos_ms;
    }
    if (s_v.som_visto || agora - s_v.t_som > (int64_t)VIDEO_ESPERA_SOM_MS * 1000) {
      // O som acabou antes do video (ou nunca veio): segue sozinho de onde estava.
      ESP_LOGI(TAG, "%s: relogio proprio a partir de %u ms", s_v.som_visto ? "som acabou" : "sem som", (unsigned)s_v.pos_ms);
      s_v.com_som = false;
      s_v.base_us = agora - (int64_t)s_v.pos_ms * 1000;
    } else {
      return s_v.pos_ms;   // o som ainda nao comecou
    }
  }
  s_v.pos_ms = (uint32_t)((agora - s_v.base_us) / 1000);
  return s_v.pos_ms;
}

// Liga o som (de novo): a musica toca do comeco e, se 'ms' > 0, vai para la.
static void ligar_som(uint32_t ms)
{
  s_v.com_som = false;
  if (!s_mp3[0]) { return; }
  // Trocando de video, a musica do anterior ainda pode estar parando: tenta
  // por ate 1 s antes de desistir do som.
  bool ligou = false;
  for (int i = 0; i < 20 && !(ligou = tab5_musica_tocar_um(s_mp3, "Vídeo")); ++i) { vTaskDelay(pdMS_TO_TICKS(50)); }
  if (!ligou) { ESP_LOGW(TAG, "o som nao ligou"); return; }
  if (ms) { tab5_musica_ir_para(ms); }
  s_v.com_som = true;
  s_v.som_visto = false;
  s_v.som_pausado = false;
  s_v.t_som = esp_timer_get_time();
}

// A musica so tem "alternar": confere se ja esta como se quer.
// 'som_pausado' lembra o que NOS pedimos: o estado da musica demora um bloco
// para refletir o pedido, e um pausar/tocar rapido leria o estado velho.
static void som_pausar(bool pausar)
{
  if (!s_v.com_som || !som_nosso() || s_v.som_pausado == pausar) { return; }
  if (pausar && !s_mus.tocando) { return; }   // ja estava parado de fora
  tab5_musica_tocar_pausar();
  s_v.som_pausado = pausar;
}

// Vai para 'ms': o relogio, o som e o quadro desse instante.
static void ir_para(uint32_t ms, bool tocando)
{
  if (ms >= s_v.dur_ms) { ms = s_v.dur_ms ? s_v.dur_ms - 1 : 0; }
  s_v.pos_ms = ms;
  s_v.base_us = esp_timer_get_time() - (int64_t)ms * 1000;
  s_v.pulo_livre = true;
  if (s_v.com_som && som_nosso()) {
    tab5_musica_ir_para(ms);
    // A posicao que a musica informa ainda e a de ANTES do pulo: guardada como
    // "ja vista", o relogio nao se realinha nela (ver relogio_ms) e segue do
    // ponto novo ate a musica informar a posicao de depois do pulo.
    s_v.som_ultimo_ms = s_mus.pos_ms;
    if (tocando) { som_pausar(false); }
  } else if (s_mp3[0] && tocando) {
    ligar_som(ms);   // o som tinha acabado: toca de novo a partir dai
  }
}

static void laco(void)
{
  tab5_video_estado_t e = { };
  e.aberto = true; e.com_som = false;
  e.quadros = s_v.tab.n; e.dur_ms = s_v.dur_ms; e.l = s_ql; e.a = s_qa;

  bool tocando = true, fim = false;
  s_v.t_fps = esp_timer_get_time();
  s_v.base_us = s_v.t_fps;
  if (tem_mp3()) { ligar_som(0); } else { s_mp3[0] = '\0'; }   // o som so conta se o .mp3 existe
  uint32_t pub_pos = UINT32_MAX;

  for (;;) {
    if (s_ped_fechar) { break; }
    bool mudou = false;

    // ---- pedidos ----
    if (s_ped_alternar.exchange(false)) {
      if (fim) {                                   // acabou: toca de novo do comeco
        fim = false; tocando = true;
        ir_para(0, true);
      } else if (tocando) {
        tocando = false;
        relogio_ms();                              // guarda a posicao de agora
        som_pausar(true);
      } else {
        tocando = true;
        s_v.base_us = esp_timer_get_time() - (int64_t)s_v.pos_ms * 1000;
        if (s_v.com_som && !som_nosso()) { ligar_som(s_v.pos_ms); }
        som_pausar(false);
      }
      mudou = true;
    }
    const int64_t ir = s_ped_ir.exchange(-1);
    if (ir >= 0) {
      if (fim) { fim = false; tocando = true; }
      ir_para((uint32_t)ir, tocando);
      s_v.ultimo = -1;                             // mostra o quadro desse instante, mesmo pausado
      mudou = true;
    }

    // ---- o quadro desse instante ----
    const uint32_t pos = tocando ? relogio_ms() : s_v.pos_ms;
    uint64_t alvo = (uint64_t)pos * 1000u / s_v.us_quadro;
    uint32_t espera = 0;
    bool atrasado = false;                         // mostrou um quadro antes do desse instante
    if (!fim && alvo >= s_v.tab.n) {                // passou do ultimo quadro
      fim = true; tocando = false;
      ESP_LOGI(TAG, "fim: %u mostrados, %u pulados (%u no 1o segundo); pior quadro: leitura %u ms, decodificacao %u ms (media %u ms); %u quadros lentos",
               (unsigned)s_v.mostrados, (unsigned)s_v.perdidos, (unsigned)s_v.perdidos_1s,
               (unsigned)(s_v.pior_ler_us / 1000), (unsigned)(s_v.pior_dec_us / 1000),
               (unsigned)(s_v.n_dec ? s_v.soma_dec_us / s_v.n_dec / 1000 : 0), (unsigned)s_v.lentos);
      s_v.pos_ms = s_v.dur_ms;
      if (s_v.com_som && som_nosso()) { tab5_musica_parar(); }
      s_v.com_som = false;
      mudou = true;
    } else if (!fim && (tocando || s_v.ultimo < 0) && (int64_t)alvo != s_v.ultimo) {
      // Atrasou so um quadro e o que faltava ja esta pronto: mostra ele agora
      // (um instante atrasado) em vez de pular, e o desse instante logo depois.
      if (tocando && s_v.adiantado >= 0 && (int64_t)alvo == s_v.adiantado + 1) {
        mostrar((uint32_t)s_v.adiantado);
        atrasado = true;
      } else {
        mostrar((uint32_t)alvo);
      }
      mudou = true;
    }
    if (s_v.falhas >= VIDEO_FALHAS_MAX) {
      tocando = false;
      e.erro = true;
      snprintf(e.erro_txt, sizeof(e.erro_txt), N_("não consegui decodificar os quadros"));
      s_v.falhas = 0;
      if (s_v.com_som && som_nosso()) { som_pausar(true); }
      mudou = true;
    }

    // ---- publica: a cada quadro novo ou mudanca, e a posicao a cada 250 ms ----
    if (mudou || (tocando && pos / 250 != pub_pos / 250)) {
      e.tocando = tocando; e.fim = fim; e.com_som = s_v.com_som;
      e.pos_ms = s_v.pos_ms; e.mostrados = s_v.mostrados; e.perdidos = s_v.perdidos; e.fps = s_v.fps;
      pub_pos = pos;
      publicar_estado(e);
    }

    // ---- espera ate o proximo quadro (ou ate alguem pedir algo) ----
    if (tocando && !e.erro) {
      const int64_t falta = (int64_t)(alvo + 1) * s_v.us_quadro / 1000 - (int64_t)s_v.pos_ms;
      espera = (falta < 1 || atrasado) ? 1 : (falta > 30 ? 30 : (uint32_t)falta);
      esperar(espera);
    } else {
      esperar(1000);   // parado: so acorda por pedido (e confere o fechar de vez em quando)
    }
  }
  if (s_v.com_som && som_nosso()) { tab5_musica_parar(); }   // fechar o video cala o som
}

// ---------------------------------------------------------------------------
// O MP4 (H.264) - o video do celular e da internet
// ---------------------------------------------------------------------------
// O leitor de MP4 (components/medf, pelo tab5_leitor) entrega os pacotes de
// video na ordem de decodificacao; o decodificador (services/tab5_h264)
// devolve os quadros na ordem de exibicao, cada um com o seu pts. O SOM e o
// do proprio arquivo: o tocador de musica toca a faixa AAC dele (o mesmo
// arquivo, aberto por ele) e o relogio e o mesmo do AVI + .mp3.
//
// A CPU NAO DA CONTA DE TUDO (360p ~12 qps, 720p ~2 qps): a politica de
// atraso decide o que nao decodificar, para o video nunca se atrasar do som:
//   - atrasado mais que VIDEO_ATRASO_PULA_MS: os quadros que ninguem usa como
//     referencia (quadros B, quase sempre) nem sao decodificados;
//   - atrasado mais que VIDEO_ATRASO_SALTA_MS: os quadros seguintes ja nao
//     seriam mostrados, entao pula ate o proximo quadro-chave (o resto da
//     sequencia depende do que foi pulado) e recomeca dele, na hora certa.
//     Um video pesado mostra o comeco de cada sequencia (com quadro-chave a
//     cada 1 s, como o do celular, fica fluido aos pedacos); pesado demais,
//     vira uma sequencia de fotos - mas o som esta sempre certo;
//   - um quadro que sai atrasado mais que VIDEO_ATRASO_MOSTRA_MS nao e
//     mostrado (menos o quadro-chave do modo alcancar: e a unica imagem que
//     ha). Ate la, mostra: melhor uma imagem um pouco atrasada que nenhuma.
#define VIDEO_MP4_L_MAX        1920        // maior quadro H.264 aceito (o decodificador se ajusta)
#define VIDEO_MP4_A_MAX        1088
#define VIDEO_MP4_ARENA        (1024 * 1024)   // leitura do arquivo: 768 KB antecipados + pacote de ate 256 KB
#define VIDEO_ATRASO_PULA_MS   80
#define VIDEO_ATRASO_MOSTRA_MS 200
#define VIDEO_ATRASO_SALTA_MS  250   // logo acima do MOSTRA: decodificar o que nao vai aparecer e so gastar
// Prioridade da tarefa no MP4: a mesma da bancada (tab5_h264). Com 3 (a do
// AVI) a decodificacao perdia o nucleo para as tarefas de leitura e de rede.
#define VIDEO_MP4_PRIO         4
#define VIDEO_MP4_PILHA        8192        // pilha da tarefa no MP4 (interna: o decodificador usa a PIE)

static bool e_mp4(const char* caminho)
{
  const char* ponto = strrchr(caminho, '.');
  return ponto && (!strcasecmp(ponto, ".mp4") || !strcasecmp(ponto, ".m4v") || !strcasecmp(ponto, ".mov"));
}

// O PPA da conversao YUV -> RGB565 (outro cliente: o de tab5_video_pintar e da
// tarefa de interface). Modo bloqueante: a tarefa do video espera o PPA, que
// leva poucos ms, e so entao segue.
static ppa_client_handle_t s_ppa_conv = nullptr;

// O CLOCK: o H.264 e decodificado pela CPU. No ESP-IDF a CPU so vai ao maximo
// com uma trava ESP_PM_CPU_FREQ_MAX; sem ela, fica no piso do perfil e a
// decodificacao levava 4x mais (279 ms por quadro de 360p em vez de 71).
// Tocando: segura. Pausado ou no fim: solta (bateria).
static esp_pm_lock_handle_t s_pm_trava = nullptr;
static bool                 s_pm_segura = false;

static void clock_maximo(bool sim)
{
  if (!s_pm_trava && esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "video", &s_pm_trava) != ESP_OK) { s_pm_trava = nullptr; return; }
  if (sim == s_pm_segura) { return; }
  if (sim) { esp_pm_lock_acquire(s_pm_trava); } else { esp_pm_lock_release(s_pm_trava); }
  s_pm_segura = sim;
}

static void mp4_ppa_soltar(void)
{
  if (s_ppa_conv) { ppa_unregister_client(s_ppa_conv); s_ppa_conv = nullptr; }
}

// O avcC (o cabecalho do H.264 no MP4) guarda o SPS e o PPS assim:
// [versao][perfil][compat][nivel][tamanho-1 do comprimento][n de SPS] e cada
// SPS com 2 bytes de tamanho na frente; depois [n de PPS] e cada PPS igual.
// O decodificador quer NALs: viram Annex-B (00 00 00 01 antes de cada um) e
// entram de uma vez. false = avcC estragado ou sem SPS/PPS.
static bool entregar_avcc(const uint8_t* c, size_t n)
{
  if (n < 7 || c[0] != 1) { return false; }
  uint8_t* nal = (uint8_t*)heap_caps_malloc(n * 2 + 64, MALLOC_CAP_SPIRAM);   // cabe com folga: cada NAL ganha 2 bytes
  if (!nal) { return false; }
  size_t k = 0, i = 5;
  int contados = 0;
  for (int parte = 0; parte < 2; ++parte) {   // 0 = SPS, 1 = PPS
    if (i >= n) { break; }
    const int quantos = parte == 0 ? (c[i] & 0x1F) : c[i];
    ++i;
    for (int j = 0; j < quantos && i + 2 <= n; ++j) {
      const size_t t = ((size_t)c[i] << 8) | c[i + 1];
      i += 2;
      if (i + t > n) { break; }
      nal[k++] = 0; nal[k++] = 0; nal[k++] = 0; nal[k++] = 1;
      memcpy(nal + k, c + i, t);
      k += t;
      i += t;
      ++contados;
    }
  }
  if (contados >= 2) { tab5_h264_decodificar(s_v.h264, nal, k, 0, 0); }
  heap_caps_free(nal);
  return contados >= 2;
}

// Abre o MP4, o decodificador e os buffers. false = ja publicou o erro.
static bool preparar_mp4(void)
{
  tab5_midia_preparar();
  if (!tab5_midia_arena(&s_v.arena, VIDEO_MP4_ARENA)) { falhar(N_("sem memória")); return false; }
  const char* erro = nullptr;
  s_v.mp4 = medf_mp4_demux_open(s_caminho, &s_v.arena, false, &erro);
  if (!s_v.mp4) {
    ESP_LOGW(TAG, "mp4: %s", erro ? erro : "?");
    falhar(N_("não é um MP4 que eu leia"));
    return false;
  }
  const medf_mp4_info_t* in = medf_mp4_demux_info(s_v.mp4);
  if (in->video.codec != MEDF_MP4_VIDEO_CODEC_H264) {
    falhar(in->video.codec == MEDF_MP4_VIDEO_CODEC_NONE ? N_("MP4 sem vídeo") : N_("só vídeo H.264"));
    return false;
  }
  const int l = (int)in->video.width, a = (int)in->video.height;
  if (l < 16 || a < 16 || l > VIDEO_MP4_L_MAX || a > VIDEO_MP4_L_MAX || (l > VIDEO_MP4_A_MAX && a > VIDEO_MP4_A_MAX)) {
    falhar(N_("quadro grande demais"));
    return false;
  }
  // avcC: o 5o byte diz o tamanho do comprimento na frente de cada NAL.
  const uint8_t* avcc = in->video.codec_private;
  if (!avcc || in->video.codec_private_size < 7 || avcc[0] != 1) { falhar(N_("MP4 sem avcC")); return false; }
  s_v.nal_tam = (uint8_t)((avcc[4] & 3) + 1);

  // presos = 1: o quadro e convertido para RGB e solto na hora (no 1080p cada
  // quadro preso a mais e 3 MB de PSRAM).
  // O limite e o do tamanho CODIFICADO (multiplo de 16): 854 de largura vira 864.
  const int lado = ((l > a ? l : a) + 15) & ~15;
  const tab5_h264_cfg_t cfg = { lado, lado, TAB5_H264_AUTO, 1 };
  s_v.h264 = tab5_h264_abrir(&cfg);
  if (!s_v.h264) { falhar(N_("sem memória para o decodificador")); return false; }
  // O SPS e o PPS vem no avcC: entram antes do primeiro pacote.
  if (!entregar_avcc(avcc, in->video.codec_private_size)) { falhar(N_("MP4 sem avcC")); return false; }

  // Os quadros prontos: RGB565 na PSRAM, alinhados para o PPA, nunca maiores
  // que a tela (1280 x 720, deitado ou em pe): o PPA reduz na conversao, em
  // passos de 1/16. Um 1080p em tamanho cheio seria 4 MB por buffer.
  int e16 = 16;
  const int maior = l > a ? l : a, menor = l > a ? a : l;
  while (e16 > 1 && (maior * e16 / 16 > 1280 || menor * e16 / 16 > 720)) { --e16; }
  s_v.escala16 = e16;
  const int ol = l * e16 / 16, oa = a * e16 / 16;
  const int passo = (ol + 15) & ~15, alt = (oa + 15) & ~15;
  const size_t bytes = ((size_t)passo * alt * 2 + 127) & ~(size_t)127;
  uint8_t* b[VIDEO_BUFS] = { };
  bool tem_todos = true;
  for (int k = 0; k < VIDEO_BUFS; ++k) {
    b[k] = (uint8_t*)heap_caps_aligned_calloc(128, 1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!b[k]) { tem_todos = false; }
  }
  const ppa_client_config_t pc = { .oper_type = PPA_OPERATION_SRM };
  if (!tem_todos || (!s_ppa_conv && ppa_register_client(&pc, &s_ppa_conv) != ESP_OK)) {
    s_ppa_conv = nullptr;
    for (int k = 0; k < VIDEO_BUFS; ++k) { heap_caps_free(b[k]); }
    falhar(N_("sem memória"));
    return false;
  }
  s_trava.travar();
  for (int k = 0; k < VIDEO_BUFS; ++k) { s_buf[k] = b[k]; }
  s_buf_bytes = bytes;
  s_passo = passo; s_ql = ol; s_qa = oa;
  s_trava.destravar();

  s_v.us_quadro = in->video.frame_interval_us > 1000 ? (uint32_t)in->video.frame_interval_us : 33333;
  s_v.dur_ms = in->duration_us > 0 ? (uint32_t)(in->duration_us / 1000) : 0;
  s_v.tab.n = s_v.dur_ms ? (uint32_t)((uint64_t)s_v.dur_ms * 1000 / s_v.us_quadro) : 0;   // so para o estado
  s_v.chave_us = -1;
  s_v.mostrar_ja = true;   // o primeiro quadro aparece logo, mesmo pausado
  // O som: a faixa AAC do proprio arquivo (o tocador de musica toca); sem
  // ela, um .mp3 de mesmo nome ao lado, como no AVI.
  if (in->audio.codec == MEDF_MP4_AUDIO_CODEC_AAC) { snprintf(s_mp3, sizeof(s_mp3), "%s", s_caminho); }
  else if (!tem_mp3()) { s_mp3[0] = '\0'; }
  ESP_LOGI(TAG, "%s: H.264 %dx%d (tela %dx%d), %.1f qps, %u s, som %s, arena do decodificador %u KB", s_caminho, l, a, ol, oa,
           1e6f / (float)s_v.us_quadro, (unsigned)(s_v.dur_ms / 1000),
           in->audio.codec == MEDF_MP4_AUDIO_CODEC_AAC ? "AAC" : s_mp3[0] ? ".mp3 ao lado" : "nenhum",
           (unsigned)(tab5_h264_arena_bytes(s_v.h264) / 1024));
  return true;
}

// Converte o quadro decodificado (YUV420 empacotado) para RGB565 num buffer
// livre e o publica. false = o PPA recusou (o quadro e so pulado).
static bool publicar_quadro_mp4(const tab5_h264_quadro_t& q)
{
  const int b = buffer_livre(-1);
  const int e16 = s_v.escala16;
  if (b < 0 || q.l * e16 / 16 > s_ql || q.a * e16 / 16 > s_qa) { return false; }
  ppa_srm_oper_config_t c = { };
  c.in.buffer          = q.px;
  c.in.pic_w           = (uint32_t)q.l_cod;
  c.in.pic_h           = (uint32_t)q.a_cod;
  c.in.block_w         = (uint32_t)q.l;
  c.in.block_h         = (uint32_t)q.a;
  c.in.block_offset_x  = (uint32_t)q.x;
  c.in.block_offset_y  = (uint32_t)q.y;
  c.in.srm_cm          = PPA_SRM_COLOR_MODE_YUV420;
  c.in.yuv_range       = q.faixa_cheia ? PPA_COLOR_RANGE_FULL : PPA_COLOR_RANGE_LIMIT;
  c.in.yuv_std         = q.bt709 ? PPA_COLOR_CONV_STD_RGB_YUV_BT709 : PPA_COLOR_CONV_STD_RGB_YUV_BT601;
  c.out.buffer         = s_buf[b];
  c.out.buffer_size    = (uint32_t)s_buf_bytes;
  c.out.pic_w          = (uint32_t)s_passo;
  c.out.pic_h          = (uint32_t)((s_qa + 15) & ~15);
  c.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;
  c.rotation_angle     = PPA_SRM_ROTATION_ANGLE_0;
  c.scale_x = c.scale_y = (float)e16 / 16.0f;
  c.mode               = PPA_TRANS_MODE_BLOCKING;
  if (ppa_do_scale_rotate_mirror(s_ppa_conv, &c) != ESP_OK) { ESP_LOGW(TAG, "o PPA recusou o quadro"); return false; }

  s_trava.travar();
  s_ultimo = b;
  s_novo = true;
  s_trava.destravar();
  ++s_v.mostrados;
  ++s_v.n_fps;
  const int64_t agora = esp_timer_get_time();
  if (agora - s_v.t_fps >= 1000000) {
    s_v.fps = (float)s_v.n_fps * 1e6f / (float)(agora - s_v.t_fps);
    s_v.t_fps = agora;
    s_v.n_fps = 0;
  }
  return true;
}

// Decodifica ate sair o proximo quadro (fica em s_v.q), aplicando a politica
// de atraso. 'pos' = o relogio agora (ms). false = nao ha mais quadros.
static bool proximo_quadro_mp4(uint32_t pos)
{
  medf_buffer_t* leitura = medf_mp4_demux_buffer(s_v.mp4);
  for (int voltas = 0; voltas < 600 && !s_ped_fechar; ++voltas) {
    if (tab5_h264_quadro(s_v.h264, &s_v.q)) { s_v.tem_q = true; return true; }
    if (s_v.acabou_leitura) {
      if (s_v.drenado) { return false; }
      tab5_h264_fim(s_v.h264);        // solta os que estavam na fila de reordenacao
      s_v.drenado = true;
      continue;
    }
    medf_mp4_packet_t p;
    const int64_t t0 = esp_timer_get_time();
    if (!medf_mp4_demux_read(s_v.mp4, &p, false)) { s_v.acabou_leitura = true; continue; }
    const uint32_t ler_us = (uint32_t)(esp_timer_get_time() - t0);
    if (ler_us > s_v.pior_ler_us) { s_v.pior_ler_us = ler_us; }
    if (p.type != MEDF_MP4_PACKET_VIDEO) { medf_buffer_release(leitura, p.ref); continue; }

    const int64_t atraso_ms = (int64_t)pos - p.pts_us / 1000;
    // Alcancar: muito atrasado, so quadros-chave (o resto depende do que foi pulado).
    if (!s_v.mostrar_ja && atraso_ms > VIDEO_ATRASO_SALTA_MS) { s_v.alcancar = true; }
    if (s_v.alcancar && !p.keyframe) { medf_buffer_release(leitura, p.ref); ++s_v.perdidos; continue; }
    if (s_v.alcancar && p.keyframe) {
      tab5_h264_recomecar(s_v.h264);   // esquece as referencias puladas
      s_v.chave_us = p.pts_us;
      if (atraso_ms <= VIDEO_ATRASO_PULA_MS) { s_v.alcancar = false; }   // alcancou o som
    } else if (!s_v.mostrar_ja && atraso_ms > VIDEO_ATRASO_PULA_MS
               && tab5_h264_descartavel(p.data, p.size, s_v.nal_tam)) {
      medf_buffer_release(leitura, p.ref);   // ninguem depende dele: nem decodifica
      ++s_v.perdidos;
      continue;
    }
    if (p.keyframe) { s_v.chave_us = p.pts_us; }

    const int64_t t1 = esp_timer_get_time();
    const h264_dec_result_t r = tab5_h264_decodificar(s_v.h264, p.data, p.size, s_v.nal_tam, p.pts_us);
    const uint32_t dec_us = (uint32_t)(esp_timer_get_time() - t1);
    medf_buffer_release(leitura, p.ref);
    if (dec_us > s_v.pior_dec_us) { s_v.pior_dec_us = dec_us; }
    if (dec_us > s_v.us_quadro) { ++s_v.lentos; }
    s_v.soma_dec_us += dec_us;
    s_v.n_dec++;
    if (r == H264_DEC_NO_MEMORY) {
      // Os quadros de referencia nao cabem na PSRAM (1080p com muitas
      // referencias e quadros B): nao adianta insistir.
      s_v.sem_memoria = true;
      return false;
    }
    if (r != H264_DEC_OK && r != H264_DEC_NO_PICTURE) {
      if (s_v.falhas++ < 3) { ESP_LOGW(TAG, "pacote em %lld ms: %s", (long long)(p.pts_us / 1000), tab5_h264_erro(s_v.h264)); }
    } else {
      s_v.falhas = 0;
    }
  }
  return false;   // so por fechar (ou um arquivo so de pacotes ruins: tenta de novo na proxima volta)
}

// Vai para 'ms': o relogio e o som (ir_para) e o leitor, no quadro-chave
// antes desse ponto. Os quadros entre a chave e 'ms' sao decodificados e
// pulados pela politica de atraso, ate o desse instante.
static void saltar_mp4(uint32_t ms, bool tocando)
{
  if (s_v.tem_q) { tab5_h264_soltar(s_v.h264, s_v.q.id); s_v.tem_q = false; }
  tab5_h264_recomecar(s_v.h264);
  int64_t caiu = 0;
  medf_mp4_demux_seek(s_v.mp4, (int64_t)ms * 1000, &caiu);
  s_v.acabou_leitura = false;
  s_v.drenado = false;
  s_v.alcancar = false;
  s_v.mostrar_ja = true;
  ir_para(ms, tocando);
  s_v.pulo_livre = true;
}

static void laco_mp4(void)
{
  tab5_video_estado_t e = { };
  e.aberto = true; e.com_som = false;
  e.quadros = s_v.tab.n; e.dur_ms = s_v.dur_ms; e.l = s_ql; e.a = s_qa;

  bool tocando = true, fim = false;
  s_v.t_fps = esp_timer_get_time();
  s_v.base_us = s_v.t_fps;
  if (s_mp3[0]) { ligar_som(0); }
  uint32_t pub_pos = UINT32_MAX;

  for (;;) {
    if (s_ped_fechar) { break; }
    bool mudou = false;

    // ---- pedidos ----
    if (s_ped_alternar.exchange(false)) {
      if (fim) {                                   // acabou: toca de novo do comeco
        fim = false; tocando = true;
        saltar_mp4(0, true);
      } else if (tocando) {
        tocando = false;
        relogio_ms();
        som_pausar(true);
      } else {
        tocando = true;
        s_v.base_us = esp_timer_get_time() - (int64_t)s_v.pos_ms * 1000;
        if (s_v.com_som && !som_nosso()) { ligar_som(s_v.pos_ms); }
        som_pausar(false);
      }
      mudou = true;
    }
    const int64_t ir = s_ped_ir.exchange(-1);
    if (ir >= 0) {
      if (fim) { fim = false; tocando = true; }
      saltar_mp4((uint32_t)ir, tocando);
      mudou = true;
    }

    const uint32_t pos = tocando ? relogio_ms() : s_v.pos_ms;

    // ---- o proximo quadro: decodifica se ainda nao ha um esperando ----
    if (!fim && !s_v.tem_q && (tocando || s_v.mostrar_ja)) {
      if (!proximo_quadro_mp4(pos) && s_v.drenado) {
        fim = true; tocando = false;
        ESP_LOGI(TAG, "fim: %u mostrados, %u pulados; pior pacote: leitura %u ms, decodificacao %u ms (media %u ms); %u lentos",
                 (unsigned)s_v.mostrados, (unsigned)s_v.perdidos, (unsigned)(s_v.pior_ler_us / 1000),
                 (unsigned)(s_v.pior_dec_us / 1000),
                 (unsigned)(s_v.n_dec ? s_v.soma_dec_us / s_v.n_dec / 1000 : 0), (unsigned)s_v.lentos);
        s_v.pos_ms = s_v.dur_ms;
        if (s_v.com_som && som_nosso()) { tab5_musica_parar(); }
        s_v.com_som = false;
        mudou = true;
      }
    }

    // ---- mostra quando chega a hora (ou ja, depois de um salto) ----
    const uint32_t agora = tocando ? relogio_ms() : s_v.pos_ms;
    if (s_v.tem_q) {
      const int64_t pts_ms = s_v.q.marca / 1000;
      if (s_v.mostrar_ja || pts_ms <= (int64_t)agora) {
        const bool chave = s_v.q.marca == s_v.chave_us;
        if (!s_v.mostrar_ja && !chave && pts_ms + VIDEO_ATRASO_MOSTRA_MS < (int64_t)agora) {
          ++s_v.perdidos;                          // saiu tarde demais: nao mostra
        } else {
          publicar_quadro_mp4(s_v.q);
          s_v.mostrar_ja = false;
          mudou = true;
        }
        tab5_h264_soltar(s_v.h264, s_v.q.id);
        s_v.tem_q = false;
      }
    }
    if (s_v.falhas >= VIDEO_FALHAS_MAX || s_v.sem_memoria) {
      tocando = false;
      e.erro = true;
      snprintf(e.erro_txt, sizeof(e.erro_txt), "%s",
               s_v.sem_memoria ? N_("vídeo pesado demais para a memória (converta para 720p)")
                               : N_("não consegui decodificar os quadros"));
      s_v.falhas = 0;
      if (s_v.sem_memoria) { fim = true; }   // nao tenta de novo
      if (s_v.com_som && som_nosso()) { tab5_musica_parar(); s_v.com_som = false; }
      mudou = true;
    }

    clock_maximo(tocando || s_v.mostrar_ja);   // decodificando: CPU no maximo

    // ---- publica: a cada quadro novo ou mudanca, e a posicao a cada 250 ms ----
    if (mudou || (tocando && agora / 250 != pub_pos / 250)) {
      e.tocando = tocando; e.fim = fim; e.com_som = s_v.com_som;
      e.pos_ms = s_v.pos_ms; e.mostrados = s_v.mostrados; e.perdidos = s_v.perdidos; e.fps = s_v.fps;
      pub_pos = agora;
      publicar_estado(e);
    }

    // ---- espera a hora do quadro que esta pronto (ou um pedido) ----
    if (tocando && !e.erro) {
      int64_t falta = s_v.tem_q ? s_v.q.marca / 1000 - (int64_t)agora : 0;
      esperar(falta < 1 ? 1 : (falta > 30 ? 30 : (uint32_t)falta));
    } else {
      esperar(s_v.mostrar_ja ? 1 : 1000);
    }
  }
  if (s_v.com_som && som_nosso()) { tab5_musica_parar(); }
  clock_maximo(false);
}

static void tarefa_video(void*)
{
  s_v.e_mp4 = e_mp4(s_caminho);
  if (s_v.e_mp4 ? preparar_mp4() : preparar()) { s_v.e_mp4 ? laco_mp4() : laco(); }
  else { while (!s_ped_fechar) { esperar(1000); } }
  liberar_sessao();
  mp4_ppa_soltar();
  xSemaphoreGive(s_saiu);
  // Quem fecha apaga a tarefa. No MP4 ela foi criada por tab5_h264_criar_tarefa
  // (pilha interna): apagar pela mesma via de sempre (vTaskDeleteWithCaps).
  for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
}

// ---------------------------------------------------------------------------
// PPA - escala o quadro para a superficie
// ---------------------------------------------------------------------------
static Trava              s_ppa_trava;
static ppa_client_handle_t s_ppa = nullptr;
static SemaphoreHandle_t   s_ppa_fim = nullptr;
// O retangulo pintado por ultimo: quando muda, o fundo e refeito (preto).
static struct { void* dst; int x, y, l, a, ql, qa; } s_geom;

// O mesmo cuidado de hal/tab5_camera: modo NAO bloqueante, com um semaphore
// nosso dado pela ISR do PPA - assim uma espera abortada nao deixa a tarefa
// sair com o hardware ainda lendo o buffer.
static bool IRAM_ATTR ao_fim_ppa(ppa_client_handle_t, ppa_event_data_t*, void* sem)
{
  BaseType_t acordou = pdFALSE;
  xSemaphoreGiveFromISR((SemaphoreHandle_t)sem, &acordou);
  return acordou == pdTRUE;
}

static bool ppa_pronto(void)
{
  if (s_ppa) { return true; }
  if (!s_ppa_fim) { s_ppa_fim = xSemaphoreCreateBinary(); }
  const ppa_client_config_t cfg = { .oper_type = PPA_OPERATION_SRM };
  if (!s_ppa_fim || ppa_register_client(&cfg, &s_ppa) != ESP_OK) { s_ppa = nullptr; return false; }
  const ppa_event_callbacks_t cbs = { .on_trans_done = ao_fim_ppa };
  if (ppa_client_register_event_callbacks(s_ppa, &cbs) != ESP_OK) {
    ppa_unregister_client(s_ppa);
    s_ppa = nullptr;
    return false;
  }
  return true;
}

static void ppa_soltar(void)
{
  Trava::Guarda g(s_ppa_trava);
  if (s_ppa) {
    if (ppa_unregister_client(s_ppa) != ESP_OK) { ESP_LOGW(TAG, "o PPA ainda estava ocupado"); }
    s_ppa = nullptr;
  }
  memset(&s_geom, 0, sizeof(s_geom));
}

bool tab5_video_pintar(void* destino, size_t destino_bytes, int pic_l, int pic_a,
                       int x, int y, int l, int a, bool so_se_novo)
{
  if (!destino || l <= 0 || a <= 0 || x < 0 || y < 0 || x + l > pic_l || y + a > pic_a) { return false; }

  // O quadro: o novo, ou (so_se_novo = false) o ultimo que saiu. O app fica
  // com ele so durante esta funcao.
  const uint16_t* px; int ql, qa, passo;
  if (so_se_novo) {
    if (!tab5_video_pegar_quadro(&px, &ql, &qa, &passo)) { return false; }
  } else {
    s_trava.travar();
    const bool tem = s_preso < 0 && s_ultimo >= 0 && s_buf[s_ultimo];
    if (tem) { s_preso = s_ultimo; s_novo = false; px = (const uint16_t*)s_buf[s_ultimo]; ql = s_ql; qa = s_qa; passo = s_passo; }
    s_trava.destravar();
    if (!tem) { return false; }
  }

  bool ok = false;
  {
    Trava::Guarda g(s_ppa_trava);
    if (ppa_pronto()) {
      // Escala uniforme que cabe, em passos de 1/16 (a precisao do PPA).
      int s16 = (l * 16) / ql;
      const int s16a = (a * 16) / qa;
      if (s16a < s16) { s16 = s16a; }
      if (s16 < 1) { s16 = 1; }
      const int sl = ql * s16 / 16, sa = qa * s16 / 16;
      const int ox = x + (l - sl) / 2, oy = y + (a - sa) / 2;

      // MUDOU O RETANGULO OU O VIDEO: fundo preto de novo. As linhas vao para a
      // memoria ANTES do PPA, que invalida o cache do destino.
      if (s_geom.dst != destino || s_geom.x != x || s_geom.y != y || s_geom.l != l || s_geom.a != a
       || s_geom.ql != ql || s_geom.qa != qa) {
        uint8_t* base = (uint8_t*)destino;
        for (int j = 0; j < a; ++j) { memset(base + ((size_t)(y + j) * pic_l + x) * 2, 0, (size_t)l * 2); }
        esp_cache_msync(base + (size_t)y * pic_l * 2, (size_t)a * pic_l * 2,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        s_geom = { destino, x, y, l, a, ql, qa };
      }

      ppa_srm_oper_config_t c = { };
      c.in.buffer          = (void*)px;
      c.in.pic_w           = (uint32_t)passo;
      c.in.pic_h           = (uint32_t)((qa + 15) & ~15);
      c.in.block_w         = (uint32_t)ql;
      c.in.block_h         = (uint32_t)qa;
      c.in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565;
      c.out.buffer         = destino;
      c.out.buffer_size    = (uint32_t)destino_bytes;
      c.out.pic_w          = (uint32_t)pic_l;
      c.out.pic_h          = (uint32_t)pic_a;
      c.out.block_offset_x = (uint32_t)ox;
      c.out.block_offset_y = (uint32_t)oy;
      c.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;
      c.rotation_angle     = PPA_SRM_ROTATION_ANGLE_0;
      c.scale_x = c.scale_y = (float)s16 / 16.0f;
      c.mode               = PPA_TRANS_MODE_NON_BLOCKING;
      c.user_data          = s_ppa_fim;
      if (ppa_do_scale_rotate_mirror(s_ppa, &c) == ESP_OK) {
        // Uma operacao leva poucos ms; 1 s so estoura se o PPA travou.
        for (int i = 0; i < 20 && !ok; ++i) { ok = xSemaphoreTake(s_ppa_fim, pdMS_TO_TICKS(50)) == pdTRUE; }
        if (!ok) { ESP_LOGW(TAG, "o PPA nao terminou"); }
      } else {
        ESP_LOGW(TAG, "o PPA recusou o quadro");
      }
    }
  }
  tab5_video_soltar_quadro();
  return ok;
}

// ---------------------------------------------------------------------------
// API PUBLICA
// ---------------------------------------------------------------------------
bool tab5_video_suportado(const char* nome)
{
  const char* ponto = nome ? strrchr(nome, '.') : nullptr;
  return ponto && ponto != nome && (!strcasecmp(ponto, ".avi") || e_mp4(nome));
}

bool tab5_video_abrir(const char* caminho)
{
  if (!caminho || !tab5_video_suportado(caminho) || strlen(caminho) >= sizeof(s_caminho)) { return false; }
  tab5_video_fechar();   // um video por vez
  Trava::Guarda g(s_ctrl);
  if (!s_saiu) { s_saiu = xSemaphoreCreateBinary(); }
  if (!s_saiu) { return false; }
  xSemaphoreTake(s_saiu, 0);   // zera um aviso antigo

  snprintf(s_caminho, sizeof(s_caminho), "%s", caminho);
  s_mp3[0] = '\0';
  s_ped_fechar = false; s_ped_alternar = false; s_ped_ir = -1;
  memset(&s_v, 0, sizeof(s_v));

  s_trava.travar();
  memset(&s_est, 0, sizeof(s_est));
  s_est.aberto = true;
  s_est.tocando = true;
  const char* nome = strrchr(caminho, '/');
  snprintf(s_est.nome, sizeof(s_est.nome), "%s", nome ? nome + 1 : caminho);
  char* ponto = strrchr(s_est.nome, '.');
  if (ponto) { *ponto = '\0' ; }
  s_trava.destravar();

  // O MP4 decodifica com a PIE (pilha interna, ver tab5_h264); o AVI usa o
  // JPEG por hardware e cabe na PSRAM.
  const BaseType_t criou = e_mp4(caminho)
      ? tab5_h264_criar_tarefa(tarefa_video, "video", VIDEO_MP4_PILHA, nullptr, VIDEO_MP4_PRIO, 0, &s_tarefa)
      : xTaskCreatePinnedToCoreWithCaps(tarefa_video, "video", VIDEO_PILHA, nullptr, VIDEO_PRIO, &s_tarefa,
                                        tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
  if (criou != pdPASS) {
    s_tarefa = nullptr;
    s_trava.travar(); memset(&s_est, 0, sizeof(s_est)); s_trava.destravar();
    ESP_LOGE(TAG, "a tarefa nao subiu");
    return false;
  }
  sistema_avisar(AVISO_VIDEO);
  return true;
}

void tab5_video_fechar(void)
{
  Trava::Guarda g(s_ctrl);
  if (s_tarefa) {
    s_ped_fechar = true;
    xTaskNotifyGive(s_tarefa);
    // A tarefa solta tudo e avisa; espera um quadro mal decodificado, uma leitura longa...
    for (int i = 0; i < 100 && xSemaphoreTake(s_saiu, pdMS_TO_TICKS(50)) != pdTRUE; ++i) {}
    // Ja bloqueada esperando para ser apagada? (Apagar uma tarefa que roda em
    // outro nucleo nao pode.)
    for (int i = 0; i < 100 && eTaskGetState(s_tarefa) != eBlocked; ++i) { vTaskDelay(1); }
    vTaskDeleteWithCaps(s_tarefa);
    s_tarefa = nullptr;
  }
  ppa_soltar();
  s_trava.travar();
  const bool tinha = s_est.aberto;
  memset(&s_est, 0, sizeof(s_est));
  s_trava.destravar();
  if (tinha) { sistema_avisar(AVISO_VIDEO); }
}

void tab5_video_pausar_tocar(void)
{
  s_ped_alternar = true;
  if (s_tarefa) { xTaskNotifyGive(s_tarefa); }
}

void tab5_video_ir_ms(uint32_t ms)
{
  s_ped_ir = (int64_t)ms;
  if (s_tarefa) { xTaskNotifyGive(s_tarefa); }
}

void tab5_video_estado(tab5_video_estado_t* e)
{
  s_trava.travar();
  *e = s_est;
  s_trava.destravar();
}

bool tab5_video_pegar_quadro(const uint16_t** px, int* l, int* a, int* passo)
{
  Trava::Guarda g(s_trava);
  if (!s_novo || s_preso >= 0 || s_ultimo < 0 || !s_buf[s_ultimo]) { return false; }
  s_preso = s_ultimo;
  s_novo = false;
  *px = (const uint16_t*)s_buf[s_ultimo];
  *l = s_ql; *a = s_qa; *passo = s_passo;
  return true;
}

void tab5_video_soltar_quadro(void)
{
  Trava::Guarda g(s_trava);
  s_preso = -1;
}
