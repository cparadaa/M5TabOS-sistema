// Servico Mesh - os NOS: a tabela e a ordem dela, os favoritos, a busca, o cache
// de nomes e a NodeDB do Tab5 no cartao. Partido de tab5_mesh.cpp (o servico);
// o que as partes compartilham esta em mesh/tab5_mesh_interno.hpp.

#include "app.h"   // sistema_avisar
#include "esp_attr.h"   // EXT_RAM_BSS_ATTR - buffers grandes na PSRAM
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include "tab5_sd.hpp"
#include "tab5_mutex.hpp"
#include "ajustes.h"
#include "tab5_mesh_ble.hpp"
#include "tab5_mesh_diario.hpp"
#include "mesh_pb.hpp"
#include "tab5_hora.hpp"
#include "nvs.h"
#include "mesh/tab5_mesh_interno.hpp"

static const char *TAG = "mesh";

// Ha quantos segundos o no foi ouvido; -1 = nao da para saber. Duas fontes,
// nesta ordem:
//   1. nosso proprio avistamento - exato, nao depende de relogio nenhum;
//   2. o last_heard do radio contra a hora emprestada dele.
static int32_t idade_do_no(const mesh_no_t& no)
{
  const uint32_t ep = tab5_mesh_epoch();
  if (no.visto_local_ms != 0) { return (int32_t)((tab5_agora_ms() - no.visto_local_ms) / 1000); }
  if (no.visto_em >= EPOCH_MINIMO && ep > no.visto_em) { return (int32_t)(ep - no.visto_em); }
  return -1;
}

// A ORDEM DA TABELA, a mesma do cliente web: o nosso no primeiro, depois do
// ouvido mais recentemente ao mais antigo, e os sem idade conhecida no fim.
// Antes era a ordem das vagas do vetor - um no que acabou de falar podia
// estar no fim da lista, e a lista "embaralhava" a cada despejo. SEM TRAVA.
static uint32_t s_prox_fixado = 0;
static int      fav_pos(uint32_t num);   // favoritos, mais abaixo

// LUGAR FIXO dado NA CHEGADA do dado (pacote ou NodeInfo com SNR de radio), e
// nao quando a tela pergunta: e ele que protege o no do despejo (achar_no),
// e o despejo acontece com a tela fechada tambem. SEM TRAVA.
void fixar_se_pelo_ar(mesh_no_t* no)
{
  if (no && !no->fixado && no->num != s_meu_num && tab5_mesh_no_pelo_ar(no)) {
    no->fixado = ++s_prox_fixado;
  }
}

// ORDEM EM CACHE. Com 256 vagas, ordenar a cada tab5_mesh_no(i) - uma vez por
// linha desenhada - viraria milhoes de comparacoes por segundo. A ordem so
// muda quando a tabela muda (s_nos_ver) ou quando o tempo passa (a idade da
// faixa de baixo): refeita no maximo uma vez por segundo, ou na hora se algo
// chegou. Com a trava tomada.
uint32_t s_nos_ver = 0;
EXT_RAM_BSS_ATTR static int s_ordem[MESH_MAX_NOS];
static int      s_ordem_n = 0;
static uint32_t s_ordem_ver = UINT32_MAX;
static uint32_t s_ordem_ms = 0;
static uint32_t s_ordem_geracao = 0;   // +1 a cada ordenacao: a busca refaz o filtro so entao

static int ordem_nos(void)
{
  if (s_ordem_ver == s_nos_ver && (tab5_agora_ms() - s_ordem_ms) < 1000) { return s_ordem_n; }
  // MUDOU, MAS ACABOU DE ORDENAR: espera 250 ms. Com trafego denso a
  // versao muda a cada pacote, e a pagina de nos pede uma linha de cada vez -
  // eram varias ordenacoes de 256 nos por desenho. Um quarto de segundo de
  // atraso na ordem nao se ve; o processamento, sim.
  if (s_ordem_ver != UINT32_MAX && (tab5_agora_ms() - s_ordem_ms) < 250) { return s_ordem_n; }
  s_ordem_ver = s_nos_ver;
  s_ordem_ms  = tab5_agora_ms();

  // Tres faixas:
  //   0 - o nosso no;
  //   F - os FAVORITOS (estrela), na ordem em que foram marcados;
  //   1 - os ouvidos PELO AR, FIXOS: na ordem em que apareceram com SNR pela
  //       primeira vez - nao descem quando outro fala;
  //   2 - o resto (MQTT, ou sem SNR), do ouvido mais recentemente ao mais
  //       antigo. Quem ganha SNR sobe para a faixa 1 e fica.
  EXT_RAM_BSS_ATTR static int64_t chave[MESH_MAX_NOS];
  int n = 0;
  for (int i = 0; i < MESH_MAX_NOS; ++i) {
    mesh_no_t& no = s_nos[i];
    if (!no.usado) { continue; }
    fixar_se_pelo_ar(&no);
    int64_t k;
    const int fp = fav_pos(no.num);
    if (no.num == s_meu_num) { k = -1; }
    else if (fp >= 0)        { k = fp; }                         // favoritos, na ordem em que foram marcados
    else if (no.fixado)      { k = ((int64_t)1 << 20) + no.fixado; }
    else {
      int64_t idade = idade_do_no(no);
      if (idade < 0) { idade = INT32_MAX; }
      k = ((int64_t)1 << 40) + idade;
    }
    int j = n++;
    while (j > 0 && k < chave[j - 1]) { s_ordem[j] = s_ordem[j - 1]; chave[j] = chave[j - 1]; --j; }
    s_ordem[j] = i; chave[j] = k;
  }
  s_ordem_n = n;
  ++s_ordem_geracao;
  return n;
}

bool tab5_mesh_no_pelo_ar(const mesh_no_t* no)
{
  return !no->via_mqtt && no->snr > -128.0f;
}

