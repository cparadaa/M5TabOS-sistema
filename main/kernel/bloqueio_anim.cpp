// Animacoes da tela de bloqueio. Contrato e regras no bloqueio_anim.hpp.
//
// Cada animacao tem tres funcoes (e uma quarta, opcional):
//   iniciar()  - sorteia o estado inicial;
//   passo(dt)  - move tudo e registra com rec() a caixa que cada objeto
//                ocupava e a que vai ocupar (essas caixas serao apagadas);
//   desenhar() - pinta todos os objetos, depois da apagada;
//   fundo()    - (opcional) pinta o fundo fixo dela; sem isso, o fundo e um
//                desenho abstrato sorteado.
// O quadro (bq_anim_quadro) liga as tres e guarda a lista de caixas para o
// shell saber onde o texto precisa ser repintado.

#include "bloqueio_anim_interno.hpp"

#include "esp_random.h"
#include "esp_timer.h"

using namespace bq_anim;

namespace bq_anim {

// ---------------------------------------------------------------------------
// Area, aleatorio, cores
// ---------------------------------------------------------------------------
int s_ax, s_ay, s_W, s_H;   // area util (onde a animacao pode tocar)

uint32_t s_rng = 2463534242u;

alignas(16) uint8_t bq_estado[BQ_ESTADO_MAX];   // estado da animacao sorteada

// Matiz 0..255 (vermelho, amarelo, verde, ciano, azul, magenta) com brilho v.
uint16_t matiz(uint8_t h, int v)
{
  const int reg = h / 43;
  const int sub = (h - reg * 43) * 6;   // 0..252
  int r, g, b;
  switch (reg) {
    case 0:  r = 255;       g = sub;       b = 0;         break;
    case 1:  r = 255 - sub; g = 255;       b = 0;         break;
    case 2:  r = 0;         g = 255;       b = sub;       break;
    case 3:  r = 0;         g = 255 - sub; b = 255;       break;
    case 4:  r = sub;       g = 0;         b = 255;       break;
    default: r = 255;       g = 0;         b = 255 - sub; break;
  }
  return c565(r * v / 255, g * v / 255, b * v / 255);
}

// ---------------------------------------------------------------------------
// Lista de caixas mexidas neste quadro
// ---------------------------------------------------------------------------
// Caixas que se SOBREPOEM viram uma so (apagar o mesmo pixel duas vezes e
// desperdicio, e a chuva, por exemplo, gera duas por coluna quase iguais). O
// resto vai como esta: o compositor aguenta muitas areas pequenas por quadro
// (compositor.cpp, MAX_AREAS). MAX_RET e so o tamanho da lista; encher e
// raro, e ai a nova se junta ao par que desperdica menos.
static const int MAX_RET = 160;
static bq_ret_t s_ret[MAX_RET];
static int      s_n_ret = 0;
bool            s_tudo  = false;   // desenhar tudo (nao so o que mudou)

static inline int area_de(const bq_ret_t& r) { return r.w * r.h; }

static bq_ret_t envolver(const bq_ret_t& a, const bq_ret_t& b)
{
  const int x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
  const int x1 = a.x + a.w > b.x + b.w ? a.x + a.w : b.x + b.w;
  const int y1 = a.y + a.h > b.y + b.h ? a.y + a.h : b.y + b.h;
  return { (int16_t)x0, (int16_t)y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0) };
}

// Pixel NAO pedido que a juncao de a e b obriga a apagar (negativo = elas se
// sobrepoem: juntar sai de graca).
static inline int desperdicio(const bq_ret_t& a, const bq_ret_t& b)
{
  return area_de(envolver(a, b)) - area_de(a) - area_de(b);
}

