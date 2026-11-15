// TOQUE LONGO - o dedo parado no mesmo lugar por TOQUE_LONGO_MS.
//
// No ao_tocar_nivel do app, a cada amostra:
//   if (toque_longo(&s_lp, x, y, pressionado)) { ...acao...; sistema_consumir_toque(); }
// Devolve true UMA vez por toque. Mexeu mais que TOQUE_LONGO_FOLGA, virou
// arraste e nao dispara mais ate o dedo sair. O sistema_consumir_toque() faz
// a soltura nao virar clique.
#pragma once

#include "tab5_hora.hpp"
#include <stdlib.h>

#define TOQUE_LONGO_MS    550
#define TOQUE_LONGO_FOLGA 24

typedef struct { int x, y; uint32_t desde; bool ativo, feito; } toque_longo_t;

static inline bool toque_longo(toque_longo_t* t, int x, int y, bool pressionado)
{
  if (!pressionado) { t->ativo = false; return false; }
  if (!t->ativo) { *t = { x, y, tab5_agora_ms(), true, false }; return false; }
  if (t->feito) { return false; }
  if (abs(x - t->x) > TOQUE_LONGO_FOLGA || abs(y - t->y) > TOQUE_LONGO_FOLGA) { t->feito = true; return false; }
  if (tab5_agora_ms() - t->desde < TOQUE_LONGO_MS) { return false; }
  t->feito = true;
  return true;
}
