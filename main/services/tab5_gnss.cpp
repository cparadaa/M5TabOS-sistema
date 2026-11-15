#include "tab5_gnss.hpp"
#include "tab5_linhas.hpp"
#include "tab5_gps.hpp"
#include "tab5_sd.hpp"
#include "tab5_arquivos.hpp"   // listar pasta e do sistema

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* TAG = "gnss";
#define GNSS_PASTA "Azimutal"

// ---------------------------------------------------------------------------
// GRAVAR
// ---------------------------------------------------------------------------
static const char* nome_constelacao(GnssType t)
{
  switch (t) {
    case GPS:     return "GPS";
    case GLONASS: return "GLONASS";
    case GALILEO: return "GALILEO";
    case BEIDOU:  return "BEIDOU";
    case QZSS:    return "QZSS";
    case SBAS:    return "SBAS";
    default:      return "?";
  }
}

uint32_t tab5_gnss_gravar(const SatelliteSnapshot_t* snap, uint32_t idade_max_ms)
{
  int com_posicao = 0;
  for (uint8_t i = 0; i < snap->count; ++i) {
    if (snap->satellites[i].tem_posicao) { com_posicao++; }
  }
  if (com_posicao == 0) { return 0; }          // nada util para registrar

  const nmea_tempo_t   tempo = tab5_gps_tempo();
  const nmea_posicao_t pos   = tab5_gps_posicao();

  // Sem hora do GPS nao ha em que arquivo gravar. Acontece nos primeiros
  // segundos, ate a primeira RMC chegar - e some sozinho.
  if (!tempo.valida) {
    ESP_LOGW(TAG, "ainda sem hora do GPS: %d satelite(s) nao registrados", com_posicao);
    return 0;
  }

  // SEM FIX NAO SE GRAVA. Elevacao e azimute sao RELATIVOS AO OBSERVADOR: sem
  // fix o receptor ainda emite GSV, mas com geometria de almanaque que NAO se
  // atualiza - 17 despejos seguidos vieram byte a byte identicos. O unico
  // sinal confiavel e o status do proprio RMC.
  if (!tempo.fix) {
    ESP_LOGW(TAG, "SEM FIX: %d satelite(s) nao registrados "
                  "(o receptor ouve, mas nao sabe onde esta - a geometria seria falsa)",
             com_posicao);
    return 0;
  }

  char arquivo[40];
  snprintf(arquivo, sizeof(arquivo), "satelites-%04u-%02u-%02u.csv",
           (unsigned)tempo.ano, (unsigned)tempo.mes, (unsigned)tempo.dia);

  // Pelo gravador do sistema (hal/tab5_sd.hpp): entrega a linha e segue - ele
  // monta o cartao sozinho.
  char linha[96];
  if (pos.valida && pos.qualidade > 0) {
    snprintf(linha, sizeof(linha), "POS,%02u:%02u:%02u,%.6f,%.6f,%.1f,%.1f,%u,%u\n",
             (unsigned)tempo.hora, (unsigned)tempo.min, (unsigned)tempo.seg,
             pos.lat, pos.lon, (double)pos.altitude_m, (double)pos.hdop,
             (unsigned)pos.qualidade, (unsigned)pos.sats_usados);
    tab5_sd_anexar(GNSS_PASTA, arquivo, linha);
  }

  const uint32_t agora_ms = pdTICKS_TO_MS(xTaskGetTickCount());
  uint32_t gravadas = 0;
  int frescos = 0, velhos = 0;
  for (uint8_t i = 0; i < snap->count; ++i) {
    const SatelliteInfo_t* s = &snap->satellites[i];
    if (!s->tem_posicao) { continue; }
    // POSICAO VELHA NAO E OBSERVACAO (ver pos_ms em nmea.h): se nao foi
    // reobservada desde o ultimo despejo, nao entra.
    if ((agora_ms - s->pos_ms) > idade_max_ms) { velhos++; continue; }
    frescos++;
    snprintf(linha, sizeof(linha), "SAT,%02u:%02u:%02u,%s,%u,%u,%u,%u\n",
             (unsigned)tempo.hora, (unsigned)tempo.min, (unsigned)tempo.seg,
             nome_constelacao(s->type), s->prn, s->elevation, s->azimuth, s->snr);
    if (tab5_sd_anexar(GNSS_PASTA, arquivo, linha)) { ++gravadas; }
  }
  ESP_LOGI(TAG, "%s: gravados %d, descartados %d por posicao velha",
           arquivo, frescos, velhos);
  return gravadas;
}

