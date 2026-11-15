// APP: Terminal - os comandos do sistema na tela, com teclado.
//
// So interface. A sessao (as linhas, a fila, a pasta atual, o comando que
// roda) e do servico (services/tab5_terminal), e os comandos sao os do
// console (console.cpp): fechar o app nao para o que roda, e voltar mostra
// tudo onde estava.
//
//   em cima    a saida, rolando (a mais nova embaixo, como todo terminal)
//   embaixo    a linha de comando: o prompt com a pasta atual e o que se digita
//
// Teclado fisico (ExtPort1): Enter roda, setas cima/baixo trazem o que foi
// digitado antes, Esc apaga a linha, Ctrl+C para o comando, Ctrl+L limpa,
// Ctrl+V cola. Sem ele: tocar na linha abre o virtual.
//
// NO PC: o botao no fim da linha de comando abre o painel do sistema
// (kernel/painel_web) - ligar o acesso pelo navegador, o endereco e a senha.
// Verde = o Terminal ja esta aberto no PC. O comando 'web' mostra o mesmo.
//
// O SSH E UM COMANDO: 'ssh usuario@host' abre a sessao SSH compartilhada com
// o navegador do PC, e o app troca SOZINHO para a tela dela (terminal_ssh.cpp)
// enquanto ela esta de pe; 'exit' (ou Encerrar) volta aos comandos, com o
// motivo na barra. A mesma regra vale na pagina /terminal/ do navegador.

#include "app.h"
#include "idioma.h"
#include "lista.hpp"
#include "teclado_virtual.hpp"
#include "tab5_terminal.hpp"
#include "transferencia.h"
#include "tab5_ui.hpp"
#include "tab5_tema.hpp"
#include "tab5_tela.hpp"
#include "tab5_teclado.hpp"
#include "tab5_ssh.hpp"
#include "tab5_terminal_web.hpp"
#include "tab5_web.hpp"
#include "painel_web.hpp"
#include "terminal_ssh.hpp"
#include <stdio.h>
#include <string.h>

static const int MX    = 12;    // margem do texto
static const int BAR_A = 64;    // a linha de comando
static const int PARAR_L = 120; // o botao "Parar" na linha, com comando rodando
static const int PC_L  = 120;   // o botao "No PC" no fim da linha
static const int TOPO_PC = 64;  // a tela do No PC: a faixa do "< Voltar"
// O fundo da saida fica PRETO (e nao TEMA_FUNDO), de proposito: e um terminal,
// e as cores do texto (e as ANSI da sessao SSH) foram feitas para fundo preto.
// Por isso este app NAO liga .fundo_tema. So a "moldura" (barra, botoes) usa o tema.
static const uint16_t FUNDO_TERM = TFT_BLACK;

static lista_t   s_lista;
static tecvirt_t s_kb;
static char      s_txt[TERM_ENTRADA] = "";
static int       s_hist = -1;          // -1 = editando a linha nova; k = o k-esimo de antes
static uint32_t  s_versao = 0xFFFFFFFF;
static bool      s_ocupado = false;
static bool      s_forcar = true;
static bool      s_ssh = false;        // a tela SSH na frente (a sessao esta de pe)
static bool      s_pc = false;         // a tela do No PC na frente (o painel do servidor web)
static uint32_t  s_pc_assin = 0;       // o que o painel mostrava (painel_web_assinatura)

#define DONO_WEB "Terminal"   // a parte do servidor web (o mesmo de tab5_terminal_web_ligar)

// A grade da fonte monoespacada, medida no desenho (muda ao girar).
static int s_cw = 14, s_lh = 26, s_cols = 48;

// Da borda de cima da fileira ate a linha de base da FreeMono12pt7b.
static const int BASE = 19;

static void fonte(lgfx::LovyanGFX& d) { d.setFont(&fonts::FreeMono12pt7b); d.setTextSize(1); }

// ---------------------------------------------------------------------------
// A SAIDA (kernel/lista: rola, recorta e desenha so o que mudou)
// ---------------------------------------------------------------------------
static int item_altura(int i, void*)
{
  const int n = tab5_terminal_tamanho(i);
  const int linhas = n <= 0 ? 1 : (n + s_cols - 1) / s_cols;
  return linhas * s_lh;
}

