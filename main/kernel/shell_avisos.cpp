// SHELL - a central de avisos: a lista de fontes ativas, o miniplayer, as partes
// de fundo (parar), o desenho e o toque da central aberta por cima do app ou da
// home. Partido de shell.cpp; o que as partes compartilham esta em
// shell_interno.hpp.

#include "esp_attr.h"
#include "tela_virtual.hpp"
#include "tab5_tela.hpp"
#include "notificacao.h"
#include "midia.h"
#include "tab5_ui.hpp"
#include "tab5_tema.hpp"
#include "tab5_hora.hpp"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "shell_interno.hpp"
#include "idioma.h"

static const char *TAG = "shell";

// ---------------------------------------------------------------------------
// CENTRAL DE AVISOS
// ---------------------------------------------------------------------------
// A barra so tem ~170 px para avisos na home (em pe): cabe UM resumo e uns
// pontos coloridos. Com dois ou tres apps avisando, o usuario via que havia
// mais coisa e nao tinha onde ler. Tocar a barra abre esta lista - a area
// util inteira, uma linha por app (notif_listar), na mesma ordem da barra.
// Tocar uma linha abre o app dela; tocar fora, a barra de novo, Esc ou
// deslizar para cima fecha. Enquanto ela esta aberta o app de baixo fica
// ENCOBERTO (sistema_app_encoberto): nao desenha, nao ouve, nao "le" nada.
//
// TRES FAIXAS, de cima para baixo:
//   AVISOS            - quantos houver; o que nao cabe ROLA (arraste vertical
//                       comecado nela);
//   EM SEGUNDO PLANO  - as partes de fundo rodando (kernel/fundo.h), um
//                       cartao cada, com o X que PARA. No maximo FUNDO_MAX
//                       (4): a faixa tem tamanho fixo e nunca rola;
//   MINIPLAYER        - so com midia (kernel/midia.h), com o X dela.
// O X e do SISTEMA, para toda parte de fundo, sem o app escrever nada (o
// contrato esta em fundo.h). Primeiro toque: vira "Parar?"; o segundo, em
// ate 3 s, para. Parar nao espera aqui: pede e o kernel recolhe
// (sistema_fundo_parar_nome) - a tela nunca congela esperando um app sair.
bool s_painel_avisos = false;

EXT_RAM_BSS_ATTR static notif_item_t s_pa_itens[NOTIF_MAX_FONTES];   // os avisos (sem os de parte de fundo)
static int          s_pa_n = 0;
static int          s_pa_primeiro = 0;               // rolagem: o primeiro aviso na tela
static int          s_pa_desenhadas = 0;             // quantas linhas couberam
static uint32_t     s_pa_versao = 0;
static fundo_info_t s_pa_fundos[FUNDO_MAX];          // os cartoes na tela
static int          s_pa_nf = 0;
static uint32_t     s_pa_fundos_sig = 0;             // nomes + resumos desenhados
static const char*  s_pa_conf = nullptr;             // nome que esta em "Parar?"
static uint32_t     s_pa_conf_ate = 0;
// PA_VAO = 0: as linhas dos avisos ficam coladas, dentro de UM cartao, e so um
// filete as separa. PA_ALCA: a folga embaixo, onde mora a alca de fechar.
static const int    PA_TOPO = 96, PA_VAO = 0, PA_MARGEM = TEMA_MARGEM, PA_ALCA = 24;
static const int    PA_CAB = 48, PA_CARTAO = 88;     // faixa de segundo plano
static const uint32_t PA_CONFIRMA_MS = 3000;

// Deitado a area e baixa: linha mais fina para caberem pelo menos tres.
static int pa_linha(const area_t& a) { return a.altura < 700 ? 88 : 96; }

// Poe a Roboto suave na altura nativa pedida (regular ou negrito), sem escala.
static void pa_fonte(lgfx::LovyanGFX& d, bool negrito, int px)
{
  d.setFont(tab5_fonte_suave(negrito, px));
  d.setTextSize(1);
}

