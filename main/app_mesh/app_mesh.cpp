// APP: Meshtastic - a JANELA do servico tab5_mesh.
//
// O app nao possui o enlace: quem fala com o C6L e o servico, que fica de pe
// com esta tela fechada. Aqui so se desenha o que ele ja sabe. E o mesmo
// desenho de Ajustes para o WiFi, e e o que permite avisar de mensagem nova sem
// o app aberto.
//
// SEGUE O APARELHO (ORI_SEGUE_IMU no descritor): deitado (1280x720) ou em pe
// (720x1280). Com o teclado encaixado, sempre deitado - a postura natural, e
// a lista de mensagens cabe muito mais.
//
// LAYOUT DEITADO
//   [trilho]  [conversas]   area principal
//     90px       200px      o resto
//
// O trilho e sempre visivel. A coluna de conversas SO aparece na pagina de
// mensagens - nas outras ela seria espaco morto. E a barra de escrita fica
// dentro da area das mensagens, nao sob as colunas.
//
// LAYOUT EM PE - nao cabem tres colunas em 720 px. Na pagina de mensagens a
// coluna de conversas ocupa o resto da largura (com mais espaco para o nome),
// e tocar numa conversa a abre POR CIMA de tudo, na largura inteira, com uma
// faixa de VOLTAR em cima. Voltar devolve a lista - o mesmo vai-e-volta de
// um app de mensagem de celular. Nas outras paginas o trilho fica, e o
// conteudo se acomoda: a tabela de nos esconde colunas, a malha vira uma
// coluna so, botoes lado a lado encolhem.
//
// O ARQUIVO FOI PARTIDO POR ASSUNTO (so mudou de lugar; o que as partes
// compartilham esta em app_mesh_interno.hpp):
//   app_mesh.cpp           estado, as listas, o trabalho de carga, o ciclo do app
//                          (desenhar, tocar, abrir, fechar) e o registro do app_t
//   app_mesh_desenho.cpp   desenho, glifos, a marca do Meshtastic, a grade das
//                          colunas, a tabela de nos, a busca, o detalhe e a malha
//   app_mesh_conversa.cpp  localizacao dentro de uma mensagem e a conversa
//   app_mesh_no.cpp        a pagina "No" (configurar o no, a internet, o diario)

#include "esp_attr.h"   // EXT_RAM_BSS_ATTR - buffers grandes na PSRAM
#include "trabalho.hpp"
#include "tab5_teclado.hpp"
#include "rolagem.hpp"
#include "tab5_ui.hpp"
#include "tab5_tema.hpp"
#include "esp_heap_caps.h"
#include "tab5_mesh_web.hpp"
#include <string.h>
#include "app_mesh_interno.hpp"

// LARGURA DA COLUNA DE CONVERSAS: 200 deitado; em pe, tudo o que sobra do
// trilho. Gravada na passada que desenha a coluna e lida pelo item e pelo
// toque - os tres concordam porque leem o mesmo numero.
int s_col_l = CONV_L;
// O "x" fica a 50 px do fim da coluna (fora da faixa da barra de rolagem),
// a partir do x da coluna.
int apagar_dx(void) { return s_col_l - 50; }

// EM PE, A CONVERSA ABERTA POR CIMA DA COLUNA. false = a lista; true = a
// conversa na largura inteira, com VOLTAR. Deitado nao e consultado (as duas
// aparecem lado a lado).
static bool s_conv_frente = false;

bool em_pe(const area_t& a) { return a.largura < a.altura; }

// Corta 't' no fim, por caractere (UTF-8), ate caber em max_l px na fonte
// que estiver ativa. Em pe quase todo texto longo passa por aqui.
void cortar_ao_caber(lgfx::LovyanGFX& d, char* t, int max_l) { tab5_cortar_ao_caber(d, t, max_l); }

// Escolhe a fonte suave do tamanho 'tam' para baixo (passo 0,2) ate 't'
// caber em max_l; no menor tamanho, corta. Deixa a fonte escolhida ativa.
void texto_que_cabe(lgfx::LovyanGFX& d, char* t, int max_l, float tam, float tam_min)
{
  tab5_texto(d, tam);
  while (tam > tam_min && (int)d.textWidth(t) > max_l) {
    tam -= 0.2f;
    tab5_texto(d, tam);
  }
  cortar_ao_caber(d, t, max_l);
}

pagina_t s_pag = PAG_MSGS;