static void item_desenhar(lgfx::LovyanGFX& d, int i, int x, int y, int, void*)
{
  char t[TERM_LINHA_MAX + 1];
  term_tipo_t tipo = TERM_SAIDA;
  const int n = tab5_terminal_linha(i, t, sizeof(t), &tipo);
  if (n <= 0) { return; }
  fonte(d);
  d.setTextDatum(top_left);
  d.setTextColor(tipo == TERM_COMANDO ? (uint16_t)TFT_GREENYELLOW
               : tipo == TERM_AVISO   ? (uint16_t)TFT_ORANGE : d.color565(210, 210, 210));
  // Quebra a cada s_cols caracteres; o acento vai na fonte do corpo
  // (tab5_mono). A altura do item conta bytes: uma linha longa com acento
  // pode ganhar uma fileira vazia no fim - so isso.
  const char* p = t;
  for (int r = 0; *p; ++r) { p = tab5_mono(d, p, x + MX, y + r * s_lh + BASE, s_cw, s_cols); }
}

// ---------------------------------------------------------------------------
// GEOMETRIA
// ---------------------------------------------------------------------------
static int kb_altura(const area_t& a) { return tecvirt_aberto(&s_kb) ? tecvirt_altura(a) : 0; }
static int bar_y(const area_t& a)     { return a.altura - kb_altura(a) - BAR_A; }   // relativo a area

// ---------------------------------------------------------------------------
// A LINHA DE COMANDO
// ---------------------------------------------------------------------------
static void desenhar_barra(lgfx::LovyanGFX& d, const area_t& a)
{
  const int y = a.y + bar_y(a);
  d.fillRect(a.x, y, a.largura, BAR_A, TEMA_SUPERFICIE);
  d.drawFastHLine(a.x, y, a.largura, TEMA_LINHA);
  fonte(d);
  d.setTextDatum(middle_left);
  char p[TERM_ENTRADA + 180];
  tab5_terminal_prompt(p, sizeof(p));
  bool oculto = false;
  if (tab5_terminal_lendo(&oculto) && oculto) {
    // A senha que um comando pediu: so asteriscos, um por caractere.
    for (const char* c = s_txt; *c; ++c) { if ((*c & 0xC0) != 0x80) { strlcat(p, "*", sizeof(p)); } }
  } else {
    strlcat(p, s_txt, sizeof(p));
  }
  strlcat(p, "_", sizeof(p));
  // So o fim, se nao couber (o que se digita fica sempre a vista).
  const int livre = (a.largura - 3 * MX - PC_L - (s_ocupado ? PARAR_L + MX : 0)) / s_cw;
  const int n = (int)strlen(p);
  const char* mostra = n > livre && livre > 0 ? p + (n - livre) : p;
  while ((*mostra & 0xC0) == 0x80) { ++mostra; }   // nao comecar no meio de um acento
  d.setTextColor(TEMA_TEXTO);
  tab5_mono(d, mostra, a.x + MX, y + BAR_A / 2 + BASE - s_lh / 2, s_cw, livre > 0 ? livre : 1);
  const bool no_pc = tab5_web_aberto(DONO_WEB);
  tab5_fonte_corpo(d);
  tab5_texto(d, 2.0f);
  tab5_botao(d, a.x + a.largura - MX - PC_L, y + 10, PC_L, BAR_A - 20, tr("No PC"),
             no_pc ? (uint16_t)TEMA_DESTAQUE : (uint16_t)TEMA_SUPERFICIE_ALTA, no_pc ? (uint16_t)TEMA_SOBRE_DESTAQUE : (uint16_t)TEMA_TEXTO);
  if (s_ocupado) {
    tab5_botao(d, a.x + a.largura - 2 * MX - PC_L - PARAR_L, y + 10, PARAR_L, BAR_A - 20, tr("Parar"),
               TEMA_PERIGO, TEMA_SOBRE_DESTAQUE);
  }
  fonte(d);
}

