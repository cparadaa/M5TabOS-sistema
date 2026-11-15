// O que os arquivos do app Ajustes compartilham: as medidas e as funcoes
// pequenas usadas por mais de um deles, o estado (declarado aqui, definido em
// app_config.cpp ou no arquivo indicado) e as funcoes que cruzam arquivos. So
// para eles: nenhum outro codigo inclui este header.
//
// O app foi partido por assunto (so mudou de arquivo, nada de logica):
//   app_config.cpp          medidas, paginas, estado, textos e botoes, linha de estado, inicio, despacho, registro do app
//   app_config_icones.cpp   icones da lista e o icone do app na tela inicial
//   app_config_rede.cpp     paginas Wi-Fi, Bluetooth, ESP-NOW, Meshtastic e MQTT
//   app_config_paginas.cpp  paginas Data e hora, Acesso pelo PC, Idioma, Tela, Teclado, Grove, Sobre e USB
#pragma once

#include "app.h"
#include "rolagem.hpp"
#include "pintura.hpp"
#include "lista.hpp"
#include "teclado_virtual.hpp"
#include "tab5_mesh_mqtt.hpp"
#include "tab5_tema.hpp"

// ---------------------------------------------------------------------------
// Medidas usadas por mais de um arquivo (as demais moram em app_config.cpp)
// ---------------------------------------------------------------------------
static const int MARGEM   = 24;
static const int CONTEUDO_Y = 112;     // abaixo do VOLTAR/titulo
static const int BTN_A = 72;

// ---------------------------------------------------------------------------
// Estado usado por mais de um arquivo (cada um definido no arquivo indicado)
// ---------------------------------------------------------------------------
// app_config.cpp:
extern pintura_t s_cfg_p;
extern char s_assin[512];
extern char s_aviso[96];
extern lista_t s_l_bt;
extern uint32_t s_bt_ver;
extern bool s_pin_editando;
extern tecvirt_t s_cfg_kb;
extern int s_dx_pag;
extern int s_dy_pag;
extern rolagem_t s_rol_pag;
extern int s_fim;
// app_config_rede.cpp:
extern lista_t s_l_wifi;
extern uint32_t s_wf_ver;
extern bool s_wf_senha;
extern char s_wf_txt[65];
extern bool s_mq_ed;
extern int s_mq_campo;
extern char s_mq_txt[64];
extern mesh_mqtt_servidor_t s_mq_p;

// ---------------------------------------------------------------------------
// Funcoes que cruzam arquivos
// ---------------------------------------------------------------------------
// app_config.cpp:
area_t janela_da_lista(const area_t& a, int y0, int n, int item_a);
area_t area_pagina(void);
int paragrafo(lgfx::LovyanGFX& d, int x, int y, int largura, const char* s);
void chevron(lgfx::LovyanGFX& d, int cx, int cy, uint16_t cor);
int info(lgfx::LovyanGFX& d, const area_t& a, int y, const char* rotulo, const char* valor, uint16_t cor = TEMA_TEXTO);
int botao_largo(lgfx::LovyanGFX& d, const area_t& a, int y, const char* txt, uint16_t cor);
void menos_mais(lgfx::LovyanGFX& d, const area_t& a, int y, const char* valor);
int menos_mais_toque(int tx, int ty, int y);
void fuso_texto(int16_t m, char* o, size_t t);
void est_wifi(char* o, size_t t);
void est_bt(char* o, size_t t);
void est_espnow(char* o, size_t t);
void est_teclado(char* o, size_t t);
void est_grove(char* o, size_t t);
const app_t* app_energia(void);
void conteudo_limpo(lgfx::LovyanGFX& d, const area_t& a);
// app_config_icones.cpp:
void ic_wifi(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_bt(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_espnow(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_mesh(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_tela(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_teclado(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_grove(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_usb(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_pc(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_sobre(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_nuvem(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_hora(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_idioma(lgfx::LovyanGFX& d, int x, int y, int l);
void ic_energia(lgfx::LovyanGFX& d, int x, int y, int l);
void icone_config(lgfx::LovyanGFX& d, int x, int y, int lado);
// app_config_rede.cpp:
int wf_item_altura(int, void*);
void wf_item_desenhar(lgfx::LovyanGFX& d, int i, int x0, int y, int largura, void*);
void pag_wifi(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void wf_tecla(int ac);
void toque_wifi(int tx, int ty);
void teclar_wifi(const char* txt, uint8_t codigo);
int bt_item_altura(int, void*);
void bt_item_desenhar(lgfx::LovyanGFX& d, int i, int x0, int y, int largura, void*);
void pag_bt(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_bt(int tx, int ty);
void pag_espnow(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_espnow(int tx, int ty);
void pag_mesh(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void pin_tecla(int ac);
void toque_mesh(int tx, int ty);
void teclar_mesh(const char* txt, uint8_t codigo);
void mq_tecla(int ac);
void pag_mqtt(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_mqtt(int tx, int ty);
void teclar_mqtt(const char* txt, uint8_t codigo);
// app_config_paginas.cpp:
void pag_hora(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_hora(int tx, int ty);
void pag_pc(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_pc(int tx, int ty);
void pag_idioma(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_idioma(int tx, int ty);
void pag_tela(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_tela(int tx, int ty);
void pag_teclado(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void pag_grove(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_grove(int tx, int ty);
void pag_sobre(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void pag_usb(lgfx::LovyanGFX& d, const area_t& a, bool tudo);
void toque_usb(int tx, int ty);

// ---------------------------------------------------------------------------
// Funcoes pequenas usadas por mais de um arquivo
// ---------------------------------------------------------------------------
static inline uint16_t cor_linha(lgfx::LovyanGFX& d) { return TEMA_LINHA; }
static inline uint16_t cor_sub(lgfx::LovyanGFX& d)   { return TEMA_TEXTO_2; }
static inline void fim(int y) { if (y > s_fim) { s_fim = y; } }
static inline bool deitado(const area_t& a) { return a.largura > a.altura; }