void rec(int x, int y, int w, int h)
{
  int x1 = x + w, y1 = y + h;
  if (x < s_ax)           { x = s_ax; }
  if (y < s_ay)           { y = s_ay; }
  if (x1 > s_ax + s_W)    { x1 = s_ax + s_W; }
  if (y1 > s_ay + s_H)    { y1 = s_ay + s_H; }
  if (x1 <= x || y1 <= y) { return; }
  bq_ret_t r = { (int16_t)x, (int16_t)y, (int16_t)(x1 - x), (int16_t)(y1 - y) };

  // Junta com quem ja se sobrepoe (de graca) - uma juncao pode abrir outra.
  for (bool juntou = true; juntou; ) {
    juntou = false;
    for (int i = 0; i < s_n_ret; ++i) {
      if (desperdicio(s_ret[i], r) <= 0) {
        r = envolver(s_ret[i], r);
        s_ret[i] = s_ret[--s_n_ret];
        juntou = true;
        break;
      }
    }
  }
  if (s_n_ret < MAX_RET) { s_ret[s_n_ret++] = r; return; }

  // Cheia: junta o par (a nova conta como candidata) que desperdica menos.
  int bi = 0, bj = -1, melhor = 0x7fffffff;
  for (int i = 0; i < s_n_ret; ++i) {
    const int dr = desperdicio(s_ret[i], r);
    if (dr < melhor) { melhor = dr; bi = i; bj = -1; }
    for (int j = i + 1; j < s_n_ret; ++j) {
      const int d = desperdicio(s_ret[i], s_ret[j]);
      if (d < melhor) { melhor = d; bi = i; bj = j; }
    }
  }
  if (bj < 0) {
    s_ret[bi] = envolver(s_ret[bi], r);
  } else {
    s_ret[bi] = envolver(s_ret[bi], s_ret[bj]);
    s_ret[bj] = r;
  }
  // Uma caixa juntada apaga objeto que nao se mexeu: quem desenha so o que
  // mudou (a cobrinha) precisa desenhar tudo.
  s_tudo = true;
}

// Caixa que cobre dois pontos, com folga 'r' em volta.
void rec2(float x0, float y0, float x1, float y1, int r)
{
  const int xa = (int)floorf(fminf(x0, x1)) - r;
  const int ya = (int)floorf(fminf(y0, y1)) - r;
  rec(xa, ya, (int)ceilf(fabsf(x1 - x0)) + 2 * r + 1, (int)ceilf(fabsf(y1 - y0)) + 2 * r + 1);
}

// ---------------------------------------------------------------------------
// FUNDO
// ---------------------------------------------------------------------------
// Um desenho fixo atras da animacao: linhas finas e escuras sobre preto. Ele
// e pintado UMA vez por bloqueio numa imagem do tamanho da area (PSRAM,
// ~1,6 MB, so enquanto bloqueado) e o "apagar" de cada caixa passa a ser
// copiar aquele pedaco da imagem em vez de pintar preto - o mesmo tanto de
// escrita de antes, entao o fundo nao custa nada por quadro. Sem memoria,
// fica o preto de sempre.
static lgfx::LGFX_Sprite* s_fundo = nullptr;
static bool s_fundo_falhou = false;   // neste bloqueio: nao tenta de novo a cada quadro
static bool s_fundo_pintar = false;   // a imagem e nova: vai inteira para a tela

static void fundo_soltar(void)
{
  delete s_fundo;
  s_fundo = nullptr;
}

// Ponto na borda da imagem, 't' de 0 a 1 dando a volta.
static void ponto_borda(int W, int H, float t, float* x, float* y)
{
  t -= floorf(t);
  const float p = 2.0f * (W + H), d = t * p;
  if (d < W)              { *x = d;                  *y = 0; }
  else if (d < W + H)     { *x = (float)W;           *y = d - W; }
  else if (d < 2 * W + H) { *x = (float)W - (d - W - H); *y = (float)H; }
  else                    { *x = 0;                  *y = (float)H - (d - 2 * W - H); }
}

// FIOS: "string art" - retas entre dois trilhos desenham curvas que nao
// existem.
static void fundo_fios(lgfx::LGFX_Sprite& s, int W, int H)
{
  const int env = 3 + irand(3);
  const uint8_t h0 = (uint8_t)irand(256);
  for (int e = 0; e < env; ++e) {
    float a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y;
    const float ta = frand(0, 1), tb = frand(0, 1);
    ponto_borda(W, H, ta, &a0x, &a0y);
    ponto_borda(W, H, ta + frand(0.15f, 0.3f), &a1x, &a1y);
    ponto_borda(W, H, tb, &b0x, &b0y);
    ponto_borda(W, H, tb + frand(0.15f, 0.3f), &b1x, &b1y);
    const int n = 44;
    const uint8_t h = (uint8_t)(h0 + e * 30);
    for (int i = 0; i <= n; ++i) {
      const float f = (float)i / n;
      const int v = 34 + (int)(40 * sinf(f * 3.1416f));
      s.drawLine((int)(a0x + (a1x - a0x) * f), (int)(a0y + (a1y - a0y) * f),
                 (int)(b1x + (b0x - b1x) * f), (int)(b1y + (b0y - b1y) * f), matiz((uint8_t)(h + i), v));
    }
  }
}

