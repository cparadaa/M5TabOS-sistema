// AREA DE TRANSFERENCIA - um texto copiado, para colar em outro lugar.
//
// Como nos celulares: UM item so, de texto, que a copia seguinte substitui.
// Nao ha historico. Cabe ate TRANSF_MAX bytes (uma nota inteira); o que
// passar e cortado na fronteira de um caractere UTF-8. O buffer e da PSRAM
// e so existe depois da primeira copia - sem copia, custo zero.
//
// Quem copia ou cola mostra o recado na barra (sistema_recado, app.h); o
// servico so guarda. Seguro de qualquer tarefa (Berry incluido).
#pragma once

#include <stddef.h>
#include <stdbool.h>

#define TRANSF_MAX 4096   // com o '\0'

// Copia 'texto' (vazio ou nulo nao muda nada). false = sem memoria.
bool   transf_copiar(const char* texto);
// O texto copiado em 'out' (com '\0', cortado em 'cap'). Devolve os bytes
// escritos sem o '\0'; 0 = a area esta vazia.
size_t transf_colar(char* out, size_t cap);
bool   transf_vazia(void);
