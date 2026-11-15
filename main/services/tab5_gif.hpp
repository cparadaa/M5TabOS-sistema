// GIF (animado ou nao) na tela. O decodificador em si e o tab5_gif_nucleo (so
// bytes e pixels, testado no Mac); aqui entram o arquivo e o desenho.
//
// O arquivo vem INTEIRO para a PSRAM (ate 16 MB): o GIF decodifica quadro a
// quadro e volta ao primeiro, entao ler do cartao a cada quadro seria lento e
// manteria o disco preso. Depois de aberto o disco ja esta solto.
//
//   tab5_gif_t* g = tab5_gif_abrir("/sdcard/gato.gif");     // worker
//   int atraso = tab5_gif_proximo(g, sprite);               // worker; desenha um quadro
//   ...espera 'atraso' ms e repete...
//   tab5_gif_fechar(g);
//
// Bloqueia (disco e decodificacao): rode no worker (sistema_trabalho), nunca
// na interface. Fechar so libera memoria e pode ser feito de qualquer tarefa.
#pragma once

#include "app.h"   // lgfx::LovyanGFX

typedef struct tab5_gif tab5_gif_t;

// nullptr = nao abriu (nao e GIF, maior que 16 MB, tela logica maior que
// 4096x4096, sem memoria ou sem disco).
tab5_gif_t* tab5_gif_abrir(const char* caminho);

int  tab5_gif_largura(const tab5_gif_t* g);   // da tela logica
int  tab5_gif_altura(const tab5_gif_t* g);
int  tab5_gif_quadros_vistos(const tab5_gif_t* g);   // quantos ja decodificou
bool tab5_gif_animado(const tab5_gif_t* g);          // mais de um quadro

// Decodifica o PROXIMO quadro sobre a tela logica (compondo com o anterior) e
// desenha a tela logica inteira em 'destino', reduzida para caber (nunca
// ampliada), centralizada, fundo preto. O fundo preto e pintado na primeira
// chamada: use um destino so deste GIF. Devolve o atraso do quadro em ms
// (minimo 20) ou -1 em erro. No fim do arquivo volta ao primeiro quadro.
int tab5_gif_proximo(tab5_gif_t* g, lgfx::LovyanGFX* destino);

void tab5_gif_fechar(tab5_gif_t* g);
