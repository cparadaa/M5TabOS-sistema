// EDITOR - texto de varias linhas, com cursor, para quem escreve mais que uma
// mensagem (o app Notas). Quebra por palavra e por '\n', rola com o dedo,
// teclado virtual do sistema (e o fisico do ExtPort1), colar da area de
// transferencia.
//
// O TEXTO E DO APP: ele passa o buffer (PSRAM) e o tamanho; o editor so mexe
// dentro dele. As posicoes de inicio de linha moram na PSRAM e so existem com
// o editor aberto.
//
// COMO USAR (ver app_notas/app_notas.cpp):
//   abrir:        editor_abrir(&s_ed, buf, cap);        (o texto ja no buf)
//   fechar:       editor_fechar(&s_ed);                 (tambem no ao_fechar)
//   desenhar:     editor_desenhar(&s_ed, d, retangulo, completo);
//   ao_tocar:     editor_tocar(&s_ed, x, y);
//   nivel:        editor_tocar_nivel(&s_ed, x, y, pressionado);
//   ao_teclar:    editor_teclar(&s_ed, texto, codigo, mod);
//
// MODO CODIGO (o Editor de Berry): editor_modo_codigo() depois de abrir -
// fonte monoespacada, numero de linha na margem, cores da sintaxe do Berry,
// Enter mantem o recuo, Tab = 2 espacos; editor_marcar_erro() pinta a linha
// do erro e leva o cursor ate ela (qualquer mudanca no texto apaga a marca).
//
// Segurar o dedo no texto abre o menu ali mesmo: copiar a PALAVRA sob o dedo,
// a LINHA (o paragrafo ate a quebra), TUDO, ou Colar; Ctrl+V cola.
#pragma once

#include "app.h"
#include "teclado_virtual.hpp"
#include "toque_longo.hpp"

typedef struct {
  char*     txt;          // do app
  int       cap;          // bytes do buffer, com o '\0'
  int       cursor;       // em bytes, sempre numa fronteira de caractere
  bool      mudou;        // o texto mudou desde abrir (o app decide gravar)

  // Estado interno - o app nao mexe.
  int16_t*  ini;          // inicio de cada linha; ini[n_lin] = fim (PSRAM, cap + 1)
  int       n_lin, topo, larg, passo, linhas_vis;
  bool      requebrar, sujo;
  area_t    ret;          // o ultimo retangulo (tela)
  int       alt_texto;    // a parte dele que e texto (o resto e do teclado)
  tecvirt_t kb;
  toque_longo_t lp;
  int       arr_y, arr_topo;
  bool      arrastando;
  bool      menu;         // o menu do toque longo esta aberto, em (menu_x, menu_y) - tela
  int       menu_x, menu_y;
  int       menu_pos;     // o byte do texto sob o dedo quando o menu abriu
  bool      codigo;       // modo codigo (ver acima)
  int       calha;        // modo codigo: px da coluna dos numeros (cresce com os digitos)
  int       linha_erro;   // linha (1 = a primeira) marcada como erro; 0 = nenhuma
  lgfx::LovyanGFX* disp;  // para medir texto no toque (so a tarefa de interface)
} editor_t;

bool editor_abrir(editor_t* e, char* buf, int cap);
void editor_fechar(editor_t* e);
void editor_desenhar(editor_t* e, lgfx::LovyanGFX& d, const area_t& r, bool completo);
void editor_tocar(editor_t* e, int x, int y);
void editor_tocar_nivel(editor_t* e, int x, int y, bool pressionado);
void editor_teclar(editor_t* e, const char* texto, uint8_t codigo, uint8_t mod);
void editor_colar(editor_t* e);
void editor_fechar_teclado(editor_t* e);
void editor_modo_codigo(editor_t* e);
void editor_marcar_erro(editor_t* e, int linha);