int  mesh_s_conv = -1;              // conversa aberta (indice na coluna); -1 = nenhuma
chave_t  s_ab = { };            // a conversa aberta (ok = false: nenhuma)
static uint32_t s_conv_ver = 0;        // a versao da ordem que mesh_s_conv segue

bool chave_igual(const chave_t& a, const chave_t& b)
{
  if (a.ok != b.ok) { return false; }
  if (!a.ok)        { return true; }
  return a.canal == b.canal && (a.canal ? a.idx == b.idx : a.no == b.no);
}

static chave_t chave_de(int c)
{
  mesh_conversa_t m;
  if (c < 0 || !tab5_mesh_conversa(c, &m)) { return chave_t{ }; }
  return chave_t{ true, m.canal, m.idx, m.no };
}

// A CONVERSA - baloes, barra de escrita, teclado e rolagem sao o componente do
// sistema (kernel/conversa). Ver conv_ler(), mais abaixo.
EXT_RAM_BSS_ATTR conversa_t s_c;   // ~1 KB, so a interface toca: PSRAM

// Historico da conversa aberta. Em RAM para a tela desenhar rapido; a fonte da
// verdade e o cartao.
// HISTORICO NA PSRAM, e 200 em vez de 40.
//
// Eram 7,6KB de RAM INTERNA presos mesmo com o app fechado - o maior simbolo
// estatico do sistema inteiro. Na PSRAM, alocado ao abrir e devolvido ao
// fechar, custa zero quando ninguem esta conversando; e 200 mensagens sao
// 38KB, irrelevantes nos 29MB livres.
//
// O numero maior nao e capricho: sem rolagem, 40 bastavam porque so cabiam
// umas 8 na tela e o resto era inalcancavel. Com rolagem, o limite deixa de ser
// a tela e passa a ser este vetor - entao ele precisa valer a pena subir.
#define HIST_MAX 200
mesh_msg_t* s_hist = nullptr;   // o que a tela mostra (so a interface le)
chave_t     s_hist_chave = { }; // de qual conversa e o s_hist

// A CARGA DO HISTORICO, FORA DA INTERFACE (kernel/trabalho). O
// worker le o cartao para ESTE buffer - nunca o que a tela desenha - e a
// interface so troca os dois no ao_aviso(AVISO_TRABALHO). Antes a leitura era
// feita na tarefa de interface a cada mensagem nova: montar o SD e ler a
// conversa inteira com a tela e o toque parados.
static mesh_msg_t*   s_carga = nullptr;
static volatile int  s_carga_n = 0;
static chave_t       s_carga_chave = { };   // pela chave: a ordem pode mudar ate o worker rodar
static bool          s_carga_cartao = false;   // o pedido inclui carregar_do_cartao
static volatile bool s_carga_pronta = false;
static bool          s_carga_pedida = false;
static bool          s_recarregar   = false;   // pediram de novo durante a carga
int  s_n_hist = 0;

// ---------------------------------------------------------------------------
// AS TRES LISTAS DO APP - todas kernel/lista
// ---------------------------------------------------------------------------
// Mensagens, conversas e nos rolam cada uma na sua janela; o trilho e a
// navegacao ficam parados. Por isso nao e a rolagem do sistema
// (altura_conteudo), que rola a area inteira.
//
//   baloes   - a lista do componente de conversa (s_c.lista).
//   s_l_conv - coluna de conversas (ate MESH_MAX_CONVERSAS; cabem umas 7).
//   s_l_nos  - tabela de nos (ate MESH_MAX_NOS; cabem umas 16), abaixo do
//              cabecalho fixo.
//
// A lista cuida de rolar, recortar, desenhar so o exposto, a barra, e do
// toque (arraste e "qual item"). O app so diz a altura e como desenhar um
// item. Ao trocar de pagina, ir_para_pagina() esconde as listas da pagina
// que saiu - uma lista que nao esta na tela nao responde a toque.
lista_t s_l_conv;
lista_t s_l_nos;
bool s_hist_pronto = false;

// Repintura completa pendente. Sem isto o app pisca: ele redesenha a 1Hz
// (periodo_ms = 1000) e cada pagina limpava a area inteira antes de escrever.
// Texto com cor de fundo SOBRESCREVE, e nao pisca; quem pisca e o fillRect.
// Mesma licao de Ajustes e da barra de status.
pintura_t s_p;

