#include "tab5_gif_nucleo.hpp"

#include <string.h>

#define LZW_MAX      4096   // entradas da tabela do LZW (codigos de ate 12 bits)
#define ATRASO_MIN   20     // ms
#define ATRASO_PADRAO 100   // ms, quando o arquivo diz 0

struct gif_nucleo {
  const uint8_t* dados;
  size_t         n;
  gif_alocar_t   alocar;
  gif_liberar_t  liberar;

  int       largura, altura;
  uint16_t* tela;              // largura x altura
  uint16_t  fundo;             // cor de fundo (da tabela global; preto sem ela)
  uint8_t   global[256 * 3];   // tabela de cor global (RGB)
  int       n_global;          // quantas cores
  size_t    inicio;            // onde comecam os blocos (depois da tabela global)
  size_t    pos;               // proximo bloco a ler
  bool      animado;
  int       vistos;
  bool      nesta_volta;       // ja decodificou algum quadro desta volta

  // O que o proximo quadro herda do Graphic Control Extension.
  int  gce_atraso;             // em ms, ja com minimo; ATRASO_PADRAO sem GCE
  int  gce_descarte;
  int  gce_transp;             // indice transparente ou -1

  // O descarte do quadro ANTERIOR, aplicado antes de desenhar o seguinte.
  int       ant_descarte;
  int       ant_x, ant_y, ant_l, ant_a;   // retangulo ja recortado na tela
  bool      ant_transp;
  uint16_t* copia;             // o retangulo antes de desenhar (descarte 3)
  size_t    copia_cap;         // em pixels

  // LZW, sem recursao: cada codigo e (prefixo, ultimo byte).
  uint16_t prefixo[LZW_MAX];
  uint8_t  sufixo[LZW_MAX];
  uint8_t  pilha[LZW_MAX + 1];
  uint16_t paleta[256];        // a tabela do quadro atual ja em RGB565
};

