// SHELL - a tela de bloqueio: bloquear e desbloquear, a animacao ao fundo, o
// relogio, a data e os avisos na frente dela. Partido de shell.cpp; o que as
// partes compartilham esta em shell_interno.hpp.

#include "esp_attr.h"
#include "bloqueio_anim.hpp"
#include "tela_virtual.hpp"
#include "ajustes.h"
#include "idioma.h"
#include "tab5_tela.hpp"
#include "notificacao.h"
#include "tab5_ui.hpp"
#include "tab5_tema.hpp"
#include "tab5_hora.hpp"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include "shell_interno.hpp"

// ---------------------------------------------------------------------------
// TELA DE BLOQUEIO
// ---------------------------------------------------------------------------
// v1, de proposito simples: hora grande, data, o aviso em destaque (a mesma
// fonte que a barra de status usa) e a dica de deslizar. Sem senha. Tudo
// que muda com o tempo compara com o cache antes de pintar, como o resto do
// sistema - o relogio e conferido a cada segundo e so repinta quando o
// minuto vira.
static char     s_bq_hora[8]   = "";
EXT_RAM_BSS_ATTR static char s_bq_aviso[4 * (NOTIF_RESUMO + 24)];   // zerado = ""
static uint32_t s_bq_dia       = 0;

static void data_por_extenso(char* out, size_t n)
{
  const uint32_t t = tab5_hora_epoch();
  if (!t) { snprintf(out, n, " "); return; }
  struct tm lt;
  tab5_hora_local(t, &lt);
  idioma_data_por_extenso(&lt, out, n);
}

// Os textos da tela de bloqueio ficam NA FRENTE da animacao.
//
// Cada texto e pintado UMA vez (quando muda) num sprite pequeno, com fundo
// preto e a fonte suave de sempre - o mesmo desenho de antes da animacao. A
// cada quadro, so onde a animacao passou por cima, o sprite e copiado para a
// tela com o PRETO TRANSPARENTE. Assim o texto nunca depende do que ha na
// pagina de tras: pintar texto suave sem fundo faz o M5GFX LER os pixels da
// tela e misturar a borda das letras com eles, e isso engrossa a letra a cada
// repintura e a deixa feia.
//
// Caixas fixas, sem se sobrepor, com folga em volta do texto.
enum { BQ_HORA, BQ_DATA, BQ_AVISO, BQ_DICA, BQ_N };

static lgfx::LGFX_Sprite* s_bq_spr[BQ_N] = {};

// AVISOS NO BLOQUEIO: uma linha por app, quantas couberem entre a data e a
// dica (ate 4). Deitado sobra espaco para uma so - a mais grave.
static const int BQ_AVISO_LINHA = 52;
static int bq_linhas_aviso(const area_t& a)
{
  const int y_hora = a.y + a.altura / 4;
  const int y_dica = a.y + a.altura - 110;
  const int n = ((y_dica - 48) - (y_hora + 190)) / BQ_AVISO_LINHA;
  return n < 1 ? 1 : (n > 4 ? 4 : n);
}

static void bq_caixas(const area_t& a, bq_ret_t r[BQ_N])
{
  const int cx = a.x + a.largura / 2;
  const int y_hora = a.y + a.altura / 4;
  const int y_dica = a.y + a.altura - 110;
  r[BQ_HORA]  = { (int16_t)(cx - 240), (int16_t)(y_hora - 92),  480, 170 };
  r[BQ_DATA]  = { (int16_t)(cx - 350), (int16_t)(y_hora + 78),  700, 64 };
  r[BQ_AVISO] = { (int16_t)(a.x + 10), (int16_t)(y_hora + 190), (int16_t)(a.largura - 20),
                  (int16_t)(bq_linhas_aviso(a) * BQ_AVISO_LINHA + 18) };
  r[BQ_DICA]  = { (int16_t)(cx - 200), (int16_t)(y_dica - 48),  400, 80 };
}