// Quantas mensagens o servico tinha quando lemos o cartao pela ultima vez.
//
// Existe para NAO reler o historico a cada repintura. Ler significa montar o
// cartao, percorrer o arquivo e desmontar - caro, e desnecessario quando so o
// desenho mudou. Recarregamos apenas quando a conversa muda ou quando chega
// mensagem nova.
// int32_t, e nao uint32_t: -1 tem que caber como "nunca li ainda" e ser
// diferente de qualquer tab5_mesh_total_msgs() de verdade (que comeca em 0)
// - ver o aviso grande junto do uso, mais abaixo.
static int64_t s_msgs_quando_li = -1;

// Versao dos estados de envio que a tela ja desenhou.
static uint32_t s_envios_ver = 0;

// Roda NO WORKER. So toca s_carga*: a tela nao le nada daqui.
static void trabalho_carregar(void*)
{
  if (s_carga_cartao) { tab5_mesh_carregar_do_cartao(); }
  const chave_t k = s_carga_chave;
  s_carga_n = k.ok ? tab5_mesh_historico_de(k.canal, k.idx, k.no, s_carga, HIST_MAX) : 0;
  s_carga_pronta = true;
}

static bool alocar_hist(void)
{
  if (!s_hist)  { s_hist  = (mesh_msg_t*)heap_caps_malloc(sizeof(mesh_msg_t) * HIST_MAX, MALLOC_CAP_SPIRAM); }
  if (!s_carga) { s_carga = (mesh_msg_t*)heap_caps_malloc(sizeof(mesh_msg_t) * HIST_MAX, MALLOC_CAP_SPIRAM); }
  return s_hist && s_carga;
}

// PEDE a carga (nao le nada aqui). Conversa diferente da que esta na tela: a
// tela esvazia na hora, em vez de mostrar a conversa anterior ate a carga
// chegar.
void carregar_hist(void)
{
  s_hist_pronto = true;   // pedido feito; o resultado chega por aviso
  if (!alocar_hist()) { s_n_hist = 0; return; }
  if (!chave_igual(s_hist_chave, s_ab)) {
    s_n_hist = 0;
    s_hist_chave = s_ab;
    atualizar_eh_canal();
    conversa_mudou(&s_c);
  }
  if (s_carga_pedida) { s_recarregar = true; return; }
  s_carga_chave  = s_ab;
  s_carga_pronta = false;
  s_carga_pedida = sistema_trabalho(trabalho_carregar, nullptr);
  if (s_carga_pedida) { s_carga_cartao = false; }
  else                { s_hist_pronto = false; }   // queue cheia: tenta na proxima passada
}

// Na interface. Carga terminou -> troca os buffers; qualquer outro aviso
// (mensagem, envio, nos) -> redesenha, e o desenhar() confere os contadores.
static void ao_aviso(uint32_t av)
{
  if ((av & AVISO_TRABALHO) && s_carga_pronta) {
    s_carga_pronta = false;
    s_carga_pedida = false;
    if (chave_igual(s_carga_chave, s_ab)) {
      mesh_msg_t* t = s_hist; s_hist = s_carga; s_carga = t;
      s_n_hist     = s_carga_n;
      s_hist_chave = s_carga_chave;
      conversa_mudou(&s_c);
    }
    // Conversa trocou durante a carga, ou chegou mensagem no meio: de novo.
    if (!chave_igual(s_carga_chave, s_ab) || s_recarregar) {
      s_recarregar = false;
      s_hist_pronto = false;
    }
    // A primeira carga tambem trouxe nomes e conversas do cartao: a coluna de
    // conversas precisa ser repintada inteira.
    pintura_pedir(&s_p);
    sistema_pedir_redesenho_completo();
    return;
  }
  // O servidor web ligou, desligou ou alguem conectou: a sub-pagina do PC (e
  // o resumo do grupo) compara a assinatura do painel no desenho.
  sistema_pedir_redesenho();
}

