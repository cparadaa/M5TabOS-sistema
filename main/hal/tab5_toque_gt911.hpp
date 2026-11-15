// Toque GT911 (ILI9881C do Tab5) sem "dedo fantasma" quando o I2C falha.
//
// DEFEITO DO DRIVER DO M5GFX (Touch_GT911.inl, getTouchRaw): se a leitura do
// I2C falha, o buffer interno (_readdata) continua com a ULTIMA leitura boa e
// o getTouchRaw devolve esses pontos como se fossem novos. Se o ultimo estado
// bom tinha um dedo, uma unica falha de I2C vira dedo parado na tela (toque
// segurado). Os membros sao private, entao nao ha como consertar por heranca
// simples: esta classe refaz so o getTouchRaw, com buffer proprio, e devolve
// 0 pontos quando a leitura falha. O resto (init/sleep/wakeup) e do original.
//
// COMO ENTRA: tab5_toque_gt911_instalar(), chamada uma vez depois do M5.begin().
#pragma once

#include <M5Unified.h>
#include <stdint.h>

// Troca o toque do painel pelo Toque_GT911. So age no ILI9881C (GT911); nos
// outros paineis nao faz nada. Devolve true se trocou. Chamar uma vez, depois
// do M5.begin().
bool tab5_toque_gt911_instalar(void);

// Quantas leituras de I2C falharam desde o boot (para diagnostico).
uint32_t tab5_toque_gt911_falhas(void);
