#include "teclado_virtual.hpp"
#include "idioma.h"
#include "tab5_tela.hpp"
#include "tab5_ui.hpp"
#include "tab5_tema.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tab5_hora.hpp"   // tab5_agora_ms
#include "tab5_teclado.hpp" // tempos da repeticao
#include <stdlib.h>
#include <string.h>

// QUATRO PAGINAS, mesma geometria (10/10/9/7 teclas): trocar de pagina so troca
// rotulos, nada pula de lugar. Cada tecla e UM caractere UTF-8.
//  0 letras: a fileira dos numeros fica sempre (senha, PIN e hora sem trocar
//    de pagina).
//  1 "?123": a pontuacao e os pares - parenteses, chaves, colchetes, sinais.
//  2 "=\<": o resto do ASCII e os acentos do portugues (a fonte do sistema
//    tem estes e nao muito mais - ver ferramentas/gerar_fontes.py). Os
//    acentos seguem o shift, como as letras.
//  3 emoji: os do dia a dia, todos da tabela leve do sistema (tab5_emoji -
//    a mesma do Mesh); nas paginas de simbolos a tecla da esquerda circula
//    "=\<" -> emoji -> "?123". Emoji nao volta as letras sozinho: da para
//    mandar varios seguidos.
static const int N_FILA[TECVIRT_FILAS] = { 10, 10, 9, 7 };
static const char* const PAGINA[4][TECVIRT_FILAS] = {
  { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm" },
  { "1234567890", "@#$%&-+()/", "*\"':;!?_=", "[]{}<>\\" },
  { "1234567890", "áàâãéêíóôõ", "úç°ºª|~^`", "•…«»×÷±" },
  { "😀😂🤣😊😍😘😉😎🤔😢", "😭😡😱🥳😇🤗👍👎👏🙏", "💪👋👀🔥🎉❤⭐✅❌", "💡📍🏠☀⚡☕🎵" },
};

#define FILAS   TECVIRT_FILAS

// O PERFIL DE CADA TIPO DE CAMPO (a tabela do .hpp). A unica fonte: abrir e
// tecvirt_tipo copiam daqui para o teclado.
enum { ACAO_OK = 0, ACAO_LINHA, ACAO_BUSCAR };
typedef struct {
  uint8_t acao;        // o que a tecla verde e
  bool    frase;       // maiuscula automatica e espaco duplo (com tecvirt_contexto)
  bool    variantes;   // acentos no toque longo
  bool    emoji;       // a pagina de emoji
  bool    ascii;       // so ASCII
  bool    digitos;     // so 0-9
} perfil_t;
static const perfil_t PERFIL[TECVIRT_TIPOS] = {
  /* TEXTO     */ { ACAO_OK,     false, true,  false, false, false },
  /* ASCII     */ { ACAO_OK,     false, false, false, true,  false },
  /* SENHA     */ { ACAO_OK,     false, false, false, true,  false },
  /* NUMERO    */ { ACAO_OK,     false, false, false, true,  true  },
  /* BUSCA     */ { ACAO_BUSCAR, false, true,  false, false, false },
  /* PARAGRAFO */ { ACAO_LINHA,  true,  true,  true,  false, false },
  /* CODIGO    */ { ACAO_LINHA,  false, true,  false, false, false },
  /* COMANDO   */ { ACAO_LINHA,  false, true,  false, false, false },
};
static const perfil_t& perfil(const tecvirt_t* t) { return PERFIL[t->tipo < TECVIRT_TIPOS ? t->tipo : TECVIRT_TEXTO]; }

// TOQUE LONGO: as variantes de cada tecla (minusculas; o shift vale), na
// ordem do teclado do Android. So o que a fonte do sistema desenha
// (ferramentas/gerar_fontes.py): sem i grave e sem U grave maiusculo.
static const struct { int base; const char* var; } VARIANTES[] = {
  { 'a', "áàâãä" }, { 'e', "éêèë" }, { 'i', "íîï" }, { 'o', "óôõòö" },
  { 'u', "úüû" },   { 'c', "ç" },    { 'n', "ñ" },
  { '.', "!?,;:…" },   // a tecla do ponto, na fileira de comando
};

// ---------------------------------------------------------------------------
// MEDIDAS POR ORIENTACAO
// ---------------------------------------------------------------------------
// Deitado (1280 de largura) a tecla tem ~120 px de largura e 52 de altura: o
// teclado e baixo e largo, e sobra pouca altura na tela. Em pe e o contrario:
// dez teclas em 720 px dao so ~67 px de largura, e a tela tem altura de sobra.
// Entao, em pe, a tecla cresce PARA CIMA (68 px, quase quadrada), o vao
// encolhe um pouco para devolver largura as teclas, e a letra fica maior. O
// teclado inteiro sobe ~80 px - o que a conversa em pe tem para dar.
static bool em_pe(const area_t& a) { return a.largura < a.altura; }
static int  alt_t(const area_t& a)    { return em_pe(a) ? 68 : 52; }   // altura de cada tecla
static int  vao(const area_t& a)      { return em_pe(a) ? 5 : 6; }
static int  px_letra(const area_t& a) { return em_pe(a) ? 34 : 30; }   // fonte das letras e numeros

static int altura_teclas(const area_t& a) { return vao(a) + (FILAS + 1) * (alt_t(a) + vao(a)); }

// O fisico encaixado esconde o virtual (ver o .hpp).
static bool fisico(void) { return tab5_teclado_presente(); }

int tecvirt_altura(const area_t& a)
{
  return fisico() ? 0 : altura_teclas(a);
}

// ---------------------------------------------------------------------------
// UTF-8 E SHIFT
// ---------------------------------------------------------------------------
int tecvirt_utf8(int cp, char out[4])
{
  if (cp < 0x80)    { out[0] = (char)cp; return 1; }
  if (cp < 0x800)   { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F)); return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

// O codepoint da coluna 'col' de uma fileira (UTF-8). 0 = fora dela.
static int cp_em(const char* s, int col)
{
  const unsigned char* u = (const unsigned char*)s;
  for (int k = 0; *u; ++k) {
    int cp, n;
    if      (u[0] < 0x80)           { cp = u[0]; n = 1; }
    else if ((u[0] & 0xE0) == 0xC0) { cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); n = 2; }
    else if ((u[0] & 0xF0) == 0xE0) { cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); n = 3; }
    else                            { cp = ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F); n = 4; }
    if (k == col) { return cp; }
    u += n;
  }
  return 0;
}

