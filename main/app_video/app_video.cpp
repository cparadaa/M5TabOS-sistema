// APP VIDEO - toca os videos do cartao: .mp4 (H.264, o do celular, com o som
// de dentro) e .avi (MJPEG, com o som do .mp3 de mesmo nome ao lado).
//
// Este app e SO A INTERFACE. Quem le o arquivo, decodifica (H.264 pela CPU,
// JPEG por hardware), escala o quadro (PPA) e toca o som e o servico do sistema
// (services/tab5_video): o app lista, pede para abrir, mostra o quadro que o
// servico entrega e leva os botoes. Para criar os arquivos no Mac:
// ferramentas/video_para_tab5.sh.
//
// DUAS TELAS (a que vale e a do servico: tem video aberto ou nao):
//   LISTA   - os videos da pasta Videos do cartao (listados pelo worker).
//   TOCANDO - o quadro no alto, embaixo a barra de progresso (toque = ir
//             para), o tempo, Pausar/Tocar e Lista.
//
// O RITMO E O DO VIDEO. Cada quadro novo, pausa, fim ou erro chega como
// AVISO_VIDEO e redesenha o app na hora: nenhum relogio por cima (como na
// Camera). O quadro vai do PPA direto para a superficie de DMA do app
// (sistema_superficie); aqui so se avisa o compositor.
//
// O app e ORI_FIXO_EM_PE: a superficie de DMA so existe com a tela em retrato.

#include "app.h"
#include "idioma.h"
#include "tab5_ui.hpp"
#include "tab5_tema.hpp"
#include "lista.hpp"
#include "pintura.hpp"
#include "trabalho.hpp"
#include "tab5_arquivos.hpp"
#include "tab5_video.hpp"

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <atomic>
#include <cstdio>
#include <cstring>

static const char* TAG = "app.video";

static const uint16_t ROXO  = 0xFB5F;   // rgb(255,106,255) - so o ICONE do app (fica como esta; a tela usa o tema)

#define PASTA_VIDEOS "/sdcard/Videos"   // onde o app procura (criada se nao existir)
#define LIN_MAX   200                   // videos na lista
#define LIN_A     112                   // altura de uma linha da lista
#define TOPO_A    104                   // o titulo da lista
#define MARGEM    24

// ---------------------------------------------------------------------------
// A LISTA - montada pelo worker, num vetor que a interface NAO esta lendo; so
// no ao_aviso a interface troca para ele (kernel/trabalho.hpp).
// ---------------------------------------------------------------------------
typedef struct {
  char     nome[TAB5_ARQ_NOME_MAX];
  uint64_t bytes;
} linha_t;
EXT_RAM_BSS_ATTR static linha_t s_a[LIN_MAX], s_b[LIN_MAX];
static linha_t* s_lin = s_a;            // a que a interface desenha
static linha_t* s_tras = s_b;           // a que o worker monta
static int  s_n = 0;
static bool s_sem_cartao = false;
static bool s_carregando = false;
static std::atomic<int>  s_n_tras{0};
static std::atomic<bool> s_sem_cartao_tras{false};
static std::atomic<bool> s_pronto{false};
static lista_t s_lista;

// O QUE O APP SABE DO SERVICO (para perceber as mudancas de tela).
static bool s_player = false;           // mostrando o player (ha video aberto)
static int  s_ql = 0, s_qa = 0;         // o tamanho do quadro com que a tela foi montada
static bool s_acordado = false;         // pedimos a tela acesa
static pintura_t s_p;
static int  s_sig = -1;                 // o que os controles mostravam

