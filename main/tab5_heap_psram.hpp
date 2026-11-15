// O HEAP DA PSRAM NO BOOT - guarda, sentinela e checagem (tab5_heap_psram.cpp).
//
// Problema em aberto (2026-10-21): raramente (~1 em 65 boots na 0.8.0) o
// cabecalho do heap da PSRAM aparece estragado e o M5.begin() cai ao alocar o
// quadro da tela (Load/Store access fault em tlsf_*). O que foi lido no lugar
// do ponteiro heap_data era o proprio contador free_bytes do heap.
// Acontece DENTRO do M5.begin, entre o primeiro e o segundo quadro de 1,8 MB
// do esp_lcd (num_fbs = 2): por isso toda reserva grande na PSRAM e toda
// sincronia grande de cache tambem conferem (tab5_heap_psram_checar, pelos
// --wrap do main/CMakeLists.txt). Com essa conferencia o defeito sumiu (0 em
// 44 boots num build que caia em 30%) - mais um sinal de que depende de
// leiaute/tempo, e de que a causa segue em aberto.
#pragma once

#include <stdint.h>

// Confere o cabecalho do heap da PSRAM contra o retrato tirado logo depois da
// montagem. Estragado: escreve no console e reinicia limpo (esp_restart, que
// para os DMAs) em vez de deixar o M5.begin() dar panico. No maximo 3 vezes
// seguidas - depois segue, e o panico (com core dump) volta a ser a resposta.
// Chamar ANTES do M5.begin().
void tab5_heap_psram_conferir(void);

// Quantos reinicios limpos seguidos a checagem fez antes deste boot (0 = nenhum).
// O tab5_log escreve no log do cartao, que so existe depois do app_main.
uint32_t tab5_heap_psram_reinicios(void);

// Teste do mecanismo, pelo console ('mem estresse [voltas] [linha]'): grava
// uma linha de cache na PSRAM, zera 1,8 MB e confere a linha. 'zerar' false =
// so a linha, sem varrer o cache (o controle do teste).
void tab5_heap_psram_estresse(int voltas, bool zerar);