// ONDAS: curvas de nivel que escorrem de um lado ao outro.
static void fundo_ondas(lgfx::LGFX_Sprite& s, int W, int H)
{
  const int n = 30;
  const float k1 = frand(0.004f, 0.009f), k2 = frand(0.011f, 0.02f);
  const float p1 = frand(0, 6.28f), p2 = frand(0, 6.28f);
  const float a1 = frand(24, 50), a2 = frand(8, 20);
  const uint8_t h0 = (uint8_t)(120 + irand(80));
  for (int i = 0; i < n; ++i) {
    const float y0 = (i + 0.5f) * H / n;
    const uint16_t cor = matiz((uint8_t)(h0 + i * 2), 40 + (i % 3) * 12);
    int px = 0, py = 0;
    for (int x = 0; x <= W + 12; x += 12) {
      const int y = (int)(y0 + a1 * sinf(x * k1 + i * 0.28f + p1) + a2 * sinf(x * k2 - i * 0.17f + p2));
      if (x > 0) { s.drawLine(px, py, x, y, cor); }
      px = x; py = y;
    }
  }
}

// TEIA: pontos ligados aos vizinhos mais proximos.
static void fundo_teia(lgfx::LGFX_Sprite& s, int W, int H)
{
  const int n = 38;
  int16_t px[n], py[n];
  for (int i = 0; i < n; ++i) { px[i] = (int16_t)irand(W); py[i] = (int16_t)irand(H); }
  const uint8_t h = (uint8_t)irand(256);
  for (int i = 0; i < n; ++i) {
    int viz[3] = { -1, -1, -1 };
    int dist[3] = { 0x7fffffff, 0x7fffffff, 0x7fffffff };
    for (int j = 0; j < n; ++j) {
      if (j == i) { continue; }
      const int dx = px[j] - px[i], dy = py[j] - py[i], d = dx * dx + dy * dy;
      for (int k = 0; k < 3; ++k) {
        if (d < dist[k]) {
          for (int m = 2; m > k; --m) { dist[m] = dist[m - 1]; viz[m] = viz[m - 1]; }
          dist[k] = d; viz[k] = j;
          break;
        }
      }
    }
    for (int k = 0; k < 3; ++k) {
      if (viz[k] >= 0) { s.drawLine(px[i], py[i], px[viz[k]], py[viz[k]], matiz((uint8_t)(h + k * 12), 44)); }
    }
  }
  for (int i = 0; i < n; ++i) { s.fillCircle(px[i], py[i], 2, matiz(h, 110)); }
}

// RAIOS: leque saindo de um ponto fora da tela, com aneis em volta (moire).
static void fundo_raios(lgfx::LGFX_Sprite& s, int W, int H)
{
  float fx, fy;
  ponto_borda(W, H, frand(0, 1), &fx, &fy);
  fx += (fx < W / 2 ? -60.0f : 60.0f);
  fy += (fy < H / 2 ? -60.0f : 60.0f);
  const float R = sqrtf((float)W * W + (float)H * H) + 80;
  const uint8_t h = (uint8_t)irand(256);
  const int n = 72;
  for (int i = 0; i < n; ++i) {
    const float ang = 6.2832f * i / n;
    s.drawLine((int)fx, (int)fy, (int)(fx + cosf(ang) * R), (int)(fy + sinf(ang) * R),
               matiz((uint8_t)(h + (i & 1) * 20), (i & 1) ? 30 : 48));
  }
  for (int r = 50; r < R; r += 46) { s.drawCircle((int)fx, (int)fy, r, matiz((uint8_t)(h + 128), 34)); }
}

// CIRCULOS: aneis de dois ou tres centros se cruzando (interferencia).
static void fundo_circulos(lgfx::LGFX_Sprite& s, int W, int H)
{
  const int n = 2 + irand(2);
  const uint8_t h = (uint8_t)irand(256);
  const int R = (int)sqrtf((float)W * W + (float)H * H);
  for (int c = 0; c < n; ++c) {
    const int cx = irand(W), cy = irand(H);
    const uint16_t cor = matiz((uint8_t)(h + c * 70), 40);
    for (int r = 14; r < R; r += 22) { s.drawCircle(cx, cy, r, cor); }
  }
}

