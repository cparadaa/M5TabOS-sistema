// GUARDA DA BATERIA - o sistema nao deixa a bateria morrer com o aparelho
// ligado.
//
// O QUE OS REGISTROS MOSTRARAM (energia-2026-10-*.csv, 4 descargas ate o fim
// em uma semana - todas de madrugada, sem ninguem olhando):
//   - a % do estimador (hal/tab5_power) bate com a carga contada em mAh: erro
//     de 1 a 3 pontos, sempre para MENOS no fim (mostra 0% com 1-2% de sobra);
//   - mas nada acontecia em 0%: o aparelho seguia ate o brownout (6,57 V, 3,29
//     V por celula - descarga profunda, que gasta a bateria) e, ligado de novo,
//     entrava em laco de reinicio, cada volta descendo mais um pouco.
//
// O QUE O SISTEMA FAZ, sem app nenhum pedir:
//   - avisa na central de avisos em 20% (atencao), 10% e 5% (urgente);
//   - em 0% (com a tensao confirmando) por 30 s seguidos, ou com a tensao abaixo
//     do piso mesmo que a % discorde, GRAVA o que esta pendente no cartao e
//     DESLIGA (M5.Power.powerOff) - a celula fica em ~3,5 V, com folga;
//   - o mesmo vale no boot: ligar com a bateria vazia desliga de novo, em vez
//     do laco de reinicio;
//   - na tomada (carregando), tudo isso se apaga.
//
// TAREFA: tab5_bateria_vigiar() roda NA TAREFA DE INTERFACE, a mesma que mede
// (kernel/core.cpp, logo depois de tab5_power_medir()): ela e a unica que vai
// ao I2C, e desligar e um pulso no expansor de I/O.
#pragma once

#include <stddef.h>

// Uma vez por medida (1 Hz), acordado ou em repouso.
void tab5_bateria_vigiar(void);

// Uma linha de estado para o console ('energia').
void tab5_bateria_resumo(char* out, size_t tam);