// Letra (ASCII ou Latin-1 minuscula/maiuscula)? E a maiuscula dela.
static bool eh_letra(int cp)
{
  return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')
      || (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7);
}
static int maiuscula(int cp)
{
  if (cp >= 'a' && cp <= 'z') { return cp - 32; }
  if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) { return cp - 0x20; }
  return cp;
}

// O que a tecla de caractere digita agora (pagina e shift aplicados).
static int cp_tecla(const tecvirt_t* t, int fila, int col)
{
  if (fila < 0 || fila >= FILAS || col < 0 || col >= N_FILA[fila]) { return 0; }
  const int cp = cp_em(PAGINA[t->pagina][fila], col);
  return t->shift != TECVIRT_SHIFT_NAO ? maiuscula(cp) : cp;
}

// ---------------------------------------------------------------------------
// TECLAS ESPECIAIS
// ---------------------------------------------------------------------------
// Como no teclado do Android: shift e apagar nas pontas da fileira do "zxcv",
// e embaixo a troca de pagina, virgula, espaco, ponto, ENVIAR e fechar. Cor,
// o que faz e a largura num lugar so: o desenho completo, o de uma tecla
// isolada e o toque precisam concordar. Na fileira de comando a largura vai
// em MEIAS unidades de 1/10 da largura (20 no total); shift e apagar ocupam
// a sobra da fileira de 7 teclas (1,5 unidade cada).
//
// ICONES DESENHADOS, NAO DIGITADOS: os codigos < 0x20 viram desenho em
// pintar_tecla (shift solto/uma letra/travado e apagar).
#define ICO_SHIFT  "\x01"
#define ICO_SHIFT1 "\x02"
#define ICO_CAPS   "\x03"
#define ICO_APAGA  "\x04"
#define ICO_ENTER  "\x05"   // nova linha (a fonte nao tem o simbolo)
enum { E_SHIFT, E_APAGA, E_MODO, E_VIRG, E_ESPACO, E_PONTO, E_ENVIAR, E_FECHAR, NESP };
#define ID_ESP(i) (64 + (i))
#define CMD0 E_MODO   // a primeira especial da fileira de comando
typedef struct { uint16_t cor; int acao; int meias; } esp_t;
static const esp_t ESP[NESP] = {
  { TEMA_LINHA,    0,              0 },   // shift / troca de pagina de simbolos (interna)
  { TEMA_LINHA,    TECVIRT_APAGAR, 0 },
  { TEMA_LINHA,    0,              3 },   // ?123 / ABC (interna)
  { TEMA_LINHA,    ',',            2 },
  { TEMA_LINHA,    ' ',            8 },
  { TEMA_LINHA,    '.',            2 },
  { TEMA_DESTAQUE, TECVIRT_ENVIAR, 3 },   // a unica tecla de destaque: enviar / OK
  { TEMA_LINHA,    TECVIRT_FECHAR, 2 },
};

// O rotulo da especial no estado atual.
static const char* rotulo_esp(const tecvirt_t* t, int i)
{
  switch (i) {
    case E_SHIFT:
      if (t->pagina == 1) { return "=\\<"; }
      if (t->pagina == 2) { return "😀"; }
      if (t->pagina == 3) { return "?123"; }
      return t->shift == TECVIRT_SHIFT_TRAVA ? ICO_CAPS : t->shift == TECVIRT_SHIFT_UMA ? ICO_SHIFT1 : ICO_SHIFT;
    case E_APAGA:  return ICO_APAGA;
    case E_MODO:   return t->pagina ? "ABC" : "?123";
    case E_VIRG:   return ",";
    case E_ESPACO: return tr("espaço");
    case E_PONTO:  return ".";
    case E_ENVIAR:
      switch (perfil(t).acao) {
        case ACAO_LINHA:  return ICO_ENTER;
        case ACAO_BUSCAR: return tr("BUSCAR");
        default:          return "OK";
      }
    default:       return "X";
  }
}

static uint16_t cor_esp(const tecvirt_t* t, int i)
{
  if (i == E_SHIFT && t->pagina == 0 && t->shift != TECVIRT_SHIFT_NAO) { return TEMA_DESTAQUE; }   // aceso = vale
  return ESP[i].cor;
}

// ---------------------------------------------------------------------------
// GEOMETRIA - calculada e nao tabelada: a largura muda com a rotacao.
// ---------------------------------------------------------------------------
static int topo_de(const area_t& a) { return a.y + a.altura - altura_teclas(a); }
static int y_fila(const area_t& a, int f) { return topo_de(a) + vao(a) + f * (alt_t(a) + vao(a)); }
static int larg_tecla(const area_t& a) { return (a.largura - 2 * vao(a) - vao(a) * 9) / 10; }   // pela maior fileira
static int recuo_fila(const area_t& a, int f)
{
  const int u = larg_tecla(a) + vao(a);
  return (a.largura - 2 * vao(a) - N_FILA[f] * u + vao(a)) / 2;
}

static bool tecla_em(const area_t& a, int fila, int col, int* x, int* y, int* l)
{
  if (fila < 0 || fila >= FILAS || col < 0 || col >= N_FILA[fila]) { return false; }
  *l = larg_tecla(a);
  *x = a.x + vao(a) + recuo_fila(a, fila) + col * (*l + vao(a));
  *y = y_fila(a, fila);
  return true;
}

static bool especial_em(const area_t& a, int i, int* x, int* y, int* l)
{
  if (i < 0 || i >= NESP) { return false; }
  const int total = a.largura - 2 * vao(a);
  if (i == E_SHIFT || i == E_APAGA) {
    const int r = recuo_fila(a, FILAS - 1);
    const int fim_letras = r + N_FILA[FILAS - 1] * (larg_tecla(a) + vao(a));
    *y = y_fila(a, FILAS - 1);
    if (i == E_SHIFT) { *x = a.x + vao(a);              *l = r - vao(a); }
    else              { *x = a.x + vao(a) + fim_letras; *l = total - fim_letras; }
    return true;
  }
  int inicio = 0;
  for (int k = CMD0; k < i; ++k) { inicio += ESP[k].meias; }
  *x = a.x + vao(a) + (inicio * total) / 20;
  *y = y_fila(a, FILAS);
  *l = ((inicio + ESP[i].meias) * total) / 20 - (inicio * total) / 20 - vao(a);
  return true;
}

static bool retangulo(const area_t& a, int id, int* x, int* y, int* l)
{
  if (id >= 64) { return especial_em(a, id - 64, x, y, l); }
  if (id < 0) { return false; }
  return tecla_em(a, id / 16, id % 16, x, y, l);
}