static void desenhar(lgfx::LovyanGFX& d, const area_t& a)
{

  // O pintura_iniciar() ja consulta sozinho se o sistema limpou a area - era o
  // passo que, esquecido, deixava a tela em branco ao acordar.
  //
  // Repintar NAO implica reler o cartao: a copia em RAM continua valida.
  // Confundir as duas coisas fazia o app montar o SD a cada toque.
  // A ORDEM DA COLUNA MUDOU: reencontra a conversa aberta pela chave. Se ela
  // saiu da coluna (canal desabilitado no no, servico trocando de via), a tela
  // volta a "escolha uma" mas a chave fica: quando ela reaparecer, reabre.
  // ANTES do pintura_iniciar, para esta passada ja sair completa - a coluna
  // inteira mudou de lugar (ver a regra da repintura completa).
  const uint32_t cv = tab5_mesh_conversas_versao();
  if (cv != s_conv_ver) {
    s_conv_ver = cv;
    if (s_ab.ok) {
      const int novo = tab5_mesh_conversa_indice(s_ab.canal, s_ab.idx, s_ab.no);
      if (novo != mesh_s_conv) {
        if ((novo < 0) != (mesh_s_conv < 0)) { conversa_trocar(&s_c); s_hist_pronto = false; }
        mesh_s_conv = novo;
      }
    }
    pintura_pedir(&s_p);
    sistema_pedir_redesenho_completo();
  }

  pintura_iniciar(&s_p);

  // A primeira carga tambem le do cartao os nomes e as conversas - e tem que
  // acontecer mesmo sem conversa aberta, senao a coluna nasce vazia sem o no.
  if (s_carga_cartao && !s_carga_pedida) { carregar_hist(); }

  // Mensagem nova no servico: ai sim vale reler.
  //
  // tab5_mesh_total_msgs(), NAO tab5_mesh_num_msgs() - sintoma: mensagem
  // nova so aparecia depois de trocar de tela e voltar. O
  // segundo satura em MESH_MAX_MSGS (8) e fica parado para sempre depois
  // da oitava mensagem desde o boot do servico - a comparacao abaixo nunca
  // mais via diferenca, entao o app so recarregava por outros caminhos
  // (trocar de conversa, trocar de pagina) que zeram s_hist_pronto direto,
  // nunca sozinho enquanto a tela ficava aberta. Ver o aviso grande no
  // .hpp junto de tab5_mesh_total_msgs().
  const int64_t nm = (int64_t)tab5_mesh_total_msgs();
  // COMPARACAO DIRETA, e nao pintura_mudou_i(). O helper devolve true em toda
  // passada COMPLETA, por desenho - e completa e o que faz o app repintar tudo.
  // Usa-lo para decidir E/S cria loop: passada completa -> "mudou" -> recarrega
  // -> pintura_pedir() -> proxima passada completa -> ... O teclado repintava a
  // cada ciclo e piscava sozinho.
  //
  // Regra: pintura_mudou_* serve para decidir DESENHO. Decisao de ler cartao,
  // abrir arquivo ou falar com hardware compara na mao.
  if (nm != s_msgs_quando_li) {
    // So PEDE a carga nova (o worker le o cartao). Quando ela chega, o
    // ao_aviso troca os buffers e pede a repintura - que aqui seria cedo
    // demais (o historico novo ainda nao existe) e duplicada.
    //
    // A posicao de leitura e da lista (kernel/lista): colado no fim, a
    // mensagem nova aparece; lendo o historico, a tela nao se mexe.
    s_hist_pronto = false;
    s_msgs_quando_li = nm;
    sistema_pedir_redesenho();
  }


  // ACK chegou (ou prazo estourou): sao os MESMOS baloes, so a marca mudou.
  // Nao releia o cartao por causa disso - o conteudo nao mudou, e montar o SD a
  // cada confirmacao seria caro a toa. Comparacao direta pelo mesmo motivo do
  // bloco acima.
  const uint32_t ev = tab5_mesh_envios_versao();
  if (ev != s_envios_ver) { s_envios_ver = ev; conversa_mudou(&s_c); }   // as marcas de envio

  // EM PE: ou a lista (trilho + coluna larga), ou a conversa por cima de
  // tudo. Deitado: trilho, coluna e conversa lado a lado, como sempre.
  const bool pe = em_pe(a);
  const bool frente = pe && s_pag == PAG_MSGS && s_conv_frente && mesh_s_conv >= 0;

  if (TUDO && !frente) { desenhar_trilho(d, a); }
  const int x0 = frente ? a.x
               : a.x + TRILHO_L + ((s_pag == PAG_MSGS && !pe) ? CONV_L : 0);

  switch (s_pag) {
    case PAG_NOS:   desenhar_nos(d, a, x0);   break;
    case PAG_MALHA: desenhar_malha(d, a, x0); break;
    case PAG_NO:    desenhar_config(d, a, x0); break;
    default:
      if (pe && !frente) {
        // Em pe, so a lista: os baloes nao estao na tela, entao nao rolam
        // nem respondem, e o teclado (se estava aberto) fecha.
        conversa_fechar_teclado(&s_c);
        lista_esconder(&s_c.lista);
      } else {
        desenhar_msgs(d, a, x0);
      }
      break;
  }
  // A coluna DEPOIS das mensagens: desenhar_msgs marca como lida a conversa
  // aberta, e o contador dela na coluna ja sai zerado nesta mesma passada.
  // Com a conversa na frente a coluna esta coberta: nao rola nem responde.
  if (s_pag == PAG_MSGS && !frente) { desenhar_conversas(d, a); }
  else                              { lista_esconder(&s_l_conv); }
}

