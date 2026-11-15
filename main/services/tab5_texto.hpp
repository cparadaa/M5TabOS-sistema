// tab5_texto: abrir um texto/CSV grande (de MB) e ler um trecho de linhas.
//
// Serve a qualquer app que mostre texto paginado (hoje o Arquivos). O motor
// fica aqui; o app so pede e desenha.
//
//   tab5_texto_t* t = tab5_texto_abrir(caminho, sistema_trabalho_cancelado);
//   int total = tab5_texto_linhas(t);
//   int n = tab5_texto_ler(t, 1000, 48, &buf[0][0], 601);   // linhas 1000..1047
//   tab5_texto_fechar(t);
//
// COMO FUNCIONA: abrir percorre o arquivo UMA vez, conta as linhas e anota
// onde comeca cada trecho de TAB5_TEXTO_BLOCO linhas (o indice que faz ir ao
// fim ou a uma posicao da barra ser rapido). Ler salta pelo indice ate perto
// da linha pedida. Os dois leem pelo tab5_leitor (read POSIX rapido). O disco
// so fica seguro DURANTE cada chamada, nao entre elas.
//
// BLOQUEIA (le o disco): chamar fora da tarefa de interface, no worker.
// Um tab5_texto_t e de UMA tarefa por vez.
#pragma once
#include <stddef.h>

// De quantas em quantas linhas o indice guarda um ponto de salto.
#define TAB5_TEXTO_BLOCO 28

typedef struct tab5_texto tab5_texto_t;

// Abre e monta o indice. 'cancelado' (pode ser nullptr) e consultado de tempos
// em tempos: se der true, desiste. nullptr = nao abriu, foi cancelado ou sem memoria.
tab5_texto_t* tab5_texto_abrir(const char* caminho, bool (*cancelado)(void));

// Quantas linhas o arquivo tem (a ultima sem '\n' conta).
int tab5_texto_linhas(const tab5_texto_t* t);

// Le ate 'max' linhas a partir da linha 'inicio' (0 = a primeira) para
// 'saida', que tem 'max' linhas de 'larg' bytes cada (uma apos a outra, cada
// uma terminada em '\0'). O '\r' do fim sai; linha maior que 'larg' - 1 e
// cortada e termina em " ..."; arquivo .csv ganha um espaco depois de cada
// ',' ou ';'. Devolve quantas linhas leu (0 = alem do fim) ou -1 se o
// arquivo nao abriu.
int tab5_texto_ler(tab5_texto_t* t, int inicio, int max, char* saida, size_t larg);

void tab5_texto_fechar(tab5_texto_t* t);   // aceita nullptr