// ---------------------------------------------------------------------------
// LISTAR (no worker)
// ---------------------------------------------------------------------------
static void trab_listar(void*)
{
  tab5_arq_item_t* it = (tab5_arq_item_t*)heap_caps_malloc(sizeof(tab5_arq_item_t) * LIN_MAX, MALLOC_CAP_SPIRAM);
  int n = 0;
  bool sem = true;
  if (it) {
    int total = 0;
    int k = tab5_arq_listar(PASTA_VIDEOS, it, LIN_MAX, &total, TAB5_ARQ_ORDEM_NOME, TAB5_VIDEO_EXTENSOES);
    if (k < 0) {   // ainda nao ha a pasta: cria (o cartao pode estar la)
      tab5_arq_criar_pasta("/sdcard", "Videos");
      k = tab5_arq_listar(PASTA_VIDEOS, it, LIN_MAX, &total, TAB5_ARQ_ORDEM_NOME, TAB5_VIDEO_EXTENSOES);
    }
    sem = k < 0;
    for (int i = 0; i < k && n < LIN_MAX; ++i) {
      if (it[i].tipo == TAB5_ARQ_PASTA) { continue; }
      snprintf(s_tras[n].nome, sizeof(s_tras[n].nome), "%s", it[i].nome);
      s_tras[n].bytes = it[i].bytes;
      ++n;
    }
    heap_caps_free(it);
  }
  s_n_tras = n;
  s_sem_cartao_tras = sem;
  s_pronto = true;
}

static void carregar(void)
{
  s_carregando = true;
  s_pronto = false;
  if (!sistema_trabalho(trab_listar, nullptr)) { s_carregando = false; }
}

// ---------------------------------------------------------------------------
// O TEMPO
// ---------------------------------------------------------------------------
static void tempo_txt(uint32_t ms, char* out, size_t n)
{
  const uint32_t s = ms / 1000;
  if (s >= 3600) { snprintf(out, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60)); }
  else           { snprintf(out, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60)); }
}

// ---------------------------------------------------------------------------
// A GEOMETRIA DO PLAYER - em coordenadas RELATIVAS a area (as do ao_tocar);
// o desenho soma a.x e a.y (coordenadas de tela).
// ---------------------------------------------------------------------------
#define BARRA_X  MARGEM
#define BARRA_A  16
#define BOTAO_A  96
#define CONTROLES_A 330          // altura reservada para os controles

typedef struct {
  int vy, vh;                    // o retangulo do video (y multiplo de 8 NA TELA, altura multiplo de 8)
  int by;                        // y da barra de progresso
  int bl;                        // largura da barra
  int bty;                       // y dos botoes
  int bl1;                       // largura de cada botao
} geo_t;

static geo_t geometria(const area_t& a, int ql, int qa)
{
  geo_t g;
  g.vy = ((a.y + 7) & ~7) - a.y;   // a superficie pede y multiplo de 8
  int vh = 0;
  if (ql > 0 && qa > 0) {
    const int disp = (a.altura - g.vy - CONTROLES_A) & ~7;
    int s16 = (a.largura * 16) / ql;
    const int s16a = (disp * 16) / qa;
    if (s16a < s16) { s16 = s16a; }
    if (s16 < 1) { s16 = 1; }
    vh = ((qa * s16 / 16) + 7) & ~7;
    if (vh > disp) { vh = disp; }
  }
  g.vh  = vh;
  g.by  = g.vy + vh + 56;
  g.bl  = a.largura - 2 * BARRA_X;
  g.bty = g.by + 112;
  g.bl1 = (a.largura - 3 * MARGEM) / 2;
  return g;
}

// ---------------------------------------------------------------------------
// DESENHO
// ---------------------------------------------------------------------------
static int linha_altura(int, void*) { return LIN_A; }