// A TELA GIROU (kernel/orientacao). Deitado -> em pe com uma conversa
// aberta: ela vem para a frente, e quem estava escrevendo segue escrevendo.
// Em pe -> deitado: lista e conversa voltam a ficar lado a lado. O resto se
// acomoda sozinho na passada completa que o sistema ja pediu.
static void ao_girar(void)
{
  s_conv_frente = (mesh_s_conv >= 0);
  pintura_pedir(&s_p);
}

// Em pe: a conversa sai da frente e a lista volta.
static void voltar_a_lista(void)
{
  s_conv_frente = false;
  conversa_fechar_teclado(&s_c);
  pintura_pedir(&s_p);
  sistema_pedir_redesenho_completo();
}

// Teclado fisico do ExtPort1: o componente de conversa trata.
static void ao_teclar(const char* texto, uint8_t codigo, uint8_t mod)
{
  // Teclado fisico tambem edita o nome do no.
  if (s_pag == PAG_NO && s_editando) {
    if      (codigo == TAB5_TECLA_ENTER)  { cfg_fechar_edicao(true); }
    else if (codigo == TAB5_TECLA_ESC)    { cfg_fechar_edicao(false); }
    else if (codigo == TAB5_TECLA_APAGAR) { cfg_tecla(TECVIRT_APAGAR); }
    else if (texto && texto[0]) {
      // Nome e senha do no sao ASCII: a letra composta pela tecla morta
      // volta a ser as duas teclas apertadas.
      char cru[8];
      tab5_teclado_ascii(texto, cru, sizeof(cru));
      for (const char* c = cru; *c && s_editando; ++c) { cfg_tecla((unsigned char)*c); }
    }
    return;
  }
  // Na tabela de nos o fisico escreve direto na busca, sem tocar na caixa.
  if (s_pag == PAG_NOS && !s_no_sel) {
    if      (codigo == TAB5_TECLA_ENTER)  { busca_fechar_teclado(); }
    else if (codigo == TAB5_TECLA_ESC)    { busca_limpar(); busca_fechar_teclado(); }
    else if (codigo == TAB5_TECLA_APAGAR) { busca_tecla(TECVIRT_APAGAR); }
    else if (texto && texto[0]) {
      char cru[8];
      tab5_teclado_ascii(texto, cru, sizeof(cru));
      for (const char* c = cru; *c; ++c) { busca_tecla((unsigned char)*c); }
    }
    return;
  }
  if (s_pag != PAG_MSGS || mesh_s_conv < 0) { return; }
  conversa_teclar(&s_c, texto, codigo, mod);
}

// TROCA DE PAGINA num lugar so. As listas da pagina que sai param de
// responder ao toque (lista_esconder) - voltam quando forem desenhadas.
static void ir_para_pagina(pagina_t p)
{
  s_pag = p;
  s_no_sel = 0;   // tocar no trilho volta sempre para a tabela
  conversa_fechar_teclado(&s_c);   // devolve a navegacao, se estava travada
  lista_esconder(&s_c.lista);
  lista_esconder(&s_l_conv);
  lista_esconder(&s_l_nos);
  lista_esconder(&s_l_cfg);
  if (s_editando) { s_editando = 0; tecvirt_fechar(&s_kb_cfg); }
  if (tecvirt_aberto(&s_kb_busca)) { tecvirt_fechar(&s_kb_busca); }   // a busca em si fica
  s_diario = false;
  s_web_pg = false;
  s_redes_pg = false;
  s_net_pg = false;
  s_net_ed = false;
  memset(&s_net_p, 0, sizeof(s_net_p));
  s_grupo = -1;
  s_cfg_msg[0] = 0;
  pintura_pedir(&s_p);
  sistema_pedir_redesenho_completo();
}

// ABRIR UMA CONVERSA (c = -1: nenhuma, "escolha uma") - da coluna, de um no ou
// de um canal disponivel. Conversa abre na mensagem mais recente.
static void abrir_conversa(int c)
{
  mesh_s_conv = c;
  s_conv_frente = (c >= 0);   // em pe, a escolhida vem para a frente (deitado, nada muda)
  s_ab = chave_de(c);
  s_conv_ver = tab5_mesh_conversas_versao();
  s_hist_pronto = false;
  atualizar_eh_canal();
  conversa_trocar(&s_c);   // fecha o teclado, vai ao fim, re-mede
  if (c >= 0) { tab5_mesh_marcar_conversa_lida(c); }
  if (s_pag == PAG_MSGS && c >= 0) {
    // Trocando de conversa pela coluna: o teclado (e o rascunho) ficam.
    pintura_pedir(&s_p);
    sistema_pedir_redesenho_completo();
    return;
  }
  ir_para_pagina(PAG_MSGS);
}