// Estrelas fixas, bem fracas (fundo do sistema solar).
void fundo_ceu(lgfx::LGFX_Sprite& s, int W, int H)
{
  for (int i = 0; i < 160; ++i) {
    const int v = 40 + irand(90);
    s.drawPixel(irand(W), irand(H), c565(v, v, v + 20));
  }
}

// Sobe a imagem inteira para a tela, em faixas (um pushSprite por faixa: a
// mesma regra de nao despejar 1,6 MB de uma vez - ver tab5_tela_limpar).
static void fundo_pintar_tudo(lgfx::LovyanGFX& d)
{
  const int FAIXA = 128;
  for (int y = 0; y < s_H; y += FAIXA) {
    const int h = (y + FAIXA <= s_H) ? FAIXA : s_H - y;
    tela_virtual_cobrir(s_ax, s_ay + y, s_W, h);
    d.setClipRect(s_ax, s_ay + y, s_W, h);
    s_fundo->pushSprite(&d, s_ax, s_ay);
  }
  d.clearClipRect();
}

// Apaga uma caixa: devolve o fundo dela (ou preto, sem fundo).
static void apagar(lgfx::LovyanGFX& d, const bq_ret_t& r)
{
  if (!s_fundo) {
    d.fillRect(r.x, r.y, r.w, r.h, TFT_BLACK);
    return;
  }
  // Cobrir antes: a caixa inteira vai ser escrita, entao o compositor nao
  // precisa trazer o quadro anterior dela da outra pagina.
  tela_virtual_cobrir(r.x, r.y, r.w, r.h);
  d.setClipRect(r.x, r.y, r.w, r.h);
  s_fundo->pushSprite(&d, s_ax, s_ay);
}

}  // namespace bq_anim

// ===========================================================================
// O QUADRO
// ===========================================================================
typedef struct {
  void (*iniciar)(void);
  void (*passo)(float dt);
  void (*desenhar)(lgfx::LovyanGFX& d);
  void (*fundo)(lgfx::LGFX_Sprite& s, int W, int H);   // nullptr = abstrato sorteado
} anim_t;

static const anim_t ANIMS[] = {
  { bolas_iniciar,    bolas_passo,    bolas_desenhar,    nullptr     },
  { fogos_iniciar,    fogos_passo,    fogos_desenhar,    nullptr     },
  { chuva_iniciar,    chuva_passo,    chuva_desenhar,    nullptr     },
  { estrelas_iniciar, estrelas_passo, estrelas_desenhar, nullptr     },
  { bolhas_iniciar,   bolhas_passo,   bolhas_desenhar,   nullptr     },
  { cobra_iniciar,    cobra_passo,    cobra_desenhar,    nullptr     },
  { lumes_iniciar,    lumes_passo,    lumes_desenhar,    nullptr     },
  { pendulos_iniciar, pendulos_passo, pendulos_desenhar, nullptr     },
  { neve_iniciar,     neve_passo,     neve_desenhar,     nullptr     },
  { cometas_iniciar,  cometas_passo,  cometas_desenhar,  nullptr     },
  { solar_iniciar,    solar_passo,    solar_desenhar,    solar_fundo },
  { logo_iniciar,     logo_passo,     logo_desenhar,     nullptr     },
};
static const int N_ANIMS = (int)(sizeof(ANIMS) / sizeof(ANIMS[0]));

static void (* const FUNDOS[])(lgfx::LGFX_Sprite&, int, int) = {
  fundo_fios, fundo_ondas, fundo_teia, fundo_raios, fundo_circulos,
};

static const int N_FUNDOS = (int)(sizeof(FUNDOS) / sizeof(FUNDOS[0]));

static int  s_atual = 0;
static int  s_fundo_atual = 0;
static bool s_pronta = false;   // a sorteada ja foi iniciada

// O dt vem do ritmo de quadro do sistema (kernel/ritmo.hpp): o tempo entre as
// varreduras em que dois quadros vao ao ar. Limitado aqui: depois de uma
// pausa (despertar), a animacao continua de onde estava em vez de saltar.
static float passo_dt(void)
{
  float dt = sistema_quadro_dt();
  if (dt < 0.005f) { dt = 0.005f; }
  if (dt > 0.10f)  { dt = 0.10f; }
  return dt;
}

