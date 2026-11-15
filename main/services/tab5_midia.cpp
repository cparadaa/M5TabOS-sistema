// MIDIA - ver tab5_midia.hpp.

#include "tab5_midia.hpp"
#include "tab5_volumes.hpp"

#include "esp_heap_caps.h"
#include <cstring>

// ---- as funcoes de arquivo do medf, pelo leitor do sistema ----------------
static void* abrir(const char* caminho, off_t* tamanho)
{
  tab5_leitor_t* r = tab5_leitor_abrir(caminho);
  if (!r) { return nullptr; }
  *tamanho = (off_t)tab5_leitor_tamanho(r);
  return r;
}

// Le 'n' bytes a partir de 'pos'. Devolve quantos leu (menos so no fim ou erro).
static size_t ler_em(void* arq, off_t pos, void* dst, size_t n)
{
  tab5_leitor_t* r = (tab5_leitor_t*)arq;
  if (!tab5_leitor_ir(r, (uint64_t)pos)) { return 0; }
  size_t feito = 0;
  while (feito < n) {
    const int k = tab5_leitor_ler(r, (uint8_t*)dst + feito, n - feito);
    if (k <= 0) { break; }
    feito += (size_t)k;
  }
  return feito;
}

static void fechar(void* arq) { tab5_leitor_fechar((tab5_leitor_t*)arq); }

void tab5_midia_preparar(void)
{
  static bool feito = false;   // medf_set_io so copia a configuracao: repetir e inofensivo
  if (feito) { return; }
  // Leitura antecipada com prioridade 5 em qualquer nucleo: acima dos
  // decodificadores (4), que ocupam o nucleo inteiro. Ela passa quase todo o
  // tempo esperando o cartao, entao nao rouba CPU de ninguem - e sem isso o
  // som de um video pesado engasgava (a leitura do AAC ficava sem vez).
  const medf_io_t io = { abrir, ler_em, fechar, 5, -1 };
  medf_set_io(&io);
  feito = true;
}

bool tab5_midia_arena(medf_arena_t* a, size_t bytes)
{
  memset(a, 0, sizeof(*a));
  a->data = (uint8_t*)heap_caps_aligned_alloc(MEDF_ARENA_ALIGNMENT, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!a->data) { return false; }
  a->size = bytes;
  return true;
}

void tab5_midia_arena_soltar(medf_arena_t* a)
{
  if (a->data) { heap_caps_free(a->data); }
  memset(a, 0, sizeof(*a));
}
