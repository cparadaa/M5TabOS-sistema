// O que os arquivos do bloqueio_anim compartilham: area util, aleatorio, cores,
// lista de caixas mexidas e as funcoes de entrada de cada animacao. So para
// eles: nenhum outro codigo inclui este header (a API publica e o
// bloqueio_anim.hpp).
//
// O bloqueio_anim foi partido por assunto (so mudou de arquivo, nada de logica):
//   bloqueio_anim.cpp            escolha da animacao, tempo, fundos, caixas, o quadro e a parede
//   bloqueio_anim_<nome>.cpp     uma animacao cada: bolas, fogos, chuva, estrelas, bolhas,
//                                cobra, vagalumes, pendulos, neve, cometas, solar, logo
#pragma once

#include "bloqueio_anim.hpp"

#include <math.h>
#include <string.h>
#include <stdint.h>

#include "tab5_ui.hpp"
#include "tela_virtual.hpp"

namespace bq_anim {

// ---------------------------------------------------------------------------
// Area, aleatorio, cores (definidos no bloqueio_anim.cpp)
// ---------------------------------------------------------------------------
extern int s_ax, s_ay, s_W, s_H;   // area util (onde a animacao pode tocar)
extern uint32_t s_rng;

static inline uint32_t rnd(void)
{
  s_rng ^= s_rng << 13; s_rng ^= s_rng >> 17; s_rng ^= s_rng << 5;
  return s_rng;
}
static inline float frand(float a, float b) { return a + (b - a) * (float)(rnd() & 0xFFFF) / 65535.0f; }
static inline int   irand(int n)            { return (int)(rnd() % (uint32_t)n); }

static inline uint16_t c565(int r, int g, int b)
{
  if (r > 255) { r = 255; } if (g > 255) { g = 255; } if (b > 255) { b = 255; }
  if (r < 0)   { r = 0; }   if (g < 0)   { g = 0; }   if (b < 0)   { b = 0; }
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// Matiz 0..255 (vermelho, amarelo, verde, ciano, azul, magenta) com brilho v.
uint16_t matiz(uint8_t h, int v);

// ---------------------------------------------------------------------------
// Caixas mexidas neste quadro e fundo
// ---------------------------------------------------------------------------
extern bool s_tudo;   // desenhar tudo (nao so o que mudou)

void rec(int x, int y, int w, int h);                                  // registra uma caixa a apagar
void rec2(float x0, float y0, float x1, float y1, int r);              // caixa que cobre dois pontos
void fundo_ceu(lgfx::LGFX_Sprite& s, int W, int H);                    // estrelas fixas (fundo do solar)

// ---------------------------------------------------------------------------
// Estado da animacao sorteada
// ---------------------------------------------------------------------------
// O estado de TODAS as animacoes divide estes bytes: so a sorteada vive. Cada
// animacao enxerga o trecho como o seu proprio tipo (bq_estado_como) e o
// static_assert garante que ele cabe.
static const size_t BQ_ESTADO_MAX = 4096;
alignas(16) extern uint8_t bq_estado[BQ_ESTADO_MAX];

template <typename T>
static inline T& bq_estado_como(void)
{
  static_assert(sizeof(T) <= BQ_ESTADO_MAX, "estado da animacao maior que bq_estado");
  return *reinterpret_cast<T*>(bq_estado);
}

// ---------------------------------------------------------------------------
// Funcoes de entrada de cada animacao (uma por arquivo bloqueio_anim_<nome>.cpp)
// ---------------------------------------------------------------------------
void bolas_iniciar(void);
void bolas_passo(float dt);
void bolas_desenhar(lgfx::LovyanGFX& d);

void fogos_iniciar(void);
void fogos_passo(float dt);
void fogos_desenhar(lgfx::LovyanGFX& d);

void chuva_iniciar(void);
void chuva_passo(float dt);
void chuva_desenhar(lgfx::LovyanGFX& d);

void estrelas_iniciar(void);
void estrelas_passo(float dt);
void estrelas_desenhar(lgfx::LovyanGFX& d);

void bolhas_iniciar(void);
void bolhas_passo(float dt);
void bolhas_desenhar(lgfx::LovyanGFX& d);

void cobra_iniciar(void);
void cobra_passo(float dt);
void cobra_desenhar(lgfx::LovyanGFX& d);

void lumes_iniciar(void);
void lumes_passo(float dt);
void lumes_desenhar(lgfx::LovyanGFX& d);

void pendulos_iniciar(void);
void pendulos_passo(float dt);
void pendulos_desenhar(lgfx::LovyanGFX& d);

void neve_iniciar(void);
void neve_passo(float dt);
void neve_desenhar(lgfx::LovyanGFX& d);

void cometas_iniciar(void);
void cometas_passo(float dt);
void cometas_desenhar(lgfx::LovyanGFX& d);

void solar_iniciar(void);
void solar_passo(float dt);
void solar_desenhar(lgfx::LovyanGFX& d);
void solar_fundo(lgfx::LGFX_Sprite& sp, int W, int H);   // fundo proprio: o ceu estrelado

void logo_iniciar(void);
void logo_passo(float dt);
void logo_desenhar(lgfx::LovyanGFX& d);
void logo_soltar(void);   // libera o texto pintado

}  // namespace bq_anim
