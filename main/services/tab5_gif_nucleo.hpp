// NUCLEO DO DECODIFICADOR DE GIF - so bytes e pixels, sem tela nem disco.
//
// Isolado de proposito: nao inclui LovyanGFX, ESP-IDF nem o cartao, e por isso
// roda NO MAC (testes/gif_teste.cpp). Quem usa e services/tab5_gif.cpp, que
// traz o arquivo para a memoria e desenha o resultado na tela.
//
// Entra o arquivo GIF inteiro (GIF87a/89a) numa area de memoria; sai, quadro a
// quadro, a "tela logica" em RGB565 (valor natural, 16 bits) ja composta com os
// quadros anteriores. Quem aloca e o chamador (alocar/liberar): no aparelho a
// PSRAM, no teste o malloc.
//
// Entende: tabela de cor global e local, LZW (codigos de 2 a 12 bits), quadro
// entrelacado, Graphic Control Extension (atraso, cor transparente e descarte
// 0/1 = mantem, 2 = fundo, 3 = restaura o anterior) e pula as outras extensoes.
// Arquivo estragado nunca le fora da area: cada indice e conferido.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define GIF_TELA_MAX 4096   // lado maximo da tela logica

typedef struct gif_nucleo gif_nucleo_t;

typedef void* (*gif_alocar_t)(size_t bytes);
typedef void  (*gif_liberar_t)(void* p);

// Le o cabecalho e prepara. 'dados' tem de continuar valido ate gif_n_fechar.
// nullptr = nao e GIF, tela maior que GIF_TELA_MAX, sem nenhum quadro ou sem memoria.
gif_nucleo_t* gif_n_abrir(const uint8_t* dados, size_t n, gif_alocar_t alocar, gif_liberar_t liberar);
void          gif_n_fechar(gif_nucleo_t* g);

int  gif_n_largura(const gif_nucleo_t* g);
int  gif_n_altura(const gif_nucleo_t* g);
bool gif_n_animado(const gif_nucleo_t* g);          // o arquivo tem mais de um quadro
int  gif_n_quadros_vistos(const gif_nucleo_t* g);   // quantos ja decodificou (conta as voltas)

// A tela logica (largura x altura, RGB565 natural, linha a linha de cima para baixo).
const uint16_t* gif_n_tela(const gif_nucleo_t* g);

// Decodifica o PROXIMO quadro por cima da tela logica. Devolve o atraso dele em
// ms (minimo 20; sem atraso no arquivo = 100) ou -1 em erro. No fim do arquivo
// volta ao primeiro quadro (laco).
int gif_n_proximo(gif_nucleo_t* g);