static void linha_desenhar(lgfx::LovyanGFX& d, int i, int x0, int y, int largura, void*)
{
  if (i < 0 || i >= s_n) { return; }
  const linha_t& l = s_lin[i];
  const int cy = y + LIN_A / 2, q = 64, qx = x0 + MARGEM;
  d.fillRect(x0, y, largura, LIN_A, TEMA_FUNDO);
  d.fillRoundRect(qx, cy - q / 2, q, q, TEMA_RAIO_M, TEMA_SUPERFICIE_ALTA);
  d.fillTriangle(qx + q * 3 / 10, cy - q / 4, qx + q * 3 / 10, cy + q / 4, qx + q * 3 / 4, cy, TEMA_DESTAQUE);
  char nome[TAB5_ARQ_NOME_MAX], tam[24];
  snprintf(nome, sizeof(nome), "%s", l.nome);
  char* ponto = strrchr(nome, '.');
  if (ponto) { *ponto = '\0'; }
  tab5_bytes_txt(tam, sizeof(tam), l.bytes);
  const int tx = qx + q + 20;
  tab5_escrever(d, nome, tx, cy - 6, 30, true, TEMA_TEXTO, TEMA_FUNDO, bottom_left, largura - tx - MARGEM - 8);
  tab5_escrever(d, tam, tx, cy + 4, 22, false, TEMA_TEXTO_2, TEMA_FUNDO, top_left);
}

static void desenhar_lista(lgfx::LovyanGFX& d, const area_t& a, bool completa)
{
  if (completa) {
    d.fillRect(a.x, a.y, a.largura, a.altura, TEMA_FUNDO);
    tab5_escrever(d, tr("Vídeos"), a.x + MARGEM, a.y + 28, TEMA_LETRA_TITULO, true, TEMA_TEXTO, TEMA_FUNDO, top_left);
  }
  const area_t janela = { a.x, a.y + TOPO_A, a.largura, a.altura - TOPO_A };
  if (s_n == 0) {
    if (completa) {
      d.fillRect(janela.x, janela.y, janela.largura, janela.altura, TEMA_FUNDO);
      const int cx = a.x + a.largura / 2, cy = janela.y + janela.altura / 3;
      if (s_carregando) {
        tab5_escrever(d, tr("Carregando..."), cx, cy, 28, false, TEMA_TEXTO_2, TEMA_FUNDO, middle_center);
      } else if (s_sem_cartao) {
        tab5_escrever(d, tr("Sem cartão SD"), cx, cy, 30, false, TEMA_TEXTO_2, TEMA_FUNDO, middle_center);
      } else {
        tab5_escrever(d, tr("Nenhum vídeo ainda"), cx, cy, 30, true, TEMA_TEXTO, TEMA_FUNDO, middle_center);
        tab5_escrever(d, tr("Copie vídeos .mp4 (do celular) ou"), cx, cy + 50, 24, false, TEMA_TEXTO_2, TEMA_FUNDO, middle_center);
        tab5_escrever(d, tr(".avi (MJPEG) para a pasta Videos"), cx, cy + 84, 24, false, TEMA_TEXTO_2, TEMA_FUNDO, middle_center);
        tab5_escrever(d, tr("do cartão. Leve: 360p ou 480p."), cx, cy + 118, 24, false, TEMA_TEXTO_2, TEMA_FUNDO, middle_center);
      }
    }
    lista_esconder(&s_lista);
  } else {
    lista_desenhar(&s_lista, d, janela, TEMA_FUNDO, completa);
  }
}

// Tela de quem ainda abre ou deu erro: so o texto e o botao Lista.
static void desenhar_aviso(lgfx::LovyanGFX& d, const area_t& a, const tab5_video_estado_t& e, bool completa)
{
  if (!completa) { return; }
  d.fillRect(a.x, a.y, a.largura, a.altura, TEMA_FUNDO);
  const int cx = a.x + a.largura / 2;
  if (e.erro) {
    tab5_escrever(d, tr("Não deu para tocar"), cx, a.y + 200, 36, true, TEMA_PERIGO, TEMA_FUNDO, middle_center);
    tab5_escrever(d, tr(e.erro_txt), cx, a.y + 260, 26, false, TEMA_TEXTO_2, TEMA_FUNDO, middle_center, a.largura - 2 * MARGEM);
  } else {
    tab5_escrever(d, tr("Abrindo o vídeo..."), cx, a.y + 200, 32, false, TEMA_TEXTO_2, TEMA_FUNDO, middle_center);
  }
  tab5_botao(d, a.x + MARGEM, a.y + 340, a.largura - 2 * MARGEM, BOTAO_A, tr("Lista"), TEMA_DESTAQUE, TEMA_SOBRE_DESTAQUE, TAB5_BOTAO_TEXTO_GRANDE);
}