static void ao_tocar_nivel(int x, int y, bool pressionado)
{
  // Tecla sob o dedo, soltura e rolagem dos baloes: o componente de conversa.
  if (s_pag == PAG_MSGS && mesh_s_conv >= 0) { conversa_tocar_nivel(&s_c, x, y, pressionado); }

  // As outras duas listas rolam cada uma na sua janela; a que nao esta na tela
  // (lista_esconder) ignora o toque.
  // Teclado do nome do no: as teclas confirmadas nesta amostra.
  if (s_pag == PAG_NO && s_editando) {
    int acoes[8];
    const int n = tecvirt_nivel(&s_kb_cfg, acoes, 8);
    for (int k = 0; k < n && s_editando; ++k) { cfg_tecla(acoes[k]); }
    return;
  }
  // Teclado da busca: as teclas confirmadas; a tabela acima dele ainda rola.
  if (s_pag == PAG_NOS && tecvirt_aberto(&s_kb_busca)) {
    int acoes[8];
    const int n = tecvirt_nivel(&s_kb_busca, acoes, 8);
    for (int k = 0; k < n && tecvirt_aberto(&s_kb_busca); ++k) { busca_tecla(acoes[k]); }
  }

  bool rolou = false;
  rolou |= lista_arrastar(&s_l_conv, x, y, pressionado);
  rolou |= lista_arrastar(&s_l_nos,  x, y, pressionado);
  rolou |= lista_arrastar(&s_l_cfg,  x, y, pressionado);
  if (s_redes_pg) { rolou |= lista_arrastar(&s_l_redes, x, y, pressionado); }
  if (rolou) { sistema_pedir_redesenho(); }
}

