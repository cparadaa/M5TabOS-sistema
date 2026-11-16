// ABERTURA DO SISTEMA - ver o cabecalho (boot_anim.hpp) para o que e e quando
// aparece. Aqui so o desenho.
//
// COMO DESENHA: um quadro inteiro por volta, na tela virtual (o objeto de
// desenho do sistema), apresentado pelo compositor - o mesmo caminho da home
// e dos apps. Assim nunca se desenha na pagina que o painel esta varrendo e
// a animacao nao pisca nem rasga.
//
// MEDIDAS: a maquete foi feita numa tela de 240 px de largura; tudo aqui e
// escrito nas medidas dela e multiplicado por K (720 / 240 = 3 no Tab5 em
// pe). Mexer num numero daqui e mexer no mesmo numero da maquete.
//
// TEMPO: a linha do tempo 't' vai de 0 a 1 em DURACAO_MS; cada parte da
// animacao comeca e termina numa fracao fixa dela. Os pontos de fundo andam
// pelo relogio de verdade (px por segundo), e nao por quadro - se o Tab5
// desenhar mais devagar, a teia anda igual, so com menos quadros.

#include "boot_anim.hpp"

#include <M5Unified.h>
#include <math.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tab5_tema.hpp"
#include "tab5_ui.hpp"
#include "tela_virtual.hpp"
#include "compositor.hpp"
#include "tab5_quadro.hpp"
#include "idioma.h"

static const char* TAG = "boot_anim";