// ---------------------------------------------------------------------------
// OS DIAS
// ---------------------------------------------------------------------------
// Varre a pasta do app atras dos arquivos de dia. Ordena decrescente para o
// dia mais recente aparecer primeiro - e quase sempre o que se quer ver. O nome
// tem a data em ISO, entao ordem alfabetica decrescente = cronologica invertida.
static int dia_mais_novo_primeiro(const void* a, const void* b)
{
  return strcmp((const char*)b, (const char*)a);
}

typedef struct { char (*dias)[TAB5_GNSS_DIA_NOME_TAM]; int max, n; } varredura_t;

static bool visita_dia(const tab5_arq_item_t* it, void* ctx)
{
  varredura_t* v = (varredura_t*)ctx;
  if (!strncmp(it->nome, "satelites-", 10)) {
    snprintf(v->dias[v->n], TAB5_GNSS_DIA_NOME_TAM, "%s", it->nome);
    v->n++;
  }
  return v->n < v->max;
}

int tab5_gnss_listar_dias(char (*dias)[TAB5_GNSS_DIA_NOME_TAM], int max)
{
  varredura_t v = { dias, max, 0 };
  char pasta[96];
  if (tab5_sd_montado() && tab5_sd_pasta(GNSS_PASTA, pasta, sizeof(pasta))) {
    tab5_arq_percorrer(pasta, visita_dia, &v);
  }
  if (v.n > 1) { qsort(dias, v.n, TAB5_GNSS_DIA_NOME_TAM, dia_mais_novo_primeiro); }
  return v.n;
}

// ---------------------------------------------------------------------------
// O DIA CARREGADO EM MEMORIA
// ---------------------------------------------------------------------------
// Antes, TROCAR DE SATELITE relia o arquivo inteiro do cartao e reinterpretava
// linha por linha - milhares delas - so para desenhar um subconjunto do que ja
// tinha sido lido. Agora o arquivo e lido UMA vez por dia escolhido, para este
// vetor.
//
// Um dia cheio de gravacao (uma amostra por minuto, ~20 satelites) da umas 29
// mil posicoes - 174KB, num aparelho com 29MB livres. E de longe o lugar mais
// barato para essa informacao estar.
#define MAX_PONTOS 40000

static tab5_gnss_ponto_t* s_pontos = nullptr;
static int  s_n_pontos = 0;
static tab5_gnss_sat_t s_sats[TAB5_GNSS_SATS_MAX];
static int  s_n_sats = 0;
static char s_dia_carregado[TAB5_GNSS_DIA_NOME_TAM] = "";

// Interpretacao a mao, no lugar do sscanf.
//
// O formato e fixo: SAT,hh:mm:ss,CONS,prn,elev,azim,snr. O sscanf precisa
// reinterpretar a string de formato a cada linha e o "%11[^,]" e das partes
// mais caras dele - com milhares de linhas isso domina o tempo de carga.
// strtoul anda no ponteiro e devolve onde parou, que e tudo de que precisamos.
//
// Devolve false na primeira coisa fora do lugar: linha truncada ou corrompida
// e descartada inteira, sem inventar valor.
static bool interpretar_sat(char* l, unsigned* hh, unsigned* mm,
                            char** cons, unsigned* prn,
                            unsigned* elev, unsigned* azim, unsigned* snr)
{
  char* p = l + 4;                                  // depois de "SAT,"
  *hh = strtoul(p, &p, 10); if (*p != ':') { return false; } ++p;
  *mm = strtoul(p, &p, 10); if (*p != ':') { return false; } ++p;
  strtoul(p, &p, 10);                               // segundos: nao usamos
  if (*p != ',') { return false; } ++p;

  *cons = p;
  while (*p && *p != ',') { ++p; }
  if (!*p) { return false; }
  *p++ = 0;                                         // termina a constelacao

  *prn  = strtoul(p, &p, 10); if (*p != ',') { return false; } ++p;
  *elev = strtoul(p, &p, 10); if (*p != ',') { return false; } ++p;
  *azim = strtoul(p, &p, 10);
  *snr = (*p == ',') ? strtoul(p + 1, &p, 10) : 0;   // arquivos antigos podem nao ter
  return true;
}

