// O que o componente exporta alem do minimp3.h: o scratch de cada quadro
// (~16 KB) mora FORA da pilha (ver o remendo MINIMP3_SCRATCH_EXTERNO no
// minimp3.h). Quem decodifica aloca minimp3_scratch_bytes() - na PSRAM - e
// aponta minimp3_scratch para la antes do primeiro mp3dec_decode_frame.
// Um decodificador por vez (o scratch e um so).
#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void* minimp3_scratch;
size_t minimp3_scratch_bytes(void);

#ifdef __cplusplus
}
#endif
