// TEMA CENTRAL DO SISTEMA - as cores, cantos, espacos e tamanhos de letra
// de TODA a interface, num lugar so.
//
// -------------------------------------------------------------------------
// POR QUE ISTO EXISTE
// -------------------------------------------------------------------------
// Ate aqui cada app escrevia a propria cor na mao (0x18E3, TFT_LIGHTGREY,
// color565(64,64,78)...): mais de 1.300 linhas espalhadas. Mudar a cara do
// sistema exigia visitar cada uma. Com o tema, o app pede pelo PAPEL da cor
// ("o fundo", "o texto apagado", "o destaque") e nao pelo numero; trocar a
// paleta (ou ganhar um modo claro um dia) vira mexer so aqui.
//
// A maquete visual da repaginacao usa exatamente estes valores (o link
// esta na entrada de 2026-11-15 do docs/PASSAGEM.md).
//
// Por enquanto NADA no sistema usa este arquivo: a troca acontece aos
// poucos, tela por tela, depois que a maquete for aprovada.
//
// -------------------------------------------------------------------------
// COMO USAR
// -------------------------------------------------------------------------
//   #include "tab5_tema.hpp"
//   d.fillScreen(TEMA_FUNDO);
//   d.fillRoundRect(x, y, l, a, TEMA_RAIO_M, TEMA_SUPERFICIE);
//   d.setTextColor(TEMA_TEXTO_2);
//
// No Berry, as mesmas cores moram no modulo 'tema' (tema.FUNDO, tema.TEXTO...).
//
// -------------------------------------------------------------------------
// SOBRE AS CORES (RGB565)
// -------------------------------------------------------------------------
// A tela do Tab5 trabalha com 16 bits por ponto: 5 bits de vermelho, 6 de
// verde e 5 de azul. Um cinza "puro" escrito em 24 bits costuma sair
// esverdeado depois do arredondamento (o verde tem um bit a mais). Por isso
// os tons escuros abaixo foram escolhidos JA em 565, conferindo a cor final;
// o comentario de cada um mostra como ela aparece de fato na tela.
#pragma once

#include <stdint.h>