bool tab5_gnss_carregar_dia(const char* arquivo)
{
  if (strcmp(s_dia_carregado, arquivo) == 0 && s_pontos) { return true; }

  if (!s_pontos) {
    s_pontos = (tab5_gnss_ponto_t*)heap_caps_malloc(sizeof(tab5_gnss_ponto_t) * MAX_PONTOS,
                                                    MALLOC_CAP_SPIRAM);
    if (!s_pontos) {
      ESP_LOGE(TAG, "sem PSRAM para o historico");
      return false;
    }
  }

  s_n_pontos = 0;
  s_n_sats   = 0;
  s_dia_carregado[0] = 0;

  char pasta[96], caminho[160];
  if (!tab5_sd_pasta_caminho(GNSS_PASTA, pasta, sizeof(pasta))) { return false; }
  snprintf(caminho, sizeof(caminho), "%s/%s", pasta, arquivo);
  tab5_linhas_t* l = tab5_linhas_abrir(caminho);   // monta o cartao e grava o pendente antes
  if (!l) { return false; }

  const uint32_t t0 = pdTICKS_TO_MS(xTaskGetTickCount());
  char linha[96];
  int voltas = 0;
  while (s_n_pontos < MAX_PONTOS && tab5_linhas_proxima(l, linha, sizeof(linha))) {
    // Cede a CPU periodicamente: isto pode rodar na tarefa de interface e o
    // arquivo passa de megabytes com horas de gravacao. Segurar o core por mais
    // de 5s faria o task watchdog reiniciar a placa.
    if ((++voltas % 256) == 0) { vTaskDelay(1); }
    if (strncmp(linha, "SAT,", 4) != 0) { continue; }

    unsigned hh, mm, prn, elev, azim, snr;
    char* cons;
    if (!interpretar_sat(linha, &hh, &mm, &cons, &prn, &elev, &azim, &snr)) { continue; }
    if (elev > 90 || azim > 359) { continue; }

    // Cataloga quem aparece. A busca e linear, mas sobre no MAXIMO 40 nomes -
    // uma tabela de dispersao aqui seria mais codigo que ganho.
    int idx = -1;
    for (int k = 0; k < s_n_sats; ++k) {
      if (s_sats[k].prn == (int)prn && strcmp(s_sats[k].cons, cons) == 0) { idx = k; break; }
    }
    if (idx < 0) {
      if (s_n_sats >= TAB5_GNSS_SATS_MAX) { continue; }
      idx = s_n_sats++;
      snprintf(s_sats[idx].cons, sizeof(s_sats[idx].cons), "%s", cons);
      s_sats[idx].prn = (int)prn;
    }

    tab5_gnss_ponto_t* pt = &s_pontos[s_n_pontos++];
    pt->sat    = (uint8_t)idx;
    pt->elev   = (uint8_t)elev;
    pt->azim   = (uint16_t)azim;
    pt->minuto = (uint16_t)(hh * 60 + mm);
    pt->snr    = (uint8_t)(snr > 99 ? 99 : snr);
  }
  tab5_linhas_fechar(l);

  snprintf(s_dia_carregado, sizeof(s_dia_carregado), "%s", arquivo);
  ESP_LOGI(TAG, "%s: %d pontos de %d satelites em %lu ms",
           arquivo, s_n_pontos, s_n_sats,
           (unsigned long)(pdTICKS_TO_MS(xTaskGetTickCount()) - t0));
  return true;
}

const tab5_gnss_ponto_t* tab5_gnss_pontos(int* n) { *n = s_n_pontos; return s_pontos; }
const tab5_gnss_sat_t*   tab5_gnss_sats(int* n)   { *n = s_n_sats;   return s_sats; }

void tab5_gnss_descarregar(void)
{
  if (s_pontos) { heap_caps_free(s_pontos); s_pontos = nullptr; }
  s_n_pontos = 0;
  s_n_sats   = 0;
  s_dia_carregado[0] = 0;
}
