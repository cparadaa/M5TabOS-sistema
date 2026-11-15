// RECURSOS - o que cada um esta usando, para o SISTEMA decidir a suspensao.
//
// A REGRA (docs/SUSPENSAO_2026-11-05.md): quem e DONO do hardware declara o
// que esta usando, dentro do proprio liga/desliga - a camera ao ligar o
// sensor, o servidor web ao subir, o Mesh ao abrir a UART. O app (em C ou em
// Berry) nunca segura a suspensao: usar o servico ja basta, e o script que
// liga a camera aparece aqui porque a camera se declarou.
//
// O QUE A SUSPENSAO PRESERVA para cada recurso esta numa tabela so
// (POLITICA, em recursos.cpp): sono leve,
// receptor do Wi-Fi, o Wi-Fi contra o cochilo, a tela acesa. O kernel junta as
// politicas de tudo que esta em uso e aplica - os mecanismos de baixo
// (travas de PM, bits do Wi-Fi) sao detalhe dele.
//
// Antes eram sete mecanismos, cada um com a sua lista de bits, e consertos de
// hardware morando em app (o gancho de camera que faltou no navegador e
// derrubou o Tab5 ao acordar).
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
  RECURSO_CAMERA = 0,      // sensor, ISP e PPA rodando
  RECURSO_SOM,             // I2S tocando (a Musica)
  RECURSO_PROCESSAMENTO,   // conta pesada que nao pode andar a 40 MHz (o TLS de um comando)
  RECURSO_REDE,            // um servico atendendo a rede (servidor web)
  RECURSO_RADIO_ESCUTA,    // o receptor do Wi-Fi ligado (ESP-NOW conversando, CSI, farejo)
  RECURSO_SERIAL_GROVE,    // a UART do Grove (o no Meshtastic)
  RECURSO_USB_HOST,        // um aparelho no USB-A
  RECURSO_CARTAO,          // gravacao longa no cartao (copiar, mover)
  RECURSO_ENERGIA_EXTERNA, // na tomada: economizar nao compensa a resposta mais lenta
  RECURSO_TELA_ACESA,      // alguem precisa da tela acesa (o PC usando o cartao)
  RECURSO_N
} recurso_t;

// DECLARA ('usando' = true) ou SOLTA o uso de 'r' por 'quem' ("navegador",
// "Meshtastic", o nome do app). Por par (recurso, quem): chamar de novo com o
// mesmo valor nao faz nada - quem le um estado periodicamente pode repassa-lo
// sem contar. De qualquer tarefa, sem bloquear (as travas de PM e do Wi-Fi
// mudam na hora). O teto do clock no repouso e fixo, do kernel (core.cpp).
// 'quem' e copiado. false = sem vaga (RECURSOS_MAX usos ao mesmo tempo).
bool sistema_recurso(recurso_t r, const char* quem, bool usando);
static inline bool sistema_recurso_usar(recurso_t r, const char* quem)   { return sistema_recurso(r, quem, true); }
static inline void sistema_recurso_soltar(recurso_t r, const char* quem) { sistema_recurso(r, quem, false); }

// QUEM USA O QUE, agora (console 'energia quem', Tarefas, Berry
// sistema.recursos()). Devolve quantos.
#define RECURSOS_MAX 24
typedef struct {
  recurso_t r;
  char      quem[24];
  uint32_t  desde_ms;   // tab5_agora_ms de quando declarou
} recurso_uso_t;
int sistema_recursos(recurso_uso_t* out, int max);

const char* sistema_recurso_nome(recurso_t r);   // "camera", "som"...
// Alguem esta usando 'r' agora? (o pendrive pergunta se ha cabo.)
bool sistema_recurso_em_uso(recurso_t r);
// 'quem' esta usando 'r' agora? (o repouso pergunta se o app aberto usa a camera.)
bool sistema_recurso_de(recurso_t r, const char* quem);

// ---- para o kernel ----------------------------------------------------------
// Em uma linha, para o log do repouso: "camera(navegador) rede(servidor web)".
void recursos_resumo(char* out, size_t n);