static void desenhar_player(lgfx::LovyanGFX& d, const area_t& a, const tab5_video_estado_t& e, bool completa)
{
  if (e.erro || e.l == 0) { desenhar_aviso(d, a, e, completa); return; }
  const geo_t g = geometria(a, e.l, e.a);

  // ---- o quadro: do PPA para a superficie; aqui so se avisa o compositor ----
  const area_t ret = { a.x, a.y + g.vy, a.largura, g.vh };
  superficie_t sup;
  if (!sistema_superficie(ret, &sup)) {
    if (completa) {
      d.fillRect(a.x, a.y, a.largura, a.altura, TEMA_FUNDO);
      tab5_escrever(d, tr("Sem memória para o vídeo"), a.x + a.largura / 2, a.y + 200, 30, false, TEMA_PERIGO, TEMA_FUNDO, middle_center);
    }
    return;
  }
  // Passada completa: a area foi limpa, o quadro que ja saiu tem que voltar.
  if (tab5_video_pintar(sup.buf, sup.bytes, sup.pic_l, sup.pic_a, sup.x, sup.y, ret.largura, ret.altura, !completa)) {
    sistema_superficie_mudou(ret);
  }

  // ---- os controles: so quando o que mostram mudou ----
  const int barra_px = e.dur_ms ? (int)((uint64_t)e.pos_ms * (uint64_t)g.bl / e.dur_ms) : 0;
  const int sig = (int)((e.pos_ms / 1000) * 4 + (e.tocando ? 2 : 0) + (e.fim ? 1 : 0)) ^ (barra_px << 20);
  if (!pintura_mudou_i(&s_p, &s_sig, sig)) { return; }

  const int y0 = a.y + g.vy + g.vh;
  d.fillRect(a.x, y0, a.largura, a.altura - g.vy - g.vh, TEMA_FUNDO);

  // o nome
  tab5_escrever(d, e.nome, a.x + MARGEM, y0 + 8, 26, true, TEMA_TEXTO, TEMA_FUNDO, top_left, a.largura - 2 * MARGEM);

  // a barra de progresso
  const int bx = a.x + BARRA_X, by = a.y + g.by;
  d.fillRoundRect(bx, by, g.bl, BARRA_A, BARRA_A / 2, TEMA_SUPERFICIE_ALTA);   // trilho
  if (barra_px > 0) { d.fillRoundRect(bx, by, barra_px < BARRA_A ? BARRA_A : barra_px, BARRA_A, BARRA_A / 2, TEMA_DESTAQUE); }
  d.fillCircle(bx + (barra_px < BARRA_A / 2 ? BARRA_A / 2 : barra_px), by + BARRA_A / 2, 14, TEMA_TEXTO);

  // o tempo: posicao a esquerda, duracao a direita, e (pequeno) o que o servico mede
  char t1[16], t2[16], diag[64];
  tempo_txt(e.pos_ms, t1, sizeof(t1));
  tempo_txt(e.dur_ms, t2, sizeof(t2));
  tab5_escrever(d, t1, bx, by + 32, 26, false, TEMA_TEXTO, TEMA_FUNDO, top_left);
  tab5_escrever(d, t2, bx + g.bl, by + 32, 26, false, TEMA_TEXTO_2, TEMA_FUNDO, top_right);
  snprintf(diag, sizeof(diag), "%.0f %s  %u %s%s", e.fps, tr("qps"), (unsigned)e.perdidos, tr("pulados"),
           e.com_som ? "" : tr("  (sem som)"));
  tab5_escrever(d, diag, bx + g.bl / 2, by + 36, 20, false, TEMA_TEXTO_2, TEMA_FUNDO, top_center);

  // os botoes
  const char* txt = e.fim ? tr("De novo") : (e.tocando ? tr("Pausar") : tr("Tocar"));
  tab5_botao(d, a.x + MARGEM, a.y + g.bty, g.bl1, BOTAO_A, txt, TEMA_DESTAQUE, TEMA_SOBRE_DESTAQUE, TAB5_BOTAO_TEXTO_GRANDE);
  tab5_botao(d, a.x + 2 * MARGEM + g.bl1, a.y + g.bty, g.bl1, BOTAO_A, tr("Lista"), TEMA_SUPERFICIE_ALTA, TEMA_TEXTO, TAB5_BOTAO_TEXTO_GRANDE);
}