void bq_anim_sortear(void)
{
  s_rng ^= esp_random();
  if (s_rng == 0) { s_rng = 2463534242u; }
  int nova = irand(N_ANIMS - 1);
  if (nova >= s_atual) { ++nova; }   // nunca a mesma duas vezes seguidas
  int fundo = irand(N_FUNDOS - 1);
  if (fundo >= s_fundo_atual) { ++fundo; }
  s_atual       = nova;
  s_fundo_atual = fundo;
  s_pronta      = false;
  s_fundo_falhou = false;
  fundo_soltar();   // desenho novo a cada bloqueio
  if (ANIMS[s_atual].iniciar != logo_iniciar) { logo_soltar(); }
}

void bq_anim_soltar(void)
{
  fundo_soltar();
  logo_soltar();
  s_pronta = false;   // o logo (se for ele) precisa da imagem de volta
}

// Cria e pinta a imagem do fundo. false = sem memoria (fica o preto).
static bool fundo_criar(void)
{
  auto* sp = new lgfx::LGFX_Sprite();
  if (!sp) { return false; }
  // O MESMO formato das paginas do painel: a copia vira memcpy puro.
  sp->setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
  sp->setPsram(true);
  if (!sp->createSprite(s_W, s_H)) { delete sp; return false; }
  sp->fillScreen(TFT_BLACK);
  if (ANIMS[s_atual].fundo) { ANIMS[s_atual].fundo(*sp, s_W, s_H); }
  else                      { FUNDOS[s_fundo_atual](*sp, s_W, s_H); }
  s_fundo = sp;
  return true;
}

void bq_anim_iniciar(const area_t& a)
{
  const bool mudou_area = a.x != s_ax || a.y != s_ay || a.largura != s_W || a.altura != s_H;
  s_ax = a.x; s_ay = a.y; s_W = a.largura; s_H = a.altura;
  s_n_ret  = 0;
  if (!s_pronta || mudou_area) {
    ANIMS[s_atual].iniciar();
    s_pronta = true;
    fundo_soltar();
  }
  if (!s_fundo && !s_fundo_falhou) { s_fundo_falhou = !fundo_criar(); }
  s_fundo_pintar = s_fundo != nullptr;
  s_tudo = true;   // a tela foi limpa: tudo precisa ser pintado
}

void bq_anim_quadro(lgfx::LovyanGFX& d, const bq_ret_t* extras, int n_extras,
                    const bq_ret_t* caixas, int n_caixas, bq_ret_t* clips)
{
  const float dt = passo_dt();

  s_n_ret = 0;
  for (int i = 0; i < n_extras; ++i) { rec(extras[i].x, extras[i].y, extras[i].w, extras[i].h); }
  if (n_extras > 0) { s_tudo = true; }

  ANIMS[s_atual].passo(dt);

  // ONDE O TEXTO PRECISA SER REPINTADO: para cada caixa de texto, a caixa que
  // cobre tudo que a animacao mexeu dentro dela. Essa caixa INTEIRA entra na
  // lista de apagar. O texto e suavizado (borda translucida): pintado duas
  // vezes no mesmo pixel ele engrossa a cada quadro. Apagando a caixa toda
  // antes, cada pixel recebe o texto uma unica vez.
  //
  // Em passadas: a juncao das caixas (rec) pode crescer uma delas por cima de
  // outro texto. Cada passada refaz o clip de cada texto com TUDO que vai ser
  // apagado dentro dele, e o proprio clip entra na lista - ate parar de mudar.
  for (int j = 0; j < n_caixas; ++j) { clips[j] = { 0, 0, 0, 0 }; }
  for (int passada = 0; passada < 4; ++passada) {
    bool mudou = false;
    for (int j = 0; j < n_caixas; ++j) {
      const bq_ret_t& cx = caixas[j];
      bq_ret_t env = { 0, 0, 0, 0 };
      for (int i = 0; i < s_n_ret; ++i) {
        const bq_ret_t& r = s_ret[i];
        const int ix0 = r.x > cx.x ? r.x : cx.x, iy0 = r.y > cx.y ? r.y : cx.y;
        const int ix1 = r.x + r.w < cx.x + cx.w ? r.x + r.w : cx.x + cx.w;
        const int iy1 = r.y + r.h < cx.y + cx.h ? r.y + r.h : cx.y + cx.h;
        if (ix1 <= ix0 || iy1 <= iy0) { continue; }
        const bq_ret_t c = { (int16_t)ix0, (int16_t)iy0, (int16_t)(ix1 - ix0), (int16_t)(iy1 - iy0) };
        env = env.w > 0 ? envolver(env, c) : c;
      }
      if (env.w <= 0 || memcmp(&env, &clips[j], sizeof(env)) == 0) { continue; }
      clips[j] = env;
      rec(env.x, env.y, env.w, env.h);
      s_tudo = true;   // o que estava nessa caixa foi apagado: tudo se redesenha
      mudou = true;
    }
    if (!mudou) { break; }
  }

  if (s_fundo_pintar) {
    fundo_pintar_tudo(d);
    s_fundo_pintar = false;
  }
  for (int i = 0; i < s_n_ret; ++i) { apagar(d, s_ret[i]); }
  d.setClipRect(s_ax, s_ay, s_W, s_H);   // nunca encosta nas barras
  ANIMS[s_atual].desenhar(d);
  d.clearClipRect();
  s_tudo = false;
}

