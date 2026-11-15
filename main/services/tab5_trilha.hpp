// TRILHA - o motor da trilha de GPS: grava a posicao no cartao a cada 5 s
// (com a janela aberta ou fechada), guarda o tracado para desenhar, lista os
// dias ja gravados e le um CSV de volta. O app Trilha so desenha e toca.
//
// Nasceu como o piloto de "multitarefa real" e e um cliente do padrao do
// sistema (kernel/fundo.h): este servico descreve o trabalho (TRILHA_FUNDO) e
// guarda o estado dele - quem cria a tarefa, mostra a pastilha, soma o perfil
// de energia e para com limpeza e o KERNEL. Liga e desliga:
// sistema_fundo_iniciar/parar(&TRILHA_FUNDO).
//
// Nao toma posse do Grove nem liga o GPS - so CONSOME tab5_gps_posicao()/
// tab5_gps_tempo(). Sem GPS ou sem fix, os pontos simplesmente nao saem.
// O cartao so e tocado pelas portas de hal/tab5_sd.hpp (gravar) e pelo leitor
// de services/tab5_volumes.hpp (ler).
//
// FORMATO DO CSV (um arquivo por dia: Trilha/trilha_NNNN.csv):
//   data_hora_utc,lat,lon,altitude_m,qualidade
//   AAAA-MM-DDTHH:MM:SSZ,-23.550000,-46.630000,760.0,1
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "fundo.h"

// A descricao que o kernel roda (ver kernel/fundo.h).
extern const fundo_def_t TRILHA_FUNDO;

// Pontos da trilha que esta gravando (numa trilha CONTINUADA, inclui os que
// ja estavam no arquivo).
uint32_t tab5_trilha_pontos(void);

// UMA TRILHA POR DIA (dia LOCAL, TAB5_FUSO_S). Antes de sistema_fundo_iniciar:
//   tab5_trilha_continuar(N) - o proximo inicio continua a trilha_N.csv (acrescenta
//                              no mesmo arquivo, e o tracado volta lido do cartao);
//   tab5_trilha_continuar(0) - o proximo inicio abre uma trilha nova (o padrao).
// Vale para UM inicio. Gravando, a meia-noite fecha a trilha do dia (ela
// fica no historico) e abre a seguinte sozinha - a gravacao nao para.
void     tab5_trilha_continuar(uint32_t numero);
// O numero da trilha gravando agora (0 = nao esta gravando).
uint32_t tab5_trilha_numero(void);

// CARIMBO DO CSV: "AAAA-MM-DDTHH:MM:SSZ" (UTC) no inicio da linha -> epoch UTC.
// false se a linha nao comeca com um carimbo (cabecalho, linha cortada).
bool     tab5_trilha_ler_carimbo(const char* linha, uint32_t* epoch_utc);
// O dia LOCAL de um instante UTC, como AAAAMMDD (0 = relogio sem acerto).
uint32_t tab5_trilha_dia_local(uint32_t epoch_utc);

// AVISO SONORO. Grava a preferencia e atualiza a fonte "Trilha" da
// notificacao (so ela - o Mesh continua tocando normal).
void tab5_trilha_definir_beep(bool tocar);
bool tab5_trilha_beep_ligado(void);

// -------------------------------------------------------------------------
// O "MAPA" - so a FORMA do caminho, para o app desenhar: pontos ligados sobre
// fundo vazio, sem mapa por baixo.
// -------------------------------------------------------------------------
// So RAM, nunca cartao - o CSV ja e o registro de verdade; isto e so para
// DESENHAR, pode perder tudo num reboot sem problema nenhum.
//
// TAB5_TRILHA_MAPA_MAX cabe SEM decimar por 5 s * 240 = 20 min de caminhada em
// resolucao cheia. Depois disso, a decimacao (abaixo): quando enche, fica com
// a METADE dos pontos e dobra o passo - a caminhada INTEIRA continua cabendo,
// so com menos detalhe no meio, em vez de "esquecer" o comeco.
#define TAB5_TRILHA_MAPA_MAX 240

typedef struct { float lat, lon; } tab5_trilha_ponto_t;

// Copia ate 'max' pontos do tracado DESTA sessao (do mais antigo pro mais
// novo) para 'out', devolve quantos copiou. Os pontos sao APROXIMADOS depois
// da primeira decimacao - servem para DESENHAR, nunca para navegacao ou para
// recalcular distancia com precisao.
uint32_t tab5_trilha_mapa(tab5_trilha_ponto_t* out, uint32_t max);

// DECIMACAO EM UMA PASSAGEM - UMA funcao para quem precisa caber uma serie
// longa num vetor curto: guarda 1 a cada 'passo' amostras; quando o vetor enche,
// descarta as posicoes impares (fica com a metade) e dobra o passo. O resultado
// cobre a serie INTEIRA, do primeiro ao ultimo ponto.
typedef struct {
  tab5_trilha_ponto_t* pts;   // o vetor (do chamador)
  uint32_t max;               // capacidade de pts
  uint32_t n;                 // pontos guardados
  uint32_t passo;             // 1 a cada 'passo' amostras entra
  uint32_t idx;               // amostras vistas
} tab5_trilha_decimador_t;
void tab5_trilha_decimar_iniciar(tab5_trilha_decimador_t* d, tab5_trilha_ponto_t* pts, uint32_t max);
void tab5_trilha_decimar_somar(tab5_trilha_decimador_t* d, float lat, float lon);

// -------------------------------------------------------------------------
// O HISTORICO - ler o que ja esta no cartao. Bloqueia (cartao): chamar do
// worker (sistema_trabalho), nunca do toque. 'cancelado' (pode ser nullptr) e
// perguntado de vez em quando; true = o resultado nao serve mais, pare.
// -------------------------------------------------------------------------
typedef bool (*tab5_trilha_cancelado_t)(void);

// "06/10 08:15" cabe em 14 bytes.
#define TAB5_TRILHA_QUANDO_TAM 14

// Os numeros das trilhas do cartao, do MAIOR (mais recente) para o menor, sem
// 'exceto_a' e 'exceto_b' (a que grava e a de hoje: ainda nao sao historico).
// 'quando' (pode ser nullptr; tam TAB5_TRILHA_QUANDO_TAM cada) recebe o inicio
// de cada trilha em hora local, so das 'com_data' mais recentes ("" = nao
// leu: abrir um arquivo por trilha seria segundos de cartao). Devolve quantas.
int tab5_trilha_listar(uint32_t* numeros, char (*quando)[TAB5_TRILHA_QUANDO_TAM], int max,
                       int com_data, uint32_t exceto_a, uint32_t exceto_b,
                       tab5_trilha_cancelado_t cancelado);

// Uma trilha lida para 'pts' (decimada em 'max'). 'primeiro'/'ultimo': epoch UTC
// do primeiro e do ultimo ponto (0 = sem). 'total' = linhas validas do CSV.
typedef struct {
  tab5_trilha_ponto_t* pts;
  uint32_t max;
  uint32_t n, total;
  uint32_t primeiro, ultimo;
} tab5_trilha_leitura_t;
void tab5_trilha_ler(uint32_t numero, tab5_trilha_leitura_t* r, tab5_trilha_cancelado_t cancelado);

// A trilha de HOJE que da para continuar: a mais nova do cartao, se o ultimo
// ponto dela e de hoje (dia local). Devolve o numero (0 = nenhuma) e, nesse
// caso, a deixa lida em 'r'.
uint32_t tab5_trilha_de_hoje(tab5_trilha_leitura_t* r, tab5_trilha_cancelado_t cancelado);