// ---------------------------------------------------------------------------
// A TELA DO NO PC (o painel do sistema, com o "< Voltar" em cima)
// ---------------------------------------------------------------------------
static void pc_abrir(bool sim)
{
  s_pc = sim;
  if (sim) { tecvirt_fechar(&s_kb); }
  s_forcar = true;
  s_versao = 0xFFFFFFFF;
  sistema_pedir_redesenho_completo();
}

static void desenhar_pc(lgfx::LovyanGFX& d, const area_t& a, bool tudo)
{
  const uint32_t assin = painel_web_assinatura();
  if (!tudo && assin == s_pc_assin) { return; }
  s_pc_assin = assin;
  tab5_tela_limpar(a.x, a.y, a.largura, TOPO_PC, TEMA_SUPERFICIE);
  tab5_fonte_corpo(d);
  tab5_texto(d, 2.0f);
  tab5_botao(d, a.x + 8, a.y + 8, 140, TOPO_PC - 16, tr("< Voltar"), TEMA_SUPERFICIE_ALTA, TEMA_TEXTO);
  painel_web_desenhar(d, a.x, a.y + TOPO_PC, a.largura, a.altura - TOPO_PC, DONO_WEB);
}

static void tocar_pc(int x, int y)
{
  const area_t a = sistema_area();
  if (y < TOPO_PC) { if (x < 160) { pc_abrir(false); } return; }
  painel_web_tocar(0, TOPO_PC, a.largura, a.altura - TOPO_PC, x, y, DONO_WEB, tab5_terminal_web_ligar);
}

static void editar(int acao)
{
  s_hist = -1;
  tecvirt_editar(s_txt, sizeof(s_txt), acao);
  sistema_pedir_redesenho();
}

static void rodar(void)
{
  tab5_terminal_enviar(s_txt);
  s_txt[0] = 0;
  s_hist = -1;
  lista_ao_inicio(&s_lista);   // a ancora e o FIM: volta para a linha mais nova
  sistema_pedir_redesenho();
}

static void historico(int passo)
{
  const int k = s_hist + passo;
  if (k < 0) { s_hist = -1; s_txt[0] = 0; }
  else if (tab5_terminal_historico(k, s_txt, sizeof(s_txt))) { s_hist = k; }
  sistema_pedir_redesenho();
}

static void colar(void)
{
  char t[TERM_ENTRADA];
  const size_t n = transf_colar(t, sizeof(t));
  for (size_t i = 0; i < n; ++i) { if (t[i] == '\n' || t[i] == '\r' || t[i] == '\t') { t[i] = ' '; } }
  strlcat(s_txt, t, sizeof(s_txt));
  s_hist = -1;
  sistema_pedir_redesenho();
}

static void acao_teclado(int ac)
{
  if (ac == TECVIRT_LINHA || ac == TECVIRT_ENVIAR) { rodar(); }   // COMANDO: a tecla verde e o Enter
  else if (ac == TECVIRT_FECHAR) { tecvirt_fechar(&s_kb); s_forcar = true; sistema_pedir_redesenho_completo(); }
  else                           { editar(ac); }
}

// ---------------------------------------------------------------------------
// A TELA SEGUE A SESSAO SSH
// ---------------------------------------------------------------------------
// De pe: a tela dela. Terminou: os comandos de volta, com o motivo na barra
// (a sessao fica LIVRE para o proximo 'ssh').
static void seguir_ssh(void)
{
  tab5_ssh_sessao_info_t i;
  tab5_ssh_sessao_info(&i);
  const bool ssh = i.estado >= TAB5_SSH_CONECTANDO && i.estado <= TAB5_SSH_ABERTA;
  if (ssh == s_ssh) { return; }
  if (ssh) {
    tecvirt_fechar(&s_kb);
    s_pc = false;   // a sessao que sobe (aqui ou no PC) vem para a frente
    ssh_tela_abrir();
  } else {
    ssh_tela_fechar();
    if (i.estado == TAB5_SSH_FIM) {
      char m[128];
      snprintf(m, sizeof(m), "ssh %s@%s: %s", i.usuario, i.host, i.msg[0] ? i.msg : tr("fim"));
      sistema_recado(m);
      tab5_ssh_sessao_esquecer_fim();
    }
  }
  s_ssh = ssh;
  s_forcar = true;
  s_versao = 0xFFFFFFFF;
  sistema_pedir_redesenho_completo();
}

