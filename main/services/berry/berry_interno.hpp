// O que os modulos Berry (berry_mod_*.cpp) e o motor (berry_apps.cpp)
// compartilham: o contexto do script em curso, os ajudantes de argumento e de
// resposta, e as funcoes que o motor chama dentro dos modulos. So para eles:
// nenhum app inclui este header - app fala com o Berry por berry_apps.hpp.
//
// ATENCAO: inclua este header POR ULTIMO em cada .cpp. Ele define a macro 'C'
// (o contexto), e um nome tao curto estragaria qualquer header incluido depois.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include "berry_apps.hpp"
#include "berry_vm.hpp"      // berry_prazo_conferir (so_janela)
#include "teclado_virtual.hpp"

// ---------------------------------------------------------------------------
// O ESTADO DE CADA APP
// ---------------------------------------------------------------------------
typedef struct {
  char  nome[24];
  char  arquivo[96];       // caminho completo do .be
  bvm*  vm;                // nullptr = fechado
  char  erro[160];         // o ultimo erro do script ("" = nenhum): aparece na tela
  int   notif;             // fonte de aviso (sistema.avisar), -1 = ainda nao
  bool  usa_imu;           // o quadro anterior chamou sistema.imu()/giro(): ler o sensor antes deste
  uint8_t funcs;           // F_*: as funcoes que o script definiu (vistas ao abrir)
  char  pasta[96];         // a pasta do app no cartao, apps/Berry/<nome> ("" = ainda nao criada nesta abertura)
  bool  dedo_antes;        // havia dedo na amostra anterior (para entregar a soltura)
  uint32_t periodo_cab;    // o '# periodo:' do cabecalho
  uint32_t periodo;        // o de agora (sistema.periodo muda)
  bool  acumular;          // sistema.acumular(true): o quadro NAO chega limpo (desenhar(tudo=false))
  lgfx::LGFX_Sprite* camadas[8];   // tela.camada: imagens guardadas (nullptr = vaga livre)
  // A PARTE DE FUNDO (sistema.fundo): o script segue rodando fundo() com a
  // janela fechada - a MESMA maquina, entao janela e fundo veem as mesmas
  // variaveis. Quem roda o laco e o kernel (kernel/fundo.h).
  bool     janela;         // a janela esta aberta (abrir ate fechar)
  uint32_t fundo_ms;       // o intervalo pedido entre duas chamadas de fundo()
  int      fundo_pedido;   // FP_*: o script pediu ligar/desligar (a interface aplica)
  volatile bool fundo_sair;   // o proprio fundo() pediu para parar, ou deu erro
  char     fundo_txt[40];  // sistema.fundo_estado(texto): a pastilha na barra
  int      fundo_n;
  fundo_def_t fundo_def;   // a descricao para o kernel (nome = o do app)
} berry_app_t;

// O pedido de ligar/desligar o fundo (sistema.fundo), aplicado pela interface.
enum { FP_NADA, FP_LIGAR, FP_DESLIGAR };

// Contagem de tempo: vezes, soma e pior (ver 'berry tempos').
typedef struct { uint32_t n, max_us; uint64_t soma_us; } tempo_t;

// O CONTEXTO DE UM TRABALHO - quem roda e onde desenha. Quem pede monta o
// seu (a interface em pedir(), o fundo no laco dele) e o aponta em s_ctx so
// durante o trabalho; os nativos leem por C. Fora de trabalho de app (o
// console) C e o contexto vazio: sem app, sem tela.
//
// POR QUE NAO VARIAVEIS SOLTAS: o app que roda e onde ele desenha sao do
// TRABALHO, nao do modulo. Hoje a tarefa do Berry e uma so (um trabalho por
// vez, s_ctx basta); para rodar maquinas em tarefas diferentes, so s_ctx
// passa a ser por tarefa - o resto ja nao assume um app so.
typedef struct {
  berry_app_t*      app;      // nullptr = o console
  bool              fundo;    // trabalho da parte de fundo: sem tela, teclado, camera nem bip
  lgfx::LovyanGFX*  d;        // onde 'tela' desenha agora (a tela, ou a camada do alvo)
  area_t            a;        // a area de 'd' (na camada: 0, 0, largura, altura)
  // A TELA de verdade, separada do alvo: tela.alvo(c) desvia d/a para a
  // camada, tela.alvo() volta para estes (nullptr fora do desenhar()).
  lgfx::LovyanGFX*  tela_d;
  area_t            tela_a;
  int               alvo;     // 0 = a tela; N = a camada N
} contexto_t;