// O ALVO DE UMA TECLA (onde o toque vale para ela) e maior que o desenho:
//  - o vao entre teclas e de alguem: cada uma responde ate a metade dele,
//    como em teclado de celular;
//  - nas pontas das fileiras recuadas ("asdf..."), meia tecla a mais para
//    fora: o recuo era ponto morto, e o dedo que mira o "a" e cai um pouco a
//    esquerda dele quer o "a", nao nada. Shift e apagar vao ate a borda;
//  - a fileira de comando vai de borda a borda e para baixo sem limite (a
//    faixa de navegacao travada entrega o toque na ultima linha da area).
static const int ALVO_SEM_FIM = 1 << 14;

static bool alvo_de(const area_t& a, int id, int* x0, int* y0, int* x1, int* y1)
{
  const int f = (vao(a) + 1) / 2;
  int x, y, l;
  if (!retangulo(a, id, &x, &y, &l)) { return false; }
  *x0 = x - f;
  *x1 = x + l + f;
  *y0 = y - f;
  *y1 = y + alt_t(a) + f;
  if (id >= 64) {
    const int i = id - 64;
    if (i == E_SHIFT || i == CMD0) { *x0 = a.x; }
    if (i == E_APAGA || i == E_FECHAR) { *x1 = a.x + a.largura; }
    if (i >= CMD0) { *y1 = ALVO_SEM_FIM; }
    return true;
  }
  const int fi = id / 16, c = id % 16;
  if (fi < FILAS - 1) {
    if (c == 0)              { *x0 -= l / 2; }
    if (c == N_FILA[fi] - 1) { *x1 += l / 2; }
  }
  return true;
}

static bool no_alvo(const area_t& a, int id, int xs, int ys, int folga)
{
  int x0, y0, x1, y1;
  if (!alvo_de(a, id, &x0, &y0, &x1, &y1)) { return false; }
  return xs >= x0 - folga && xs < x1 + folga && ys >= y0 - folga && ys < y1 + folga;
}

// HISTERESE: o dedo que ja esta numa tecla so passa para a vizinha quando
// entra HISTERESE_PX nela. Na fronteira exata, o tremor do dedo (e o do
// GT911) trocava a tecla de uma amostra para outra - e a que valia era a da
// ultima amostra antes de soltar, no sorteio. 'atual' = a tecla em que o
// dedo estava (TECVIRT_NENHUMA para um toque novo).
static const int HISTERESE_PX = 8;

static int tecla_no_ponto(const area_t& a, int xs, int ys, int atual)
{
  if (atual != TECVIRT_NENHUMA && no_alvo(a, atual, xs, ys, HISTERESE_PX)) { return atual; }
  for (int fi = 0; fi < FILAS; ++fi) {
    for (int c = 0; c < N_FILA[fi]; ++c) {
      if (no_alvo(a, fi * 16 + c, xs, ys, 0)) { return fi * 16 + c; }
    }
  }
  for (int i = 0; i < NESP; ++i) {
    if (no_alvo(a, ID_ESP(i), xs, ys, 0)) { return ID_ESP(i); }
  }
  return TECVIRT_NENHUMA;
}

static bool eh_apagar(int id) { return id == ID_ESP(E_APAGA); }

// ---------------------------------------------------------------------------
// DESENHO - so primitivas e a fonte do sistema (cache de glifos): nenhuma
// imagem guardada. Uma imagem do teclado pronto custaria ~420 KB de PSRAM por
// estado para economizar alguns ms numa pintura que so acontece ao abrir e ao
// trocar shift/pagina; no dia a dia so 1-2 teclas sao repintadas por toque.
// ---------------------------------------------------------------------------

// A fonte de cada especial, uma vez por largura: o MAIOR rotulo que ela pode
// ter ("ENVIAR", "espaço", "?123") desce de 2 em 2 ate caber (5 px de
// respiro de cada lado).
static void medir(tecvirt_t* t, lgfx::LovyanGFX& d)
{
  const char* const VARIANTES[NESP][2] = {   // os rotulos no idioma atual
    { "=\\<", "?123" }, { "", "" }, { "?123", "ABC" }, { ",", "" },
    { tr("espaço"), "" }, { ".", "" }, { "OK", tr("BUSCAR") }, { "X", "" },
  };
  const int chave = t->a.largura ^ (int)(idioma_versao() << 16);   // outra largura ou outro idioma: mede de novo
  if (t->med_larg == chave) { return; }
  t->med_larg = chave;
  for (int i = 0; i < NESP; ++i) {
    int x, y, l;
    especial_em(t->a, i, &x, &y, &l);
    int px = px_letra(t->a);
    for (; px > 14; px -= 2) {
      d.setFont(tab5_fonte_suave(true, px));
      int w = 0;
      for (int v = 0; v < 2; ++v) { const int wv = (int)d.textWidth(VARIANTES[i][v]); if (wv > w) { w = wv; } }
      if (w <= l - 10) { break; }
    }
    t->px_esp[i] = (uint8_t)px;
  }
}

// Seta do shift: cheia quando vale (uma letra ou travado), so o contorno
// quando solta; travado ganha a barra embaixo.
static void icone_shift(lgfx::LovyanGFX& d, int cx, int cy, char tipo, uint16_t tinta)
{
  const int oy = (tipo == ICO_CAPS[0]) ? cy - 3 : cy;
  if (tipo == ICO_SHIFT[0]) {
    for (int o = 0; o < 2; ++o) {
      d.drawTriangle(cx, oy - 15 + o, cx - 15 + o, oy + 1 - o, cx + 15 - o, oy + 1 - o, tinta);
      d.drawRect(cx - 6 + o, oy + 1 - o, 12 - 2 * o, 13, tinta);
    }
    return;
  }
  d.fillTriangle(cx, oy - 15, cx - 15, oy + 1, cx + 15, oy + 1, tinta);
  d.fillRect(cx - 6, oy + 1, 12, 13, tinta);
  if (tipo == ICO_CAPS[0]) { d.fillRect(cx - 13, oy + 18, 26, 4, tinta); }
}

// Nova linha: o desenho de sempre do "return" - desce pela direita e volta
// para a esquerda com a ponta da seta.
static void icone_enter(lgfx::LovyanGFX& d, int cx, int cy, uint16_t tinta)
{
  for (int o = 0; o < 3; ++o) {
    d.drawFastVLine(cx + 12 + o, cy - 14, 18, tinta);
    d.drawFastHLine(cx - 12, cy + 2 + o, 27, tinta);
  }
  d.fillTriangle(cx - 22, cy + 3, cx - 10, cy - 7, cx - 10, cy + 13, tinta);
}