static const app_t* app_por_nome(const char* nome)
{
  if (!nome) { return nullptr; }
  for (int i = 0; i < SISTEMA_NUM_APPS; ++i) {
    if (strcmp(SISTEMA_APPS[i]->nome, nome) == 0) { return SISTEMA_APPS[i]; }
  }
  return nullptr;
}

void painel_avisos_abrir(bool abrir)
{
  if (s_painel_avisos == abrir) { return; }
  s_painel_avisos = abrir;
  s_pa_primeiro = 0;
  s_pa_conf = nullptr;
  s_sujo_layout   = true;   // abre por cima / devolve o app: passada completa
}

// ---- O MINIPLAYER, embaixo da central (kernel/midia.h) --------------------
// Com musica tocando (ou pausada), a central ganha um bloco no rodape: titulo,
// a linha de progresso com os tempos e anterior / tocar-pausar / proxima.
// Tocar os botoes NAO fecha a central (pular tres musicas e tres toques);
// tocar o titulo abre o tocador. A linha de progresso busca, se o tocador
// deixar (midia_info_t.busca).
static bool     s_pa_midia     = false;   // a central foi montada COM o miniplayer
static uint32_t s_pa_midia_pos = 0;       // midia_versao_posicao() desenhada
static const int MP_ALT = 236;

typedef struct {
  int x, y, w, h;            // a caixa
  int tit_y;                 // centro da linha do titulo
  int lin_x, lin_y, lin_w;   // a linha de progresso
  int bt_y, bt_r;            // centro e raio dos botoes
  int bt_ant, bt_toc, bt_prox;   // centros x
  int fx, fy;                // o X (parar), no canto
} mp_geo_t;

static mp_geo_t mp_geo(const area_t& a)
{
  mp_geo_t g;
  g.x = a.x + PA_MARGEM;
  g.w = a.largura - 2 * PA_MARGEM;
  g.h = MP_ALT;
  g.y = a.y + a.altura - MP_ALT - PA_ALCA;
  g.tit_y = g.y + 40;
  g.lin_x = g.x + 32;
  g.lin_w = g.w - 64;
  g.lin_y = g.y + 84;
  g.bt_y  = g.y + 170;
  g.bt_r  = 40;
  const int cx = g.x + g.w / 2;
  g.bt_toc = cx; g.bt_ant = cx - 150; g.bt_prox = cx + 150;
  g.fx = g.x + g.w - 46; g.fy = g.tit_y;
  return g;
}

// O X que para uma parte de fundo, ou o "Parar?" da confirmacao, centrado em
// (cx, cy). Devolve a meia-largura desenhada (para o titulo nao encostar).
static bool pa_confirmando(const char* nome)
{
  return nome && s_pa_conf && strcmp(s_pa_conf, nome) == 0 && (int32_t)(s_pa_conf_ate - tab5_agora_ms()) > 0;
}

static int desenhar_x_parar(lgfx::LovyanGFX& d, int cx, int cy, bool confirmar)
{
  if (confirmar) {
    // "Parar?": pilula escura com borda e letra em vermelho (perigo).
    pa_fonte(d, true, 24);
    const int l = (int)d.textWidth(tr("Parar?")) + 32, h = 48;
    d.fillRoundRect(cx + 24 - l, cy - h / 2, l, h, h / 2, TEMA_SUPERFICIE_ALTA);
    d.drawRoundRect(cx + 24 - l, cy - h / 2, l, h, h / 2, TEMA_PERIGO);
    d.setTextDatum(middle_center);
    d.setTextColor(TEMA_PERIGO, TEMA_SUPERFICIE_ALTA);
    d.drawString(tr("Parar?"), cx + 24 - l / 2, cy);
    tab5_fonte_corpo(d);
    return l - 24;
  }
  // O botao: circulo de 48 px com o X em vermelho.
  d.fillCircle(cx, cy, 24, TEMA_SUPERFICIE_ALTA);
  for (int k = -1; k <= 1; ++k) {
    d.drawLine(cx - 9 + k, cy - 9, cx + 9 + k, cy + 9, TEMA_PERIGO);
    d.drawLine(cx - 9 + k, cy + 9, cx + 9 + k, cy - 9, TEMA_PERIGO);
  }
  return 26;
}