// ---------------------------------------------------------------------------
// FAVORITOS - a estrela, a mesma do no e do cliente web
// ---------------------------------------------------------------------------
// O Meshtastic ja tem favoritos: NodeInfo.is_favorite (campo 10) no dump, e o
// AdminMessage set_favorite_node (39) / remove_favorite_node (40) para mudar.
// Usando os dele, a estrela marcada aqui aparece no cliente web e na tela do
// no, e o no protege o favorito do despejo da NodeDB dele tambem.
//
// Guardados TAMBEM aqui (NVS), na ordem em que foram marcados - e a ordem no
// topo da tabela. Sem o radio (modo internet) a lista local vale sozinha; a
// mudanca feita assim fica PENDENTE e vai para o no no proximo dump. Com o
// radio, o dump e a verdade: estrela tirada no cliente web some daqui tambem.
#define MESH_NVS_FAVS  "favs"
uint32_t s_favs[MESH_MAX_FAVS];
int      s_n_favs = 0;
static bool     s_favs_lidos = false;
typedef struct { uint32_t num; bool quer; } fav_pend_t;
#define MESH_FAV_PEND 8
static fav_pend_t s_fav_pend[MESH_FAV_PEND];
static int        s_n_fav_pend = 0;

void recordar_favs(void)
{
  if (s_favs_lidos) { return; }
  s_favs_lidos = true;
  nvs_handle_t h;
  if (nvs_open(MESH_NVS, NVS_READONLY, &h) != ESP_OK) { return; }
  size_t n = sizeof(s_favs);
  if (nvs_get_blob(h, MESH_NVS_FAVS, s_favs, &n) == ESP_OK) { s_n_favs = (int)(n / sizeof(uint32_t)); }
  nvs_close(h);
}

static void salvar_favs(void)
{
  nvs_handle_t h;
  if (nvs_open(MESH_NVS, NVS_READWRITE, &h) != ESP_OK) { return; }
  if (s_n_favs) { nvs_set_blob(h, MESH_NVS_FAVS, s_favs, s_n_favs * sizeof(uint32_t)); }
  else          { nvs_erase_key(h, MESH_NVS_FAVS); }
  nvs_commit(h);
  nvs_close(h);
}

// Posicao na lista de favoritos; -1 = nao e. SEM TRAVA.
static int fav_pos(uint32_t num)
{
  recordar_favs();
  for (int i = 0; i < s_n_favs; ++i) { if (s_favs[i] == num) { return i; } }
  return -1;
}

// So a lista local (e a NVS, se mudou). SEM TRAVA.
static void fav_aplicar(uint32_t num, bool quer)
{
  const int pos = fav_pos(num);
  if (quer == (pos >= 0)) { return; }
  if (quer) {
    if (s_n_favs >= MESH_MAX_FAVS) { ESP_LOGW(TAG, "favoritos cheios (%d)", MESH_MAX_FAVS); return; }
    s_favs[s_n_favs++] = num;
    achar_no(num);   // favorito fica na tabela mesmo sem ter falado nesta sessao
  } else {
    for (int i = pos; i < s_n_favs - 1; ++i) { s_favs[i] = s_favs[i + 1]; }
    --s_n_favs;
  }
  salvar_favs();
  ++s_nos_ver;
}

static int fav_pendente(uint32_t num)
{
  for (int i = 0; i < s_n_fav_pend; ++i) { if (s_fav_pend[i].num == num) { return i; } }
  return -1;
}

// O no esta do outro lado e ja terminou o dump: da para mandar admin.
bool radio_aceita_admin(void)
{
  return s_servico_ativo && s_meu_num != 0 && s_config_ok_ms != 0
      && (s_via == AJUSTES_MESH_SERIAL || (s_via == AJUSTES_MESH_BLUETOOTH && mesh_ble_conectado()));
}

// ToRadio{ packet: MeshPacket{ to: nos, decoded: Data{ portnum: ADMIN(6),
// payload: <AdminMessage> }, id } }. Sem 'from' (= 0): e o que o no
// reconhece como admin LOCAL, de um cliente conectado - aceito sem chave de
// sessao (AdminModule.cpp, ramo mp.from == 0, da 2.5 ate a atual).
// Unico caminho de AdminMessage do servico: favoritos e tab5_mesh_admin.
#define PORT_ADMIN 6
bool tab5_mesh_interno_admin(const uint8_t* adm, size_t n_adm)
{
  if (!adm || n_adm == 0 || n_adm > MESH_PAYLOAD_MAX + 33) { return false; }   // Data.payload: 233
  if (s_meu_num == 0) { return false; }   // sem o nosso numero, o 'to' iria 0 - e 0 nao e o no
  const mesh_pb_envelope_t e = { s_meu_num, -1, PORT_ADMIN, adm, n_adm, esp_random() | 1u, 0, false };
  uint8_t quadro[MESH_PAYLOAD_MAX + 88];
  const size_t n = mesh_pb_to_radio(&e, quadro, sizeof(quadro));
  const bool ok = n && enlace_enviar(quadro, n);
  if (ok) { mesh_diario_nota_envio(MESH_ENV_ADMIN); }
  return ok;
}

bool tab5_mesh_interno_pode_admin(void) { return radio_aceita_admin(); }

static bool enviar_admin_favorito(uint32_t num, bool quer)
{
  // AdminMessage{ 39: set_favorite_node | 40: remove_favorite_node = num }
  uint8_t adm[12];
  pb_escritor_t w = pb_escritor(adm, sizeof(adm));
  pb_w_num(&w, quer ? 39 : 40, num);
  const bool ok = tab5_mesh_interno_admin(adm, pb_tamanho(&w));
  ESP_LOGI(TAG, "favorito %08lX -> %s no no: %s", (unsigned long)num, quer ? "marcar" : "tirar", ok ? "enviado" : "falhou");
  return ok;
}