// Apagar: corpo com ponta a esquerda e um X vazado na cor da tecla.
static void icone_apagar(lgfx::LovyanGFX& d, int cx, int cy, uint16_t cor, uint16_t tinta)
{
  d.fillTriangle(cx - 24, cy, cx - 9, cy - 14, cx - 9, cy + 14, tinta);
  d.fillRect(cx - 9, cy - 14, 33, 29, tinta);
  for (int o = -1; o <= 1; ++o) {
    d.drawLine(cx + 1 + o, cy - 6, cx + 13 + o, cy + 6, cor);
    d.drawLine(cx + 13 + o, cy - 6, cx + 1 + o, cy + 6, cor);
  }
}

static void pintar_tecla(const tecvirt_t* t, lgfx::LovyanGFX& d, int id, bool acesa)
{
  int x, y, l;
  if (!retangulo(t->a, id, &x, &y, &l)) { return; }
  char letra[5];
  const char* txt;
  uint16_t cor;
  int px;
  if (id >= 64) {
    txt = rotulo_esp(t, id - 64);
    cor = cor_esp(t, id - 64);
    px  = t->px_esp[id - 64];
  } else {
    letra[tecvirt_utf8(cp_tecla(t, id / 16, id % 16), letra)] = 0;
    txt = letra;
    cor = TEMA_SUPERFICIE_ALTA;
    px  = px_letra(t->a);
  }
  // Letra e icone: branco sobre o destaque, texto comum nas demais.
  const uint16_t tinta = cor == TEMA_DESTAQUE ? (uint16_t)TEMA_SOBRE_DESTAQUE : (uint16_t)TEMA_TEXTO;
  // Acesa = DESTAQUE_SUAVE, e nao "a mesma cor mais clara": debaixo do dedo so
  // a borda aparece, e uma cor DIFERENTE e o que se ve nela.
  if (acesa) { cor = TEMA_DESTAQUE_SUAVE; }
  d.fillRoundRect(x, y, l, alt_t(t->a), TEMA_RAIO_M, cor);

  const int cx = x + l / 2, cy = y + alt_t(t->a) / 2;
  if ((unsigned char)txt[0] < 0x20) {
    if (txt[0] == ICO_APAGA[0])      { icone_apagar(d, cx, cy, cor, tinta); }
    else if (txt[0] == ICO_ENTER[0]) { icone_enter(d, cx, cy, tinta); }
    else                             { icone_shift(d, cx, cy, txt[0], tinta); }
    return;
  }
  d.setFont(tab5_fonte_suave(true, px));
  d.setTextColor(tinta, cor);
  d.drawString(txt, cx, cy);
}

static void soltar_dedos(tecvirt_t* t);

// A FAIXA DAS VARIANTES (toque longo), por cima da fileira de cima da tecla:
// uma celula por variante, a escolhida em destaque.
static void pintar_faixa(tecvirt_t* t, lgfx::LovyanGFX& d)
{
  const int a = alt_t(t->a);
  d.fillRoundRect(t->pop_x - 4, t->pop_y - 4, t->pop_n * t->pop_l + 8, a + 8, TEMA_RAIO_M, TEMA_LINHA);
  d.setFont(tab5_fonte_suave(true, px_letra(t->a)));
  for (int k = 0; k < t->pop_n; ++k) {
    const int x = t->pop_x + k * t->pop_l;
    const uint16_t cor = k == t->pop_sel ? (uint16_t)TEMA_DESTAQUE : (uint16_t)TEMA_SUPERFICIE_ALTA;
    d.fillRoundRect(x + 2, t->pop_y, t->pop_l - 4, a, TEMA_RAIO_M, cor);
    char u[5];
    u[tecvirt_utf8(t->pop_cp[k], u)] = 0;
    d.setTextColor(k == t->pop_sel ? (uint16_t)TEMA_SOBRE_DESTAQUE : (uint16_t)TEMA_TEXTO, cor);
    d.drawString(u, x + t->pop_l / 2, t->pop_y + a / 2);
  }
}

// Visivel ou nao (o fisico entrou ou saiu com ele aberto): a trava da
// navegacao acompanha, e o app redesenha inteiro - a area dele mudou de tamanho.
static void sincronizar(tecvirt_t* t)
{
  const bool quer = t->aberto && !fisico();
  if (quer == t->trava) { return; }
  t->trava = quer;
  t->sujo = true;
  soltar_dedos(t);
  sistema_travar_navegacao(quer);
  sistema_pedir_redesenho_completo();
}

void tecvirt_pintar(tecvirt_t* t, lgfx::LovyanGFX& d, bool completo)
{
  sincronizar(t);
  if (!t->aberto || fisico()) { return; }
  const bool tudo = completo || t->sujo;
  const bool rot  = !tudo && t->rotulos;
  if (!tudo && !rot && t->tecla_sob == t->tecla_pinta && !t->pop_pintar) { return; }   // nada mudou: nem toca na tela

  const lgfx::TextStyle estilo = d.getTextStyle();
  const lgfx::IFont* fonte = d.getFont();
  medir(t, d);
  d.setTextSize(1);
  d.setTextDatum(middle_center);

  if (tudo || rot) {
    // O fundo so e limpo quando o teclado aparece ou muda de lugar; na troca
    // de shift/pagina as teclas sao opacas, os cantos ja estao no fundo do painel, e
    // repintar todas e mais simples (e quase tao barato) que escolher quais.
    if (tudo) { tab5_tela_limpar(t->a.x, topo_de(t->a), t->a.largura, altura_teclas(t->a), TEMA_SUPERFICIE); }
    else if (t->pop_limpar) {   // a faixa fechou: so o retangulo dela (as teclas vem em seguida)
      tab5_tela_limpar(t->pop_x - 4, t->pop_y - 4, t->pop_n * t->pop_l + 8, alt_t(t->a) + 8, TEMA_SUPERFICIE);
    }
    t->pop_limpar = false;
    for (int f = 0; f < FILAS; ++f) {
      for (int c = 0; c < N_FILA[f]; ++c) { pintar_tecla(t, d, f * 16 + c, f * 16 + c == t->tecla_sob); }
    }
    for (int i = 0; i < NESP; ++i) { pintar_tecla(t, d, ID_ESP(i), ID_ESP(i) == t->tecla_sob); }
    t->sujo = t->rotulos = false;
  } else {
    // Retorno da tecla: no maximo duas - a que o dedo deixou e a que encontrou.
    if (t->tecla_pinta != TECVIRT_NENHUMA) { pintar_tecla(t, d, t->tecla_pinta, false); }
    if (t->tecla_sob   != TECVIRT_NENHUMA) { pintar_tecla(t, d, t->tecla_sob, true); }
  }
  t->tecla_pinta = t->tecla_sob;
  if (t->pop_dedo >= 0 && (tudo || rot || t->pop_pintar)) { pintar_faixa(t, d); }
  t->pop_pintar = false;

  d.setFont(fonte);
  d.setTextStyle(estilo);
}

