// NOTAS - textos curtos, um .txt por nota na pasta do app no cartao
// (/sdcard/apps/Notas/nota_0001.txt).
//
// SO PELAS PORTAS DO SISTEMA: gravar e tab5_sd_substituir (troca inteira,
// mesma trava de todo mundo, diario com o cartao emprestado), ler e
// tab5_sd_abrir, apagar e tab5_sd_apagar, listar e tab5_arq_listar. Nada de
// fopen/remove/mkdir por fora.
//
// O NOME DO ARQUIVO E FIXO (numero); o TITULO e a primeira linha do texto e
// a PREVIA e a seguinte com texto, lidos na listagem (como nos apps de notas
// do celular). Mudar a primeira linha nao renomeia nada.
//
// Todas BLOQUEIAM (tocam no cartao): chamar do worker (sistema_trabalho),
// nunca da tarefa de interface.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NOTAS_APP       "Notas"
#define NOTA_MAX        4096   // bytes do texto, com o '\0'
#define NOTA_ARQ_MAX    24     // "nota_0001.txt", com o '\0'
#define NOTA_TITULO_MAX 48

typedef struct {
  char    arquivo[NOTA_ARQ_MAX];
  char    titulo[NOTA_TITULO_MAX];
  char    previa[NOTA_TITULO_MAX];   // a segunda linha com texto ("" = so o titulo)
  int64_t data;              // ultima mudanca (epoch), 0 = desconhecida
} nota_t;

// As notas, a mais nova primeiro. 'prox' recebe o arquivo para a proxima
// nota nova. -1 = sem cartao.
int  notas_listar(nota_t* out, int max, char prox[NOTA_ARQ_MAX]);
bool notas_ler(const char* arquivo, char* buf, int cap);
// Texto vazio apaga a nota.
bool notas_gravar(const char* arquivo, const char* texto);
bool notas_apagar(const char* arquivo);

// GRAVA EM FUNDO, SEM ESPERAR: guarda uma copia do texto e grava num trabalho
// solto (sistema_trabalho_solto) - quem chamou pode fechar e liberar o seu
// buffer antes de o cartao terminar. Pode ser chamada da interface.
// false = sem memoria ou sem vaga para o trabalho (nada foi gravado).
bool notas_gravar_em_fundo(const char* arquivo, const char* texto);
