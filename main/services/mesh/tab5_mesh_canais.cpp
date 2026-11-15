// Servico Mesh - os CANAIS do no: ativos, com nome, lembrados entre um boot e
// outro. Partido de tab5_mesh.cpp (o servico); o que as partes compartilham esta
// em mesh/tab5_mesh_interno.hpp.

#include <string.h>
#include "esp_log.h"
#include <stdio.h>
#include "tab5_mutex.hpp"
#include "tab5_mesh_mqtt.hpp"
#include "nvs.h"
#include "mesh/tab5_mesh_interno.hpp"

static const char *TAG = "mesh";

static bool s_canais_lidos      = false;   // a NVS ja foi consultada

// ---------------------------------------------------------------------------
// OS CANAIS DO NO - ativos, com nome, e lembrados entre um boot e outro
// ---------------------------------------------------------------------------
// A configuracao dos canais so chega no dump do radio. Sem guarda-la, com o no
// fora (so consulta, ou modo internet) nao havia como saber que um canal
// tinha sido DESABILITADO: o arquivo canal-N.txt continuava no cartao e a
// conversa voltava na varredura da pasta. Fica na NVS do proprio mesh, ao
// lado do nosso numero - e informacao que o radio contou, nao preferencia.
//
// Gravada em LOTE (s_canais_sujos), no fim do dump ou na volta de 15 s da
// tarefa: o dump traz os 8 canais em sequencia e cada um seria uma escrita.
typedef struct { uint8_t ativo; char nome[MESH_NOME_CONVERSA]; } canal_salvo_t;
#define MESH_NVS_CANAIS "canais"

void recordar_canais(void)
{
  if (s_canais_lidos) { return; }
  s_canais_lidos = true;
  nvs_handle_t h;
  if (nvs_open(MESH_NVS, NVS_READONLY, &h) != ESP_OK) { return; }
  canal_salvo_t tab[MESH_MAX_CANAIS];
  size_t n = sizeof(tab);
  if (nvs_get_blob(h, MESH_NVS_CANAIS, tab, &n) == ESP_OK && n == sizeof(tab)) {
    for (int i = 0; i < MESH_MAX_CANAIS; ++i) {
      s_canal_ativo[i] = tab[i].ativo != 0;
      tab[i].nome[sizeof(tab[i].nome) - 1] = 0;
      snprintf(s_canal_nome[i], sizeof(s_canal_nome[i]), "%s", tab[i].ativo ? tab[i].nome : "");
    }
    s_canais_conhecidos = true;
  }
  nvs_close(h);
}

// A COPIA sob a trava, a GRAVACAO fora dela: a NVS grava flash (o cache
// para, ms), e com a trava presa o radio esperava junto para guardar o
// proximo quadro. Roda na carona da tarefa "cartao" (ver mesh_na_carona).
void salvar_canais(void)
{
  canal_salvo_t tab[MESH_MAX_CANAIS];
  travar();
  if (!s_canais_sujos) { destravar(); return; }
  s_canais_sujos = false;
  memset(tab, 0, sizeof(tab));
  for (int i = 0; i < MESH_MAX_CANAIS; ++i) {
    tab[i].ativo = s_canal_ativo[i] ? 1 : 0;
    snprintf(tab[i].nome, sizeof(tab[i].nome), "%s", s_canal_nome[i]);
  }
  destravar();
  nvs_handle_t h;
  if (nvs_open(MESH_NVS, NVS_READWRITE, &h) != ESP_OK) { return; }
  nvs_set_blob(h, MESH_NVS_CANAIS, tab, sizeof(tab));
  nvs_commit(h);
  nvs_close(h);
}

// Nome de um canal para mostrar. Nome vazio no Meshtastic quer dizer "o nome
// do preset de modem" - LongFast no padrao de fabrica.
static void nome_do_canal(int idx, char* out, size_t tam)
{
  if (idx >= 0 && idx < MESH_MAX_CANAIS && s_canal_nome[idx][0]) { snprintf(out, tam, "%s", s_canal_nome[idx]); }
  else if (idx == 0) { snprintf(out, tam, "LongFast"); }
  else               { snprintf(out, tam, "Canal %d", idx); }
}

// Some com a conversa de um canal da coluna, SEM marcar como apagada: quem a
// tirou foi a configuracao do no, nao voce. Reativando o canal no no, ela
// volta sozinha no proximo dump, com o historico do cartao.
static void esconder_conversa_canal(int idx)
{
  for (int i = 0; i < MESH_MAX_CONVERSAS; ++i) {
    if (s_conv[i].usada && s_conv[i].canal && s_conv[i].idx == idx) {
      memset(&s_conv[i], 0, sizeof(s_conv[i]));
      s_conv_seq[i] = 0;
      ++s_conv_ver;
      publicar_notif();   // as nao lidas dela saem do total
    }
  }
}

