 /* M5tabOS: a conta da memoria do Berry (port/be_port_m5.c). */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* O teto de TODAS as maquinas Berry juntas. Um app medido gasta dezenas de KB
 * (o maior exemplo, ~60 KB no pico): 4 MB e folga para muitos apps vivos ao
 * mesmo tempo e ainda deixa a PSRAM para quem e do sistema. As camadas
 * (tela.camada) NAO entram aqui - sao do M5GFX e tem o limite delas. */
#define BERRY_ORCAMENTO (4u * 1024u * 1024u)

/* Uma maquina nova monta os modulos FORA de qualquer pcall: sem memoria ali o
 * Berry abortaria o aparelho. Quem cria confere antes que cabe isto. */
#define BERRY_FOLGA_VM  (64u * 1024u)

size_t berry_mem_usada(void);   /* bytes em uso agora, todas as maquinas */
size_t berry_mem_pico(void);    /* o maior ja visto desde o boot */
int    berry_mem_cabe(size_t n);

#ifdef __cplusplus
}
#endif