// ---------------------------------------------------------------------------
// ESTADO: shift, pagina, maiuscula automatica
// ---------------------------------------------------------------------------
static void mudar(tecvirt_t* t, uint8_t shift, uint8_t pagina)
{
  if (shift == t->shift && pagina == t->pagina) { return; }
  t->shift = shift;
  t->pagina = pagina;
  t->rotulos = true;
  sistema_pedir_redesenho();
}

void tecvirt_tipo(tecvirt_t* t, uint8_t tipo)
{
  if (tipo >= TECVIRT_TIPOS) { tipo = TECVIRT_TEXTO; }
  t->tipo = tipo;
  t->frase = PERFIL[tipo].frase;
  t->tem_contexto = false;
  t->ult_acao = 0;
  // A pagina atual pode ter sumido (emoji num campo de senha): volta as letras.
  if (t->pagina == 3 && !PERFIL[tipo].emoji) { t->pagina = 0; }
  if (!t->frase && t->shift == TECVIRT_SHIFT_UMA) { t->shift = TECVIRT_SHIFT_NAO; }
  t->rotulos = true;
  sistema_pedir_redesenho();
}

void tecvirt_contexto(tecvirt_t* t, const char* texto, int cursor)
{
  if (!t->frase || !t->aberto || t->shift == TECVIRT_SHIFT_TRAVA) { return; }
  int i = cursor;
  const int n = texto ? (int)strlen(texto) : 0;
  if (i > n) { i = n; }
  if (i < 0) { i = 0; }
  t->antes[0] = i > 0 ? texto[i - 1] : 0;
  t->antes[1] = i > 1 ? texto[i - 2] : 0;
  t->tem_contexto = true;
  bool espaco = false;
  while (i > 0 && texto[i - 1] == ' ') { --i; espaco = true; }
  const bool inicio = i == 0 || texto[i - 1] == '\n'
                  || (espaco && (texto[i - 1] == '.' || texto[i - 1] == '!' || texto[i - 1] == '?'));
  mudar(t, inicio ? TECVIRT_SHIFT_UMA : TECVIRT_SHIFT_NAO, t->pagina);
}

bool tecvirt_editar(char* buf, size_t cap, int acao)
{
  size_t n = strlen(buf);
  if (acao == TECVIRT_APAGAR) {
    while (n > 0) {
      const unsigned char b = (unsigned char)buf[--n];
      if ((b & 0xC0) != 0x80) { break; }   // continuacao de UTF-8: tira o caractere inteiro
    }
    buf[n] = 0;
    return true;
  }
  if (acao <= 0) { return false; }
  if (acao < 0x20) { return true; }
  char u[4];
  const int k = tecvirt_utf8(acao, u);
  if (n + k < cap) { memcpy(buf + n, u, k); buf[n + k] = 0; }
  return true;
}

// ---------------------------------------------------------------------------
// CICLO DE VIDA
// ---------------------------------------------------------------------------
static void soltar_dedos(tecvirt_t* t)
{
  for (int k = 0; k < SISTEMA_TOQUES_MAX; ++k) {
    auto& dd = t->dedos[k];
    dd.id = -1;
    dd.tecla = dd.firme = dd.cand = TECVIRT_NENHUMA;
    dd.cand_n = 0;
    dd.eco = dd.repetiu = false;
  }
  if (t->pop_dedo >= 0) { t->pop_dedo = -1; t->pop_limpar = t->rotulos = true; }   // a faixa some com os dedos
}

void tecvirt_abrir(tecvirt_t* t, uint8_t tipo)
{
  if (t->aberto) { if (tipo != t->tipo) { tecvirt_tipo(t, tipo); } return; }
  t->aberto = true;
  t->shift = TECVIRT_SHIFT_NAO;
  t->pagina = 0;
  t->sujo = true;
  t->rotulos = false;
  t->livre = 0;
  t->tecla_sob = t->tecla_pinta = t->ultima_tecla = TECVIRT_NENHUMA;
  t->solta.tecla = TECVIRT_NENHUMA;
  t->pop_dedo = -1;
  t->pop_limpar = false;
  soltar_dedos(t);
  tecvirt_tipo(t, tipo);
  // O INICIO fica logo abaixo da tecla de espaco - mas so com o virtual na
  // tela: com o fisico, nada cobre a navegacao.
  t->trava = !fisico();
  if (t->trava) { sistema_travar_navegacao(true); }
}

void tecvirt_fechar(tecvirt_t* t)
{
  if (!t->aberto) { return; }
  t->aberto = false;
  t->tecla_sob = t->tecla_pinta = TECVIRT_NENHUMA;
  soltar_dedos(t);
  if (t->trava) { t->trava = false; sistema_travar_navegacao(false); }
}

void tecvirt_posicionar(tecvirt_t* t, const area_t& a)
{
  if (a.x != t->a.x || a.y != t->a.y || a.largura != t->a.largura || a.altura != t->a.altura) {
    t->a = a;
    t->sujo = true;
  }
}

bool tecvirt_contem(const tecvirt_t* t, int xs, int ys)
{
  return t->aberto && !fisico() && ys >= topo_de(t->a) && xs >= t->a.x && xs < t->a.x + t->a.largura;
}

// ---------------------------------------------------------------------------
// TOQUE
// ---------------------------------------------------------------------------

// O DEDO QUE ABRIU O TECLADO NAO DIGITA. A soltura do toque que abriu (ou
// uma amostra atrasada do GT911 logo depois dela) caia numa tecla e o texto
// ja nascia com uma letra. Ate a tela ficar sem dedo por duas amostras
// seguidas depois de abrir, nada responde (nivel e clique).
static const int LIVRE_AMOSTRAS = 2;

static bool dedos_ativos(const tecvirt_t* t)
{
  for (int k = 0; k < SISTEMA_TOQUES_MAX; ++k) { if (t->dedos[k].id >= 0) { return true; } }
  return false;
}

