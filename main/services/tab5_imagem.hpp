// IMAGEM - abrir e desenhar uma imagem de qualquer disco, de qualquer tamanho.
//
// Quem decodifica e este servico; o app so da a caixa onde a imagem tem de
// caber. Formatos (reconhecidos pelo CONTEUDO, nao pela extensao): JPEG
// (baseline; o progressivo nao e suportado), PNG, BMP, QOI e GIF.
//
// SEM LIMITE DE TAMANHO DO ARQUIVO. O arquivo e lido aos poucos pelo leitor
// rapido (tab5_leitor, blocos de 64 KB) e decodificado enquanto chega; a
// memoria gasta e a do decodificador, nao a do arquivo. A excecao e o GIF, que
// vem inteiro para a PSRAM (ate 16 MB, ver tab5_gif.hpp).
//
// Tudo aqui BLOQUEIA (disco + decodificacao): rode no worker (sistema_trabalho),
// nunca na tarefa de interface.
#pragma once

#include "app.h"   // lgfx::LovyanGFX

// Extensoes que o sistema abre como imagem: .jpg .jpeg .png .bmp .qoi .gif
bool tab5_imagem_suportada(const char* nome);

// Desenha a imagem INTEIRA dentro de 'destino' (um canvas/sprite ja alocado):
// na maior escala que cabe, so REDUZ (nunca amplia), centralizada, fundo preto.
// 'cancelado' (pode ser nullptr) e perguntado durante a leitura; devolvendo
// true, para e devolve false. false tambem se nao leu/reconheceu a imagem.
// Um GIF mostra o primeiro quadro (para animar, ver tab5_gif.hpp).
bool tab5_imagem_desenhar(const char* caminho, lgfx::LovyanGFX* destino, bool (*cancelado)(void));

// Largura e altura reais, lendo so o cabecalho. false se nao reconheceu.
bool tab5_imagem_tamanho(const char* caminho, int* largura, int* altura);

// O tamanho de uma imagem l x a reduzida para caber em L x A (mesma proporcao;
// nunca maior que o original). Usado tambem pelo GIF.
void tab5_imagem_caber(int l, int a, int L, int A, int* saida_l, int* saida_a);
