// TERMINAL - a sessao de comandos do aparelho, para a tela (app Terminal) e
// para quem mais quiser (Berry, amanha o SSH).
//
// O QUE E. Os comandos SAO os do console serial (console.cpp): 'help', 'ls',
// 'cd', 'cat', 'ping', 'wifi', 'ble', 'berry', 'mem', 'tarefas'... uma tabela
// so, com a ajuda junto. O console ganhou a ideia de SESSAO (a pasta atual e o
// Ctrl+C de quem digita - console_sessao_t); este servico e a sessao da tela.
// Nada de comando duplicado: o que entrar no console aparece aqui, e vice-versa.
//
// COMO A SAIDA CHEGA AQUI. No ESP-IDF o stdout e de cada tarefa. A tarefa que
// roda os comandos troca o dela por um FILE* (funopen) que guarda o texto em
// linhas - os comandos continuam com o printf de sempre, sem saber.
//
// RODA POR TRAS. Cada linha roda numa PARTE DE FUNDO do sistema (kernel/
// fundo.h): fechar o app nao para o comando. O que passa de 3 s ganha a
// pastilha "Terminal" (com o comando) e o X da central interrompe; o curto
// nao pisca na barra. Sem comando, nao ha tarefa nenhuma: o fundo sai quando
// a fila seca e a vaga do pool volta a dormir.
//
// MEMORIA. As linhas (TERM_LINHAS x 128 B, ~51 KB) ficam na PSRAM e so
// existem depois da primeira abertura; ficam vivas com o app fechado (voltar
// mostra o que estava la). Fila, historico e estado: PSRAM tambem. Na RAM
// interna, so a trava e a pilha emprestada do pool enquanto um comando roda.
//
// QUEM CHAMA: qualquer tarefa - a tela (app Terminal) e o navegador do PC
// (services/tab5_terminal_web) mandam linhas para a MESMA sessao. O comando
// roda numa parte de fundo que o kernel liga na interface
// (sistema_fundo_pedir).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TERM_LINHA_MAX  126   // bytes de UMA linha guardada; mais longa continua na seguinte
#define TERM_LINHAS     400   // as que ficam na tela; a mais antiga sai
#define TERM_ENTRADA    192   // uma linha digitada (o limite do console)

typedef enum {
  TERM_SAIDA = 0,    // o que o comando escreveu
  TERM_COMANDO,      // a linha digitada, com o prompt
  TERM_AVISO,        // o proprio terminal ("^C", "na fila")
} term_tipo_t;

// Prepara a sessao (as linhas na PSRAM e a saudacao). false = sem memoria.
bool tab5_terminal_abrir(void);

// Roda a linha (ou poe na fila, se outro comando esta rodando). false = fila cheia.
bool tab5_terminal_enviar(const char* linha);
// Ctrl+C: o comando que roda para na proxima pergunta (console_interrompido).
void tab5_terminal_interromper(void);
bool tab5_terminal_ocupado(void);          // rodando ou com fila
void tab5_terminal_limpar(void);           // a tela vazia (o comando 'clear' faz o mesmo)

// LEITURA, para desenhar. versao muda a cada texto novo; i = 0 e a mais antiga.
uint32_t tab5_terminal_versao(void);
int      tab5_terminal_linhas(void);
// O NUMERO ABSOLUTO da linha i = 0 (as linhas sao numeradas desde o boot; a
// mais antiga que sai do anel e o 'clear' fazem este numero subir). Quem le de
// longe (o navegador) pede so o que passou do que ja tem.
uint32_t tab5_terminal_primeira(void);
// A linha de numero ABSOLUTO 'abs' (a mesma linha sempre, mesmo com o anel
// andando; so a ultima ainda cresce). -1 = ja saiu do anel ou ainda nao existe.
int      tab5_terminal_linha_abs(uint32_t abs, char* out, size_t n, term_tipo_t* tipo);
int      tab5_terminal_tamanho(int i);     // bytes da linha i (0 se nao ha)
int      tab5_terminal_linha(int i, char* out, size_t n, term_tipo_t* tipo);   // -1 = nao ha
void     tab5_terminal_prompt(char* out, size_t n);   // "/sdcard/Música $ ", ou a pergunta do comando
// O comando que roda esta lendo uma linha (console_ler_linha): o que se
// digitar e dele. 'oculto' = senha (a tela mascara).
bool     tab5_terminal_lendo(bool* oculto);
// O que foi digitado antes: k = 0 e o ultimo. false = nao ha tantos.
bool     tab5_terminal_historico(int k, char* out, size_t n);