// Um canal da configuracao (FromRadio campo 10), JA com os zeros do proto3
// tratados (ver o leitor). Com a trava tomada.
void aplicar_canal(int idx, int papel, const char* nome_radio, const uint8_t* psk, size_t n_psk)
{
  recordar_canais();   // antes de comparar: a NVS nao pode sobrescrever o que chegou agora
  const bool ativo = (papel != 0);
  char nome[MESH_NOME_CONVERSA] = "";
  if (ativo) {
    if (nome_radio[0]) { snprintf(nome, sizeof(nome), "%s", nome_radio); }
    else if (idx == 0) { snprintf(nome, sizeof(nome), "LongFast"); }
    else               { snprintf(nome, sizeof(nome), "Canal %d", idx); }
  }
  mesh_mqtt_aprender_canal(idx, papel, nome, ativo ? psk : nullptr, ativo ? n_psk : 0);

  if (s_canal_ativo[idx] != ativo || strcmp(s_canal_nome[idx], nome) != 0 || !s_canais_conhecidos) {
    s_canais_sujos = true;
  }
  s_canal_ativo[idx] = ativo;
  snprintf(s_canal_nome[idx], sizeof(s_canal_nome[idx]), "%s", nome);
  s_canais_conhecidos = true;

  if (!ativo) {
    // DESABILITADO NO NO: sai da coluna. Era o caso do canal desligado no
    // cliente web que continuava aparecendo aqui.
    esconder_conversa_canal(idx);
    return;
  }
  ESP_LOGI(TAG, "canal %d: \"%s\"", idx, nome);

  // CANAL EXISTE, ENTAO A CONVERSA EXISTE - salvo se voce a apagou. Sem isso
  // um canal criado no celular ficava invisivel ate alguem falar nele.
  if (!esta_apagada(true, (uint8_t)idx)) { achar_ou_criar_conversa(true, (uint8_t)idx, 0); }
  // Renomeia a que ja exista. O arquivo e pelo INDICE (canal-0.txt), entao
  // renomear nao desgruda o historico.
  for (int k = 0; k < MESH_MAX_CONVERSAS; ++k) {
    if (s_conv[k].usada && s_conv[k].canal && s_conv[k].idx == idx) {
      snprintf(s_conv[k].nome, sizeof(s_conv[k].nome), "%s", nome);
    }
  }
}

// As conversas dos canais ativos (sem as apagadas), ou so o primario quando
// nenhuma configuracao e conhecida ainda. Com a trava tomada.
void criar_conversas_dos_canais(void)
{
  recordar_canais();
  bool algum = false;
  for (int idx = 0; idx < MESH_MAX_CANAIS; ++idx) {
    if (!s_canais_conhecidos || !s_canal_ativo[idx]) { continue; }
    algum = true;
    if (!esta_apagada(true, (uint8_t)idx)) { achar_ou_criar_conversa(true, (uint8_t)idx, 0); }
  }
  if (!algum && !esta_apagada(true, 0)) { achar_ou_criar_conversa(true, 0, 0); }
}

// Indices dos canais a OFERECER: os ativos; sem nenhum conhecido, o primario.
static int lista_canais(uint8_t out[MESH_MAX_CANAIS + 1])
{
  int n = 0;
  for (int idx = 0; idx < MESH_MAX_CANAIS; ++idx) {
    if (s_canais_conhecidos && s_canal_ativo[idx]) { out[n++] = (uint8_t)idx; }
  }
  if (n == 0) { out[n++] = 0; }
  return n;
}

int tab5_mesh_num_canais(void)
{
  uint8_t l[MESH_MAX_CANAIS + 1];
  travar();
  recordar_canais();
  const int n = lista_canais(l);
  destravar();
  return n;
}

bool tab5_mesh_canal(int i, uint8_t* idx_saida, char* nome, size_t tam, bool* visivel)
{
  uint8_t l[MESH_MAX_CANAIS + 1];
  bool ok = false;
  travar();
  recordar_canais();
  const int n = lista_canais(l);
  if (i >= 0 && i < n) {
    const uint8_t idx = l[i];
    if (idx_saida) { *idx_saida = idx; }
    if (nome)      { nome_do_canal(idx, nome, tam); }
    if (visivel) {
      *visivel = false;
      for (int k = 0; k < MESH_MAX_CONVERSAS; ++k) {
        if (s_conv[k].usada && s_conv[k].canal && s_conv[k].idx == idx) { *visivel = true; break; }
      }
    }
    ok = true;
  }
  destravar();
  return ok;
}
