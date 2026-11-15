// A IMPLEMENTACAO do minimp3 (um header so - ver minimp3.h), compilada uma vez,
// aqui. O scratch de cada quadro vem de minimp3_scratch (minimp3_externo.h):
// na pilha ele nao caberia em nenhuma vaga do pool de tarefas (a maior e de
// 6 KB).
#include <stddef.h>
#include "minimp3_externo.h"

#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"

void* minimp3_scratch = NULL;

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD                 // RISC-V: sem SSE/NEON
#define MINIMP3_SCRATCH_EXTERNO minimp3_scratch
#include "minimp3.h"

size_t minimp3_scratch_bytes(void) { return sizeof(mp3dec_scratch_t); }