extern contexto_t* s_ctx;   // definido em berry_apps.cpp
#define C s_ctx

// Um membro de modulo: funcao nativa (fn) ou constante inteira (valor).
// Cada tabela termina com { nullptr }.
typedef struct { const char* nome; bntvfunc fn; int valor; } membro_t;

// As tabelas dos modulos, registradas por berry_modulos_registrar (berry_apps.cpp).
extern const membro_t BERRY_MOD_TELA[];
extern const membro_t BERRY_MOD_SISTEMA[];
extern const membro_t BERRY_MOD_TECLADO[];
extern const membro_t BERRY_MOD_CARTAO[];
extern const membro_t BERRY_MOD_ARQUIVOS[];
extern const membro_t BERRY_MOD_ESPNOW[];
extern const membro_t BERRY_MOD_MESH[];
extern const membro_t BERRY_MOD_CAMERA[];
extern const membro_t BERRY_MOD_GNSS[];
extern const membro_t BERRY_MOD_MUSICA[];
extern const membro_t BERRY_MOD_IDIOMA[];
extern const membro_t BERRY_MOD_TRANSFERENCIA[];
extern const membro_t BERRY_MOD_REDE[];
extern const membro_t BERRY_MOD_AJUSTES[];
extern const membro_t BERRY_MOD_TEMA[];

// ---------------------------------------------------------------------------
// AJUDANTES DOS MODULOS
// ---------------------------------------------------------------------------
static inline int arg_int(bvm* vm, int i, int padrao)
{
  return (be_top(vm) >= i && be_isnumber(vm, i)) ? (int)be_toint(vm, i) : padrao;
}

// Um numero de 32 bits SEM sinal (numero de no do Meshtastic). O inteiro do
// Berry aqui e de 32 bits COM sinal: um no acima de 2^31 chega negativo (o que
// mesh.no()/meu() devolvem) ou, digitado por extenso (4294967295), vira REAL -
// e o cast de real fora da faixa para int e indefinido (saia um numero
// qualquer, que escapava das checagens). Real fora de 0..2^32-1: 'padrao'.
static inline uint32_t arg_u32(bvm* vm, int i, uint32_t padrao)
{
  if (be_top(vm) < i) { return padrao; }
  if (be_isint(vm, i)) { return (uint32_t)be_toint(vm, i); }
  if (be_isreal(vm, i)) {
    const double v = (double)be_toreal(vm, i);
    return (v >= 0.0 && v <= 4294967295.0) ? (uint32_t)v : padrao;
  }
  return padrao;
}

static inline const char* arg_txt(bvm* vm, int i)
{
  return (be_top(vm) >= i) ? be_tostring(vm, i) : "";
}

// Montar mapa/lista para devolver ao script. mapa_novo/lista_nova deixam DOIS
// valores na pilha (o objeto e o dado dele); fim() tira o dado e sobra o objeto.
static inline void mapa_novo(bvm* vm)  { be_newobject(vm, "map"); }
static inline void lista_nova(bvm* vm) { be_newobject(vm, "list"); }
static inline void fim(bvm* vm)        { be_pop(vm, 1); }
static inline void por(bvm* vm) { be_data_insert(vm, -3); be_pop(vm, 2); }
static inline void m_int(bvm* vm, const char* k, int v)          { be_pushstring(vm, k); be_pushint(vm, v); por(vm); }
static inline void m_real(bvm* vm, const char* k, double v)      { be_pushstring(vm, k); be_pushreal(vm, (breal)v); por(vm); }
static inline void m_txt(bvm* vm, const char* k, const char* v)  { be_pushstring(vm, k); be_pushstring(vm, v ? v : ""); por(vm); }
static inline void m_bool(bvm* vm, const char* k, bool v)        { be_pushstring(vm, k); be_pushbool(vm, v); por(vm); }
static inline void l_real(bvm* vm, double v)      { be_pushreal(vm, (breal)v); be_data_push(vm, -2); be_pop(vm, 1); }
static inline void l_int(bvm* vm, int v)          { be_pushint(vm, v); be_data_push(vm, -2); be_pop(vm, 1); }
static inline void l_txt(bvm* vm, const char* v)  { be_pushstring(vm, v); be_data_push(vm, -2); be_pop(vm, 1); }