// Confirma uma tecla (com o freio contra repique). Shift e troca de pagina
// ficam aqui dentro; o resto vai para a lista do app. Devolve a acao, ou 0.
//
// SHIFT COMO NO CELULAR: um toque arma para UMA letra (ela sai maiuscula e o
// shift solta sozinho); outro toque em ate SHIFT_DUPLO_MS trava (caps lock);
// um toque travado solta. Nas paginas de simbolos a mesma tecla alterna
// entre as duas paginas. ESPACO numa pagina de simbolos volta as letras, e
// um acento (pagina 2) tambem: a palavra continua em letras.
// O tipo do campo aceita este caractere?
static bool aceita(const tecvirt_t* t, int cp)
{
  const perfil_t& p = perfil(t);
  if (p.digitos) { return cp >= '0' && cp <= '9'; }
  if (p.ascii)   { return cp >= 0x20 && cp < 0x7F; }
  return true;
}

static int confirmar(tecvirt_t* t, int tecla)
{
  static const uint32_t REPIQUE_MS     = 45;
  static const uint32_t SHIFT_DUPLO_MS = 400;
  if (tecla == TECVIRT_NENHUMA) { return 0; }
  const uint32_t agora = tab5_agora_ms();
  // APAGAR e a tecla batida em rajada no mesmo ponto: sem freio de repique.
  if (!eh_apagar(tecla) && tecla == t->ultima_tecla && agora - t->tecla_ms < REPIQUE_MS) { return 0; }
  t->tecla_ms = agora;
  t->ultima_tecla = (int16_t)tecla;

  if (tecla == ID_ESP(E_SHIFT)) {
    // Nas paginas de simbolos ela circula "?123" -> "=\<" -> emoji, e o emoji
    // so existe nos tipos que o aceitam.
    if (t->pagina) {
      const uint8_t prox = t->pagina == 1 ? 2 : (t->pagina == 2 && perfil(t).emoji) ? 3 : 1;
      mudar(t, t->shift, prox);
      return 0;
    }
    uint8_t s = TECVIRT_SHIFT_NAO;
    if (t->shift == TECVIRT_SHIFT_NAO)                                   { s = TECVIRT_SHIFT_UMA; }
    else if (t->shift == TECVIRT_SHIFT_UMA && agora - t->shift_ms < SHIFT_DUPLO_MS) { s = TECVIRT_SHIFT_TRAVA; }
    t->shift_ms = agora;
    mudar(t, s, 0);
    return 0;
  }
  if (tecla == ID_ESP(E_MODO)) { mudar(t, t->shift, t->pagina ? 0 : 1); return 0; }

  int ac;
  if (tecla >= 64) {
    if (tecla - 64 >= NESP) { return 0; }
    ac = ESP[tecla - 64].acao;
    if (ac == TECVIRT_ENVIAR && perfil(t).acao == ACAO_LINHA) { ac = TECVIRT_LINHA; }
  } else {
    ac = cp_tecla(t, tecla / 16, tecla % 16);
  }
  // O QUE O CAMPO NAO ACEITA NAO SAI (e nao muda shift nem pagina).
  if (ac > 0 && !aceita(t, ac)) { return 0; }
  if (ac > 0) {
    // Depois do caractere: o shift de UMA letra solta; espaco e acento voltam as letras.
    const uint8_t s = t->shift == TECVIRT_SHIFT_UMA ? (uint8_t)TECVIRT_SHIFT_NAO : t->shift;
    const bool volta = t->pagina && (ac == ' ' || (t->pagina == 2 && eh_letra(ac)));
    mudar(t, s, volta ? 0 : t->pagina);
  }
  return ac;
}

// ---------------------------------------------------------------------------
// TOQUE LONGO: A FAIXA DAS VARIANTES
// ---------------------------------------------------------------------------
// As variantes da tecla agora (so na pagina das letras). 0 = nenhuma.
static int variantes_de(const tecvirt_t* t, int tecla, int32_t* out)
{
  if (t->pagina != 0 || !perfil(t).variantes) { return 0; }
  int base;
  if (tecla == ID_ESP(E_PONTO))      { base = '.'; }
  else if (tecla >= 0 && tecla < 64) { base = cp_em(PAGINA[0][tecla / 16], tecla % 16); }
  else                               { return 0; }
  for (const auto& v : VARIANTES) {
    if (v.base != base) { continue; }
    int n = 0;
    for (int k = 0; n < 6; ++k) {
      const int cp = cp_em(v.var, k);
      if (!cp) { break; }
      out[n++] = (t->shift != TECVIRT_SHIFT_NAO && eh_letra(cp)) ? maiuscula(cp) : cp;
    }
    return n;
  }
  return 0;
}

// Abre a faixa da tecla para o dedo k: uma fileira acima, comecando sobre a
// tecla e empurrada para dentro nas bordas. false = a tecla nao tem variantes.
static bool abrir_faixa(tecvirt_t* t, int k, int tecla)
{
  const int n = variantes_de(t, tecla, t->pop_cp);
  int x, y, l;
  if (!n || !retangulo(t->a, tecla, &x, &y, &l)) { return false; }
  const int cel = larg_tecla(t->a) + vao(t->a);
  const int esq = t->a.x + vao(t->a), dir = t->a.x + t->a.largura - vao(t->a);
  int x0 = x + l / 2 - cel / 2;
  if (x0 + n * cel > dir) { x0 = dir - n * cel; }
  if (x0 < esq) { x0 = esq; }
  t->pop_dedo = (int8_t)k;
  t->pop_tecla = (int16_t)tecla;
  t->pop_n = (uint8_t)n;
  t->pop_sel = 0;
  t->pop_x = (int16_t)x0;
  t->pop_y = (int16_t)(y - alt_t(t->a) - vao(t->a));
  t->pop_l = (int16_t)cel;
  t->pop_dedo_x = (int16_t)(x + l / 2);
  t->pop_mexeu = false;
  t->pop_pintar = true;
  return true;
}

// A variante escolhida vale como tecla digitada: o shift de UMA letra solta.
static int confirmar_variante(tecvirt_t* t)
{
  t->tecla_ms = tab5_agora_ms();
  t->ultima_tecla = t->pop_tecla;
  if (t->shift == TECVIRT_SHIFT_UMA) { mudar(t, TECVIRT_SHIFT_NAO, t->pagina); }
  return (int)t->pop_cp[t->pop_sel];
}

static bool letra_ou_numero(int cp) { return eh_letra(cp) || (cp >= '0' && cp <= '9'); }

