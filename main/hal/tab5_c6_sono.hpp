// O C6 DORMINDO COM O WI-FI ASSOCIADO - o lado do P4.
//
// Com o enlace SDIO de pe o C6 nao pode entrar em light sleep, e o Wi-Fi
// conectado custa ~30 mA na bateria com a tela apagada. O firmware do C6
// (c6/, a partir do c6-5) sabe dormir associado: o P4 avisa (POWER_SAVE_ON
// pela interrupcao do SDIO), os dois lados desmontam o enlace, e o C6 dorme
// ouvindo o roteador de tempos em tempos.
//
// VOLTAR, DOS DOIS LADOS:
//   - o C6 tem algo para o P4 (uma mensagem do MQTT, um ESP-NOW): ele remonta
//     o escravo e pulsa o IO2, que chega aqui no G14;
//   - o P4 precisa da rede (tela acesa, keepalive): nao ha fio para o C6, entao
//     o P4 baixa a linha CMD do SDIO (G13) por 2 ms - parada, ela e o despertar
//     do C6 - e espera o mesmo pulso no G14.
// Nos dois casos o P4 remonta o lado dele SEM resetar o C6 (o reset derrubaria
// a associacao): o esp_hosted so faz isso quando acha que o host voltou de um
// deep sleep, e a remontagem aqui o convence disso por um desvio no link
// (__wrap_eh_host_port_wakeup_reason_get, main/CMakeLists.txt).
//
// SOB AJUSTE: o modo automatico (abaixo) e desligado de fabrica; o console
// tem 'c6 sono auto ligar|desligar' e os comandos manuais da prova.
// Com o enlace desmontado, o que o P4 manda pela rede e descartado, e o
// Bluetooth precisa estar desligado (a pilha NimBLE do P4 fala com o C6 pelo
// mesmo enlace).
//
// De qualquer tarefa com a pilha na RAM interna, menos a de interface (a
// remontagem leva centenas de ms).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef enum {
  TAB5_C6_SONO_ACORDADO = 0,   // enlace de pe (o normal)
  TAB5_C6_SONO_DORMINDO,       // enlace desmontado, o C6 pode dormir
  TAB5_C6_SONO_FALHOU,         // a remontagem nao fechou: o Wi-Fi precisa ser religado
} tab5_c6_sono_t;

typedef struct {
  tab5_c6_sono_t estado;
  uint32_t entradas;           // vezes que o enlace foi desmontado
  uint32_t pelo_p4;            // vezes que o P4 o trouxe de volta (CMD)
  uint32_t pelo_c6;            // vezes que o C6 chamou (trafego para o P4)
  uint32_t falhas;
  uint32_t ultimo_religar_ms;  // quanto levou a ultima remontagem
  uint32_t dormindo_ms;        // tempo somado com o enlace desmontado
} tab5_c6_sono_estado_t;

// Desmonta o enlace e deixa o C6 dormir. false = nao deu, e 'motivo' diz por
// que (sem enlace, Bluetooth ligado, firmware do C6 sem a funcao...).
bool tab5_c6_sono_dormir(char* motivo, size_t tam);

// Traz o enlace de volta (se estiver desmontado). false = nao voltou.
bool tab5_c6_sono_acordar(void);

// O MODO AUTOMATICO (ajustes_c6_sono): o enlace desmonta sozinho com a tela
// apagada e quieto por 3 s, e volta com a tela acesa, com trafego do C6 ou com
// o P4 transmitindo. iniciar(): no boot, sobe a tarefa se o ajuste estiver ligado.
void tab5_c6_sono_iniciar(void);
void tab5_c6_sono_automatico(bool ligar);
void tab5_c6_sono_estado(tab5_c6_sono_estado_t* out);
void tab5_c6_sono_rx(uint32_t* arp, uint32_t* icmp, uint32_t* outro, uint32_t* falha);   // depuracao da prova
