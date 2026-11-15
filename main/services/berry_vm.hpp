// A TAREFA DO BERRY - onde TODO codigo Berry roda (console e apps).
//
// UMA tarefa, com 32 KB de pilha NA PSRAM. O compilador e a maquina do Berry
// sao recursivos e o pcall guarda um jmp_buf: os 8 KB do console (e os da
// interface) nao bastam - chamar uma funcao definida antes parava o aparelho.
// A pilha na PSRAM nao gasta a RAM interna; o preco e que esta tarefa nao pode
// gravar na flash (NVS): o cache desliga durante a gravacao.
//
// O MODELO: quem precisa rodar Berry entrega um trabalho e ESPERA ele
// terminar (berry_rodar). Os pedidos sao enfileirados um de cada vez, entao
// uma maquina (bvm) nunca e usada por duas tarefas ao mesmo tempo. A
// interface tambem pede assim - e, enquanto espera, nao desenha: o desenho
// que o script faz pela tarefa do Berry nao concorre com ninguem.
//
// PRIORIDADE: parada, a tarefa fica na 1; durante um pedido ela HERDA a de
// quem pediu (a interface e 4) - quem espera por ela nao fica atras do resto.
// Quem chega com prioridade maior enquanto outro trabalho roda (a interface
// esperando um fundo() acabar) tambem empurra: o trabalho em curso termina
// na prioridade de quem espera.
//
// LIMITE DE TEMPO: cada trabalho tem um prazo. Passou, a maquina para o
// script com o erro "tempo_esgotado" (o gancho e chamado a cada ~1 milhao de
// instrucoes) e o pcall de quem chamou recebe o erro. O script NAO consegue
// pegar esse erro com try/except. Um laco infinito num app nao trava a tela.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

extern "C" {
#include "berry.h"
}

typedef void (*berry_trabalho_t)(void* ctx);

// Roda fn(ctx) na tarefa do Berry e espera terminar. 'prazo_ms' = tempo
// maximo de Berry rodando dentro dele (0 = sem limite). false = a tarefa nao
// subiu. De qualquer tarefa, menos a do proprio Berry.
bool berry_rodar(berry_trabalho_t fn, void* ctx, uint32_t prazo_ms);

// Maquina nova, ja com o gancho de tempo e os modulos do sistema (tela,
// sistema). So de dentro de um trabalho. nullptr = o orcamento de memoria do
// Berry (berry_mem.h) esta cheio.
bvm* berry_vm_nova(void);

// Passou do prazo do trabalho em curso (berry_rodar): encerra o script com
// tempo_esgotado (nao volta). Chamada pelo gancho da maquina e pelas funcoes
// do sistema que podem demorar (tela, ver so_janela em berry_interno.hpp).
void berry_prazo_conferir(bvm* vm);

// Compila e roda 'codigo' (ou o arquivo, se 'arquivo') em vm. Com 'mostrar',
// imprime o valor de uma expressao. Erro: impresso e devolvido em 'erro'.
// So de dentro de um trabalho.
bool berry_executar(bvm* vm, const char* codigo, bool arquivo, bool mostrar,
                    char* erro, size_t erro_tam);

// CHAMAR UMA FUNCAO DO SCRIPT, em dois passos (o Berry quer a funcao na
// pilha antes dos argumentos):
//   if (berry_funcao(vm, "tocar")) {          // empilhou a funcao
//     be_pushint(vm, x); be_pushint(vm, y);   // os argumentos
//     berry_chamar(vm, 2, erro, sizeof erro);
//   }
// berry_funcao: false = o script nao definiu (nada empilhado).
// berry_chamar: false = deu erro (impresso e copiado em 'erro'). Desempilha tudo.
bool berry_funcao(bvm* vm, const char* nome);

// VERIFICAR SEM RODAR (o Editor): compila 'codigo' numa maquina nova, so para
// achar erro de sintaxe, e apaga a maquina. false = tem erro: a mensagem em
// 'erro' e a linha (1 = a primeira) em 'linha' (0 = nao disse). Bloqueia ate
// a tarefa do Berry responder: fora da interface.
bool berry_verificar(const char* codigo, size_t n, char* erro, size_t erro_tam, int* linha);
bool berry_chamar(bvm* vm, int argc, char* erro, size_t erro_tam);

// Pilha livre da tarefa, em bytes (diagnostico).
uint32_t berry_pilha_livre(void);

// MEDIDA DA ENTREGA: do pedido ate a tarefa do Berry comecar a rodar (troca de
// tarefa). Deve ficar em dezenas de us; se subir, alguem esta na frente dela.
typedef struct {
  uint32_t entregas;
  uint64_t espera_soma_us;
  uint32_t espera_max_us;
} berry_medida_t;
void berry_medida(berry_medida_t* m, bool zerar);