static void desenhar(lgfx::LovyanGFX& d, const area_t& a)
{
  pintura_iniciar(&s_p);
  if (!s_player) { desenhar_lista(d, a, pintura_completa(&s_p)); return; }
  lista_esconder(&s_lista);
  tab5_video_estado_t e;
  tab5_video_estado(&e);
  desenhar_player(d, a, e, pintura_completa(&s_p));
}

// ---------------------------------------------------------------------------
// AVISOS - o servico mudou ou o worker terminou
// ---------------------------------------------------------------------------
static void ao_aviso(uint32_t av)
{
  if ((av & AVISO_TRABALHO) && s_carregando && s_pronto.load()) {
    s_carregando = false;
    linha_t* t = s_lin; s_lin = s_tras; s_tras = t;
    s_n = s_n_tras;
    s_sem_cartao = s_sem_cartao_tras;
    lista_itens_mudaram(&s_lista, s_n);
    sistema_pedir_redesenho_completo();
  }
  if (av & AVISO_VIDEO) {
    tab5_video_estado_t e;
    tab5_video_estado(&e);
    // A TELA TROCOU (abriu ou fechou um video) ou o video mostrou o tamanho do
    // quadro: monta de novo, com a area limpa.
    if (e.aberto != s_player || e.l != s_ql || e.a != s_qa) {
      const bool voltou = s_player && !e.aberto;
      s_player = e.aberto;
      s_ql = e.l;
      s_qa = e.a;
      s_sig = -1;
      if (voltou) { carregar(); }
      sistema_pedir_redesenho_completo();
    }
    // A tela fica acesa enquanto toca: sem toque, o repouso apagaria o filme.
    const bool toca = e.aberto && e.tocando;
    if (toca != s_acordado) {
      s_acordado = toca;
      sistema_manter_acordado(SISTEMA_ACORDADO_APP, toca);
    }
  }
  sistema_pedir_redesenho();
}

// ---------------------------------------------------------------------------
// TOQUE - x e y RELATIVOS a area
// ---------------------------------------------------------------------------
static void abrir_video(const linha_t& l)
{
  char caminho[sizeof(PASTA_VIDEOS) + TAB5_ARQ_NOME_MAX + 2];
  snprintf(caminho, sizeof(caminho), "%s/%s", PASTA_VIDEOS, l.nome);
  if (!tab5_video_abrir(caminho)) { ESP_LOGW(TAG, "nao abriu %s", caminho); }
  // A tela troca no AVISO_VIDEO que o servico publica.
}

static void voltar_para_lista(void)
{
  tab5_video_fechar();   // para o som e solta o disco; o aviso volta para a lista
}

static void ao_tocar_nivel(int x, int y, bool pressionado)
{
  if (!s_player && s_n > 0 && lista_arrastar(&s_lista, x, y, pressionado)) { sistema_pedir_redesenho(); }
}