static inline uint16_t cor565(uint8_t r, uint8_t g, uint8_t b)
{
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline int u16(const uint8_t* p) { return p[0] | (p[1] << 8); }

// Pula os sub-blocos de dados que comecam em 'p' (tamanho, bytes, ... 0).
// Devolve a posicao depois do 0 final; n se o arquivo acabou antes.
static size_t pular_subblocos(const uint8_t* d, size_t n, size_t p)
{
  while (p < n) {
    const size_t t = d[p++];
    if (t == 0) { return p; }
    if (t > n - p) { return n; }
    p += t;
  }
  return n;
}

// Lida o cabecalho de um quadro (descritor de imagem em d[p], que e 0x2C):
// devolve a posicao do primeiro sub-bloco de dados LZW, ou 0 se estiver cortado.
// Preenche as informacoes pedidas (qualquer ponteiro pode ser nullptr).
typedef struct {
  int x, y, l, a;
  bool entrelacado;
  int  n_local;            // cores da tabela local (0 = nao tem)
  size_t tabela_local;     // onde ela comeca
  int  codigo_minimo;
} quadro_t;

static size_t ler_quadro(const uint8_t* d, size_t n, size_t p, quadro_t* q)
{
  if (n - p < 10) { return 0; }   // 0x2C + 9 bytes do descritor
  q->x = u16(d + p + 1);
  q->y = u16(d + p + 3);
  q->l = u16(d + p + 5);
  q->a = u16(d + p + 7);
  const uint8_t flags = d[p + 9];
  q->entrelacado = (flags & 0x40) != 0;
  q->n_local = (flags & 0x80) ? (2 << (flags & 7)) : 0;
  p += 10;
  q->tabela_local = p;
  const size_t bytes = (size_t)q->n_local * 3;
  if (bytes > n - p) { return 0; }
  p += bytes;
  if (p >= n) { return 0; }
  q->codigo_minimo = d[p++];
  return p;
}

// Quantos quadros ha no arquivo, contando so ate 2 (basta para "animado").
static int contar_quadros(const uint8_t* d, size_t n, size_t p)
{
  int quadros = 0;
  while (p < n && quadros < 2) {
    const uint8_t b = d[p];
    if (b == 0x21) {                           // extensao: rotulo + sub-blocos
      if (n - p < 2) { break; }
      p = pular_subblocos(d, n, p + 2);
    } else if (b == 0x2C) {                    // quadro
      quadro_t q;
      const size_t dados = ler_quadro(d, n, p, &q);
      if (!dados) { break; }
      ++quadros;
      p = pular_subblocos(d, n, dados);
    } else {                                   // 0x3B (fim) ou lixo
      break;
    }
  }
  return quadros;
}

gif_nucleo_t* gif_n_abrir(const uint8_t* dados, size_t n, gif_alocar_t alocar, gif_liberar_t liberar)
{
  if (n < 13 || (memcmp(dados, "GIF87a", 6) && memcmp(dados, "GIF89a", 6))) { return nullptr; }
  const int l = u16(dados + 6), a = u16(dados + 8);
  if (l < 1 || a < 1 || l > GIF_TELA_MAX || a > GIF_TELA_MAX) { return nullptr; }

  gif_nucleo_t* g = (gif_nucleo_t*)alocar(sizeof(gif_nucleo_t));
  if (!g) { return nullptr; }
  memset(g, 0, sizeof(*g));
  g->dados = dados;
  g->n = n;
  g->alocar = alocar;
  g->liberar = liberar;
  g->largura = l;
  g->altura = a;

  const uint8_t flags = dados[10];
  size_t p = 13;
  if (flags & 0x80) {                          // tabela de cor global
    g->n_global = 2 << (flags & 7);
    const size_t bytes = (size_t)g->n_global * 3;
    if (bytes > n - p) { gif_n_fechar(g); return nullptr; }
    memcpy(g->global, dados + p, bytes);
    p += bytes;
    const int bg = dados[11];
    if (bg < g->n_global) { g->fundo = cor565(g->global[bg * 3], g->global[bg * 3 + 1], g->global[bg * 3 + 2]); }
  }
  g->inicio = g->pos = p;
  const int quadros = contar_quadros(dados, n, p);
  if (quadros == 0) { gif_n_fechar(g); return nullptr; }
  g->animado = quadros > 1;

  g->tela = (uint16_t*)alocar((size_t)l * a * sizeof(uint16_t));
  if (!g->tela) { gif_n_fechar(g); return nullptr; }
  for (size_t i = 0; i < (size_t)l * a; ++i) { g->tela[i] = g->fundo; }
  g->gce_atraso = ATRASO_PADRAO;
  g->gce_transp = -1;
  return g;
}

void gif_n_fechar(gif_nucleo_t* g)
{
  if (!g) { return; }
  gif_liberar_t liberar = g->liberar;
  if (g->tela)  { liberar(g->tela); }
  if (g->copia) { liberar(g->copia); }
  liberar(g);
}

int  gif_n_largura(const gif_nucleo_t* g)        { return g->largura; }
int  gif_n_altura(const gif_nucleo_t* g)         { return g->altura; }
bool gif_n_animado(const gif_nucleo_t* g)        { return g->animado; }
int  gif_n_quadros_vistos(const gif_nucleo_t* g) { return g->vistos; }
const uint16_t* gif_n_tela(const gif_nucleo_t* g) { return g->tela; }

// ---------------------------------------------------------------------------
// DESCARTE DO QUADRO ANTERIOR
// ---------------------------------------------------------------------------
static void aplicar_descarte(gif_nucleo_t* g)
{
  if (g->ant_l <= 0 || g->ant_a <= 0) { return; }
  // 2 = volta ao fundo (preto se o quadro tinha cor transparente); 3 = restaura.
  const uint16_t cor = g->ant_transp ? 0 : g->fundo;
  for (int y = 0; y < g->ant_a; ++y) {
    uint16_t* linha = g->tela + (size_t)(g->ant_y + y) * g->largura + g->ant_x;
    if (g->ant_descarte == 2) {
      for (int x = 0; x < g->ant_l; ++x) { linha[x] = cor; }
    } else if (g->ant_descarte == 3 && g->copia) {
      memcpy(linha, g->copia + (size_t)y * g->ant_l, (size_t)g->ant_l * sizeof(uint16_t));
    }
  }
}

// ---------------------------------------------------------------------------
// LEITOR DE BITS DOS SUB-BLOCOS
// ---------------------------------------------------------------------------
typedef struct {
  const uint8_t* d;
  size_t   n;
  size_t   pos;        // proximo byte (ou o byte de tamanho do proximo sub-bloco)
  size_t   resta;      // bytes que faltam no sub-bloco atual
  uint32_t acumulado;
  int      bits;
  bool     fim;        // acabaram os dados (terminador ou fim do arquivo)
} bits_t;

static bool proximo_byte(bits_t* b, uint8_t* out)
{
  while (b->resta == 0) {
    if (b->fim || b->pos >= b->n) { b->fim = true; return false; }
    const size_t t = b->d[b->pos++];
    if (t == 0) { b->fim = true; return false; }
    b->resta = t > b->n - b->pos ? b->n - b->pos : t;   // sub-bloco cortado no fim do arquivo
  }
  *out = b->d[b->pos++];
  --b->resta;
  return true;
}

// Le 'tam' bits (LSB primeiro). false = sem mais dados.
static bool pegar_bits(bits_t* b, int tam, int* codigo)
{
  while (b->bits < tam) {
    uint8_t v;
    if (!proximo_byte(b, &v)) { return false; }
    b->acumulado |= (uint32_t)v << b->bits;
    b->bits += 8;
  }
  *codigo = (int)(b->acumulado & ((1u << tam) - 1));
  b->acumulado >>= tam;
  b->bits -= tam;
  return true;
}

// ---------------------------------------------------------------------------
// DECODIFICAR UM QUADRO
// ---------------------------------------------------------------------------
// Poe um pixel (indice de cor) na posicao corrente do quadro e anda para o
// proximo, seguindo a ordem entrelacada se for o caso. false = o quadro encheu.
typedef struct {
  const quadro_t* q;
  int  x, linha;       // posicao dentro do quadro (linha ja na ordem do arquivo)
  int  passo;          // entrelacado: 0..3
  bool cheio;
} cursor_t;

static const int ENT_INICIO[4] = { 0, 4, 2, 1 };
static const int ENT_SALTO[4]  = { 8, 8, 4, 2 };

static void avancar(cursor_t* c)
{
  const quadro_t* q = c->q;
  if (++c->x < q->l) { return; }
  c->x = 0;
  if (!q->entrelacado) {
    if (++c->linha >= q->a) { c->cheio = true; }
    return;
  }
  c->linha += ENT_SALTO[c->passo];
  while (c->linha >= q->a && c->passo < 3) { c->linha = ENT_INICIO[++c->passo]; }
  if (c->linha >= q->a) { c->cheio = true; }
}

static void decodificar_lzw(gif_nucleo_t* g, const quadro_t* q, size_t dados)
{
  const int minimo = q->codigo_minimo;
  if (minimo < 2 || minimo > 8 || q->l <= 0 || q->a <= 0) { return; }

  bits_t b = { g->dados, g->n, dados, 0, 0, 0, false };
  cursor_t c = { q, 0, 0, 0, false };
  const int limpar = 1 << minimo, fim_cod = limpar + 1;
  int proximo = fim_cod + 1, tam = minimo + 1;
  int anterior = -1, primeiro = 0;

  while (!c.cheio) {
    int codigo;
    if (!pegar_bits(&b, tam, &codigo)) { break; }
    if (codigo == limpar) { proximo = fim_cod + 1; tam = minimo + 1; anterior = -1; continue; }
    if (codigo == fim_cod) { break; }

    int sp = 0;
    int atual = codigo;
    if (anterior < 0) {                       // primeiro codigo depois do limpar
      if (codigo >= limpar) { break; }        // estragado
      primeiro = codigo;
    } else {
      if (codigo > proximo) { break; }        // estragado
      if (codigo == proximo) {                // ainda nao existe: anterior + 1o byte dele
        g->pilha[sp++] = (uint8_t)primeiro;
        atual = anterior;
      }
      while (atual >= limpar) {               // desmonta a cadeia, do fim para o comeco
        if (sp >= LZW_MAX) { sp = -1; break; }
        g->pilha[sp++] = g->sufixo[atual];
        atual = g->prefixo[atual];
      }
      if (sp < 0) { break; }
      primeiro = atual;
    }
    g->pilha[sp++] = (uint8_t)atual;
    if (anterior >= 0 && proximo < LZW_MAX) {   // aprende: anterior + 1o byte desta
      g->prefixo[proximo] = (uint16_t)anterior;
      g->sufixo[proximo]  = (uint8_t)primeiro;
      ++proximo;
      if (proximo == (1 << tam) && tam < 12) { ++tam; }
    }
    anterior = codigo;

    // A pilha sai de tras para frente (o ultimo byte entrou primeiro).
    while (sp > 0 && !c.cheio) {
      const uint8_t idx = g->pilha[--sp];
      const int gx = q->x + c.x, gy = q->y + c.linha;
      if (gx < g->largura && gy < g->altura && idx != g->gce_transp) {
        g->tela[(size_t)gy * g->largura + gx] = g->paleta[idx];
      }
      avancar(&c);
    }
  }
}

int gif_n_proximo(gif_nucleo_t* g)
{
  const uint8_t* d = g->dados;
  const size_t n = g->n;
  for (;;) {
    if (g->pos >= n || d[g->pos] == 0x3B) {   // fim: volta ao primeiro quadro
      if (!g->nesta_volta) { return -1; }     // uma volta inteira sem quadro: nada a mostrar
      g->pos = g->inicio;
      g->nesta_volta = false;
      g->ant_descarte = 0;
      g->ant_l = g->ant_a = 0;
      for (size_t i = 0; i < (size_t)g->largura * g->altura; ++i) { g->tela[i] = g->fundo; }
      continue;
    }
    const uint8_t b = d[g->pos];
    if (b == 0x21) {                          // extensao
      if (n - g->pos < 2) { g->pos = n; continue; }
      const uint8_t rotulo = d[g->pos + 1];
      if (rotulo == 0xF9 && n - g->pos >= 8 && d[g->pos + 2] == 4) {   // Graphic Control
        const uint8_t f = d[g->pos + 3];
        const int cs = u16(d + g->pos + 4);
        g->gce_descarte = (f >> 2) & 7;
        g->gce_atraso = cs == 0 ? ATRASO_PADRAO : (cs * 10 < ATRASO_MIN ? ATRASO_MIN : cs * 10);
        g->gce_transp = (f & 1) ? d[g->pos + 6] : -1;
      }
      g->pos = pular_subblocos(d, n, g->pos + 2);
      continue;
    }
    if (b != 0x2C) { g->pos = n; continue; }  // lixo: trata como fim

    // ---- um quadro ----
    quadro_t q;
    const size_t dados = ler_quadro(d, n, g->pos, &q);
    if (!dados) { g->pos = n; continue; }     // cortado

    aplicar_descarte(g);                       // o que o quadro anterior pediu

    // Tabela do quadro: a local se houver, senao a global; indice fora dela = preto.
    const uint8_t* tab = q.n_local ? d + q.tabela_local : g->global;
    const int ncores = q.n_local ? q.n_local : g->n_global;
    for (int i = 0; i < 256; ++i) {
      g->paleta[i] = i < ncores ? cor565(tab[i * 3], tab[i * 3 + 1], tab[i * 3 + 2]) : 0;
    }

    // Retangulo do quadro recortado na tela (para o descarte do proximo).
    int rx = q.x, ry = q.y, rl = q.l, ra = q.a;
    if (rx >= g->largura || ry >= g->altura) { rl = ra = 0; }
    else {
      if (rx + rl > g->largura) { rl = g->largura - rx; }
      if (ry + ra > g->altura)  { ra = g->altura - ry; }
    }
    if (g->gce_descarte == 3 && rl > 0 && ra > 0) {   // guarda o que vai ser coberto
      const size_t preciso = (size_t)rl * ra;
      if (preciso > g->copia_cap) {
        if (g->copia) { g->liberar(g->copia); }
        g->copia = (uint16_t*)g->alocar(preciso * sizeof(uint16_t));
        g->copia_cap = g->copia ? preciso : 0;
      }
      if (g->copia) {
        for (int y = 0; y < ra; ++y) {
          memcpy(g->copia + (size_t)y * rl, g->tela + (size_t)(ry + y) * g->largura + rx, (size_t)rl * sizeof(uint16_t));
        }
      }
    }

    decodificar_lzw(g, &q, dados);

    const int atraso = g->gce_atraso;
    g->ant_descarte = g->gce_descarte;
    g->ant_x = rx; g->ant_y = ry; g->ant_l = rl; g->ant_a = ra;
    g->ant_transp = g->gce_transp >= 0;
    g->gce_atraso = ATRASO_PADRAO;             // o GCE vale so para o quadro que vem logo depois
    g->gce_descarte = 0;
    g->gce_transp = -1;
    g->pos = pular_subblocos(d, n, dados);
    g->nesta_volta = true;
    ++g->vistos;
    return atraso;
  }
}