// Primeiro toque arma o "Parar?"; o segundo, no prazo, para. true = parou.
static bool pa_tocar_x(const char* nome)
{
  if (pa_confirmando(nome)) {
    s_pa_conf = nullptr;
    sistema_fundo_parar_nome(nome);
    ESP_LOGI(TAG, "central: parando '%s' (pedido do usuario)", nome);
    return true;
  }
  s_pa_conf = nome;
  s_pa_conf_ate = tab5_agora_ms() + PA_CONFIRMA_MS;
  return false;
}

static void mp_tempo(char* out, size_t n, uint32_t ms)
{
  const uint32_t s = ms / 1000;
  if (s >= 3600) { snprintf(out, n, "%u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60)); }
  else           { snprintf(out, n, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60)); }
}

static void desenhar_miniplayer(lgfx::LovyanGFX& d, const area_t& a)
{
  midia_info_t mi;
  if (!midia_ler(&mi)) { return; }
  s_pa_midia_pos = midia_versao_posicao();
  const mp_geo_t g = mp_geo(a);
  d.fillRoundRect(g.x, g.y, g.w, g.h, TEMA_RAIO_G, TEMA_DESTAQUE_SUAVE);

  // Titulo (abre o tocador).
  char t[MIDIA_TITULO + 4];
  snprintf(t, sizeof(t), "%s", mi.titulo[0] ? mi.titulo : tr("Música"));
  d.fillRect(g.x + 24, g.tit_y - 30, g.w - 48, 60, TEMA_DESTAQUE_SUAVE);
  const int meia_x = desenhar_x_parar(d, g.fx, g.fy, pa_confirmando(mi.fonte));
  pa_fonte(d, true, TEMA_LETRA_SUBTITULO);
  tab5_cortar_com_reticencias(d, t, sizeof(t), g.fx - meia_x - 16 - (g.x + 34));
  d.setTextDatum(middle_left);
  d.setTextColor(TEMA_TEXTO, TEMA_DESTAQUE_SUAVE);
  d.drawString(t, g.x + 34, g.tit_y);
  tab5_fonte_corpo(d);

  // A linha de progresso: trilho fino, parte tocada clara e uma bolinha.
  const uint32_t dur = mi.dur_ms, pos = (mi.pos_ms < dur || !dur) ? mi.pos_ms : dur;
  const int feito = dur ? (int)((uint64_t)g.lin_w * pos / dur) : 0;
  d.fillRoundRect(g.lin_x, g.lin_y - 3, g.lin_w, 6, 3, TEMA_LINHA);
  if (feito > 0) { d.fillRoundRect(g.lin_x, g.lin_y - 3, feito, 6, 3, TEMA_TEXTO); }
  if (dur) { d.fillCircle(g.lin_x + feito, g.lin_y, 9, TEMA_TEXTO); }
  char tp[16], td[16];
  mp_tempo(tp, sizeof(tp), pos);
  if (dur) { mp_tempo(td, sizeof(td), dur); } else { snprintf(td, sizeof(td), "--:--"); }
  pa_fonte(d, false, TEMA_LETRA_LEGENDA);
  d.setTextColor(TEMA_TEXTO_2, TEMA_DESTAQUE_SUAVE);
  d.setTextDatum(top_left);
  d.drawString(tp, g.lin_x, g.lin_y + 14);
  d.setTextDatum(top_right);
  d.drawString(td, g.lin_x + g.lin_w, g.lin_y + 14);
  tab5_fonte_corpo(d);

  // Os botoes: anterior, tocar/pausar (cheio, no destaque), proxima.
  const int y = g.bt_y, r = g.bt_r;
  d.fillCircle(g.bt_toc, y, r, TEMA_DESTAQUE);
  if (mi.tocando) {
    d.fillRect(g.bt_toc - 14, y - 16, 10, 32, TEMA_SOBRE_DESTAQUE);
    d.fillRect(g.bt_toc + 4,  y - 16, 10, 32, TEMA_SOBRE_DESTAQUE);
  } else {
    d.fillTriangle(g.bt_toc - 11, y - 18, g.bt_toc - 11, y + 18, g.bt_toc + 19, y, TEMA_SOBRE_DESTAQUE);
  }
  const int sx = 16, sy = 15;
  // anterior: barra + triangulo para a esquerda
  d.fillRect(g.bt_ant - sx - 4, y - sy, 6, 2 * sy, TEMA_TEXTO);
  d.fillTriangle(g.bt_ant + sx, y - sy, g.bt_ant + sx, y + sy, g.bt_ant - sx + 4, y, TEMA_TEXTO);
  // proxima: triangulo para a direita + barra
  d.fillTriangle(g.bt_prox - sx, y - sy, g.bt_prox - sx, y + sy, g.bt_prox + sx - 4, y, TEMA_TEXTO);
  d.fillRect(g.bt_prox + sx - 2, y - sy, 6, 2 * sy, TEMA_TEXTO);
  d.setTextDatum(top_left);
}

// true = o toque foi no miniplayer (e ja foi atendido).
static bool miniplayer_tocar(const area_t& a, int tx, int ty)
{
  if (!s_pa_midia) { return false; }
  const mp_geo_t g = mp_geo(a);
  if (ty < g.y || ty >= g.y + g.h || tx < g.x || tx >= g.x + g.w) { return false; }
  midia_info_t mi;
  if (!midia_ler(&mi)) { return true; }
  if (abs(tx - g.fx) <= 44 && abs(ty - g.fy) <= 40) {   // o X
    pa_tocar_x(mi.fonte);   // o X para a parte de fundo que toca
    s_sujo_layout = true;
    return true;
  }
  const int folga = 30;
  if (abs(ty - g.bt_y) <= g.bt_r + folga) {
    if      (abs(tx - g.bt_toc)  <= g.bt_r + folga) { midia_tocar_pausar(); }
    else if (abs(tx - g.bt_ant)  <= g.bt_r + folga) { midia_anterior(); }
    else if (abs(tx - g.bt_prox) <= g.bt_r + folga) { midia_proxima(); }
    return true;
  }
  if (abs(ty - g.lin_y) <= 34) {
    if (mi.busca && mi.dur_ms && tx >= g.lin_x - 20 && tx <= g.lin_x + g.lin_w + 20) {
      int dx = tx - g.lin_x;
      if (dx < 0) { dx = 0; }
      if (dx > g.lin_w) { dx = g.lin_w; }
      midia_ir_para((uint32_t)((uint64_t)mi.dur_ms * dx / g.lin_w));
    }
    return true;
  }
  if (ty < g.lin_y - 34) {   // o titulo: abre o tocador
    const app_t* destino = app_por_nome(mi.app);
    painel_avisos_abrir(false);
    if (destino && destino != s_app) { abrir_app(destino); }
  }
  return true;
}

// As partes de fundo que viram cartao (o tocador com miniplayer fica so nele).
static int pa_ler_fundos(fundo_info_t* out, const midia_info_t* mi)
{
  fundo_info_t todos[FUNDO_MAX];
  const int n = sistema_fundo_listar(todos, FUNDO_MAX);
  int k = 0;
  for (int i = 0; i < n; ++i) {
    if (mi && mi->ativa && mi->fonte && strcmp(todos[i].nome, mi->fonte) == 0) { continue; }
    out[k++] = todos[i];
  }
  return k;
}

static uint32_t pa_assinatura_fundos(const fundo_info_t* f, int n)
{
  uint32_t h = 2166136261u + (uint32_t)n;
  for (int i = 0; i < n; ++i) {
    for (const char* p = f[i].nome; *p; ++p) { h = (h ^ (uint8_t)*p) * 16777619u; }
    for (const char* p = f[i].resumo; *p; ++p) { h = (h ^ (uint8_t)*p) * 16777619u; }
  }
  return h;
}

// GEOMETRIA DAS FAIXAS - a mesma conta para desenhar e para o toque.
typedef struct {
  int av_y0, av_y1;   // avisos
  int fu_y0;          // cabecalho "Em segundo plano" (fu_y0 == av_y1 quando nao ha)
  int colunas;
} pa_geo_t;

static pa_geo_t pa_geo(const area_t& a, int n_fundos, bool midia)
{
  pa_geo_t g;
  g.colunas = a.largura >= 1000 ? 4 : 2;
  const int linhas = (n_fundos + g.colunas - 1) / g.colunas;
  const int base = a.y + a.altura - (midia ? MP_ALT + PA_ALCA + 12 : PA_ALCA);
  const int faixa = n_fundos ? PA_CAB + linhas * (PA_CARTAO + 12) : 0;
  g.fu_y0 = base - faixa;
  g.av_y0 = a.y + PA_TOPO;
  g.av_y1 = g.fu_y0 - 8;
  return g;
}

static void pa_cartao_ret(const area_t& a, const pa_geo_t& g, int i, int* x, int* y, int* w)
{
  const int util = a.largura - 2 * PA_MARGEM;
  *w = (util - (g.colunas - 1) * 12) / g.colunas;
  *x = a.x + PA_MARGEM + (i % g.colunas) * (*w + 12);
  *y = g.fu_y0 + PA_CAB + (i / g.colunas) * (PA_CARTAO + 12);
}

static void desenhar_cartoes(lgfx::LovyanGFX& d, const area_t& a, const pa_geo_t& g)
{
  if (!s_pa_nf) { return; }
  const int lx = a.x + PA_MARGEM;
  d.fillRect(a.x, g.fu_y0, a.largura, PA_CAB, TEMA_FUNDO);
  // Rotulo da faixa em maiusculas (so ASCII; as demais letras ficam como estao).
  char rot[40];
  snprintf(rot, sizeof(rot), "%s", tr("Em segundo plano"));
  for (char* p = rot; *p; ++p) { if (*p >= 'a' && *p <= 'z') { *p -= 'a' - 'A'; } }
  pa_fonte(d, true, TEMA_LETRA_LEGENDA);
  d.setTextDatum(middle_left);
  d.setTextColor(TEMA_TEXTO_2, TEMA_FUNDO);
  d.drawString(rot, lx + 8, g.fu_y0 + PA_CAB / 2);
  tab5_fonte_corpo(d);
  for (int i = 0; i < s_pa_nf; ++i) {
    int x, y, w;
    pa_cartao_ret(a, g, i, &x, &y, &w);
    const fundo_info_t& f = s_pa_fundos[i];
    d.fillRoundRect(x, y, w, PA_CARTAO, 20, TEMA_SUPERFICIE);
    const int xx = x + w - 40, cy = y + PA_CARTAO / 2;
    const int meia = desenhar_x_parar(d, xx, cy, pa_confirmando(f.nome));
    const int tw = xx - meia - 12 - (x + 20);
    char t[48];
    snprintf(t, sizeof(t), "%s", tr(f.nome));
    pa_fonte(d, true, TEMA_LETRA_CORPO);
    tab5_cortar_com_reticencias(d, t, sizeof(t), tw);
    d.setTextDatum(middle_left);
    d.setTextColor(TEMA_TEXTO, TEMA_SUPERFICIE);
    d.drawString(t, x + 20, y + PA_CARTAO * 33 / 100);
    pa_fonte(d, false, 24);
    char r[NOTIF_RESUMO + 8];
    snprintf(r, sizeof(r), "%s", f.resumo);
    tab5_cortar_com_reticencias(d, r, sizeof(r), tw);
    d.setTextColor(TEMA_TEXTO_2, TEMA_SUPERFICIE);
    d.drawString(r, x + 20, y + PA_CARTAO * 70 / 100);
  }
  tab5_fonte_corpo(d);
  d.setTextDatum(top_left);
}

// Muda o LAYOUT da central: aviso novo/lido, miniplayer entrou/saiu, parte
// de fundo entrou/saiu/mudou o resumo, ou um "Parar?" venceu o prazo.
bool painel_avisos_mudou(void)
{
  if (!s_painel_avisos) { return false; }
  midia_info_t mi;
  const bool tem_midia = midia_ler(&mi);
  if (notif_versao() != s_pa_versao || tem_midia != s_pa_midia) { return true; }
  if (s_pa_conf && (int32_t)(s_pa_conf_ate - tab5_agora_ms()) <= 0) { s_pa_conf = nullptr; return true; }
  fundo_info_t f[FUNDO_MAX];
  const int n = pa_ler_fundos(f, &mi);
  return pa_assinatura_fundos(f, n) != s_pa_fundos_sig;
}

// Muda so o que o miniplayer mostra (o segundo, tocar/pausar, a musica).
bool painel_avisos_dados_mudou(void)
{
  return s_painel_avisos && s_pa_midia && midia_versao_posicao() != s_pa_midia_pos;
}

void desenhar_painel_avisos(lgfx::LovyanGFX& d, const area_t& a, bool completo)
{
  // Passada de DADOS (nao completa): so o miniplayer pode ter mudado.
  if (!completo) {
    if (s_pa_midia && midia_versao_posicao() != s_pa_midia_pos) { desenhar_miniplayer(d, a); }
    return;
  }
  s_pa_versao = notif_versao();
  midia_info_t mi;
  s_pa_midia = midia_ler(&mi);
  s_pa_nf = pa_ler_fundos(s_pa_fundos, &mi);
  s_pa_fundos_sig = pa_assinatura_fundos(s_pa_fundos, s_pa_nf);
  {
    // A linha cinza de cada parte de fundo sai dos avisos: ela ja esta no
    // cartao dela (ou no miniplayer), com o X. Avisos de verdade ficam.
    notif_item_t todos[NOTIF_MAX_FONTES];
    const int n = notif_listar(todos, NOTIF_MAX_FONTES);
    fundo_info_t f[FUNDO_MAX];
    const int nf = sistema_fundo_listar(f, FUNDO_MAX);
    s_pa_n = 0;
    for (int i = 0; i < n; ++i) {
      bool de_fundo = false;
      for (int k = 0; k < nf; ++k) { if (strcmp(todos[i].nome, f[k].nome) == 0) { de_fundo = true; } }
      if (todos[i].nivel == NOTIF_ESTADO && de_fundo) { continue; }
      if (s_pa_midia && mi.fonte && strcmp(todos[i].nome, mi.fonte) == 0) { continue; }
      s_pa_itens[s_pa_n++] = todos[i];
    }
  }
  const pa_geo_t g = pa_geo(a, s_pa_nf, s_pa_midia);

  // Fundo do painel (o app de baixo continua encoberto).
  d.fillRect(a.x, a.y, a.largura, a.altura, TEMA_FUNDO);

  const int x0 = a.x + PA_MARGEM, w = a.largura - 2 * PA_MARGEM;
  tab5_titulo(d, TEMA_LETRA_TITULO);
  d.setTextDatum(middle_left);
  d.setTextColor(TEMA_TEXTO, TEMA_FUNDO);
  d.drawString(tr("Avisos"), x0, a.y + PA_TOPO / 2);
  pa_fonte(d, false, 24);
  d.setTextDatum(middle_right);
  d.setTextColor(TEMA_TEXTO_2, TEMA_FUNDO);
  d.drawString(tr("toque fora para fechar"), x0 + w, a.y + PA_TOPO / 2);

  // ---- avisos, com rolagem ----
  const int h = pa_linha(a), passo = h + PA_VAO;
  const int cabem = (g.av_y1 - g.av_y0 + PA_VAO) / passo;
  const int max_primeiro = s_pa_n > cabem ? s_pa_n - cabem : 0;
  if (s_pa_primeiro > max_primeiro) { s_pa_primeiro = max_primeiro; }
  if (s_pa_primeiro < 0) { s_pa_primeiro = 0; }
  s_pa_desenhadas = 0;
  if (s_pa_n == 0) {
    pa_fonte(d, false, TEMA_LETRA_CORPO);
    d.setTextColor(TEMA_TEXTO_2, TEMA_FUNDO);
    d.setTextDatum(middle_center);
    d.drawString(tr("Nenhum aviso"), a.x + a.largura / 2, (g.av_y0 + g.av_y1) / 2);
  }
  // Um cartao so para as linhas que cabem; o filete separa uma da outra.
  int visiveis = s_pa_n - s_pa_primeiro;
  if (visiveis > cabem) { visiveis = cabem; }
  if (visiveis > 0) { d.fillRoundRect(x0, g.av_y0, w, visiveis * passo, TEMA_RAIO_G, TEMA_SUPERFICIE); }
  for (int k = 0; k < visiveis; ++k) {
    const int y = g.av_y0 + k * passo;
    const notif_item_t& it = s_pa_itens[s_pa_primeiro + k];
    const uint16_t cor = cor_do_nivel(it.nivel);
    const app_t* origem = app_por_nome(it.app);
    if (k > 0) { d.drawFastHLine(x0 + 24, y, w - 48, TEMA_LINHA); }

    // Faixa de 6 px na cor do nivel (recuada, para nao sair dos cantos do cartao).
    d.fillRoundRect(x0 + 12, y + 16, 6, h - 32, 3, cor);

    // O icone do app de origem, quando ha; o texto comeca depois dele.
    int tx0 = x0 + 32;
    if (origem && origem->icone) {
      origem->icone(d, tx0, y + (h - 56) / 2, 56);
      tx0 += 72;
    }

    // Contagem a direita: pilula na cor do nivel, numero escuro por cima.
    char num[8];
    if (it.quantidade > 999) { snprintf(num, sizeof(num), "999+"); }
    else                     { snprintf(num, sizeof(num), "%d", it.quantidade); }
    pa_fonte(d, true, 24);
    const int bl = (int)d.textWidth(num) + 28, ba = 40;
    const int bx = x0 + w - 24 - bl;
    d.fillRoundRect(bx, y + h / 2 - ba / 2, bl, ba, ba / 2, cor);
    d.setTextDatum(middle_center);
    d.setTextColor(TEMA_FUNDO, cor);
    d.drawString(num, bx + bl / 2, y + h / 2);

    // Quem (negrito) e o que (resumo). Quem abre outro app ganha a seta.
    char quem[40];
    snprintf(quem, sizeof(quem), origem ? "%s  >" : "%s", tr(it.nome));
    d.setTextDatum(middle_left);
    d.setTextColor(TEMA_TEXTO, TEMA_SUPERFICIE);
    pa_fonte(d, true, TEMA_LETRA_CORPO);
    d.drawString(quem, tx0, y + h * 34 / 100);
    pa_fonte(d, false, 24);
    d.setTextColor(TEMA_TEXTO_2, TEMA_SUPERFICIE);
    char r[NOTIF_RESUMO + 8];
    snprintf(r, sizeof(r), "%s", it.resumo);
    tab5_cortar_com_reticencias(d, r, sizeof(r), bx - 16 - tx0);
    d.drawString(r, tx0, y + h * 70 / 100);
    ++s_pa_desenhadas;
  }
  // Ha mais acima/abaixo: diz quantos, e que arrastar rola.
  pa_fonte(d, false, TEMA_LETRA_LEGENDA);
  d.setTextColor(TEMA_TEXTO_3, TEMA_FUNDO);
  d.setTextDatum(middle_right);
  if (s_pa_primeiro > 0) {
    char m[32];
    snprintf(m, sizeof(m), tr("%d acima - arraste"), s_pa_primeiro);
    d.drawString(m, x0 + w - 8, g.av_y0 - 14);
  }
  const int abaixo = s_pa_n - s_pa_primeiro - s_pa_desenhadas;
  if (abaixo > 0) {
    char m[32];
    snprintf(m, sizeof(m), tr("%d abaixo - arraste"), abaixo);
    d.drawString(m, x0 + w - 8, g.av_y0 + s_pa_desenhadas * passo + 14);
  }

  desenhar_cartoes(d, a, g);
  if (s_pa_midia) { desenhar_miniplayer(d, a); }

  // A alca de fechar: pilula de 72x6 no fim do painel.
  d.fillRoundRect(a.x + (a.largura - 72) / 2, a.y + a.altura - 15, 72, 6, 3, TEMA_LINHA);
  d.setTextDatum(top_left);
  tab5_fonte_corpo(d);
}

// Toque com a central aberta ('ty' ja corrigido). Linha ou cartao = abre o
// app dele; X = parar (com confirmacao); qualquer outro lugar = fecha.
void painel_avisos_tocar(const area_t& a, int tx, int ty)
{
  if (miniplayer_tocar(a, tx, ty)) { return; }
  const pa_geo_t g = pa_geo(a, s_pa_nf, s_pa_midia);
  const app_t* destino = nullptr;
  bool fechar = true;

  for (int i = 0; i < s_pa_nf; ++i) {
    int x, y, w;
    pa_cartao_ret(a, g, i, &x, &y, &w);
    if (tx < x || tx >= x + w || ty < y || ty >= y + PA_CARTAO) { continue; }
    if (tx >= x + w - 120) {   // o X (com folga para o dedo)
      pa_tocar_x(s_pa_fundos[i].nome);
      s_sujo_layout = true;
      return;
    }
    destino = app_por_nome(s_pa_fundos[i].nome);
    break;
  }
  if (!destino) {
    const int h = pa_linha(a), passo = h + PA_VAO;
    for (int k = 0; k < s_pa_desenhadas; ++k) {
      const int y = g.av_y0 + k * passo;
      if (ty >= y && ty < y + h && tx >= a.x + PA_MARGEM && tx < a.x + a.largura - PA_MARGEM) {
        destino = app_por_nome(s_pa_itens[s_pa_primeiro + k].app);
        break;
      }
    }
  }
  if (s_pa_conf) {   // tocar em outro lugar desarma o "Parar?" (e nao fecha)
    s_pa_conf = nullptr;
    s_sujo_layout = true;
    if (!destino) { fechar = false; }
  }
  if (!fechar) { return; }
  painel_avisos_abrir(false);
  // O app que ja esta aberto so reaparece - fechar e abrir de novo perderia
  // o que ele estava mostrando.
  if (destino && destino != s_app) { abrir_app(destino); }
}

// Arraste vertical na faixa de avisos: rola, de uma linha em uma linha.
// true = foi rolagem (o gesto nao fecha a central).
bool painel_avisos_rolar(int y0, int dy)
{
  const area_t a = sistema_area();
  const pa_geo_t g = pa_geo(a, s_pa_nf, s_pa_midia);
  if (y0 < g.av_y0 || y0 >= g.av_y1) { return false; }
  const int passo = pa_linha(a) + PA_VAO;
  const int cabem = (g.av_y1 - g.av_y0 + PA_VAO) / passo;
  if (s_pa_n <= cabem) { return false; }   // nada a rolar: o deslize fecha, como sempre
  int linhas = (abs(dy) + passo / 2) / passo;
  if (linhas < 1) { linhas = 1; }
  s_pa_primeiro += dy < 0 ? linhas : -linhas;   // arrastar para cima mostra os de baixo
  s_sujo_layout = true;   // desenhar_painel_avisos limita ao intervalo valido
  return true;
}