// Fim do dump: o que foi mudado sem o radio vai agora. Com a trava.
void enviar_favs_pendentes(void)
{
  if (!radio_aceita_admin()) { return; }
  for (int i = 0; i < s_n_fav_pend; ++i) { enviar_admin_favorito(s_fav_pend[i].num, s_fav_pend[i].quer); }
  s_n_fav_pend = 0;
}

bool tab5_mesh_favorito(uint32_t num)
{
  travar();
  const bool f = fav_pos(num) >= 0;
  destravar();
  return f;
}

void tab5_mesh_definir_favorito(uint32_t num, bool quer)
{
  if (num == 0) { return; }
  travar();
  if (num != s_meu_num) {
    fav_aplicar(num, quer);
    // Para o no: agora, se ele esta ai; senao fica pendente para o dump.
    int k = fav_pendente(num);
    if (radio_aceita_admin() && enviar_admin_favorito(num, quer)) {
      if (k >= 0) { s_fav_pend[k] = s_fav_pend[--s_n_fav_pend]; }
    } else {
      if (k < 0 && s_n_fav_pend < MESH_FAV_PEND) { k = s_n_fav_pend++; }
      if (k >= 0) { s_fav_pend[k] = fav_pend_t{ num, quer }; }
    }
  }
  destravar();
  sistema_avisar(AVISO_MESH_NOS);
}

// O que o dump disse de um no (campo 10 ausente = false, e proto3). Pendente
// nosso vence: e uma mudanca que o no ainda nao recebeu.
void fav_do_radio(uint32_t num, bool fav)
{
  if (num == s_meu_num || s_via == AJUSTES_MESH_INTERNET || fav_pendente(num) >= 0) { return; }
  fav_aplicar(num, fav);
}

bool tab5_mesh_no(int idx, mesh_no_t* saida)
{
  bool ok = false;
  travar();
  const int n = ordem_nos();
  if (idx >= 0 && idx < n && s_nos[s_ordem[idx]].usado) {   // ordem de ate 250 ms atras
    *saida = s_nos[s_ordem[idx]];
    // Campo DERIVADO, preenchido so na copia: sem isso envelheceria entre a
    // leitura e o desenho. Sem fonte nenhuma fica -1, e a tela escreve "?".
    saida->idade_s = idade_do_no(*saida);
    saida->favorito = fav_pos(saida->num) >= 0;
    ok = true;
  }
  destravar();
  return ok;
}

// ---------------------------------------------------------------------------
// BUSCA NA TABELA (ver tab5_mesh.hpp)
// ---------------------------------------------------------------------------
static const char* curto_conhecido(uint32_t num);

// Minusculas e sem acento, para "jose" achar "José" e "BR" achar "br". So o
// Latin-1 de duas bytes (C3 xx), que e onde estao os acentos do portugues; o
// resto do UTF-8 passa como veio (um emoji so acha o mesmo emoji).
static void dobrar(const char* in, char* out, size_t tam)
{
  static const char LATIN[64 + 1] =
    "aaaaaaaceeeeiiii"   // C3 80..8F  (A-grave .. I-trema)
    "dnooooo*ouuuuyps"   // C3 90..9F
    "aaaaaaaceeeeiiii"   // C3 A0..AF
    "dnooooo/ouuuuypy";  // C3 B0..BF
  size_t o = 0;
  const unsigned char* p = (const unsigned char*)in;
  while (*p && o + 1 < tam) {
    if (*p == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) { out[o++] = LATIN[p[1] - 0x80]; p += 2; continue; }
    out[o++] = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : (char)*p;
    ++p;
  }
  out[o] = 0;
}

// O no bate com a busca (ja dobrada)? Nome longo, curto - os da tabela ou, sem
// eles, os do cache de nomes, que e o que a linha mostra - e o id "!a1b2c3d4"
// (com ou sem o '!'). SEM TRAVA: chamada de dentro dela.
static bool no_bate(const mesh_no_t& no, const char* busca)
{
  char t[MESH_NOME_LONGO + 8];
  const char* nomes[2] = { no.longo[0] ? no.longo : longo_conhecido(no.num),
                           no.curto[0] ? no.curto : curto_conhecido(no.num) };
  for (const char* nome : nomes) {
    if (!nome) { continue; }
    dobrar(nome, t, sizeof(t));
    if (strstr(t, busca)) { return true; }
  }
  snprintf(t, sizeof(t), "!%08lx", (unsigned long)no.num);
  return strstr(t, busca) != nullptr;
}

// O filtro em cache, como a ordem: a pagina pede uma linha de cada vez, e
// filtrar 256 nos por linha desenhada seria o mesmo desperdicio. Refeito
// quando a busca muda ou a ordem foi refeita. Com a trava tomada.
EXT_RAM_BSS_ATTR static int s_busca_ordem[MESH_MAX_NOS];
static int      s_busca_n = 0;
static char     s_busca_txt[MESH_BUSCA_MAX] = "";
static uint32_t s_busca_geracao = UINT32_MAX;

static int filtrar_nos(const char* busca)
{
  const int n = ordem_nos();
  char b[MESH_BUSCA_MAX];
  dobrar(busca ? busca : "", b, sizeof(b));
  if (s_busca_geracao == s_ordem_geracao && strcmp(b, s_busca_txt) == 0) { return s_busca_n; }
  s_busca_geracao = s_ordem_geracao;
  snprintf(s_busca_txt, sizeof(s_busca_txt), "%s", b);
  s_busca_n = 0;
  for (int k = 0; k < n; ++k) {
    if (!b[0] || no_bate(s_nos[s_ordem[k]], b)) { s_busca_ordem[s_busca_n++] = s_ordem[k]; }
  }
  return s_busca_n;
}

int tab5_mesh_num_nos_busca(const char* busca)
{
  travar();
  const int n = filtrar_nos(busca);
  destravar();
  return n;
}