// ---------------------------------------------------------------------------
// DESENHO
// ---------------------------------------------------------------------------
static void desenhar(lgfx::LovyanGFX& d, const area_t& a)
{
  seguir_ssh();
  const bool tudo = sistema_area_foi_limpa() || s_forcar;
  s_forcar = false;
  if (s_ssh) { ssh_tela_desenhar(d, a, tudo); return; }
  if (s_pc)  { desenhar_pc(d, a, tudo); return; }
  s_ocupado = tab5_terminal_ocupado();

  fonte(d);
  s_cw = d.textWidth("M");
  s_lh = d.fontHeight() + 2;
  if (s_cw <= 0) { s_cw = 14; }
  const int cols = (a.largura - 2 * MX - 8) / s_cw;   // 8: a coluna da barra de rolagem
  if (cols != s_cols) { s_cols = cols > 8 ? cols : 8; s_versao = 0xFFFFFFFF; }

  const uint32_t v = tab5_terminal_versao();
  if (v != s_versao) { s_versao = v; lista_itens_mudaram(&s_lista, tab5_terminal_linhas()); }

  const area_t janela = { a.x, a.y, a.largura, bar_y(a) };
  lista_desenhar(&s_lista, d, janela, FUNDO_TERM, tudo);
  desenhar_barra(d, a);   // barata: sempre (o texto digitado, o botao Parar)

  tecvirt_posicionar(&s_kb, a);
  tecvirt_pintar(&s_kb, d, tudo);
  tab5_fonte_corpo(d);
}

static uint32_t periodo_agora(void)
{
  // Rodando: a saida chega a qualquer hora (10 quadros/s bastam para ler).
  // Acabou de parar ou sobrou texto: mais um quadro (o botao Parar sai).
  // Parado: so quando alguem toca ou tecla.
  const bool o = tab5_terminal_ocupado();
  return (o || o != s_ocupado || tab5_terminal_versao() != s_versao) ? 100 : 0;
}

// ---------------------------------------------------------------------------
// TOQUE E TECLADO
// ---------------------------------------------------------------------------
static void ao_tocar(int x, int y)
{
  if (s_ssh) { ssh_tela_tocar(x, y); return; }
  if (s_pc)  { tocar_pc(x, y); return; }
  const area_t a = sistema_area();
  if (tecvirt_contem(&s_kb, a.x + x, a.y + y)) {
    const int ac = tecvirt_clique(&s_kb, a.x + x, a.y + y);
    if (ac) { acao_teclado(ac); }
    return;
  }
  const int by = bar_y(a);
  if (y >= by && y < by + BAR_A) {
    if (x >= a.largura - MX - PC_L) { pc_abrir(true); return; }
    if (s_ocupado && x >= a.largura - 2 * MX - PC_L - PARAR_L) { tab5_terminal_interromper(); return; }
    if (!tecvirt_aberto(&s_kb)) { tecvirt_abrir(&s_kb, TECVIRT_COMANDO); s_forcar = true; sistema_pedir_redesenho_completo(); }
    return;
  }
  lista_item_tocado(&s_lista, x, y, nullptr);   // fecha o arraste; a saida nao tem botao
}

static void ao_tocar_nivel(int x, int y, bool pressionado)
{
  if (s_ssh) { ssh_tela_tocar_nivel(x, y, pressionado); return; }
  if (s_pc)  { return; }
  if (tecvirt_aberto(&s_kb) && tecvirt_altura(sistema_area()) > 0) {
    int acoes[8];
    const int n = tecvirt_nivel(&s_kb, acoes, 8);
    for (int i = 0; i < n; ++i) { acao_teclado(acoes[i]); }
  }
  if (lista_arrastar(&s_lista, x, y, pressionado)) { sistema_pedir_redesenho(); }
}

