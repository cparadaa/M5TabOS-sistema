// Vigia do Grove (kernel/grove.cpp): sonda o cabo no boot, quando o modulo
// muda e, sem nada conclusivo, mais duas vezes. Nunca na tarefa de interface.
#pragma once

void grove_iniciar(void);           // uma vez, da tarefa de interface, antes do loop
void grove_pedir_sondagem(void);    // de qualquer tarefa; nao bloqueia
bool grove_sondando(void);          // do pedido ate a sondagem terminar
