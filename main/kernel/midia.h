// TOCANDO AGORA - o que esta tocando, para o SISTEMA mostrar e controlar.
//
// A musica toca na parte de fundo de um app (kernel/fundo.h), com a janela
// fechada. Quem esta em outro app ainda precisa ver o que toca e pausar sem
// abrir o tocador: a barra mostra uma pastilha propria (nota + titulo), que
// convive com a pastilha de avisos, e a central de avisos (tocar a barra)
// ganha um miniplayer embaixo - anterior, tocar/pausar, proxima e a linha de
// progresso.
//
// Uma vaga so: um tocador de cada vez. Quem toca PUBLICA o estado (de
// qualquer tarefa) e registra os controles; o sistema so le e chama.
//
// Os controles rodam NA TAREFA DE INTERFACE: devem so marcar um pedido para a
// parte de fundo atender (sem esperar nada, sem I2C, sem cartao).
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define MIDIA_TITULO 64

typedef struct {
  void (*tocar_pausar)(void);
  void (*proxima)(void);
  void (*anterior)(void);
  void (*ir_para)(uint32_t ms);   // opcional (nullptr = a linha de progresso nao busca)
} midia_controles_t;

// Assume a vaga. 'fonte' = o nome da parte de fundo que toca (a central tira
// o cartao dela: o miniplayer ja e o cartao); 'app' = o app que tocar o
// titulo abre. Os dois precisam sobreviver (literal ou estatico).
void midia_assumir(const char* fonte, const char* app, const midia_controles_t* c);
// Solta a vaga (parou de tocar de vez): a pastilha e o miniplayer somem.
void midia_soltar(void);

// O estado atual. titulo nullptr = mantem o anterior.
void midia_publicar(const char* titulo, bool tocando, uint32_t pos_ms, uint32_t dur_ms);

typedef struct {
  bool        ativa;
  bool        tocando;
  bool        busca;      // tem ir_para
  const char* fonte;      // a parte de fundo que toca
  const char* app;        // o app que o miniplayer abre
  char        titulo[MIDIA_TITULO];
  uint32_t    pos_ms, dur_ms;
  uint16_t    cor;        // cor de destaque do tocador (RGB565); 0 = a do sistema (lilas)
} midia_info_t;

// A cor de destaque do tocador (o tema dele): a pastilha da barra e o
// miniplayer passam a usar esta. 0 = a do sistema.
void midia_definir_cor(uint16_t cor565);

bool     midia_ler(midia_info_t* out);   // false = nada tocando (out zerado)
// Muda quando titulo, tocando/pausado ou a vaga mudam - o que a BARRA mostra.
uint32_t midia_versao(void);
// Muda tambem a cada segundo de posicao - o que o MINIPLAYER mostra.
uint32_t midia_versao_posicao(void);

// Para quem desenha (a interface): repassa ao dono da vaga.
void midia_tocar_pausar(void);
void midia_proxima(void);
void midia_anterior(void);
void midia_ir_para(uint32_t ms);