bool tab5_mesh_no_busca(const char* busca, int idx, mesh_no_t* saida)
{
  bool ok = false;
  travar();
  const int n = filtrar_nos(busca);
  if (idx >= 0 && idx < n && s_nos[s_busca_ordem[idx]].usado) {
    *saida = s_nos[s_busca_ordem[idx]];
    saida->idade_s = idade_do_no(*saida);
    saida->favorito = fav_pos(saida->num) >= 0;
    ok = true;
  }
  destravar();
  return ok;
}

// ---------------------------------------------------------------------------
// CACHE DE NOMES DOS NOS
// ---------------------------------------------------------------------------
// COMO O MESHTASTIC IDENTIFICA UM NO. O identificador e o "num" - um uint32
// derivado dos ultimos 4 bytes do MAC do radio. Ele NAO muda: e ele que vai no
// campo 1 do MeshPacket e e por ele que se endereca uma mensagem direta.
//
// O nome longo e o curto sao ETIQUETAS que a pessoa escolhe e pode trocar
// quando quiser. Trocar o nome NAO cria outro no - o num continua o mesmo, e
// para o sistema e o mesmo interlocutor com rotulo novo. Um no novo de verdade
// e um num novo, e ai e outra conversa mesmo.
//
// Por isso o arquivo da conversa sempre foi nomeado pelo NUM (no-A1B2C3D4.txt),
// e isso estava certo desde o inicio. O que faltava era guardar a ETIQUETA: sem
// o C6L plugado, s_nos esta vazio e a conversa nascia chamada "A1B2C3D4".
//
// Este cache resolve isso. Vive em nos.txt, uma linha por avistamento, e o
// leitor fica com a ULTIMA de cada num - assim renomear e so acrescentar uma
// linha, sem reescrever arquivo nenhum.
//
// HISTORICO, NAO "AO VIVO" - de proposito DESACOPLADO de MESH_MAX_NOS (32,
// o tamanho da tabela AO VIVO em s_nos - ver achar_no() e o despejo do mais
// antigo). Um historico de nos que so cresce - MESH_MAX_NOS era pequeno demais para
// isso TAMBEM (o despejo da tabela ao vivo resolve "quem esta ativo agora",
// mas nao "quem ja vi alguma vez"). Cada entrada e so 4+24+1 bytes; 256
// delas sao ~8KB - nada para um ESP32-P4 com PSRAM.
//
// SOBRE A BUSCA ANTES DE ACRESCENTAR (a duvida): sim, precisa
// - e uma busca LINEAR, sem indice nenhum, exatamente como ja era com 32.
// Ela so roda quando uma NodeInfo chega (nao a cada pacote - NodeInfo e
// esporadica) e uma vez por linha ao carregar o arquivo no boot. Com 256
// entradas isso e no maximo 256 comparacoes de inteiro - microssegundos,
// mesmo no pior caso. Nao compensa uma estrutura mais esperta (hash, arvore)
// para um numero deste tamanho; so compensaria na casa dos milhares.
#define MESH_ARQ_NOS "nos.txt"
// 1024: o cache de nomes enchia com os nos da internet e, cheio, recusava
// nome novo - inclusive o de um vizinho de radio. ~53 KB na PSRAM.
#define MESH_MAX_CONHECIDOS 1024

// CURTO E LONGO, os dois - a tabela de nos saia toda sem nome. Antes so o curto persistia aqui
// (ver o aviso grande junto de lembrar_nome() mais abaixo).
typedef struct { uint32_t num; char curto[MESH_NOME_CURTO]; char longo[MESH_NOME_LONGO]; bool sujo; } no_conhecido_t;
EXT_RAM_BSS_ATTR static no_conhecido_t s_conhecidos[MESH_MAX_CONHECIDOS];
static int s_n_conhecidos = 0;

// O ARQUIVO JA FOI LIDO NESTA SESSAO. Antes a guarda era "s_n_conhecidos ==
// 0": se o cartao nao montava na hora da leitura, a tabela ficava vazia, o
// dump do no a enchia, e da primeira vez que o cartao voltava TODOS os nos
// iam de novo para o fim do arquivo - que so crescia, com as mesmas linhas,
// a cada boot assim. Agora ninguem grava antes de ler (descarregar_nomes).
bool s_nomes_lidos = false;

// SEM TRAVA: chamadas de dentro dela. Nulo (nao string vazia) quando nao ha
// cache - "sem entrada" e "entrada vazia" sao a mesma coisa aqui, os dois
// jeitos de dizer "nada para mostrar".
static const char* curto_conhecido(uint32_t num)
{
  for (int i = 0; i < s_n_conhecidos; ++i) {
    if (s_conhecidos[i].num == num) { return s_conhecidos[i].curto[0] ? s_conhecidos[i].curto : nullptr; }
  }
  return nullptr;
}

const char* longo_conhecido(uint32_t num)
{
  for (int i = 0; i < s_n_conhecidos; ++i) {
    if (s_conhecidos[i].num == num) { return s_conhecidos[i].longo[0] ? s_conhecidos[i].longo : nullptr; }
  }
  return nullptr;
}

// O QUE AS FONTES DO SISTEMA DESENHAM. As fontes suaves cobrem ASCII e o
// Latin-1 que o portugues usa (2 bytes em UTF-8, lider 0xC2/0xC3); emoji e
// simbolos (3-4 bytes) viram caixinha. Copia so o desenhavel, sem espacos
// nas pontas. "?" sozinho e o placeholder do Meshtastic para "ninguem
// escolheu um curto" - vale como vazio.
void nome_legivel(const char* in, char* out, size_t tam)
{
  if (!tam) { return; }
  size_t o = 0;
  const unsigned char* p = (const unsigned char*)(in ? in : "");
  while (*p && o + 1 < tam) {
    if (*p >= 0x20 && *p <= 0x7e) { out[o++] = (char)*p++; continue; }
    if ((*p == 0xC2 || *p == 0xC3) && (p[1] & 0xC0) == 0x80) {
      if (o + 2 >= tam) { break; }
      out[o++] = (char)p[0]; out[o++] = (char)p[1]; p += 2; continue;
    }
    // Sequencia que a fonte nao tem: pula ela inteira.
    const int n = ((*p & 0xE0) == 0xC0) ? 2 : ((*p & 0xF0) == 0xE0) ? 3 : ((*p & 0xF8) == 0xF0) ? 4 : 1;
    for (int k = 0; k < n && *p; ++k) { ++p; }
  }
  out[o] = 0;
  size_t ini = 0;
  while (out[ini] == ' ') { ++ini; }
  if (ini) { memmove(out, out + ini, strlen(out + ini) + 1); }
  size_t n = strlen(out);
  while (n && out[n - 1] == ' ') { out[--n] = 0; }
  if (!strcmp(out, "?")) { out[0] = 0; }
}

