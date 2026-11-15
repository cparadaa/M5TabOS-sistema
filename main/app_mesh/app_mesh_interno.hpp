// O que os arquivos do app Mesh (app_mesh.cpp e app_mesh_*.cpp) compartilham: as
// constantes e os tipos usados por mais de um deles, o estado (declarado aqui,
// definido em app_mesh.cpp ou no arquivo indicado) e as funcoes que cruzam
// arquivos. So para eles: nenhum outro app inclui este header.
//
// O app foi partido por assunto (so mudou de arquivo, nada de logica):
//   app_mesh.cpp           estado, as listas, o ciclo do app e o registro do app_t
//   app_mesh_desenho.cpp   desenho, glifos, marca, grade das colunas, nos, busca, malha
//   app_mesh_conversa.cpp  localizacao dentro de mensagem e a conversa
//   app_mesh_no.cpp        a pagina "No"
#pragma once

#include "lista.hpp"
#include "app.h"
#include "tab5_mesh.hpp"
#include "tab5_mesh_mqtt.hpp"   // servidores do modo internet
#include "teclado_virtual.hpp"
#include "conversa.hpp"
#include "pintura.hpp"

// ---------------------------------------------------------------------------
// Constantes e tipos usados por mais de um arquivo
// ---------------------------------------------------------------------------
static const int TRILHO_L = 90;
static const int CONV_L   = 200;
static const int ITEM_A   = 72;      // altura de um item nas colunas
// BOTAO DE APAGAR CONVERSA. Geometria num lugar so, porque desenho e toque
// precisam concordar - e ja foi por concordarem "quase" que as colunas dos nos
// sairam do lugar uma vez.
//
// Fica ABAIXO do nome, e nao ao lado: ao lado obrigaria a encurtar o nome de 8
// para 6 caracteres, e o item tem 72px de altura com o nome ocupando so a
// primeira linha. A altura estava sobrando; a largura, nao.
static const int APAGAR_L  = 30;
static const int APAGAR_A  = 28;
static const int APAGAR_DY = 40;            // a partir do y do item
static const int FRENTE_TOPO_A = 84;        // a faixa do VOLTAR e do nome
static const int FRENTE_VOLTAR_L = 200, FRENTE_VOLTAR_A = 56, FRENTE_VOLTAR_Y = 14;
// Ordem do trilho: CONVERSAS primeiro. E a pagina que se usa mais, e a que
// motiva abrir o app - as outras sao consulta.
// A ORDEM IMPORTA: ela e a mesma dos quatro primeiros glifos (GL_CONVERSAS,
// GL_NOS, GL_MALHA, GL_CONFIG), e o trilho converte a pagina em glifo por
// conversao direta. Trocar a ordem de um sem o outro poe o icone errado.
typedef enum { PAG_MSGS = 0, PAG_NOS, PAG_MALHA, PAG_NO, PAG_TOTAL } pagina_t;
// A CONVERSA ABERTA PELA CHAVE, e nao so pelo indice. O indice e a posicao na
// coluna, e a coluna se reordena (direta com mensagem nova sobe, conversa
// nova, canal desabilitado no no). Guardando so o indice, a tela passava a
// mostrar e a ENVIAR para a conversa vizinha. Quando a versao da ordem muda
// (tab5_mesh_conversas_versao), o indice e reencontrado por esta chave.
typedef struct { bool ok; bool canal; uint8_t idx; uint32_t no; } chave_t;
// Atalho local: dentro do desenhar() todos os blocos perguntam "esta passada e
// completa?", e escrever pintura_completa(&s_p) em cada um ficaria ilegivel.
#define TUDO  pintura_completa(&s_p)
typedef enum { GL_CONVERSAS = 0, GL_NOS, GL_MALHA, GL_CONFIG, GL_DIFUSAO, GL_DIRETO } glifo_t;
typedef struct {
  int         col;      // caractere onde a coluna comeca
  int         larg;
  bool        direita;  // alinhamento do valor E do titulo
  const char* titulo;
} coluna_t;
typedef struct { int x, y, l, a; uint8_t idx; } chip_t;   // coordenadas LOCAIS da area
static const int BUSCA_LIMPAR_L = 56;      // o "x", na ponta direita da caixa
static const int DET_BTN_A = 64;
static const int DET_BTN_Y_DA_BASE = 84;    // de baixo para cima, ver ao_tocar
// Botao da ESTRELA, no canto de cima a direita do cartao (coordenadas a partir
// da direita da area, para valer em qualquer largura).
static const int DET_FAV_L = 260, DET_FAV_A = 56, DET_FAV_Y = 14, DET_FAV_DIR = 24;