// "AA:BB:CC:DD:EE:FF" - o MAC em texto (o ESP-NOW e a rede mostram do mesmo jeito).
static inline void mac_txt(const uint8_t mac[6], char out[18])
{
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// O que e da JANELA (tela, camadas, teclado, camera, bip...) no fundo(): erro
// claro - o fundo roda com a janela fechada, nao ha onde desenhar nem tocar.
static inline bool so_janela(bvm* vm, const char* o_que)
{
  berry_prazo_conferir(vm);   // cada desenho custa ms: o prazo vale aqui tambem
  if (!C->fundo) { return true; }
  char msg[96];
  snprintf(msg, sizeof(msg), "%s e da janela: nao pode ser usado dentro de fundo()", o_que);
  be_raise(vm, "fundo_error", msg);
  return false;
}

// 'tela' fora do desenhar(): erro claro em vez de desenhar em lugar nenhum.
static inline bool exige_tela(bvm* vm)
{
  if (!so_janela(vm, "a tela")) { return false; }
  if (C->d) { return true; }
  be_raise(vm, "tela_error", "a tela so pode ser usada dentro de desenhar() (ou numa camada: tela.alvo)");
  return false;
}

// A pasta (relativa a /sdcard/apps) dos dados de um app: Berry/<nome>.
static inline void pasta_rel(const char* nome, char* out, size_t tam)
{
  snprintf(out, tam, "%s/%s", BERRY_PASTA, nome);
}

// ---------------------------------------------------------------------------
// O QUE E DA JANELA (bip, teclado, tela acesa, camera): ha UMA janela na
// tela, entao um estado so - e so trabalho da janela mexe nele (os nativos
// recusam no fundo, ver so_janela). Os modulos so PEDEM; quem aplica e a
// interface, em pedir() (berry_apps.cpp), depois que o trabalho volta.
// ---------------------------------------------------------------------------
// O bip pedido pelo script (sistema.bip). Definido em berry_mod_sistema.cpp.
extern float    s_bip_hz;
extern uint32_t s_bip_ms;

// Manter a tela acesa (sistema.acordado): -1 = nada pedido. Idem.
extern int s_acordado_pedido;

// O TECLADO VIRTUAL DO SISTEMA (um so: so um app Berry fica aberto por vez).
// Definido em berry_mod_tela.cpp.
extern tecvirt_t s_kb;
enum { KB_NADA, KB_ABRIR, KB_FECHAR };
extern int     s_kb_pedido;
extern uint8_t s_kb_tipo;   // o tipo do campo do ultimo teclado.abrir(tipo)

// ---------------------------------------------------------------------------
// O QUE O MOTOR CHAMA DENTRO DOS MODULOS
// ---------------------------------------------------------------------------
// tela (berry_mod_tela.cpp)
int  altura_livre(void);                // a altura que sobra para o app (o teclado aberto come a base)
void alvo_tela(void);                   // o desenho volta para a tela
void camadas_liberar(berry_app_t* a);   // as camadas da janela voltam ao fechar

// QUANTO CADA DESENHO CUSTA ('berry tempos'): o tempo dentro de cada funcao de 'tela'.
enum { P_LIMPAR, P_RETANGULO, P_CIRCULO, P_LINHA, P_TEXTO, P_MEDIR, P_ARREDONDADO, P_TRIANGULO, P_PIXEL, P_BOTAO, P_COLAR, P_N };
extern const char* const P_NOME[P_N];
extern tempo_t s_prim[P_N];

// gnss (berry_mod_gnss.cpp): solta a assinatura do GPS (a do app, ou qualquer uma se a == nullptr)
void gnss_soltar(const berry_app_t* a);

// rede (berry_mod_rede.cpp): o app fechou - cancela o pedido dele e libera a resposta
void rede_soltar(const berry_app_t* a);

// camera (berry_mod_camera.cpp)
bool cam_no_ar(void);                     // o preview esta ligado
void cam_aplicar(void);                   // aplica o que o script pediu (so a interface; nada pedido = nada a fazer)
void cam_quadro(const berry_app_t* app);  // a cada desenhar, antes do script
void cam_limpar_fora(const area_t& a);    // a area sem a faixa do preview
void cam_fechar(void);                    // o app fechou: tudo desce

// ajustes (berry_mod_ajustes.cpp): volume([v]) 0..100 - o musica.volume usa a mesma
int berry_volume(bvm* vm);