bool curto_utilizavel(const char* s)
{
  char t[MESH_NOME_CURTO * 2];
  nome_legivel(s, t, sizeof(t));
  return t[0] != 0;
}

// SEM TRAVA: chamada de dentro dela (lembrar_nome, achar_ou_criar_conversa).
// Ao vivo primeiro (mais fresco), depois o cache do cartao.
void nome_exibicao(uint32_t num, char* out, size_t out_tam)
{
  const char* curto = nullptr;
  const char* longo = nullptr;
  for (int i = 0; i < MESH_MAX_NOS; ++i) {
    if (s_nos[i].usado && s_nos[i].num == num) {
      if (s_nos[i].curto[0]) { curto = s_nos[i].curto; }
      if (s_nos[i].longo[0]) { longo = s_nos[i].longo; }
      break;
    }
  }
  char t[MESH_NOME_LONGO];
  nome_legivel(curto ? curto : curto_conhecido(num), t, sizeof(t));
  if (!t[0]) { nome_legivel(longo ? longo : longo_conhecido(num), t, sizeof(t)); }
  if (t[0]) { snprintf(out, out_tam, "%s", t); return; }
  snprintf(out, out_tam, "!%08lx", (unsigned long)num);
}

void tab5_mesh_nome_exibicao(uint32_t num, char* out, size_t out_tam)
{
  travar();
  nome_exibicao(num, out, out_tam);
  destravar();
}

// Ver o aviso completo em tab5_mesh.hpp. Wrapper fino sobre curto_conhecido()
// (privada deste arquivo) - existe so para expor "ha cache, sem hex" para
// quem, diferente de tab5_mesh_nome_exibicao(), NAO quer o fallback de hex.
bool tab5_mesh_nome_cache(uint32_t num, char* out, size_t out_tam)
{
  const char* cache = curto_conhecido(num);
  if (!cache) { return false; }
  snprintf(out, out_tam, "%s", cache);
  return true;
}

// Irma de tab5_mesh_nome_cache(), para o nome LONGO - ver o aviso grande no
// .hpp.
bool tab5_mesh_nome_longo_cache(uint32_t num, char* out, size_t out_tam)
{
  const char* cache = longo_conhecido(num);
  if (!cache) { return false; }
  snprintf(out, out_tam, "%s", cache);
  return true;
}

// Grava so quando MUDA. O node_info chega a cada apresentacao e a cada
// telemetria; anotar sempre encheria o cartao com a mesma linha.
//
// 'curto'/'longo': cada um NULO independentemente quando este NodeInfo nao
// trouxe (ou trouxe algo inutilizavel) daquele campo - so atualiza o que
// veio, nunca apaga o outro que ja estava no cache.
void lembrar_nome(uint32_t num, const char* curto, const char* longo)
{
  const bool tem_curto = curto && curto[0];
  const bool tem_longo = longo && longo[0];
  if (num == 0 || (!tem_curto && !tem_longo)) { return; }

  int idx = -1;
  for (int i = 0; i < s_n_conhecidos; ++i) {
    if (s_conhecidos[i].num == num) { idx = i; break; }
  }
  if (idx < 0) {
    if (s_n_conhecidos >= MESH_MAX_CONHECIDOS) { return; }
    idx = s_n_conhecidos++;
    s_conhecidos[idx].num = num;
    s_conhecidos[idx].curto[0] = 0;
    s_conhecidos[idx].longo[0] = 0;
  }

  bool mudou = false;
  if (tem_curto && strcmp(s_conhecidos[idx].curto, curto) != 0) {
    snprintf(s_conhecidos[idx].curto, sizeof(s_conhecidos[idx].curto), "%s", curto);
    mudou = true;
  }
  if (tem_longo && strcmp(s_conhecidos[idx].longo, longo) != 0) {
    snprintf(s_conhecidos[idx].longo, sizeof(s_conhecidos[idx].longo), "%s", longo);
    mudou = true;
  }
  if (!mudou) { return; }

  // NAO GRAVA AGORA - so marca. Gravar aqui montava e desmontava o cartao uma
  // vez POR NO, e o radio despeja a lista inteira de uma vez na apresentacao:
  // no boot sairam oito montagens em um segundo, cada uma com o aviso do LDO
  // do SDMMC atras. A escrita vale um lote, nao um evento.
  s_conhecidos[idx].sujo = true;

  // A conversa que ja exista passa a usar o nome novo, pela MESMA regra da
  // exibicao (curto, longo, id). O ARQUIVO dela e nomeado pelo num, entao
  // renomear aqui nao desgruda o historico.
  for (int i = 0; i < MESH_MAX_CONVERSAS; ++i) {
    if (s_conv[i].usada && !s_conv[i].canal && s_conv[i].no == num) {
      nome_exibicao(num, s_conv[i].nome, sizeof(s_conv[i].nome));
    }
  }
}

