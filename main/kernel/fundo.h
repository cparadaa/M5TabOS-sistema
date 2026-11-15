// PARTE DE FUNDO DE UM APP - trabalho que continua quando a janela fecha
// .
//
// -------------------------------------------------------------------------
// O PROBLEMA
// -------------------------------------------------------------------------
// Um app vive enquanto esta na tela: ao_fechar() libera tudo. Mas existe o app
// cujo TRABALHO nao e a tela - gravar a trilha do GPS, mandar a posicao pela
// internet, um calculo longo. A Trilha resolvia isso virando um servico feito
// a mao (services/tab5_trilha, com tarefa, notificacao e parada proprias). O
// segundo app assim teria que repetir tudo - e cada um repetiria diferente.
//
// -------------------------------------------------------------------------
// O MODELO
// -------------------------------------------------------------------------
// O app se divide em duas metades:
//   - a JANELA: o app de sempre (desenha, toca, nasce e morre com a tela);
//   - a PARTE DE FUNDO: um loop que o KERNEL roda numa tarefa DELE. O app so
//     descreve (fundo_def_t) e pede para ligar/desligar. Fechar a janela nao
//     para o fundo.
//
// O que o SISTEMA garante a toda parte de fundo, sem o app escrever nada:
//   - TAREFA propria, prioridade 1 (abaixo da interface, dos radios e do
//     worker) - o loop mais pesado nunca trava o toque. A excecao e o que
//     tem PRAZO de tempo real (o som): ver fundo_def_t.prioridade;
//   - VISIVEL SEMPRE: enquanto roda, uma notificacao NOTIF_ESTADO com o nome
//     dela (pastilha cinza na barra). Nada roda escondido do usuario - o
//     fundo de trabalho curto (aviso_ms) aparece so se passar do prazo;
//   - ENERGIA somada: o perfil declarado entra no perfil efetivo do aparelho
//     (junto do app da tela, ou da home) enquanto ela rodar - WiFi pedido
//     aqui fica de pe com qualquer app na tela e em repouso;
//   - PARADA LIMPA: pedir para parar ACORDA o loop (fundo_esperar volta na
//     hora) e o sistema espera ele sair antes de chamar ao_parar();
//   - QUEM PARA: a janela, o console ("fundo parar <nome>") e O USUARIO, pelo
//     X do cartao na central de avisos (tocar a barra). Ver o contrato abaixo.
//
// -------------------------------------------------------------------------
// O CONTRATO DA PARADA - o usuario manda
// -------------------------------------------------------------------------
// Toda parte de fundo aparece na central de avisos, na faixa "Em segundo
// plano", com um X. O X e do SISTEMA: nenhum app escreve nada para te-lo, e
// nenhum app pode recusar. Primeiro toque: "Parar?"; o segundo, em 3 s, para.
// Midia (kernel/midia.h) ganha o mesmo X no miniplayer, no lugar do cartao.
//
// O que o app garante, em troca:
//   - o loop sai logo depois do pedido (fundo_esperar/fundo_ceder voltam
//     false; espera longa sem eles atrasa a parada);
//   - TUDO que precisa sobreviver e salvo no ao_parar() (ou antes): ele roda
//     na interface depois que o loop saiu, sempre - inclusive quando quem
//     parou foi o usuario;
//   - parar no meio nao corrompe nada (arquivo fechado a cada escrita, ou
//     fechado no ao_parar). A janela pode estar fechada e nao e avisada.
// E o que o sistema garante: o pedido nao congela a tela (a parada do X pede
// e segue; quem recolhe e o kernel quando o loop sai), e a pastilha mostra
// "parando..." ate la. Passou de 3 s sem sair: a vaga fica "abandonada" e e
// limpa quando o loop por fim sair.
//
// Parte de fundo que pode ficar OCIOSA (musica pausada) deve se encerrar
// sozinha depois de um tempo - o tocador sai com 5 min de pausa. Esquecida,
// ela ocupa uma das FUNDO_MAX vagas e gasta energia sem ninguem ver.
//
// -------------------------------------------------------------------------
// O CONTRATO DO LOOP (corpo)
// -------------------------------------------------------------------------
//   static void meu_laco(fundo_t* f) {
//       while (fundo_esperar(f, 5000)) {       // dorme 5 s; false = pare
//           ...trabalho com esperas LIMITADAS...
//           fundo_estado(f, n, "42 envios");    // o texto da pastilha
//       }
//   }                                           // retornou: o sistema limpa
//
//   - NUNCA espere para sempre (portMAX_DELAY): o pedido de parada precisa
//     conseguir chegar. Espera de verdade e fundo_esperar().
//   - Loop de CALCULO (sem esperar nada) chama fundo_ceder(f) a cada volta:
//     sem isso a tarefa ocupa o core inteiro, o IDLE dele nunca roda, o
//     watchdog dispara e o aparelho nao dorme. fundo_ceder so cede de fato
//     a cada ~20 ms, entao pode ser chamado em loop apertado.
//   - O loop NAO desenha. O que a janela precisa mostrar, o fundo guarda em
//     estado proprio (protegido) e a janela le por copia - igual servico.
//   - Memoria do fundo e do fundo: estatica ou alocada no ao_iniciar() e
//     liberada no ao_parar(). NUNCA um buffer da janela - ela pode fechar a
//     qualquer momento.
//   - Retornar do loop sozinho (terminou o trabalho) e permitido: o sistema
//     percebe e limpa como numa parada pedida.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "tab5_power.hpp"
#include "notificacao.h"

