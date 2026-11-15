// Animacao do bloqueio: Onda de pendulos: cada bola num ritmo um pouco diferente.
// (parte do bloqueio_anim; o que e comum a todas esta no bloqueio_anim_interno.hpp).

#include "bloqueio_anim_interno.hpp"

namespace bq_anim {

// ---------------------------------------------------------------------------
// 8. PENDULOS - a "onda de pendulos": cada bola num ritmo um pouco diferente,
//    e os desenhos (serpente, duas fileiras, caos, fileira de novo) se repetem
// ---------------------------------------------------------------------------
namespace {
static const int N_PEND = 18;
struct Pendulos { float t; float ox[N_PEND]; };
}

// Bola i faz (K + i) idas e vindas a cada CICLO segundos: no fim do ciclo
// todas voltam juntas, e no meio passam pelas figuras.
static const float PEND_CICLO = 60.0f;
static const int   PEND_K     = 12;
static const int   PEND_R     = 12;

static float pend_x(int i, float t)
{
  const float amp = s_W * 0.5f - PEND_R - 18;
  return s_ax + s_W * 0.5f + amp * sinf(6.2832f * (PEND_K + i) * t / PEND_CICLO);
}
static float pend_y(int i)
{
  return s_ay + 50 + (float)i * (s_H - 100) / (N_PEND - 1);
}

void pendulos_iniciar(void)
{
  Pendulos& s = bq_estado_como<Pendulos>();
  s.t = 0;   // todas juntas, comecando a se separar
  for (int i = 0; i < N_PEND; ++i) { s.ox[i] = pend_x(i, 0); }
}

void pendulos_passo(float dt)
{
  Pendulos& s = bq_estado_como<Pendulos>();
  s.t += dt;
  if (s.t >= PEND_CICLO) { s.t -= PEND_CICLO; }
  for (int i = 0; i < N_PEND; ++i) {
    const float x = pend_x(i, s.t), y = pend_y(i);
    rec2(s.ox[i], y, x, y, PEND_R + 2);
    s.ox[i] = x;
  }
}

void pendulos_desenhar(lgfx::LovyanGFX& d)
{
  const Pendulos& s = bq_estado_como<Pendulos>();
  for (int i = 0; i < N_PEND; ++i) {
    const int x = (int)s.ox[i], y = (int)pend_y(i);
    d.fillCircle(x, y, PEND_R, matiz((uint8_t)(i * 13), 230));
    d.fillCircle(x - 4, y - 4, 3, c565(240, 240, 240));
  }
}

}  // namespace bq_anim
