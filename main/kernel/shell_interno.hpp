// O que os arquivos do shell compartilham: a geometria usada por mais de um
// deles, o estado (declarado aqui, definido no arquivo indicado) e as funcoes
// internas que cruzam arquivos. So para eles: nenhum app inclui este header.
// A ponte core <-> shell continua sendo o kernel/internal.hpp (incluido por
// este); app fala com o sistema so por app.h.
//
// O shell foi partido por assunto (so mudou de arquivo, nada de logica):
//   shell.cpp           geometria do toque, correcao vertical, diagnostico, gestos, tratar_toque
//   shell_barra.cpp     icone de bateria, barra de status, hora, faixa de baixo da home, botao da navegacao
//   shell_home.cpp      paginas da home, ordem dos icones, mover/remover icone, desenho da home
//   shell_bloqueio.cpp  tela de bloqueio
//   shell_avisos.cpp    central de avisos e miniplayer
#pragma once

#include "internal.hpp"
#include "tab5_tema.hpp"

// ---------------------------------------------------------------------------
// Geometria usada por mais de um arquivo
// ---------------------------------------------------------------------------
// GEOMETRIA DO BOTAO DA FAIXA (a pilula na faixa de navegacao) - NUM SO
// LUGAR, porque desenhar_barra_nav() PINTA neste retangulo
// e tratar_toque() agora PRECISA saber o mesmo retangulo para so fechar o
// app quando o toque cai dentro dele (antes, qualquer toque na faixa
// inteira de 96px - a LARGURA TODA da tela - fechava o app; so o Y era
// checado). Duas constantes divergentes aqui voltariam a abrir esse buraco.
static const int NAV_BTN_L  = 160;               // largura da pilula (era 180, junto com a faixa mais baixa)
static const int NAV_BTN_A  = ALTURA_NAV - 32;   // altura da pilula
static const int NAV_BTN_FOLGA = 48;            // toque alem da pilula, de cada lado (ver tratar_toque)
static const int NAV_BTN_Y0 = 16;                // deslocamento do topo da faixa ate o topo da pilula

static const int ICONE_LADO   = TEMA_ICONE_APP;
// Colunas da grade: 4 em pe; deitada (o teclado fisico encaixado deita a home,
// kernel/orientacao) cabem 8 com 2 linhas nos 1280 px.
static inline int icone_cols(const area_t& a) { return a.largura > a.altura ? 8 : 4; }


// ---------------------------------------------------------------------------
// Estado usado por mais de um arquivo (cada um definido no arquivo indicado)
// ---------------------------------------------------------------------------
// shell_home.cpp:
extern const app_t* s_remover_app;   // esperando o "Remover" da confirmacao

// ---------------------------------------------------------------------------
// Funcoes que cruzam arquivos
// ---------------------------------------------------------------------------
// shell_barra.cpp:
uint16_t cor_do_nivel(notif_nivel_t n);   // a cor de cada gravidade: barra, central e bloqueio
void hora_agora(char* out, size_t n);     // a hora da barra de cima (e da tela de bloqueio)

// shell_home.cpp:
bool home_mudar_pagina(int delta);
// Repinta so o cartao do relogio da home quando o minuto (ou o Wi-Fi/Mesh)
// mudou. O loop principal chama no mesmo tique da barra de status; sem a home
// visivel nao faz nada.
void home_cartao_atualizar(void);
int home_pos_em(const area_t& a, int tx, int ty, bool folga);
void home_remover_tocar(int tx, int ty);

// shell_avisos.cpp:
bool painel_avisos_rolar(int y0, int dy);
void painel_avisos_tocar(const area_t& a, int tx, int ty);