// ---------------------------------------------------------------------------
// O MESMO DESENHO COMO PAPEL DE PAREDE (home). Declarada no .hpp. A imagem
// so existe durante a chamada: pintada, copiada para a tela e devolvida - a
// home so e desenhada inteira (abrir, trocar de pagina, voltar de um app),
// entao nao ha por que segurar 1,6 MB o tempo todo. A mesma semente faz o
// desenho sair igual toda vez.
// O PAPEL DE PAREDE GUARDADO. Gerar o desenho procedural num sprite do
// tamanho da area (~1,6 MB) a cada passada completa da home - todo fechar de
// app, todo despertar - era o mesmo trabalho, com o mesmo resultado (mesma
// semente), repetido na transicao mais pesada do sistema. Agora o sprite
// pronto fica enquanto estilo/semente/area forem os mesmos - TAMBEM com um
// app aberto e no repouso (06/11/2026: o kernel o soltava nessas duas horas, e
// a volta para a home refazia o desenho inteiro: a passada completa da home
// media 145 ms contra 17-38 ms dos apps). Sao ~1,6 MB de PSRAM, com 30 MB
// livres; a PSRAM guarda o conteudo no sono leve.
static lgfx::LGFX_Sprite* s_parede = nullptr;
static int      s_parede_estilo = -1;
static uint32_t s_parede_semente = 0;
static int      s_parede_l = 0, s_parede_a = 0;

void bq_parede_soltar(void)
{
  delete s_parede;
  s_parede = nullptr;
  s_parede_estilo = -1;
}

bool bq_fundo_parede(lgfx::LovyanGFX& d, const area_t& a, int estilo, uint32_t semente)
{
  if (estilo < 0 || estilo >= N_FUNDOS) { return false; }
  if (a.largura <= 0 || a.altura <= 0) { return false; }
  const bool mesma = s_parede && s_parede_estilo == estilo && s_parede_semente == semente
                  && s_parede_l == a.largura && s_parede_a == a.altura;
  if (!mesma) {
    bq_parede_soltar();
    auto* sp = new lgfx::LGFX_Sprite();
    if (!sp) { return false; }
    sp->setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
    sp->setPsram(true);
    if (!sp->createSprite(a.largura, a.altura)) { delete sp; return false; }
    sp->fillScreen(TFT_BLACK);
    // A semente e do papel de parede, nao da animacao: o gerador comum e
    // emprestado e devolvido como estava.
    const uint32_t rng = s_rng;
    s_rng = semente ? semente : 2463534242u;
    FUNDOS[estilo](*sp, a.largura, a.altura);
    s_rng = rng;
    s_parede = sp;
    s_parede_estilo = estilo; s_parede_semente = semente;
    s_parede_l = a.largura; s_parede_a = a.altura;
  }

  // EM FAIXAS, como toda escrita grande (ver tab5_tela_limpar): a imagem
  // inteira de uma vez seca o FIFO do painel. E coberta: o compositor nao
  // precisa trazer da pagina da frente o que vai ser sobrescrito.
  const int FAIXA = 128;
  for (int y = 0; y < a.altura; y += FAIXA) {
    const int h = (y + FAIXA <= a.altura) ? FAIXA : a.altura - y;
    tela_virtual_cobrir(a.x, a.y + y, a.largura, h);
    d.setClipRect(a.x, a.y + y, a.largura, h);
    s_parede->pushSprite(&d, a.x, a.y);
  }
  d.clearClipRect();
  return true;
}