// Descarrega os nomes pendentes. UMA montagem para todos.
//
// Chamada pela tarefa do mesh, espacada: o cartao e caro de montar e nome de no
// nao e informacao urgente - se o aparelho reiniciar antes da descarga, o
// proximo node_info reensina.
void descarregar_nomes(void)
{
  int pendentes = 0;
  for (int i = 0; i < s_n_conhecidos; ++i) { if (s_conhecidos[i].sujo) { ++pendentes; } }
  if (pendentes == 0) { return; }

  // Sem ter lido o arquivo, nao da para saber se o nome e novo: tenta ler
  // agora; sem cartao, os pendentes esperam a proxima volta.
  if (!s_nomes_lidos) { carregar_nomes(); }
  if (!s_nomes_lidos) { return; }
  pendentes = 0;
  for (int i = 0; i < s_n_conhecidos; ++i) { if (s_conhecidos[i].sujo) { ++pendentes; } }
  if (pendentes == 0) { return; }

  // FORMATO GANHOU UM CAMPO: "hex,curto,longo" - era so
  // "hex,curto". carregar_nomes() (abaixo) le os dois formatos: uma linha
  // antiga sem o segundo virgula so deixa o longo vazio, sem quebrar nada.
  // Pelo gravador do sistema: entregar e na hora, gravar e em lote.
  for (int i = 0; i < s_n_conhecidos; ++i) {
    if (!s_conhecidos[i].sujo) { continue; }
    char linha[96];
    snprintf(linha, sizeof(linha), "%08lX,%s,%s\n", (unsigned long)s_conhecidos[i].num,
             s_conhecidos[i].curto, s_conhecidos[i].longo);
    tab5_sd_anexar(MESH_PASTA, MESH_ARQ_NOS, linha);
    s_conhecidos[i].sujo = false;
  }
  ESP_LOGI(TAG, "%d nome(s) de no gravados", pendentes);
}

// Le o cache do cartao. Ultima linha de cada num vence.
//
// DOIS FORMATOS aceitos - ver o aviso em descarregar_nomes(): "hex,curto"
// (antigo) e "hex,curto,longo" (atual). A quebra de linha e removida ANTES
// de procurar a segunda virgula - ela so pode estar no fim de verdade,
// nao importa quantas virgulas vierem antes.
void carregar_nomes(void)
{
  if (!tab5_sd_montar()) { return; }
  FILE* f = tab5_sd_abrir(MESH_PASTA, MESH_ARQ_NOS, "r");
  if (!f) { s_nomes_lidos = true; tab5_sd_desmontar(); return; }   // ainda nao existe

  // Quem ja estava na tabela antes da leitura veio do no ao vivo.
  EXT_RAM_BSS_ATTR static bool ao_vivo[MESH_MAX_CONHECIDOS];
  for (int i = 0; i < MESH_MAX_CONHECIDOS; ++i) { ao_vivo[i] = (i < s_n_conhecidos); }

  int linhas = 0;
  char linha[96];
  while (fgets(linha, sizeof(linha), f)) {
    char* resto = strchr(linha, ',');
    if (!resto) { continue; }
    *resto++ = 0;   // linha = so o hex agora
    ++linhas;

    char* fim = strpbrk(resto, "\r\n");
    if (fim) { *fim = 0; }   // resto = "curto" ou "curto,longo", sem quebra

    char* curto = resto;
    char* longo = strchr(resto, ',');
    if (longo) { *longo++ = 0; }   // formato novo: separa os dois campos

    if (!curto[0] && (!longo || !longo[0])) { continue; }

    const uint32_t num = (uint32_t)strtoul(linha, nullptr, 16);
    if (num == 0) { continue; }

    int idx = -1;
    for (int i = 0; i < s_n_conhecidos; ++i) {
      if (s_conhecidos[i].num == num) { idx = i; break; }
    }
    // O no ao vivo ja ensinou o nome antes desta leitura: ele vence, e so
    // precisa ir para o arquivo se for diferente da ULTIMA linha dele la.
    if (idx >= 0 && ao_vivo[idx]) {
      s_conhecidos[idx].sujo = strcmp(s_conhecidos[idx].curto, curto) != 0
                            || strcmp(s_conhecidos[idx].longo, longo ? longo : "") != 0;
      continue;
    }
    if (idx < 0) {
      if (s_n_conhecidos >= MESH_MAX_CONHECIDOS) { continue; }
      idx = s_n_conhecidos++;
      s_conhecidos[idx].num = num;
      s_conhecidos[idx].curto[0] = 0;
      s_conhecidos[idx].longo[0] = 0;
      s_conhecidos[idx].sujo = false;
    }
    if (curto[0])         { snprintf(s_conhecidos[idx].curto, sizeof(s_conhecidos[idx].curto), "%s", curto); }
    if (longo && longo[0]) { snprintf(s_conhecidos[idx].longo, sizeof(s_conhecidos[idx].longo), "%s", longo); }
  }
  fclose(f);
  s_nomes_lidos = true;

  // COMPACTA. O arquivo e "so acrescentar" (renomear = uma linha nova), entao
  // cresce com linhas que a leitura acima ja descartou; e arquivos gravados
  // antes da guarda s_nomes_lidos tem o mesmo no repetido dezenas de vezes.
  // Com sobra demais, reescreve com uma linha por no - e ja leva junto o que
  // estava pendente.
  if (linhas > s_n_conhecidos + 32) {
    // O arquivo novo inteiro na PSRAM e UMA troca pela porta do sistema
    // (tab5_sd_substituir) - nunca meio arquivo.
    const size_t por = 8 + 3 + MESH_NOME_CURTO + MESH_NOME_LONGO;
    const size_t cap = (size_t)s_n_conhecidos * por + 1;
    char* txt = (char*)heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    if (txt) {
      size_t k = 0;
      for (int i = 0; i < s_n_conhecidos && k < cap; ++i) {
        k += (size_t)snprintf(txt + k, cap - k, "%08lX,%s,%s\n", (unsigned long)s_conhecidos[i].num,
                              s_conhecidos[i].curto, s_conhecidos[i].longo);
      }
      if (k < cap && tab5_sd_substituir(MESH_PASTA, MESH_ARQ_NOS, txt, k)) {
        for (int i = 0; i < s_n_conhecidos; ++i) { s_conhecidos[i].sujo = false; }
        ESP_LOGI(TAG, "cartao: nos.txt compactado de %d para %d linha(s)", linhas, s_n_conhecidos);
      }
      heap_caps_free(txt);
    }
  }
  tab5_sd_desmontar();
  ESP_LOGI(TAG, "cartao: %d no(s) conhecido(s)", s_n_conhecidos);
}