// ---------------------------------------------------------------------------
// Estado usado por mais de um arquivo (cada um definido no arquivo indicado)
// ---------------------------------------------------------------------------
// app_mesh.cpp:
extern int s_col_l;
extern pagina_t s_pag;
extern int mesh_s_conv;
extern chave_t s_ab;
extern conversa_t s_c;
extern mesh_msg_t* s_hist;
extern chave_t s_hist_chave;
extern int s_n_hist;
extern lista_t s_l_conv;
extern lista_t s_l_nos;
extern bool s_hist_pronto;
extern pintura_t s_p;

// app_mesh_desenho.cpp:
extern chip_t s_chips[MESH_MAX_CANAIS + 1];
extern int s_n_chips;
extern char s_busca[MESH_BUSCA_MAX];
extern tecvirt_t s_kb_busca;
extern chip_t s_chip_busca;
extern uint32_t s_no_sel;

// app_mesh_no.cpp:
extern int s_grupo;
extern lista_t s_l_cfg;
extern lista_t s_l_redes;
extern int s_editando;
extern tecvirt_t s_kb_cfg;
extern bool s_diario;
extern bool s_web_pg;
extern bool s_redes_pg;
extern bool s_net_pg;
extern bool s_net_ed;
extern mesh_mqtt_servidor_t s_net_p;
extern char s_cfg_msg[64];

// ---------------------------------------------------------------------------
// Funcoes internas que cruzam arquivos (cada uma definida no arquivo indicado)
// ---------------------------------------------------------------------------
// app_mesh.cpp:
int apagar_dx(void);
bool em_pe(const area_t& a);
void cortar_ao_caber(lgfx::LovyanGFX& d, char* t, int max_l);
void texto_que_cabe(lgfx::LovyanGFX& d, char* t, int max_l, float tam, float tam_min);
bool chave_igual(const chave_t& a, const chave_t& b);
void carregar_hist(void);

// app_mesh_desenho.cpp:
void caixa_glifo(lgfx::LovyanGFX& d, int x, int y, int l, int a, glifo_t g, uint16_t fundo,
                 uint16_t tinta);
void marca_mesh(lgfx::LovyanGFX& d, int x, int y, int lado);
void desenhar_trilho(lgfx::LovyanGFX& d, const area_t& a);
int conv_altura(int, void*);
void conv_desenhar(lgfx::LovyanGFX& d, int i, int x, int y, int largura, void*);
void desenhar_conversas(lgfx::LovyanGFX& d, const area_t& a);
void campo(char* linha, const coluna_t& c, const char* txt);
int nos_altura(int, void*);
void nos_desenhar(lgfx::LovyanGFX& d, int i, int x, int y, int, void*);
void busca_abrir_teclado(void);
void busca_fechar_teclado(void);
void busca_limpar(void);
void busca_tecla(int ac);
int det_btn_l(const area_t& a);
int det_fav_l(const area_t& a);
void desenhar_nos(lgfx::LovyanGFX& d, const area_t& a, int x0);
void desenhar_malha(lgfx::LovyanGFX& d, const area_t& a, int x0);

// app_mesh_conversa.cpp:
void distancia_direcao(float lat1, float lon1, float lat2, float lon2, float* dist_km,
                       const char** direcao);
// Declaradas aqui, definidas mais abaixo perto de desenhar_msgs() (onde
// nasceram) - trilho e coluna de conversas tambem precisam da
// mesma paleta agora, e vem ANTES no arquivo.
uint16_t fundo_msg(lgfx::LovyanGFX& d);
void atualizar_eh_canal(void);
int conv_contar(void*);
bool conv_ler(int i, conversa_msg_t* o, void*);
bool conv_enviar(const char* texto, void*);
void desenhar_msgs(lgfx::LovyanGFX& d, const area_t& a, int x0);

// app_mesh_no.cpp:
int cfg_altura(int, void*);
void cfg_desenhar(lgfx::LovyanGFX& d, int item, int x, int y, int largura, void*);
int rede_altura(int, void*);
void rede_desenhar(lgfx::LovyanGFX& d, int i, int x0, int y, int largura, void*);
void desenhar_config(lgfx::LovyanGFX& d, const area_t& a, int x0);
void cfg_fechar_edicao(bool confirmar);
void cfg_tecla(int ac);
void tocar_config(int x, int y);