// UM DEDO POR TECLA, CADA UM COM A SUA SOLTURA.
//
// A tecla acende sob o dedo e vale na SOLTURA daquele dedo - da para
// escorregar ate a certa antes de soltar. Digitando rapido o dedo seguinte
// desce antes de o anterior subir: cada dedo (id do controlador) tem a sua
// tecla, e as letras saem na ordem em que os dedos soltam.
//
// A TECLA QUE VALE E A FIRME, nao a da ultima amostra. Ao subir, a area de
// contato encolhe e o centro dela anda alguns pixels - a ultima amostra antes
// da soltura pegava a vizinha e a letra saia errada. O dedo que desce fixa a
// tecla na hora; escorregar para outra so troca depois de TECLA_FIRME
// amostras seguidas nela (~40 ms): a correcao de quem escorrega de proposito
// continua valendo, o tropeco da soltura nao. A tecla acesa e sempre a firme
// - o que se ve aceso e o que vai ser digitado.
//
// A LETRA SAI NA PRIMEIRA AMOSTRA SEM O DEDO. O GT911 as vezes perde um dedo
// por uma amostra, e a volta dele viraria letra dupla. A volta e reconhecida
// pelo que ela e - a mesma tecla, o mesmo ponto, NA AMOSTRA SEGUINTE a
// soltura - e marcada como ECO: acende, mas nao digita de novo. A janela e
// contada em amostras: o intervalo entre amostras varia com o desenho (20 a
// 45 ms), e um limite so em ms ficava ora curto, ora longo. ECO_MS_MAX e o
// teto: numa volta lenta (desenho pesado), "a amostra seguinte" pode chegar
// tarde o bastante para ser uma batida nova de verdade no mesmo ponto.
// Escorregou para outra tecla, deixa de ser eco.
//
// O ECO NAO VALE PARA O APAGAR, e nunca encadeia. Batido rapido no mesmo
// ponto, o apagar subia e descia dentro da janela do eco: a batida era
// engolida, e a soltura dela virava a referencia da seguinte - uma rajada
// inteira sumia. So a soltura que DIGITOU serve de referencia, e o apagar
// nunca e eco.
//
// APAGAR SEGURADO REPETE, como em qualquer teclado: depois de REP_INICIO_MS
// com o dedo parado nele, apaga um a cada REP_MS.
int tecvirt_nivel(tecvirt_t* t, int* acoes, int max)
{
  static const int      SALTO_PX      = 45;
  static const uint32_t ECO_AMOSTRAS  = 1;
  static const uint32_t ECO_MS_MAX    = 60;   // e nunca mais que isto (volta lenta)
  static const uint8_t  TECLA_FIRME   = 2;
  // Os mesmos tempos do teclado fisico (hal/tab5_teclado.hpp).
  static const uint32_t REP_INICIO_MS = TAB5_TECLA_REPETE_INICIO_MS;
  static const uint32_t REP_MS        = TAB5_TECLA_REPETE_MS;
  static const uint32_t LONGO_MS      = 450;   // segurar uma letra: as variantes
  static const uint32_t ESPACO_DUPLO_MS = 700;
  sincronizar(t);
  if (!t->aberto || fisico()) { return 0; }
  ++t->amostra;
  sistema_toque_t q[SISTEMA_TOQUES_MAX];
  const int n = sistema_toques(q, SISTEMA_TOQUES_MAX);
  const area_t ap = sistema_area();
  // Dedo na faixa do teclado: o toque e dele, a soltura nao vira clique em
  // nada (ver sistema_consumir_toque). Inclusive na carencia de abertura.
  for (int i = 0; i < n; ++i) {
    if (tecvirt_contem(t, ap.x + q[i].x, ap.y + q[i].y)) { sistema_consumir_toque(); break; }
  }
  if (t->livre < LIVRE_AMOSTRAS) {
    if (n) { t->livre = 0; } else { ++t->livre; }
    soltar_dedos(t);
    return 0;
  }

  auto tecla_de = [&](const sistema_toque_t& p, int atual) {
    return (int16_t)tecla_no_ponto(t->a, ap.x + p.x, ap.y + p.y, atual);
  };
  int na = 0;
  const uint32_t agora = tab5_agora_ms();
  auto por = [&](int ac) { if (na < max) { acoes[na++] = ac; } };
  // ESPACO DUPLO = ". " (texto corrido): o primeiro espaco e trocado pelo ponto.
  // ESPACO DUPLO = ". " (PARAGRAFO): o primeiro espaco vira ponto. Decidido
  // pelo TEXTO que o app contou (tecvirt_contexto), nao pela memoria das
  // teclas: com o cursor movido entre os dois espacos, a memoria apagaria
  // a letra errada.
  auto anotar = [&](int ac) {
    if (!ac) { return; }
    const bool duplo = ac == ' ' && t->frase && t->tem_contexto && t->ult_acao == ' '
                    && agora - t->ult_ms < ESPACO_DUPLO_MS
                    && t->antes[0] == ' ' && letra_ou_numero((unsigned char)t->antes[1]);
    if (duplo) {
      por(TECVIRT_APAGAR); por('.'); por(' ');
      t->ult_acao = 0;   // o terceiro espaco seguido e um espaco
      t->ult_ms = agora;
      return;
    }
    por(ac);
    t->ult_acao = ac; t->ult_ms = agora;
  };
  const int16_t antes = t->tecla_sob;

  // 1) Dedos que ja estavam: seguem a tecla sob eles, ou soltam.
  for (int k = 0; k < SISTEMA_TOQUES_MAX; ++k) {
    auto& dd = t->dedos[k];
    if (dd.id < 0) { continue; }
    int achou = -1;
    for (int i = 0; i < n; ++i) { if (q[i].id == dd.id) { achou = i; break; } }
    // O DEDO DA FAIXA: escorregar escolhe a variante, soltar a digita.
    if (k == t->pop_dedo) {
      if (achou >= 0) {
        // Ate o dedo ESCORREGAR vale a primeira variante (como no Android e no
        // iPhone): perto da borda a faixa e empurrada para dentro, e a celula
        // embaixo do dedo parado nao e a que a pessoa quis.
        const int xs = ap.x + q[achou].x;
        if (abs(xs - t->pop_dedo_x) > 12) { t->pop_mexeu = true; }
        int sel = t->pop_mexeu ? (xs - t->pop_x) / t->pop_l : 0;
        if (sel < 0) { sel = 0; }
        if (sel >= t->pop_n) { sel = t->pop_n - 1; }
        if (sel != t->pop_sel) { t->pop_sel = (uint8_t)sel; t->pop_pintar = true; sistema_pedir_redesenho(); }
        dd.x = q[achou].x; dd.y = q[achou].y;
        continue;
      }
      anotar(confirmar_variante(t));
      t->pop_dedo = -1;
      t->pop_limpar = t->rotulos = true;   // limpa so o lugar da faixa e repinta as teclas
      t->solta.tecla = TECVIRT_NENHUMA;
      dd.id = -1;
      dd.tecla = dd.firme = dd.cand = TECVIRT_NENHUMA;
      dd.cand_n = 0;
      dd.eco = dd.repetiu = false;
      continue;
    }
    if (achou >= 0) {
      const int16_t sob = tecla_de(q[achou], dd.tecla);
      const int dx = q[achou].x - dd.x, dy = q[achou].y - dd.y;
      if (sob != dd.tecla && (abs(dx) > SALTO_PX || abs(dy) > SALTO_PX)) {
        // O CONTROLADOR REAPROVEITA O ID: o dedo sobe e o proximo desce
        // dentro de uma amostra, e para o sistema e o "mesmo dedo" que pulou
        // de tecla. Um dedo de verdade nao anda mais que ~45 px numa amostra;
        // um salto maior e um toque novo - a tecla anterior vale e a nova
        // comeca firme.
        if (!dd.eco && !dd.repetiu) { anotar(confirmar(t, dd.firme)); }
        dd.firme = sob;
        dd.cand = TECVIRT_NENHUMA; dd.cand_n = 0;
        dd.desde = dd.rep_ms = agora;
        dd.eco = dd.repetiu = false;
      } else if (sob == dd.firme) {
        dd.cand = TECVIRT_NENHUMA; dd.cand_n = 0;      // voltou: o desvio nao conta
      } else {
        if (sob == dd.cand) { if (dd.cand_n < 255) { ++dd.cand_n; } }
        else                { dd.cand = sob; dd.cand_n = 1; }
        if (dd.cand_n >= TECLA_FIRME) {
          dd.firme = sob;
          dd.cand = TECVIRT_NENHUMA; dd.cand_n = 0;
          dd.eco = dd.repetiu = false;
          dd.rep_ms = agora;
        }
      }
      dd.tecla = sob;
      dd.x = q[achou].x; dd.y = q[achou].y;
      // Repeticao do APAGAR segurado.
      if (eh_apagar(dd.firme) && agora - dd.rep_ms >= (dd.repetiu ? REP_MS : REP_INICIO_MS)) {
        anotar(confirmar(t, dd.firme));
        dd.repetiu = true;
        dd.rep_ms = agora;
      }
      // Segurou parado numa tecla com variantes: abre a faixa (uma por vez).
      else if (t->pop_dedo < 0 && !dd.eco && !dd.repetiu && dd.cand_n == 0 && dd.firme != TECVIRT_NENHUMA
            && agora - dd.rep_ms >= LONGO_MS && abrir_faixa(t, k, dd.firme)) {
        sistema_pedir_redesenho();
      }
    } else {
      const bool digita = !dd.eco && !dd.repetiu;
      if (digita) { anotar(confirmar(t, dd.firme)); }
      // So a soltura que digitou e referencia de eco - um eco nunca gera outro.
      t->solta = { digita ? dd.firme : (int16_t)TECVIRT_NENHUMA, dd.x, dd.y, t->amostra, agora };
      dd.id = -1;
      dd.tecla = dd.firme = dd.cand = TECVIRT_NENHUMA;
      dd.cand_n = 0;
      dd.eco = dd.repetiu = false;
    }
  }
  // 2) Dedos novos: a tecla sob o ponto de descida ja nasce firme.
  for (int i = 0; i < n; ++i) {
    bool conhecido = false;
    for (int k = 0; k < SISTEMA_TOQUES_MAX; ++k) { if (t->dedos[k].id == q[i].id) { conhecido = true; break; } }
    if (conhecido) { continue; }
    for (int k = 0; k < SISTEMA_TOQUES_MAX; ++k) {
      auto& dd = t->dedos[k];
      if (dd.id >= 0) { continue; }
      dd.id = (int8_t)q[i].id;
      dd.tecla = dd.firme = tecla_de(q[i], TECVIRT_NENHUMA);
      dd.cand = TECVIRT_NENHUMA; dd.cand_n = 0;
      dd.x = q[i].x; dd.y = q[i].y;
      dd.desde = dd.rep_ms = agora;
      dd.repetiu = false;
      dd.eco = dd.firme != TECVIRT_NENHUMA && dd.firme == t->solta.tecla
            && t->amostra - t->solta.amostra <= ECO_AMOSTRAS && agora - t->solta.ms <= ECO_MS_MAX
            && !eh_apagar(dd.firme)
            && abs(dd.x - t->solta.x) <= SALTO_PX && abs(dd.y - t->solta.y) <= SALTO_PX;
      break;
    }
  }
  // 3) A tecla acesa e a firme do dedo que desceu por ultimo.
  int recente = -1;
  for (int k = 0; k < SISTEMA_TOQUES_MAX; ++k) {
    if (t->dedos[k].id < 0) { continue; }
    if (recente < 0 || (int32_t)(t->dedos[k].desde - t->dedos[recente].desde) >= 0) { recente = k; }
  }
  t->tecla_sob = recente >= 0 ? t->dedos[recente].firme : TECVIRT_NENHUMA;
  if (t->tecla_sob != antes || na) { sistema_pedir_redesenho(); }
  return na;
}