// ---------------------------------------------------------------------------

// Acha ou cria a entrada do no. A tabela e pequena e a busca linear: com 32
// entradas isso e mais rapido que qualquer indice, e nao ha alocacao.
mesh_no_t* achar_no(uint32_t num)
{
  for (int i = 0; i < MESH_MAX_NOS; ++i) {
    if (s_nos[i].usado && s_nos[i].num == num) { return &s_nos[i]; }
  }
  for (int i = 0; i < MESH_MAX_NOS; ++i) {
    if (!s_nos[i].usado) {
      memset(&s_nos[i], 0, sizeof(s_nos[i]));
      s_nos[i].usado = true;
      s_nos[i].num = num;
      s_nos[i].snr = -128.0f;
      // SALTOS COMECA DESCONHECIDO, nao 0 (nos apareciam sem nome e sem
      // saltos). memset zera para 0, que e
      // EXATAMENTE o valor de "vizinho direto" - um no que so foi criado por
      // achar_no() (visto num pacote, mas ainda sem NodeInfo) mostraria "0
      // saltos" como se fosse confirmado, quando na verdade e so "ainda nao
      // sei". So ler_node_info() (NodeInfo, campo 9) escreve um saltos de
      // verdade; ate la, o sentinela evita a mentira por omissao.
      s_nos[i].saltos = MESH_SALTOS_DESCONHECIDO;
      ++s_nos_ver;
      return &s_nos[i];
    }
  }

  // TABELA CHEIA - isto devolvia nullptr e o no NOVO era
  // ignorado para sempre: quem enchesse as vagas primeiro (nem sempre os
  // nos mais relevantes - so os primeiros ouvidos desde o boot) trancava a
  // tabela, e ninguem mais entrava, nem gente conversando via MQTT que nunca
  // tinha aparecido antes. A tabela nunca trocava nos antigos por novos.
  //
  // A SAIDA: despejar alguem (criterio logo abaixo). Isso e seguro porque mesh_no_t e so um CACHE
  // de metadados ao vivo (nome, bateria, posicao) - o historico de mensagens
  // mora no cartao, indexado pelo NUMERO do no, e sobrevive intacto ao no
  // sair da tabela. Se o no despejado voltar a falar, ele reaparece na
  // proxima NodeInfo, como um no "novo" comum.
  // QUEM SAI, em tres passadas - a primeira que achar candidato decide:
  //   0 - os da INTERNET (via_mqtt) nao fixos: sao muitos, voltam no proximo
  //       pacote e nao sao alcancaveis sem a internet;
  //   1 - qualquer nao fixo;
  //   2 - qualquer um (tabela inteira de fixos - raro com 256).
  // O nosso nunca. Dentro da passada, o de MAIOR idade - pela idade completa
  // (avistamento nosso OU last_heard do radio), e nao so pelo avistamento:
  // no do dump tem avistamento 0 e era despejado primeiro mesmo tendo sido
  // ouvido ha um minuto, enquanto o resto do dump girava numa vaga so.
  int mais_velho = -1;
  int64_t maior = -1;
  for (int pass = 0; pass < 3 && mais_velho < 0; ++pass) {
    for (int i = 0; i < MESH_MAX_NOS; ++i) {
      const mesh_no_t& c = s_nos[i];
      if (c.num == s_meu_num || fav_pos(c.num) >= 0) { continue; }   // nem o nosso nem favorito
      if (pass == 0 && (c.fixado || !c.via_mqtt)) { continue; }
      if (pass == 1 && c.fixado) { continue; }
      int64_t idade = idade_do_no(c);
      if (idade < 0) { idade = INT64_MAX / 2; }   // nunca ouvido: o mais velho de todos
      if (idade > maior) { maior = idade; mais_velho = i; }
    }
  }
  if (mais_velho < 0) { mais_velho = 0; }
  // SEM LOG AQUI DE PROPOSITO (poluia o log). Isto e estado normal de regime (a tabela FICA cheia numa malha com
  // mais de 32 nos, e cada despejo e esperado, nao um erro) - so a CONTAGEM
  // fica guardada, para consultar quando precisar em vez de aparecer sozinha
  // toda hora. Ver tab5_mesh_despejos() e o comando "mesh" no console.
  ++s_despejos;
  ++s_nos_ver;
  memset(&s_nos[mais_velho], 0, sizeof(s_nos[mais_velho]));
  s_nos[mais_velho].usado = true;
  s_nos[mais_velho].num = num;
  s_nos[mais_velho].snr = -128.0f;
  s_nos[mais_velho].saltos = MESH_SALTOS_DESCONHECIDO;   // ver o aviso no ramo de cima
  return &s_nos[mais_velho];
}