static void ao_teclar(const char* txt, uint8_t codigo, uint8_t mod)
{
  if (s_ssh) { ssh_tela_teclar(txt, codigo, mod); return; }
  if (s_pc)  { if (codigo == TAB5_TECLA_ESC) { pc_abrir(false); } return; }
  if (mod & TAB5_TECLA_CTRL) {
    const char c = txt ? (char)(txt[0] | 0x20) : 0;   // minuscula
    if (c == 'c') { tab5_terminal_interromper(); }
    else if (c == 'l') { tab5_terminal_limpar(); }
    else if (c == 'v') { colar(); }
    sistema_pedir_redesenho();
    return;
  }
  switch (codigo) {
    case TAB5_TECLA_ENTER:  rodar(); return;
    case TAB5_TECLA_APAGAR: editar(TECVIRT_APAGAR); return;
    case TAB5_TECLA_ESC:    s_txt[0] = 0; s_hist = -1; sistema_pedir_redesenho(); return;
    case TAB5_TECLA_CIMA:   historico(+1); return;
    case TAB5_TECLA_BAIXO:  historico(-1); return;
    default: break;
  }
  if (codigo || (mod & TAB5_TECLA_ALT) || !txt || !txt[0]) { return; }
  if (strlen(s_txt) + strlen(txt) < sizeof(s_txt)) {
    for (const char* p = txt; *p; ++p) { if ((unsigned char)*p < 0x20) { return; } }
    strlcat(s_txt, txt, sizeof(s_txt));
    s_hist = -1;
    sistema_pedir_redesenho();
  }
}

// ---------------------------------------------------------------------------
// CICLO DE VIDA
// ---------------------------------------------------------------------------
static bool ao_abrir(void)
{
  if (!tab5_terminal_abrir()) { return false; }
  lista_definir(&s_lista, ROLAGEM_FIM, tab5_terminal_linhas(), item_altura, item_desenhar, nullptr);
  s_versao = 0xFFFFFFFF;
  s_hist = -1;
  s_forcar = true;
  // Sessao SSH no ar (aberta aqui ou no PC): o app abre nela.
  s_ssh = false;
  s_pc = false;
  seguir_ssh();
  return true;
}

static void ao_aviso(uint32_t)
{
  seguir_ssh();   // a sessao subiu ou terminou (aqui, no PC ou por 'ssh fechar')
  if (s_ssh) { ssh_tela_aviso(); } else { sistema_pedir_redesenho(); }
}

static void ao_fechar(void)
{
  // A sessao continua (o comando que roda tambem, e a SSH): so a tela sai.
  tecvirt_fechar(&s_kb);
  if (s_ssh) { ssh_tela_fechar(); }
}

// A grade muda de largura: as alturas das linhas tambem.
static void ao_girar(void)
{
  s_versao = 0xFFFFFFFF;
  s_forcar = true;
}

static void icone_terminal(lgfx::LovyanGFX& d, int x, int y, int lado)
{
  d.fillRoundRect(x, y, lado, lado, lado / 6, d.color565(20, 24, 22));
  d.drawRoundRect(x, y, lado, lado, lado / 6, d.color565(60, 80, 64));
  const uint16_t c = TFT_GREENYELLOW;
  const int m = lado / 4, cy = y + lado / 2, t = lado / 14 + 1;
  // ">" e "_"
  for (int k = 0; k < t; ++k) {
    d.drawLine(x + m, cy - lado / 6 + k, x + m + lado / 6, cy + k, c);
    d.drawLine(x + m + lado / 6, cy + k, x + m, cy + lado / 6 + k, c);
  }
  d.fillRect(x + m + lado / 4, cy + lado / 6 - t + 1, lado / 4, t, c);
}

extern const app_t APP_TERMINAL = {
  .nome  = "Terminal",
  .cor   = TFT_GREENYELLOW,
  .perfil = { },
  .usa_sd = false,     // quem precisa do cartao e o comando, e ele monta (console)
  .exige_sd = false,
  .ao_abrir  = ao_abrir,
  .ao_fechar = ao_fechar,
  .desenhar  = desenhar,
  .ao_tocar  = ao_tocar,
  .ao_tocar_nivel = ao_tocar_nivel,
  .ao_teclar = ao_teclar,
  .periodo_agora = periodo_agora,
  .avisos = AVISO_SSH | AVISO_WEB,
  .ao_aviso = ao_aviso,
  .icone = icone_terminal,
  .ao_girar = ao_girar,
  .comandos = nullptr,
  .fora_da_home = false,
  .no_pc = tab5_terminal_web_ligar,
};