static void ao_tocar(int x, int y)
{
  if (!s_player) {
    int dy = 0;
    const int i = lista_item_tocado(&s_lista, x, y, &dy);
    if (i >= 0 && i < s_n) { abrir_video(s_lin[i]); }
    return;
  }
  tab5_video_estado_t e;
  tab5_video_estado(&e);
  const area_t a = sistema_area();
  if (e.erro || e.l == 0) {   // so ha o botao Lista
    if (tab5_dentro(x, y, MARGEM, 340, a.largura - 2 * MARGEM, BOTAO_A)) { voltar_para_lista(); }
    return;
  }
  const geo_t g = geometria(a, e.l, e.a);
  if (tab5_dentro(x, y, 0, g.vy, a.largura, g.vh)) { tab5_video_pausar_tocar(); return; }   // toque no video
  if (y >= g.by - 36 && y < g.by + BARRA_A + 36 && e.dur_ms) {                                 // na barra: ir para
    int px = x - BARRA_X;
    if (px < 0) { px = 0; }
    if (px > g.bl) { px = g.bl; }
    tab5_video_ir_ms((uint32_t)((uint64_t)px * e.dur_ms / (uint64_t)g.bl));
    return;
  }
  if (tab5_dentro(x, y, MARGEM, g.bty, g.bl1, BOTAO_A)) { tab5_video_pausar_tocar(); return; }
  if (tab5_dentro(x, y, 2 * MARGEM + g.bl1, g.bty, g.bl1, BOTAO_A)) { voltar_para_lista(); }
}

// ---------------------------------------------------------------------------
// ABRIR E FECHAR
// ---------------------------------------------------------------------------
static bool ao_abrir(void)
{
  tab5_video_estado_t e;
  tab5_video_estado(&e);   // aberto pelo Arquivos: o app ja nasce no player
  s_player = e.aberto;
  s_ql = e.l;
  s_qa = e.a;
  s_sig = -1;
  s_acordado = false;
  s_n = 0;
  s_sem_cartao = false;
  lista_definir(&s_lista, ROLAGEM_INICIO, 0, linha_altura, linha_desenhar, nullptr);
  if (!s_player) { carregar(); }
  else { ao_aviso(AVISO_VIDEO); }   // liga a tela acesa se ja toca
  return true;
}

static void ao_fechar(void)
{
  tab5_video_fechar();   // fechar o app para o filme e o som
  if (s_acordado) { sistema_manter_acordado(SISTEMA_ACORDADO_APP, false); s_acordado = false; }
  s_player = false;
}

// ---------------------------------------------------------------------------
// ICONE DA TELA INICIAL - um quadrado de cinema com o triangulo de tocar.
// ---------------------------------------------------------------------------
static void icone_video(lgfx::LovyanGFX& d, int x, int y, int lado)
{
  d.fillRoundRect(x, y, lado, lado, lado / 6, d.color565(40, 20, 45));
  const int tela_l = lado * 62 / 100, tela_a = lado * 42 / 100;
  const int tx = x + (lado - tela_l) / 2, ty = y + (lado - tela_a) / 2;
  d.fillRoundRect(tx, ty, tela_l, tela_a, lado / 14, ROXO);
  const int cx = tx + tela_l / 2, cy = ty + tela_a / 2, r = tela_a / 4;
  d.fillTriangle(cx - r * 7 / 10, cy - r, cx - r * 7 / 10, cy + r, cx + r, cy, d.color565(40, 20, 45));
}

extern const app_t APP_VIDEO = {
  .nome  = "Vídeo",
  .cor   = ROXO,
  .perfil = { .alto_falante = true },   // o som do filme (nao da para pedir depois do boot: ver ajustes.h)
  .usa_sd = true,
  .exige_sd = false,                    // um vídeo aberto do pendrive tambem toca
  .orientacao = ORI_FIXO_EM_PE,         // o quadro e DMA na tela em rotacao 0
  .ao_abrir  = ao_abrir,
  .ao_fechar = ao_fechar,
  .desenhar  = desenhar,
  .ao_tocar  = ao_tocar,
  .ao_tocar_nivel = ao_tocar_nivel,
  .periodo_ms = 0,                      // o relogio e o dos quadros (AVISO_VIDEO)
  .avisos    = AVISO_TRABALHO | AVISO_VIDEO,
  .ao_aviso  = ao_aviso,
  .icone     = icone_video,
  .fundo_tema = true,                   // o sistema limpa a area com o fundo do tema
};
