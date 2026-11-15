// ENSAIO DE CONSUMO - quanto cada servico custa com a tela apagada.
//
// Nasceu da analise de bateria de 2026-10-16: em repouso o aparelho puxava
// 75-100 mA (17 h de cheio a vazio), com patamares de ~46, ~68, ~76, ~97, ~112
// e ~140 mA conforme o que estava ligado. Pela serial nao da para medir: o
// cabo USB carrega a bateria e o INA226 so ve a diferenca. O ensaio roda
// sozinho, NA BATERIA, e grava o resultado no cartao.
//
// COMO USAR: 'energia ensaio' no console, tirar o cabo, deixar a tela apagar e
// esperar ~45 min sem mexer. Ao ligar o cabo de novo, 'energia ensaio' mostra
// a tabela (tambem em Sistema/ensaio_energia.csv e no log).
//
// AS FASES SAO CUMULATIVAS - cada uma desliga mais um servico, e a diferenca
// para a anterior e o custo dele:
//   0 base (como esta)   1 sem Mesh pela internet   2 sem o 5V do Grove
//   3 sem Bluetooth      4 sem Wi-Fi (o C6 segue alimentado)
// Cada fase: 2 min de acomodacao + N min medindo, contando SO os segundos em
// repouso e descarregando (acordar a tela ou ligar o cabo pausa a contagem).
// No fim (ou em 'energia ensaio parar') tudo volta como estava.
//
// Os ajustes mexidos vao para a NVS (e o caminho que os servicos obedecem).
// Se o aparelho desligar no meio, os valores originais estao no log ("ensaio:
// originais ...") para devolver na mao.
#pragma once

#include <stddef.h>
#include <stdbool.h>

bool tab5_ensaio_comecar(int min_por_fase, int fases = 5);   // false = ja ha um rodando; fases 1 = so a base
void tab5_ensaio_parar(void);                 // devolve os ajustes (na proxima volta da interface)
void tab5_ensaio_imprimir(void);              // estado + tabela, no stdout (console)

// 1 Hz, na tarefa de interface (mexe no expansor I2C), logo depois da medida.
void tab5_ensaio_vigiar(void);