namespace {

// ---------------------------------------------------------------------------
// AJUSTES DA ANIMACAO
// ---------------------------------------------------------------------------
constexpr int   DURACAO_MS = 1600;   // da tela vazia ao "[ conectado ]" inteiro
constexpr int   ESPERA_MS  = 300;    // o nome pronto fica um instante na tela
constexpr int   N_FUNDO    = 60;     // pontos de fundo (escolha do usuario na maquete)
constexpr float ALCANCE    = 71.0f;  // distancia maxima de uma ligacao (px da maquete)
constexpr float MAQ_LARG   = 240.0f; // largura da tela da maquete
constexpr float VEL_MAX    = 13.5f;  // velocidade maxima de um ponto (px da maquete por s)
constexpr int   ALT_FAIXA  = 32;     // linhas de tela por faixa da teia (ver desenhar_teia)

// Os sete nos principais: o desenho da malha, em px da maquete, a partir do
// centro do grupo. Ficam FIXOS; os pontos de fundo e que passeiam.
constexpr int   N_NOS = 7;
const float NOS_MAQ[N_NOS][2] = {
  {-55, -60}, {0, -85}, {55, -55}, {62, 10}, {20, 40}, {-35, 30}, {-65, -5},
};
constexpr float ESCALA_NOS = 1.45f;  // o grupo de nos e 45% maior que o desenho base

const char NOME[] = "M5tabOS";
constexpr int N_LETRAS = sizeof(NOME) - 1;
// Os caracteres "embaralhados" da decifracao e dos pacotes que voam.
const char SIMBOLOS[] = "01#$%&*+<>/=?ABCDEFXZ";

struct ponto_t { float x, y, vx, vy; };
struct no_t    { float x, y; };

// ---------------------------------------------------------------------------
// CONTAS PEQUENAS
// ---------------------------------------------------------------------------
float limitar(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

// Comeca rapido e chega devagar (curva cubica) - todo movimento da animacao.
float suave(float t)
{
  const float u = 1.0f - limitar(t);
  return 1.0f - u * u * u;
}

// Numero sorteado entre 'lo' e 'hi'.
float sorteio(float lo, float hi)
{
  return lo + (hi - lo) * (float)(esp_random() % 10000) / 10000.0f;
}

char simbolo_sorteado(void)
{
  return SIMBOLOS[esp_random() % (sizeof(SIMBOLOS) - 1)];
}

// A cor 'cor' com opacidade 'a' (0 = so o fundo, 1 = so a cor) por cima de
// 'fundo'. O M5GFX nao tem linha transparente; como a animacao sempre
// desenha sobre o fundo liso, a mistura e calculada antes e a linha sai
// opaca - o mesmo resultado na tela.
uint16_t misturar(uint16_t cor, uint16_t fundo, float a)
{
  a = limitar(a);
  const int r1 = (cor >> 11) & 31, g1 = (cor >> 5) & 63, b1 = cor & 31;
  const int r0 = (fundo >> 11) & 31, g0 = (fundo >> 5) & 63, b0 = fundo & 31;
  const int r = r0 + (int)lroundf((r1 - r0) * a);
  const int g = g0 + (int)lroundf((g1 - g0) * a);
  const int b = b0 + (int)lroundf((b1 - b0) * a);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

float distancia(float x0, float y0, float x1, float y1)
{
  return sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
}

// ---------------------------------------------------------------------------
// O ESTADO DA ANIMACAO (so existe enquanto ela roda)
// ---------------------------------------------------------------------------
// Uma linha da teia, ja com a cor final e a faixa de altura que ocupa.
struct linha_t {
  float    x0, y0, x1, y1;
  int16_t  y_min, y_max;
  uint16_t cor;
};
// O maximo possivel: todo par de pontos de fundo, cada no com cada ponto
// (2 linhas = 2 px de espessura) e todo par de nos.
constexpr int MAX_LINHAS = N_FUNDO * (N_FUNDO - 1) / 2 + N_NOS * N_FUNDO * 2 + N_NOS * (N_NOS - 1) / 2;

struct anim_t {
  float    k;           // px do Tab5 por px da maquete
  float    cx;          // centro horizontal
  float    y_nome;      // linha de base do nome (como na maquete)
  int      larg, alt;
  no_t     nos[N_NOS];
  ponto_t  pontos[N_FUNDO];
  linha_t  linhas[MAX_LINHAS];   // as linhas do quadro atual (montar_teia)
  int      n_linhas;
  uint16_t cor_ponto;
};

void preparar(anim_t& a, int larg, int alt)
{
  a.larg = larg;
  a.alt = alt;
  a.k = larg / MAQ_LARG;
  a.cx = larg / 2.0f;
  a.y_nome = 220.0f * a.k;
  for (int i = 0; i < N_NOS; i++) {
    a.nos[i].x = a.cx + NOS_MAQ[i][0] * ESCALA_NOS * a.k;
    a.nos[i].y = a.y_nome - 26.0f * a.k + NOS_MAQ[i][1] * ESCALA_NOS * a.k;
  }
  // Cada boot sorteia outra teia.
  const float v = VEL_MAX * a.k;
  for (int i = 0; i < N_FUNDO; i++) {
    a.pontos[i] = { sorteio(0, (float)larg), sorteio(0, (float)alt), sorteio(-v, v), sorteio(-v, v) };
  }
}

// Anda os pontos de fundo 'dt' segundos, quicando nas bordas da tela.
void mover_pontos(anim_t& a, float dt)
{
  for (int i = 0; i < N_FUNDO; i++) {
    ponto_t& p = a.pontos[i];
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.x < 0 || p.x > a.larg) { p.vx = -p.vx; p.x = p.x < 0 ? 0 : (float)a.larg; }
    if (p.y < 0 || p.y > a.alt)  { p.vy = -p.vy; p.y = p.y < 0 ? 0 : (float)a.alt; }
  }
}

// ---------------------------------------------------------------------------
// AS CAMADAS DO QUADRO, de tras para a frente
// ---------------------------------------------------------------------------

// A TEIA - EM DUAS ETAPAS, POR CAUSA DA MEMORIA.
//
// A tela mora na PSRAM, a mesma memoria que o painel le sem parar (~118 MB/s).
// Uma linha inclinada escrita direto la toca um pixel aqui, outro ali: cada
// pixel puxa e devolve um bloco de 128 B. Com ~60 mil pixels por quadro, isso
// virava centenas de MB/s de trafego espalhado - num boot a frio, com o
// cartao montando junto, o painel ficou sem dados por um quadro (UNDERRUN e
// QUADRO ATRASADO no log, com o nucleo em linha_direta): a PISCADA AZUL.
//
// Por isso: (1) montar_teia() so faz a conta de quais linhas existem e de que
// cor; (2) desenhar_teia() pinta a tela em FAIXAS de poucas linhas numa
// memoria INTERNA (rapida e fora do caminho do painel) e cola cada faixa
// pronta na pagina de tras de uma vez, em sequencia - a PSRAM so recebe
// escrita em fila, como numa copia comum.

// Monta a lista de linhas do quadro. 'g' acende tudo junto no comeco (0..1);
// 's' sao os segundos desde o inicio (o pisca das ligacoes entre os nos).
void montar_teia(anim_t& a, float g, float s)
{
  a.n_linhas = 0;
  auto juntar = [&a](float x0, float y0, float x1, float y1, uint16_t cor) {
    if (cor == TEMA_FUNDO || a.n_linhas >= MAX_LINHAS) { return; }   // apagada demais para aparecer
    linha_t& l = a.linhas[a.n_linhas++];
    l = { x0, y0, x1, y1, (int16_t)floorf(fminf(y0, y1)), (int16_t)ceilf(fmaxf(y0, y1)), cor };
  };

  // Pontos de fundo entre si: linhas finas e bem apagadas, mais fortes quanto
  // mais perto.
  const float alc = ALCANCE * a.k;
  for (int i = 0; i < N_FUNDO; i++) {
    for (int j = i + 1; j < N_FUNDO; j++) {
      const ponto_t& p = a.pontos[i];
      const ponto_t& q = a.pontos[j];
      const float dx = p.x - q.x, dy = p.y - q.y;
      if (fabsf(dx) >= alc || fabsf(dy) >= alc) { continue; }   // longe: nem calcula a raiz
      const float dist = sqrtf(dx * dx + dy * dy);
      if (dist >= alc) { continue; }
      juntar(p.x, p.y, q.x, q.y, misturar(TEMA_DESTAQUE, TEMA_FUNDO, 0.16f * (1.0f - dist / alc) * g));
    }
  }

  // Nos principais com os pontos que passam perto: a ligacao que "nasce e
  // solta" - ciano, 2 px (duas linhas lado a lado) e alcancando mais longe.
  const float alc_no = alc * 1.25f;
  for (int n = 0; n < N_NOS; n++) {
    for (int i = 0; i < N_FUNDO; i++) {
      const ponto_t& p = a.pontos[i];
      const float dist = distancia(a.nos[n].x, a.nos[n].y, p.x, p.y);
      if (dist >= alc_no) { continue; }
      const uint16_t cor = misturar(TEMA_APP_CIANO, TEMA_FUNDO, 0.38f * (1.0f - dist / alc_no) * g);
      juntar(a.nos[n].x, a.nos[n].y, p.x, p.y, cor);
      juntar(a.nos[n].x + 1, a.nos[n].y, p.x + 1, p.y, cor);
    }
  }

  // Nos principais entre si: cada par perto acende e apaga no seu proprio
  // ritmo (a malha "conversando").
  for (int i = 0; i < N_NOS; i++) {
    for (int j = i + 1; j < N_NOS; j++) {
      if (distancia(a.nos[i].x, a.nos[i].y, a.nos[j].x, a.nos[j].y) >= 110.0f * a.k) { continue; }
      const float ritmo = 0.5f + 0.5f * sinf(s * 0.9f + i * 1.3f + j * 0.7f);
      if (ritmo <= 0.45f) { continue; }
      juntar(a.nos[i].x, a.nos[i].y, a.nos[j].x, a.nos[j].y,
             misturar(TEMA_TEXTO_2, TEMA_FUNDO, 0.22f * (ritmo - 0.45f) / 0.55f * g));
    }
  }

  a.cor_ponto = misturar(TEMA_TEXTO_2, TEMA_FUNDO, 0.35f * g);
}

// O pedaco da linha 'l' que cai na faixa (linhas y0..y0+alt da tela) de
// 'buf' (larg x alt). Passo a passo pelo eixo mais longo, sempre com a MESMA
// conta para a linha inteira - as faixas vizinhas emendam sem degrau.
void linha_na_faixa(uint16_t* buf, int larg, int y0, int alt, const linha_t& l)
{
  if (l.y_max < y0 || l.y_min >= y0 + alt) { return; }   // nao passa por esta faixa
  const float dx = l.x1 - l.x0, dy = l.y1 - l.y0;
  const int n = (int)fmaxf(fabsf(dx), fabsf(dy));
  if (n == 0) { return; }
  const float px = dx / n, py = dy / n;
  // So os passos cuja altura cai na faixa (com meio pixel de folga).
  int k0 = 0, k1 = n;
  if (py != 0.0f) {
    float ka = (y0 - 0.5f - l.y0) / py, kb = (y0 + alt - 0.5f - l.y0) / py;
    if (ka > kb) { const float t = ka; ka = kb; kb = t; }
    k0 = ka > 0.0f ? (int)floorf(ka) : 0;
    k1 = kb < n ? (int)ceilf(kb) : n;
  }
  for (int k = k0; k <= k1; k++) {
    const int x = (int)(l.x0 + px * k + 0.5f);
    const int y = (int)(l.y0 + py * k + 0.5f) - y0;
    if ((unsigned)x < (unsigned)larg && (unsigned)y < (unsigned)alt) { buf[y * larg + x] = l.cor; }
  }
}

// Pinta a teia na tela inteira, faixa por faixa: limpa a faixa no fundo,
// passa as linhas e os pontos que caem nela e a cola na pagina de tras
// (tela_virtual_colar - ja anota a faixa como coberta e desenhada).
void desenhar_teia(lgfx::LovyanGFX& d, const anim_t& a, uint16_t* faixa, int alt_faixa)
{
  for (int y0 = 0; y0 < a.alt; y0 += alt_faixa) {
    const int alt = (y0 + alt_faixa <= a.alt) ? alt_faixa : a.alt - y0;
    // Limpa a faixa de dois em dois pixels (uma palavra de 32 bits = dois
    // pixels de 16): metade das escritas. A largura (720) e sempre par.
    uint32_t* f32 = (uint32_t*)faixa;
    const uint32_t dois = ((uint32_t)TEMA_FUNDO << 16) | TEMA_FUNDO;
    const int n_pares = a.larg * alt / 2;
    for (int i = 0; i < n_pares; i++) { f32[i] = dois; }
    for (int i = 0; i < a.n_linhas; i++) { linha_na_faixa(faixa, a.larg, y0, alt, a.linhas[i]); }
    // Os pontos de fundo (4x4) por cima das linhas.
    for (int i = 0; i < N_FUNDO; i++) {
      const int px = (int)a.pontos[i].x - 2, py = (int)a.pontos[i].y - 2 - y0;
      for (int yy = py; yy < py + 4; yy++) {
        if ((unsigned)yy >= (unsigned)alt) { continue; }
        for (int xx = px; xx < px + 4; xx++) {
          if ((unsigned)xx < (unsigned)a.larg) { faixa[yy * a.larg + xx] = a.cor_ponto; }
        }
      }
    }
    // Pelo DMA2D (a CPU levava ~30 ms copiando a tela inteira na PSRAM);
    // se o DMA recusar, pela CPU.
    if (!tela_virtual_colar_faixa(faixa, y0, alt)) { tela_virtual_colar(&d, 0, y0, a.larg, alt, faixa, a.larg); }
  }
}

// OS NOS PRINCIPAIS: ponto claro com um halo ciano que respira. Aparecem um
// depois do outro no comeco.
void desenhar_nos(lgfx::LovyanGFX& d, const anim_t& a, float t, float s)
{
  for (int i = 0; i < N_NOS; i++) {
    const float e = suave((t - i * 0.03f) / 0.12f);
    if (e <= 0.0f) { continue; }
    const float respira = 1.0f + 0.12f * sinf(s * 2.0f + i);
    d.fillCircle((int)a.nos[i].x, (int)a.nos[i].y, (int)(7.0f * a.k * e * respira),
                 misturar(TEMA_APP_CIANO, TEMA_FUNDO, 0.18f * e));
    d.fillCircle((int)a.nos[i].x, (int)a.nos[i].y, (int)(3.2f * a.k * e),
                 misturar(TEMA_TEXTO_2, TEMA_FUNDO, e));
  }
}

// OS PACOTES: caracteres que saem dos nos e voam ate o lugar do nome.
void desenhar_pacotes(lgfx::LovyanGFX& d, const anim_t& a, float t)
{
  d.setFont(tab5_fonte_suave(false, TEMA_LETRA_CORPO));
  d.setTextDatum(middle_center);
  char c[2] = { 0, 0 };
  for (int k = 0; k < 32; k++) {
    const float p = suave((t - 0.2f - k * 0.011f) / 0.35f);
    if (p <= 0.0f || p >= 1.0f) { continue; }   // ainda nao saiu, ou ja chegou
    const no_t& n = a.nos[k % N_NOS];
    const float alvo_x = a.cx + (-48.0f + (k % N_NOS) * 16.0f) * a.k;
    const float alvo_y = a.y_nome - 10.0f * a.k;
    c[0] = simbolo_sorteado();
    d.setTextColor((k % 2) ? TEMA_SUCESSO : TEMA_APP_CIANO);
    d.drawString(c, (int)(n.x + (alvo_x - n.x) * p), (int)(n.y + (alvo_y - n.y) * p));
  }
}

// O NOME DECIFRADO: cada letra embaralha em verde ate a sua vez e entao
// fica branca. Letras em casas de largura igual (como num terminal) e um
// cursor verde piscando no fim.
void desenhar_nome(lgfx::LovyanGFX& d, const anim_t& a, float t, int ms)
{
  constexpr float INICIO = 0.5f;
  if (t < INICIO) { return; }

  // SEM caixa atras do nome, de proposito: o nome fica JUNTO com a teia,
  // as linhas passando por tras das letras (pedido do usuario - a caixa
  // escura aparecia como um quadrado apagando a teia). As linhas sao bem
  // apagadas, e o nome em branco negrito se le por cima delas.
  d.setFont(tab5_fonte_suave(true, TEMA_LETRA_GRANDE));
  d.setTextDatum(middle_center);
  const int casa = d.textWidth("M") + 6;               // a letra mais larga do nome
  const int x0 = (int)a.cx - casa * N_LETRAS / 2;
  const int y_meio = (int)(a.y_nome - 10.0f * a.k);
  char c[2] = { 0, 0 };
  for (int i = 0; i < N_LETRAS; i++) {
    const bool pronta = t >= INICIO + 0.05f + i * 0.04f;
    c[0] = pronta ? NOME[i] : simbolo_sorteado();
    d.setTextColor(pronta ? TEMA_TEXTO : TEMA_SUCESSO);
    d.drawString(c, x0 + casa * i + casa / 2, y_meio);
  }

  // Cursor de terminal: 350 ms aceso, 350 apagado.
  if (t > INICIO + 0.4f && (ms / 350) % 2 == 0) {
    const int alt = TEMA_LETRA_GRANDE * 3 / 4;
    d.fillRect(x0 + casa * N_LETRAS + 4, y_meio - alt / 2, alt / 2, alt, TEMA_SUCESSO);
  }

  // "[ conectado ]" acendendo devagar embaixo.
  const float e = suave((t - 0.82f) / 0.15f);
  if (e > 0.0f) {
    d.setFont(tab5_fonte_suave(false, TEMA_LETRA_CORPO));
    d.setTextColor(misturar(TEMA_TEXTO_3, TEMA_FUNDO, e));
    d.drawString(tr("[ conectado ]"), (int)a.cx, (int)(a.y_nome + 28.0f * a.k));
  }
}

}  // namespace

void boot_anim_mostrar(void)
{
  auto& d = tela_virtual();
  // A faixa e colada linha a linha na pagina: so com a tela em pe (rotacao 0,
  // a do boot). Girada, a conta seria outra - sem animacao, e o boot segue.
  if (d.getRotation() != 0) { ESP_LOGW(TAG, "tela girada - sem animacao"); return; }

  // O estado (~60 KB, a lista de linhas) na PSRAM, lido em sequencia; a
  // faixa (720 x 32 px = 45 KB) na memoria INTERNA - ver desenhar_teia. As
  // dois so durante a animacao.
  anim_t* a = (anim_t*)heap_caps_malloc(sizeof(anim_t), MALLOC_CAP_SPIRAM);
  // Alinhada e em memoria de DMA: o DMA2D le a faixa direto daqui.
  uint16_t* faixa = (uint16_t*)heap_caps_aligned_alloc(64, (size_t)d.width() * ALT_FAIXA * 2,
                                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!a || !faixa) {
    ESP_LOGW(TAG, "sem memoria - sem animacao");
    heap_caps_free(a);
    heap_caps_free(faixa);
    return;
  }
  preparar(*a, d.width(), d.height());

  // Para o log do fim: a animacao fez a tela falhar algum quadro?
  uint32_t pior_us = 0, periodo_us = 0;
  const uint32_t atrasos0 = tab5_quadro_atrasos(&pior_us, &periodo_us);
  const uint32_t underruns0 = tab5_quadro_underruns();

  const int64_t t0 = esp_timer_get_time();
  int64_t antes = t0;
  int quadros = 0;
  bool pulou = false;
  for (;;) {
    const int64_t agora = esp_timer_get_time();
    const int ms = (int)((agora - t0) / 1000);
    if (ms > DURACAO_MS + ESPERA_MS) { break; }

    // Um toque pula o resto da animacao.
    M5.update();
    if (M5.Touch.getCount() > 0) { pulou = true; break; }

    const float t = limitar((float)ms / DURACAO_MS);
    const float s = ms / 1000.0f;
    mover_pontos(*a, (agora - antes) / 1e6f);
    antes = agora;

    montar_teia(*a, suave(t / 0.25f), s);
    desenhar_teia(d, *a, faixa, ALT_FAIXA);   // cobre a tela inteira: nao precisa limpar antes   // cobre a tela inteira: nao precisa limpar antes
    desenhar_nos(d, *a, t, s);
    desenhar_pacotes(d, *a, t);
    desenhar_nome(d, *a, t, ms);
    comp_apresentar();
    comp_esperar_troca();   // so desenha o proximo quando este ja esta na tela
    quadros++;
  }

  // Quem pulou com o dedo: espera solta-lo (ate 1 s), para o toque nao virar
  // um toque na tela inicial que vem a seguir.
  if (pulou) {
    for (int i = 0; i < 100 && M5.Touch.getCount() > 0; i++) {
      vTaskDelay(pdMS_TO_TICKS(10));
      M5.update();
    }
  }

  const int total_ms = (int)((esp_timer_get_time() - t0) / 1000);
  const uint32_t atrasos = tab5_quadro_atrasos(&pior_us, &periodo_us) - atrasos0;
  const uint32_t underruns = tab5_quadro_underruns() - underruns0;
  // Com falha vira AVISO (vai para o log do cartao): e a piscada azul.
  if (atrasos || underruns) {
    ESP_LOGW(TAG, "%d quadros em %d ms - a tela FALHOU: %lu atrasado(s), %lu underrun(s)",
             quadros, total_ms, (unsigned long)atrasos, (unsigned long)underruns);
  } else {
    ESP_LOGI(TAG, "%d quadros em %d ms (%d q/s), tela sem falhas%s", quadros, total_ms,
             total_ms > 0 ? quadros * 1000 / total_ms : 0, pulou ? ", pulada pelo toque" : "");
  }

  heap_caps_free(a);
  heap_caps_free(faixa);
  // Devolve o estado global do texto como o sistema espera (o loop principal
  // conta com top_left e a fonte de corpo).
  d.setTextDatum(top_left);
  tab5_fonte_corpo(d);
}