#define FUNDO_MAX 4   // partes de fundo rodando ao mesmo tempo

typedef struct fundo_s fundo_t;
typedef void (*fundo_corpo_t)(fundo_t* f);

typedef struct {
  const char*   nome;       // literal: nome da tarefa (<=15), da notificacao e nas listas
  fundo_corpo_t corpo;
  uint32_t      pilha;      // bytes; 0 = 4096
  tab5_perfil_t perfil;     // o que precisa LIGADO enquanto roda (soma no perfil efetivo)
  // Opcionais. Rodam na tarefa de quem pediu (a interface, na pratica).
  bool (*ao_iniciar)(void); // prepara o estado; false = recusa ligar
  void (*ao_parar)(void);   // depois que o loop saiu: fecha e libera
  bool (*com_som)(void);    // preferencia de beep da notificacao; nullptr = com som
  // A PASTILHA SO DEPOIS DE aviso_ms RODANDO (0 = na hora). Para o fundo de
  // trabalho curto e frequente (um comando do Terminal): o que termina antes
  // nao pisca na barra; o que demora aparece, com o X, como qualquer fundo.
  uint32_t aviso_ms;
  // PRIORIDADE DA TAREFA (0 = a padrao, 1). So para quem tem PRAZO e trabalho
  // CURTO por volta: o som da Musica. Na 1 ele perdia a vez para a captura da
  // camera (3) e a interface (4), que juntas ocupam os dois nucleos, e o
  // alto-falante secava. Acima da interface so cabe um laco que faz uma
  // fracao do seu tempo e DORME (decodifica um bloco e espera a fila) - um
  // laco de calculo aqui trava o toque.
  uint32_t prioridade;
} fundo_def_t;

// ---- para a janela (e qualquer um) -----------------------------------------
bool sistema_fundo_iniciar(const fundo_def_t* d);   // false: ja rodando, sem vaga ou recusou
bool sistema_fundo_parar(const fundo_def_t* d);     // ESPERA o loop sair (ate 3 s)
bool sistema_fundo_ativo(const fundo_def_t* d);
// LIGAR DE QUALQUER TAREFA, sem esperar (o servidor web rodando um comando do
// Terminal): a interface liga na volta seguinte. Se o mesmo fundo ainda esta
// numa vaga (saindo), o pedido espera ele sair e liga de novo - e o que o
// laco que "sai quando a fila seca" precisa. Pedir de novo o que ja foi
// pedido e o mesmo pedido. Da interface, liga na hora se a vaga estiver livre.
// false = sem vaga para o pedido (FUNDO_MAX pedidos ja esperando).
bool sistema_fundo_pedir(const fundo_def_t* d);

typedef struct {
  const char* nome;
  uint32_t    ha_s;                  // rodando ha quantos segundos
  char        resumo[NOTIF_RESUMO];  // o ultimo fundo_estado()
} fundo_info_t;
int  sistema_fundo_listar(fundo_info_t* out, int max);
// Pede a parada pelo NOME, de qualquer tarefa, SEM ESPERAR (o X da central e
// o console usam esta): o kernel para de verdade no proprio laco.
bool sistema_fundo_parar_nome(const char* nome);

// ---- para o loop ----------------------------------------------------------
bool fundo_esperar(fundo_t* f, uint32_t ms);   // dorme ate ms; false = pediram para parar
bool fundo_ceder(fundo_t* f);                  // loop de calculo; false = pediram para parar
bool fundo_rodando(const fundo_t* f);
void fundo_estado(fundo_t* f, int quantidade, const char* resumo);

// ---- para o kernel --------------------------------------------------------
void fundo_somar_perfil(tab5_perfil_t& p);   // chamado por sistema_impor_preferencias()
void fundo_manter(void);                     // no loop da interface: limpa quem terminou sozinho,
                                             // reaplica o perfil quando o conjunto mudou
