// OS SCRIPTS BERRY NO CARTAO - listar, ler e gravar os .be de apps/Berry.
//
// Quem edita script (o Editor do aparelho, o editor web em berry_web) passa
// por aqui: a mesma pasta, o mesmo limite de tamanho, o mesmo nome valido.
// Tudo pelas portas do tab5_sd (montar/abrir para ler, substituir para gravar).
//
// BLOQUEIAM (cartao): nunca da tarefa de interface - do worker
// (sistema_trabalho) ou de uma tarefa de servico (o servidor web).
#pragma once

#include <stddef.h>
#include "tab5_arquivos.hpp"

// O tamanho maximo de um script EDITAVEL, com o '\0'. O editor do aparelho
// guarda o inicio das linhas em int16 (ate 32767) - o web tem o mesmo teto.
// O app em si roda com qualquer tamanho: o limite e so de quem edita.
#define BERRY_SCRIPT_MAX 32767

// Os .be da pasta (so arquivos, sem as pastas de dados dos apps), por nome.
// Devolve quantos (ate 'max'); -1 = sem cartao.
int berry_scripts_listar(tab5_arq_item_t* out, int max);

// Le 'arq' INTEIRO em 'buf' (com '\0'). NUNCA CORTA: um arquivo maior que
// tam - 1 devolve BERRY_LER_GRANDE e buf vazio - quem edita e grava de volta
// um script cortado estraga o app (foi o que aconteceu com o Mostruario).
typedef enum { BERRY_LER_OK = 0, BERRY_LER_FALHOU, BERRY_LER_GRANDE } berry_ler_t;
berry_ler_t berry_script_ler(const char* arq, char* buf, size_t tam);

// Grava 'arq' inteiro (tab5_sd_substituir: nunca meio arquivo; com o cartao
// emprestado ao computador, vai para o diario). false = nao gravou.
bool berry_script_gravar(const char* arq, const char* txt, size_t n);

// Nome de arquivo aceitavel para um script: termina em .be, sem barra, sem
// comecar por ponto, so letras, numeros, espaco, '_', '-' e '.'. Para quem
// recebe o nome de fora (o navegador).
bool berry_script_nome_valido(const char* arq);

// O nome do app de um script: o '# nome:' do cabecalho, ou o do arquivo sem o
// .be. Nao bloqueia (so le o texto).
void berry_script_nome_app(const char* txt, const char* arq, char* out, size_t n);