// ---------------------------------------------------------------------------
// A NODEDB DO TAB5, NO CARTAO
// ---------------------------------------------------------------------------
// O rádio guarda no maximo MAX_NUM_NODES (100 no C6L ate a 2.7, 120 na 2.8):
// constante do firmware dele. A tabela daqui tem 256 vagas e aprende tambem
// pelo trafego ao vivo - e, guardada no cartao, sobrevive ao reinicio: o Tab5
// passa a conhecer mais nos do que o radio consegue lembrar.
//
// Binario com cabecalho (magia, versao, tamanho do registro): se mesh_no_t
// mudar de forma numa versao nova, o arquivo velho e ignorado em vez de lido
// torto. Gravado a cada 10 min se a tabela mudou, e ao parar o servico.
//
// Ao carregar, o que e "desta sessao" volta zerado: avistamento local, SNR e o
// lugar fixo no topo. Um no ouvido ontem nao esta "no ar" hoje; ele volta ao
// topo quando for ouvido de novo.
#define MESH_ARQ_NODEDB "nodedb.bin"
typedef struct { char magia[4]; uint16_t versao; uint16_t tam; uint32_t n; } nodedb_cab_t;
// A COPIA SO EXISTE ENQUANTO GRAVA OU LE (31 KB de PSRAM): era um vetor fixo,
// usado duas vezes por boot e de 10 em 10 minutos.
static mesh_no_t* nodedb_copia(void)
{
  return (mesh_no_t*)heap_caps_malloc(sizeof(mesh_no_t) * MESH_MAX_NOS, MALLOC_CAP_SPIRAM);
}
uint32_t s_nodedb_ver_salva = 0;
// A NodeDB do cartao foi lida nesta subida do servico (ou, com o cartao
// presente, nao existia). So entao a tabela pode substitui-la - uma tabela que
// comecou vazia por falta de cartao apagaria os nos guardados.
static bool s_nodedb_lida = false;

void salvar_nodedb(void)
{
  mesh_no_t* s_nodedb_copia = nodedb_copia();
  if (!s_nodedb_copia) { return; }
  int n = 0;
  travar();
  for (int i = 0; i < MESH_MAX_NOS; ++i) {
    if (s_nos[i].usado && s_nos[i].num != 0) { s_nodedb_copia[n++] = s_nos[i]; }
  }
  const uint32_t ver = s_nos_ver;
  destravar();
  if (n == 0 || !s_nodedb_lida) { heap_caps_free(s_nodedb_copia); return; }
  // Cabecalho e tabela num bloco so, e UMA troca pela porta do sistema: um
  // reinicio no meio deixa a NodeDB antiga inteira, nunca meia - e com o
  // cartao emprestado ao computador ela vai para o diario.
  const size_t tam = sizeof(nodedb_cab_t) + sizeof(mesh_no_t) * (size_t)n;
  uint8_t* bloco = (uint8_t*)heap_caps_malloc(tam, MALLOC_CAP_SPIRAM);
  if (bloco) {
    const nodedb_cab_t cab = { { 'M', 'N', 'D', 'B' }, 1, (uint16_t)sizeof(mesh_no_t), (uint32_t)n };
    memcpy(bloco, &cab, sizeof(cab));
    memcpy(bloco + sizeof(cab), s_nodedb_copia, sizeof(mesh_no_t) * (size_t)n);
    if (tab5_sd_substituir(MESH_PASTA, MESH_ARQ_NODEDB, bloco, tam)) {
      s_nodedb_ver_salva = ver;
      ESP_LOGI(TAG, "cartao: %d no(s) na NodeDB do Tab5", n);
    }
    heap_caps_free(bloco);
  }
  heap_caps_free(s_nodedb_copia);
}

// Com a tabela vazia (logo depois do memset do iniciar). Le o cartao SEM a
// trava (E/S lenta) para a copia, e so a passagem para a tabela e travada - a
// tela pode estar lendo a tabela ao mesmo tempo.
void carregar_nodedb(void)
{
  s_nodedb_lida = false;
  if (!tab5_sd_montar()) { return; }
  s_nodedb_lida = true;   // com o cartao: ou le, ou o arquivo nao existe (o primeiro salvar cria)
  FILE* f = tab5_sd_abrir(MESH_PASTA, MESH_ARQ_NODEDB, "rb");
  if (!f) { tab5_sd_desmontar(); return; }
  mesh_no_t* s_nodedb_copia = nodedb_copia();
  if (!s_nodedb_copia) { fclose(f); tab5_sd_desmontar(); return; }
  nodedb_cab_t cab;
  int lidos = 0;
  if (fread(&cab, sizeof(cab), 1, f) == 1 && !memcmp(cab.magia, "MNDB", 4)
   && cab.versao == 1 && cab.tam == sizeof(mesh_no_t)) {
    const int total = (cab.n < MESH_MAX_NOS) ? (int)cab.n : MESH_MAX_NOS;
    lidos = (int)fread(s_nodedb_copia, sizeof(mesh_no_t), (size_t)total, f);
  } else {
    ESP_LOGW(TAG, "cartao: nodedb.bin de outra versao - ignorado");
  }
  fclose(f);
  tab5_sd_desmontar();

  int n = 0;
  travar();
  for (int i = 0; i < lidos && n < MESH_MAX_NOS; ++i) {
    mesh_no_t no = s_nodedb_copia[i];
    if (no.num == 0) { continue; }
    // Os da rede americana (removida) ficavam marcados aqui: nao voltam.
    if (no.rede_antiga != 0) { continue; }
    no.usado = true;
    no.visto_local_ms = 0;
    no.idade_s = -1;
    no.fixado = 0;
    no.snr = -128.0f;
    no.favorito = false;
    s_nos[n++] = no;
  }
  ++s_nos_ver;
  s_nodedb_ver_salva = s_nos_ver;
  destravar();
  heap_caps_free(s_nodedb_copia);
  ESP_LOGI(TAG, "cartao: %d no(s) lembrado(s) da NodeDB do Tab5", n);
}

// Ver o aviso completo em tab5_mesh.hpp. So LEITURA - nunca cria entrada
// (diferente de achar_no): perguntar "qual o enlace deste no" nao pode ter o
// efeito colateral de inventar um no na tabela.
bool tab5_mesh_no_enlace(uint32_t num, bool* via_mqtt, uint8_t* saltos)
{
  for (int i = 0; i < MESH_MAX_NOS; ++i) {
    if (s_nos[i].usado && s_nos[i].num == num) {
      if (via_mqtt) { *via_mqtt = s_nos[i].via_mqtt; }
      if (saltos)   { *saltos   = s_nos[i].saltos;   }
      return true;
    }
  }
  return false;
}