// Monta uma cor 565 a partir dos tres canais ja reduzidos (r 0..31, g 0..63,
// b 0..31). Usada so para os tons escuros escolhidos a dedo.
constexpr uint16_t tema_565(uint8_t r5, uint8_t g6, uint8_t b5)
{
  return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

// Monta uma cor 565 a partir de 0xRRGGBB (24 bits), arredondando para baixo.
constexpr uint16_t tema_hex(uint32_t rgb)
{
  return (uint16_t)((((rgb >> 16) & 0xF8) << 8) | (((rgb >> 8) & 0xFC) << 3) | ((rgb & 0xFF) >> 3));
}

// -------------------------------------------------------------------------
// SUPERFICIES - do fundo para frente (cada camada um tom mais clara)
// -------------------------------------------------------------------------
#define TEMA_FUNDO            tema_565(1, 3, 3)      // #080C18 quase preto azulado: a tela
#define TEMA_SUPERFICIE       tema_565(2, 6, 5)      // #101829 cartao, linha de lista, barra
#define TEMA_SUPERFICIE_ALTA  tema_565(4, 10, 7)     // #202839 cartao em cima de cartao, tecla
#define TEMA_LINHA            tema_565(5, 13, 9)     // #29344A divisoria e borda fina

// -------------------------------------------------------------------------
// TEXTO - tres niveis de importancia
// -------------------------------------------------------------------------
#define TEMA_TEXTO            tema_hex(0xF2F5FA)     // principal
#define TEMA_TEXTO_2          tema_hex(0x9AA5B8)     // secundario: legenda, valor de apoio
#define TEMA_TEXTO_3          tema_hex(0x5D687A)     // desativado, dica

// -------------------------------------------------------------------------
// DESTAQUE - a UNICA cor forte do sistema (botao principal, selecao, ligado)
// -------------------------------------------------------------------------
#define TEMA_DESTAQUE         tema_hex(0x5B8CFF)     // azul
#define TEMA_DESTAQUE_SUAVE   tema_hex(0x1F2F55)     // fundo de item selecionado
#define TEMA_SOBRE_DESTAQUE   tema_hex(0xFFFFFF)     // texto em cima do destaque

// -------------------------------------------------------------------------
// ESTADOS - so para comunicar algo (nunca como enfeite)
// -------------------------------------------------------------------------
#define TEMA_SUCESSO          tema_hex(0x34C77B)     // verde: ok, conectado, carregando
#define TEMA_ALERTA           tema_hex(0xF5B83D)     // amarelo: atencao, bateria baixa
#define TEMA_PERIGO           tema_hex(0xFF5C5C)     // vermelho: erro, apagar
#define TEMA_INFO             tema_hex(0x39B8E6)     // ciano: aviso neutro

// -------------------------------------------------------------------------
// CORES DE APP - o fundo do icone na tela inicial (app_t.cor)
// -------------------------------------------------------------------------
// Tons de mesma luminosidade, para nenhum icone "gritar" mais que o outro.
#define TEMA_APP_AZUL         tema_hex(0x4C7DFF)
#define TEMA_APP_CIANO        tema_hex(0x22B5D6)
#define TEMA_APP_VERDE        tema_hex(0x2FBF71)
#define TEMA_APP_LIMA         tema_hex(0x9BCB3C)
#define TEMA_APP_AMARELO      tema_hex(0xF2B33D)
#define TEMA_APP_LARANJA      tema_hex(0xF27B3D)
#define TEMA_APP_VERMELHO     tema_hex(0xEF5350)
#define TEMA_APP_ROSA         tema_hex(0xE8608F)
#define TEMA_APP_ROXO         tema_hex(0x9B6BF2)
#define TEMA_APP_GRAFITE      tema_565(12, 27, 16)   // #626D83 apps de sistema

// -------------------------------------------------------------------------
// CANTOS ARREDONDADOS (raio em px)
// -------------------------------------------------------------------------
#define TEMA_RAIO_P   8     // etiqueta, campo pequeno
#define TEMA_RAIO_M   16    // botao, linha de lista, tecla
#define TEMA_RAIO_G   24    // cartao, painel
#define TEMA_RAIO_ICONE 28  // icone de app (quadrado de 128 px)

// -------------------------------------------------------------------------
// ESPACOS (margem e distancia entre elementos, em px) - sempre multiplo de 4
// -------------------------------------------------------------------------
#define TEMA_ESPACO_1   4
#define TEMA_ESPACO_2   8
#define TEMA_ESPACO_3   12
#define TEMA_ESPACO_4   16
#define TEMA_ESPACO_5   24
#define TEMA_ESPACO_6   32
#define TEMA_MARGEM     TEMA_ESPACO_5   // distancia da borda da tela ao conteudo

// -------------------------------------------------------------------------
// TAMANHOS DE LETRA (altura da linha em px, para tab5_fonte_suave/tab5_titulo)
// -------------------------------------------------------------------------
// Todos sao alturas NATIVAS da Roboto suavizada (ferramentas/gerar_fontes.py):
// nunca escalam.
#define TEMA_LETRA_LEGENDA     20   // rotulo pequeno, hora na lista
#define TEMA_LETRA_CORPO       28   // texto comum
#define TEMA_LETRA_SUBTITULO   32   // nome de item, cabecalho de cartao
#define TEMA_LETRA_TITULO      40   // titulo da pagina
#define TEMA_LETRA_GRANDE      56   // numero de destaque (relogio, %)

// -------------------------------------------------------------------------
// TAMANHOS DE TOQUE (px) - o dedo precisa de alvo grande
// -------------------------------------------------------------------------
#define TEMA_TOQUE_MIN       72    // menor altura de botao ou linha tocavel
#define TEMA_ICONE_APP       128   // lado do icone de app na tela inicial