static void ao_tocar(int x, int y)
{
  const area_t a = sistema_area();

  // Teclado aberto: a faixa dele (largura inteira) e so dele.
  if (conversa_teclado_aberto(&s_c) && s_pag == PAG_MSGS && y >= a.altura - tecvirt_altura(a)) {
    conversa_tocar(&s_c, x, y);
    return;
  }

  // EM PE, CONVERSA NA FRENTE: nao ha trilho nem coluna embaixo do dedo -
  // so a faixa do VOLTAR e a conversa.
  if (em_pe(a) && s_pag == PAG_MSGS && s_conv_frente && mesh_s_conv >= 0) {
    if (y < FRENTE_TOPO_A) {
      if (x >= 16 && x < 16 + FRENTE_VOLTAR_L
       && y >= FRENTE_VOLTAR_Y && y < FRENTE_VOLTAR_Y + FRENTE_VOLTAR_A) {
        voltar_a_lista();
      }
      return;
    }
    conversa_tocar(&s_c, x, y);
    return;
  }

  if (x < TRILHO_L) {
    for (int i = 0; i < PAG_TOTAL; ++i) {
      const int iy = 16 + i * (ITEM_A + 12);
      if (y >= iy && y < iy + ITEM_A) {
        s_conv_frente = false;   // em pe, o trilho leva sempre a lista
        ir_para_pagina((pagina_t)i);
        return;
      }
    }
    return;
  }

  // PAGINA DE NOS: canal abre a conversa; no abre o cartao dele.
  if (s_pag == PAG_NOS) {
    // CARTAO DE UM NO aberto: ESTRELA, VOLTAR e CONVERSAR.
    if (s_no_sel) {
      const int fav_l = det_fav_l(a);
      const int fx = a.largura - DET_FAV_DIR - fav_l;
      if (s_no_sel != tab5_mesh_meu_numero()
       && x >= fx && x < fx + fav_l && y >= DET_FAV_Y && y < DET_FAV_Y + DET_FAV_A) {
        tab5_mesh_definir_favorito(s_no_sel, !tab5_mesh_favorito(s_no_sel));
        sistema_pedir_redesenho();
        return;
      }
      const int by = a.altura - DET_BTN_Y_DA_BASE;
      if (y >= by && y < by + DET_BTN_A) {
        const int bx = TRILHO_L + 24;
        const int bl = det_btn_l(a);
        if (x >= bx && x < bx + bl) {
          s_no_sel = 0;
          pintura_pedir(&s_p);
          sistema_pedir_redesenho_completo();
        } else if (x >= bx + bl + 24 && x < bx + 2 * bl + 24
                && s_no_sel != tab5_mesh_meu_numero()) {
          // -1 quando o numero nao serve (zero, ou o proprio no).
          const int c = tab5_mesh_conversa_com(s_no_sel);
          s_no_sel = 0;
          if (c >= 0) { abrir_conversa(c); }
        }
      }
      return;
    }

    // O TECLADO DA BUSCA aberto: a faixa dele e so dele.
    if (tecvirt_aberto(&s_kb_busca) && tecvirt_contem(&s_kb_busca, a.x + x, a.y + y)) {
      const int ac = tecvirt_clique(&s_kb_busca, a.x + x, a.y + y);
      if (ac) { busca_tecla(ac); }
      return;
    }

    // A CAIXA DE PESQUISA: o "x" limpa; o resto abre o teclado.
    {
      const chip_t& k = s_chip_busca;
      if (k.l && x >= k.x && x < k.x + k.l && y >= k.y && y < k.y + k.a) {
        if (s_busca[0] && x >= k.x + k.l - BUSCA_LIMPAR_L) { busca_limpar(); }
        else { busca_abrir_teclado(); }
        return;
      }
    }

    // UM CANAL: abre a conversa dele (e a traz de volta, se foi apagada).
    for (int i = 0; i < s_n_chips; ++i) {
      const chip_t& k = s_chips[i];
      if (x < k.x || x >= k.x + k.l || y < k.y || y >= k.y + k.a) { continue; }
      const int c = tab5_mesh_conversa_do_canal(k.idx);
      if (c >= 0) { abrir_conversa(c); }
      return;
    }

    // UM NO: o cartao dele. Dali se conversa - antes o toque abria a conversa
    // direto, e nao havia onde ver MAC, tensao ou posicao.
    const int i = lista_item_tocado(&s_l_nos, x, y, nullptr);
    if (i < 0) { return; }
    mesh_no_t no;
    if (!tab5_mesh_no_busca(s_busca, i, &no)) { return; }
    s_no_sel = no.num;
    pintura_pedir(&s_p);
    sistema_pedir_redesenho_completo();
    return;
  }

  if (s_pag == PAG_NO) { tocar_config(x, y); return; }
  if (s_pag == PAG_MALHA) { return; }

  if (s_pag != PAG_MSGS) { return; }

  // Coluna de conversas.
  if (x < TRILHO_L + s_col_l) {
    int dy = 0;
    const int i = lista_item_tocado(&s_l_conv, x, y, &dy);
    if (i < 0) { return; }
    dy -= 8;                          // o item comeca pelo respiro (conv_desenhar)
    if (dy < 0 || dy >= ITEM_A) { return; }   // no respiro entre itens: nada

    // O "x" vem ANTES de selecionar: ele esta dentro do item, e tratar a
    // selecao primeiro faria o toque no botao virar toque no item.
    const int ax = TRILHO_L + 1 + apagar_dx();
    if (i == mesh_s_conv
     && x >= ax && x < ax + APAGAR_L
     && dy >= APAGAR_DY && dy < APAGAR_DY + APAGAR_A
     && tab5_mesh_esquecer_conversa(i)) {
      // Nenhuma conversa seleciona no lugar: os indices acima deste andaram
      // uma casa, e herdar o indice abriria a conversa VIZINHA - uma que a
      // pessoa nao pediu. Melhor voltar para "escolha uma".
      abrir_conversa(-1);
      return;
    }
    abrir_conversa(i);
    return;
  }

  // Barra de escrita e botao enviar.
  // Coluna da conversa: barra de escrita, ENV e rolagem - o componente.
  if (mesh_s_conv >= 0) { conversa_tocar(&s_c, x, y); }
}