// Desenha o texto 'i' em 't' (um sprite ou a tela), com (ox, oy) = canto da
// caixa em coordenadas de tela. Fundo preto, sempre: o preto (TFT_BLACK) nao e
// cor de tema, e a chave TRANSPARENTE da copia para a tela - e ele que deixa a
// animacao aparecer atras do texto.
static void bq_texto(lgfx::LovyanGFX& t, const area_t& a, int i, const char* hora,
                     const char* data, const char* aviso, int ox, int oy)
{
  const int cx = a.x + a.largura / 2 - ox;
  const int y_hora = a.y + a.altura / 4 - oy;
  t.setTextDatum(middle_center);
  switch (i) {
    case BQ_HORA:
      tab5_fonte_corpo(t);
      tab5_texto(t, 7.0f);    // 56 px, a maior altura nativa da fonte de corpo; dobrada abaixo
      t.setTextSize(2);       // inteiro (nao fracionario): 1 pixel de fonte = 2x2 na tela
      t.setTextColor(TEMA_TEXTO, TFT_BLACK);
      t.drawString(hora, cx, y_hora);
      t.setTextSize(1);
      break;
    case BQ_DATA:
      tab5_fonte_corpo(t);
      tab5_texto(t, 3.5f);
      t.setTextColor(TEMA_TEXTO_2, TFT_BLACK);
      t.drawString(data, cx, y_hora + 110);
      break;
    case BQ_AVISO: {
      // 'aviso' e uma linha por app: o primeiro caractere e o nivel ('0'..'3'),
      // o resto o texto, e '\n' separa (montado em desenhar_bloqueio).
      tab5_fonte_corpo(t);
      tab5_texto(t, 3.0f);
      int y = y_hora + 225;
      for (const char* p = aviso; *p; ) {
        const char* fim = strchr(p, '\n');
        const size_t n = fim ? (size_t)(fim - p) : strlen(p);
        if (n > 1) {
          char linha[NOTIF_RESUMO + 24];
          snprintf(linha, sizeof(linha), "%.*s", (int)(n - 1), p + 1);
          t.setTextColor(cor_do_nivel((notif_nivel_t)(p[0] - '0')), TFT_BLACK);
          t.drawString(linha, cx, y);
          y += BQ_AVISO_LINHA;
        }
        if (!fim) { break; }
        p = fim + 1;
      }
    } break;
    default: {
      const int y_dica = a.y + a.altura - 110 - oy;
      t.fillRoundRect(cx - 60, y_dica - 40, 120, 8, 4, TEMA_DESTAQUE);   // barra de deslizar
      tab5_fonte_corpo(t);
      tab5_texto(t, 3.0f);
      t.setTextColor(TEMA_TEXTO_3, TFT_BLACK);
      t.drawString(tr("Deslize para cima"), cx, y_dica);
    } break;
  }
}

static void bq_sprites_soltar(void)
{
  for (int i = 0; i < BQ_N; ++i) {
    delete s_bq_spr[i];
    s_bq_spr[i] = nullptr;
  }
}

// Cria os sprites (PSRAM, so enquanto bloqueado). false = sem memoria: o
// texto cai no desenho direto, com fundo preto.
static bool bq_sprites_criar(const bq_ret_t cx[BQ_N])
{
  for (int i = 0; i < BQ_N; ++i) {
    auto* sp = new lgfx::LGFX_Sprite();
    if (sp) {
      sp->setColorDepth(16);
      sp->setPsram(true);
    }
    if (!sp || !sp->createSprite(cx[i].w, cx[i].h)) {
      delete sp;
      bq_sprites_soltar();
      return false;
    }
    s_bq_spr[i] = sp;
  }
  return true;
}

void bloquear_tela(void)
{
  s_bloqueado   = true;
  s_painel_avisos = false;   // a central nao sobrevive ao bloqueio
  s_bq_hora[0]  = 0;   // forca o primeiro desenho completo
  s_sujo_layout = true;
  bq_anim_sortear();
}

void bloqueio_ao_ligar_tela(void)
{
  if (s_bloqueado)            { bq_anim_sortear(); }   // acordou ainda bloqueada: outra animacao
  else if (ajustes_bloqueio()) { bloquear_tela(); }
}

void desbloquear_tela(void)
{
  s_bloqueado   = false;
  s_sujo_layout = true;   // volta home/app: repintura completa, nav inclusive
  bq_sprites_soltar();
  bq_anim_soltar();
}

