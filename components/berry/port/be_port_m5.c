/* M5tabOS: a memoria do Berry, na PSRAM, com ORCAMENTO.
 *
 * O Berry faz MUITAS alocacoes pequenas (strings, listas, closures). Com o
 * malloc padrao elas cairiam na RAM interna, que e a mais disputada do
 * sistema (ver docs/ARQUITETURA.md). A PSRAM tem 32 MB e e mais lenta - para um
 * interpretador de script, a troca certa.
 *
 * O ORCAMENTO: todas as maquinas juntas (apps, fundos, console) nao passam de
 * BERRY_ORCAMENTO. Passou, berry_malloc devolve NULL - o Berry entao coleta o
 * lixo, tenta de novo e, sem sobra, levanta "memory_error" DENTRO do script
 * (o pcall de quem chamou recebe). Um 'while true l.push(...)' vira erro na
 * tela do app em vez de comer a PSRAM da camera, do mesh e das pilhas. */
#include <stddef.h>
#include "esp_heap_caps.h"
#include "berry_mem.h"

static volatile size_t s_usada = 0;
static volatile size_t s_pico  = 0;

static void somar(size_t n)
{
  const size_t u = __atomic_add_fetch(&s_usada, n, __ATOMIC_RELAXED);
  if (u > s_pico) { s_pico = u; }   /* so diagnostico: corrida aqui so erra o pico */
}

static int cabe(size_t n)
{
  return s_usada + n <= BERRY_ORCAMENTO;
}

void* berry_malloc(size_t n)
{
  if (!cabe(n)) { return NULL; }
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  if (p) { somar(heap_caps_get_allocated_size(p)); }
  return p;
}

void berry_free(void* p)
{
  if (!p) { return; }
  __atomic_sub_fetch(&s_usada, heap_caps_get_allocated_size(p), __ATOMIC_RELAXED);
  heap_caps_free(p);
}

void* berry_realloc(void* p, size_t n)
{
  if (!p) { return berry_malloc(n); }
  const size_t antes = heap_caps_get_allocated_size(p);
  if (n > antes && !cabe(n - antes)) { return NULL; }   /* o bloco velho continua valendo */
  void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
  if (!q) { return NULL; }
  __atomic_sub_fetch(&s_usada, antes, __ATOMIC_RELAXED);
  somar(heap_caps_get_allocated_size(q));
  return q;
}

size_t berry_mem_usada(void) { return s_usada; }
size_t berry_mem_pico(void)  { return s_pico; }
int    berry_mem_cabe(size_t n) { return cabe(n); }