static bool ao_abrir(void)
{
  // O ARQUIVO PRIMEIRO. Sem o C6L plugado a lista de conversas nasce vazia -
  // ela so era povoada por mensagem chegando - e o app abria dizendo "sem
  // conversas" com o cartao cheio delas.
  //
  // Vale com o radio no ar tambem: conversas antigas aparecem desde a abertura,
  // e nao so quando alguem volta a falar.
  // Nomes, conversas e o historico vem do cartao PELO WORKER, junto da
  // primeira carga (ver trabalho_carregar) - a tela abre na hora.
  s_carga_cartao = true;
  s_carga_pedida = false;
  s_carga_pronta = false;
  s_recarregar   = false;
  s_hist_chave   = chave_t{ };

  s_hist_pronto = false;
  conversa_abrir(&s_c, &s_p, conv_contar, conv_ler, conv_enviar, nullptr);
  s_c.limite = MESH_PAYLOAD_MAX;   // o teto do protocolo, nao o do buffer (ver tab5_mesh.hpp)
  s_c.cor_enviar = TEMA_DESTAQUE;   // a unica cor forte do tema (o texto do botao e TEMA_SOBRE_DESTAQUE)
  lista_definir(&s_l_conv, ROLAGEM_INICIO, 0, conv_altura, conv_desenhar, nullptr);
  s_l_conv.topo = 4;   // o 1o item ja traz 8px de respiro: 12 do topo, como sempre foi
  lista_definir(&s_l_nos, ROLAGEM_INICIO, 0, nos_altura, nos_desenhar, nullptr);
  lista_definir(&s_l_cfg, ROLAGEM_INICIO, 0, cfg_altura, cfg_desenhar, nullptr);
  lista_definir(&s_l_redes, ROLAGEM_INICIO, 0, rede_altura, rede_desenhar, nullptr);
  pintura_pedir(&s_p);
  sistema_travar_navegacao(false);
  return true;
}

static void ao_fechar(void)
{
  // Devolve o historico. Sao 38KB de PSRAM que nao fazem falta a ninguem com o
  // app fechado - e o motivo de ele nao ser mais estatico.
  // (O sistema ja esperou o worker antes de chamar isto.)
  if (s_hist)  { heap_caps_free(s_hist);  s_hist  = nullptr; }
  if (s_carga) { heap_caps_free(s_carga); s_carga = nullptr; }
  s_hist_chave = chave_t{ };
  if (s_editando) { s_editando = 0; tecvirt_fechar(&s_kb_cfg); }
  if (tecvirt_aberto(&s_kb_busca)) { tecvirt_fechar(&s_kb_busca); }
  s_diario = false;
  s_web_pg = false;
  s_redes_pg = false;
  s_net_pg = false;
  s_net_ed = false;
  memset(&s_net_p, 0, sizeof(s_net_p));
  s_grupo = -1;
  conversa_fechar(&s_c);   // devolve o cache dos baloes e a navegacao
  s_n_hist = 0;
  s_hist_pronto = false;
}

static uint32_t periodo_agora(void)
{
  return (s_pag == PAG_NOS || s_pag == PAG_MALHA || s_pag == PAG_NO) ? 1000 : 0;
}

extern const app_t APP_MESH = {
  .nome  = "Mesh",
  .cor   = TFT_GREENYELLOW,
  // O Grove ja e mantido pelo ajuste do sistema. O IMU e para a tela seguir
  // o aparelho.
  .perfil = { .imu = true },
  // O historico e dos servicos do Mesh (services/mesh), que montam o cartao
  // so enquanto leem ou gravam: o app nao segura o cartao aberto (com ele
  // aberto, o pendrive era recusado).
  .usa_sd = false,
  // Em pe ou deitado conforme o aparelho; com o teclado encaixado, deitado -
  // a postura natural de quem escreve (ver o LAYOUT no topo do arquivo).
  .orientacao = ORI_SEGUE_IMU,
  .ao_abrir  = ao_abrir,
  .ao_fechar = ao_fechar,
  .desenhar  = desenhar,
  .ao_tocar  = ao_tocar,
  .ao_tocar_nivel = ao_tocar_nivel,   // acende a tecla sob o dedo
  .ao_teclar = ao_teclar,             // teclado fisico do ExtPort1
  .periodo_ms = 0,
  // Relogio SO nas paginas que mostram idade (Nos, Malha); nas mensagens o app
  // so acorda por aviso - mensagem nova, envio confirmado, no novo, carga do
  // historico pronta.
  .periodo_agora = periodo_agora,
  .avisos = AVISO_MESH_MSG | AVISO_MESH_ENVIO | AVISO_MESH_NOS | AVISO_TRABALHO | AVISO_WEB,
  .ao_aviso = ao_aviso,
  // O icone da tela inicial e a propria marca do Meshtastic: quem procura este
  // app procura o simbolo dele, nao a letra M.
  .icone = marca_mesh,
  .ao_girar = ao_girar,
  .comandos = nullptr,
  .fora_da_home = false,
  .no_pc = tab5_mesh_web_ligar,
  .fundo_tema = true,   // o sistema limpa a área com o fundo do tema
};
