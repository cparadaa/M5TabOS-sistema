// ORIENTACAO DA TELA - quem decide se o app aberto fica em pe ou deitado.
//
// O app declara (app_t.orientacao, ver kernel/app.h); este modulo aplica. Para
// ORI_EM_PE e ORI_DEITADO e so a rotacao certa na abertura. Para
// ORI_SEGUE_IMU ele tambem vigia a gravidade enquanto o app esta aberto e
// gira entre os DOIS estados que existem - em pe (rotacao 0) e deitado
// (rotacao 3, ExtPort1 embaixo). Nunca o lado oposto de nenhum deles.
//
// NAO E UMA TAREFA. O IMU fica no I2C interno, que so a tarefa de interface
// pode usar (ver hal/tab5_imu.hpp); entao a vigia e uma funcao que o laco da
// interface chama a cada volta. Com nenhum app seguindo o IMU ela retorna na
// primeira linha: quem nao usa nao paga leitura nenhuma.
//
// TUDO AQUI E DA TAREFA DE INTERFACE.
#pragma once

#include <stdint.h>
#include "app.h"

// Aplica a orientacao inicial do app que esta abrindo. Chamar ANTES do perfil
// e de qualquer desenho: girar muda largura e altura, e tudo que for
// calculado a partir delas precisa ver o valor novo.
void orientacao_ao_abrir(const app_t* app);

// O app fechou: volta em pe (a home e em pe; deitada com o teclado fisico
// encaixado) e para de vigiar o IMU.
void orientacao_ao_fechar(void);

// Uma volta da vigia. 'pode_girar' = nenhum dedo na tela, nada na frente
// (bloqueio). A amostragem continua mesmo quando nao pode - so a troca
// espera. Vale tambem na home: encaixar ou tirar o teclado fisico gira a tela
// de qualquer um (ver s_teclado no .cpp); o IMU so para app ORI_SEGUE_IMU.
//
// Devolve true quando GIROU nesta chamada: quem chama pede a passada
// completa e avisa o app (ao_girar).
bool orientacao_vigiar(uint32_t agora_ms, bool pode_girar);

// Quanto falta para a proxima amostra, em ms - para a interface parada nao
// dormir alem dela. UINT32_MAX quando nenhum app segue o IMU.
uint32_t orientacao_prazo_ms(uint32_t agora_ms);