bool tecvirt_centro(const tecvirt_t* t, int acao, int* xs, int* ys)
{
  int x, y, l;
  for (int fi = 0; fi < FILAS; ++fi) {
    for (int c = 0; c < N_FILA[fi]; ++c) {
      const int cp = cp_tecla(t, fi, c);   // com o shift: a letra vale nas duas caixas
      if ((cp == acao || (acao > 0 && maiuscula(cp) == maiuscula(acao))) && tecla_em(t->a, fi, c, &x, &y, &l)) {
        *xs = x + l / 2; *ys = y + alt_t(t->a) / 2;
        return true;
      }
    }
  }
  for (int i = 0; i < NESP; ++i) {
    if (ESP[i].acao == acao && especial_em(t->a, i, &x, &y, &l)) {
      *xs = x + l / 2; *ys = y + alt_t(t->a) / 2;
      return true;
    }
  }
  return false;
}

int tecvirt_clique(tecvirt_t* t, int xs, int ys)
{
  // So a rede para o toque rapido demais para o nivel ter visto (senao daria
  // letra dupla): o nivel cuida de tudo que ele viu.
  if (!t->aberto || fisico() || t->livre < LIVRE_AMOSTRAS || dedos_ativos(t) || tab5_agora_ms() - t->tecla_ms < 120) { return 0; }
  return confirmar(t, tecla_no_ponto(t->a, xs, ys, TECVIRT_NENHUMA));   // confirmar() marca tecla_ms
}
