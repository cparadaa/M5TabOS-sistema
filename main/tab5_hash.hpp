// HASH FNV-1a de 32 bits - a unica copia do sistema. Serve para chave de cache,
// assinatura de "mudou?" e conferencia de dados; nao e criptografia.
#pragma once

#include <stdint.h>
#include <stddef.h>

#define TAB5_FNV1A_INICIO 2166136261u

// Bytes quaisquer. 'h' encadeia: o hash de uma parte pode ser o inicio da outra.
static inline uint32_t tab5_fnv1a(const void* dados, size_t n, uint32_t h = TAB5_FNV1A_INICIO)
{
  const uint8_t* p = (const uint8_t*)dados;
  for (size_t i = 0; i < n; ++i) { h = (h ^ p[i]) * 16777619u; }
  return h;
}

// Texto terminado em 0. 'sem_caixa' = A-Z contam como a-z ("Queen" e "queen"
// dao o mesmo hash).
static inline uint32_t tab5_fnv1a_txt(const char* s, uint32_t h = TAB5_FNV1A_INICIO, bool sem_caixa = false)
{
  for (; *s; ++s) {
    uint8_t c = (uint8_t)*s;
    if (sem_caixa && c >= 'A' && c <= 'Z') { c += 32; }
    h = (h ^ c) * 16777619u;
  }
  return h;
}
