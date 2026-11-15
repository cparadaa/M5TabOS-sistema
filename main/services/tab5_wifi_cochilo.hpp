// WI-FI COCHILA EM REPOUSO - o Wi-Fi conectado era 40% do consumo com a tela
// apagada.
//
// MEDIDO ('energia ensaio', 2026-10-16): repouso 72-74 mA, 28-33 deles do Wi-Fi
// conectado - mesmo em modem sleep maximo, porque o C6 nao pode dormir de
// verdade com o SDIO ativo (ver o exemplo cp_light_sleep_controller.c do
// esp_hosted). Sem Wi-Fi, 38 mA: a bateria dura quase o dobro.
//
// A REGRA (ajuste "economia em repouso" na pagina Wi-Fi, ligada de fabrica):
//   - tela apagada ha COCHILO_APOS_S e nada precisando da rede -> desliga;
//   - acender a tela religa na hora;
//   - a cada JANELA_CADA_S religa sozinho e fica JANELA_S conectado, para
//     o Mesh pela internet e o relogio pegarem o que houver;
//   - "precisa da rede": alguem pediu o receptor ligado (ESP-NOW conversando,
//     CSI, farejo) ou uma parte de fundo declarou o radio no perfil.
// O ajuste de Wi-Fi do usuario (ajustes_wifi) nao muda: so a rede desce.
//
// TAREFA: tab5_wifi_cochilo_vigiar() roda na tarefa de interface (ligar o
// Wi-Fi mexe no expansor I2C), uma vez por segundo.
#pragma once

#include <stddef.h>

void tab5_wifi_cochilo_vigiar(void);
void tab5_wifi_cochilo_resumo(char* out, size_t tam);   // uma linha para o console