// Um quadro da tela de bloqueio (~25 Hz): animacao ao fundo, texto na frente.
void desenhar_bloqueio(lgfx::LovyanGFX& d, const area_t& a, bool completo)
{
  char hora[8], data[48], aviso[sizeof(s_bq_aviso)] = "";
  hora_agora(hora, sizeof(hora));
  data_por_extenso(data, sizeof(data));

  // UM APP POR LINHA, e nao so o destaque: com Mesh e ESP-NOW avisando ao
  // mesmo tempo, o bloqueio mostrava um e escondia o outro. A ultima linha
  // que cabe vira "+N" quando sobra app.
  notif_item_t lista[NOTIF_MAX_FONTES];
  const int n_av = notif_listar(lista, NOTIF_MAX_FONTES);
  const int cabem = bq_linhas_aviso(a);
  size_t usado = 0;
  for (int i = 0; i < n_av && i < cabem; ++i) {
    char txt[NOTIF_RESUMO + 24];
    if (i == cabem - 1 && n_av > cabem) {
      snprintf(txt, sizeof(txt), tr("+%d outros avisos"), n_av - i);
    } else if (lista[i].resumo[0]) {
      snprintf(txt, sizeof(txt), "%s: %s", tr(lista[i].nome), lista[i].resumo);
    } else {
      snprintf(txt, sizeof(txt), "%s: %d", tr(lista[i].nome), lista[i].quantidade);
    }
    const int k = snprintf(aviso + usado, sizeof(aviso) - usado, "%c%s\n",
                           (char)('0' + (int)lista[i].nivel), txt);
    if (k < 0 || (size_t)k >= sizeof(aviso) - usado) { break; }
    usado += (size_t)k;
  }

  const bool hora_mudou  = strcmp(hora, s_bq_hora) != 0;
  const bool aviso_mudou = strcmp(aviso, s_bq_aviso) != 0;
  snprintf(s_bq_hora, sizeof(s_bq_hora), "%s", hora);
  snprintf(s_bq_aviso, sizeof(s_bq_aviso), "%s", aviso);

  bq_ret_t caixa[BQ_N];
  bq_caixas(a, caixa);
  const bool mudou[BQ_N] = { completo || hora_mudou, completo || hora_mudou, completo || aviso_mudou, completo };

  // Os sprites nascem (ou renascem) com o desenho completo e so sao pintados
  // de novo quando o texto muda.
  if (completo && !s_bq_spr[0]) { bq_sprites_criar(caixa); }
  const lgfx::TextStyle estilo = d.getTextStyle();
  for (int i = 0; i < BQ_N; ++i) {
    if (!mudou[i] || !s_bq_spr[i]) { continue; }
    s_bq_spr[i]->fillScreen(TFT_BLACK);
    bq_texto(*s_bq_spr[i], a, i, hora, data, aviso, caixa[i].x, caixa[i].y);
  }

  // Texto que mudou: a caixa e apagada (a animacao se refaz nela) e repintada.
  bq_ret_t extras[BQ_N];
  int n_extras = 0;
  if (completo) {
    bq_anim_iniciar(a);
  } else {
    for (int i = 0; i < BQ_N; ++i) { if (mudou[i]) { extras[n_extras++] = caixa[i]; } }
  }
  bq_ret_t clips[BQ_N];
  bq_anim_quadro(d, extras, n_extras, caixa, BQ_N, clips);

  const uint16_t preto = 0;   // cor transparente do sprite
  for (int i = 0; i < BQ_N; ++i) {
    bq_ret_t clip = clips[i];
    if (mudou[i]) { clip = caixa[i]; }
    if (clip.w <= 0) { continue; }
    d.setClipRect(clip.x, clip.y, clip.w, clip.h);
    if (s_bq_spr[i]) {
      s_bq_spr[i]->pushSprite(&d, caixa[i].x, caixa[i].y, preto);
    } else {
      bq_texto(d, a, i, hora, data, aviso, 0, 0);
    }
    d.clearClipRect();
  }
  d.setTextStyle(estilo);
  tab5_fonte_corpo(d);
}
