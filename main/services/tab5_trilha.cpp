#include "tab5_trilha.hpp"
#include "tab5_linhas.hpp"
#include "tab5_gps.hpp"
#include "tab5_sd.hpp"
#include "tab5_arquivos.hpp"   // listar pasta e do sistema
#include "tab5_hora.hpp"
#include "tab5_mutex.hpp"
#include "notificacao.h"
#include "ajustes.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static const char* TAG = "trilha";
#define TRILHA_PASTA "Trilha"
#define AMOSTRA_MS   5000   // um ponto a cada 5s - trilha nao precisa de mais que isso

static uint32_t       s_pontos  = 0;   // uint32_t sozinho: leitura/escrita de palavra e atomica nesta CPU
// O nome muda em dois momentos: tab5_trilha_iniciar (antes da tarefa subir) e a
// virada do dia (na tarefa).
static char           s_nome[32] = "";
static uint32_t       s_numero   = 0;     // o N de trilha_N.csv gravando (0 = parado)
static uint32_t       s_continuar = 0;    // pedido para o proximo inicio (tab5_trilha_continuar)
static bool           s_carregar = false; // o laco comeca lendo o arquivo existente
static uint32_t       s_dia = 0;          // AAAAMMDD LOCAL da trilha gravando (0 = ainda nao sabe)

// ---------------------------------------------------------------------------
// A DECIMACAO - uma so, usada pelo mapa ao vivo e pela leitura do historico.
// ---------------------------------------------------------------------------
void tab5_trilha_decimar_iniciar(tab5_trilha_decimador_t* d, tab5_trilha_ponto_t* pts, uint32_t max)
{
  d->pts   = pts;
  d->max   = max;
  d->n     = 0;
  d->passo = 1;
  d->idx   = 0;
}

void tab5_trilha_decimar_somar(tab5_trilha_decimador_t* d, float lat, float lon)
{
  if ((d->idx++ % d->passo) != 0) { return; }
  if (d->n >= d->max) {
    for (uint32_t k = 0; k < d->max / 2; ++k) { d->pts[k] = d->pts[k * 2]; }
    d->n      = d->max / 2;
    d->passo *= 2;
    // Passo dobrou - a amostra atual pode nao caber mais na cadencia nova.
    if ((d->idx % d->passo) != 0) { return; }
  }
  d->pts[d->n].lat = lat;
  d->pts[d->n].lon = lon;
  ++d->n;
}

// ---------------------------------------------------------------------------
// O MAPA AO VIVO
// ---------------------------------------------------------------------------
// Escrito pela tarefa "trilha" (laco), lido pela tarefa do app (de dentro de
// desenhar()) - tarefas DIFERENTES, por isso o mutex; nao e o mesmo caso de
// s_pontos/s_nome acima (aqueles sao publicados so DEPOIS de estaveis, e uma
// palavra sozinha nao rasga leitura).
static tab5_trilha_ponto_t     s_mapa[TAB5_TRILHA_MAPA_MAX];
static tab5_trilha_decimador_t s_mapa_dec;       // quem decima s_mapa (n = pontos validos)
static bool                    s_mapa_pronto = false;   // s_mapa_dec ja foi iniciado
static uint32_t                s_mapa_dia   = 0;   // AAAAMMDD do tracado atual; 0 = nenhum ainda
static Trava s_mapa_mutex;

static inline void travar_mapa(void)    { s_mapa_mutex.travar(); }
static inline void destravar_mapa(void) { s_mapa_mutex.destravar(); }

// Recomeca o tracado do zero. Chamar com o mapa travado.
static void mapa_zerar(void)
{
  tab5_trilha_decimar_iniciar(&s_mapa_dec, s_mapa, TAB5_TRILHA_MAPA_MAX);
  s_mapa_pronto = true;
}

// UM TRACADO POR DIA (acumular tudo na mesma imagem a tornaria ilegivel).
// Uma gravacao pode ficar ligada por dias (e o app so para no botao, nunca
// sozinho); sem isto, uma caminhada de segunda ficaria emendada com a de terca
// na MESMA imagem - e, depois de decimado o suficiente, viraria uma mancha sem
// formato nenhum, sem representar caminhada nenhuma de verdade.
// 'dia_chave' e AAAAMMDD no dia LOCAL (tab5_trilha_dia_local) - comparado
// contra o dia do tracado ATUAL; virou, reseta como se fosse um inicio novo,
// mas SEM tocar no arquivo/contagem de pontos (a gravacao no cartao
// continua, so o desenho recomeca do zero).
static void mapa_virar_dia_se_precisar(uint32_t dia_chave)
{
  if (dia_chave == s_mapa_dia) { return; }
  if (s_mapa_dia != 0) {   // 0 = primeiro ponto desta sessao, nao e "virada" de verdade
    ESP_LOGI(TAG, "mapa: virou o dia (%lu -> %lu) - tracado zerado",
             (unsigned long)s_mapa_dia, (unsigned long)dia_chave);
  }
  mapa_zerar();
  s_mapa_dia = dia_chave;
}

static void mapa_adicionar(float lat, float lon, uint32_t dia_chave)
{
  travar_mapa();
  mapa_virar_dia_se_precisar(dia_chave);
  tab5_trilha_decimar_somar(&s_mapa_dec, lat, lon);
  destravar_mapa();
}

uint32_t tab5_trilha_mapa(tab5_trilha_ponto_t* out, uint32_t max)
{
  travar_mapa();
  const uint32_t total = s_mapa_pronto ? s_mapa_dec.n : 0;
  const uint32_t n = (total < max) ? total : max;
  for (uint32_t i = 0; i < n; ++i) { out[i] = s_mapa[i]; }
  destravar_mapa();
  return n;
}

// ---------------------------------------------------------------------------
// O CARTAO
// ---------------------------------------------------------------------------
// Caminho absoluto de "trilha_NNNN.csv" (para o leitor). false = sem pasta.
static bool caminho_da_trilha(uint32_t numero, char* out, size_t tam)
{
  char pasta[96];
  if (!tab5_sd_pasta_caminho(TRILHA_PASTA, pasta, sizeof(pasta))) { return false; }
  snprintf(out, tam, "%s/trilha_%04lu.csv", pasta, (unsigned long)numero);
  return true;
}

// "AAAA-MM-DDTHH:MM:SSZ,lat,lon,altitude_m,qualidade" - so data e lat/lon
// interessam para o tracado. O primeiro campo (data) tem virgula nenhuma
// dentro, entao o PRIMEIRO strchr ja aponta para depois dela. false = o
// cabecalho, ou linha cortada.
static bool ler_ponto(const char* linha, uint32_t* ep, float* lat, float* lon)
{
  if (!tab5_trilha_ler_carimbo(linha, ep)) { return false; }
  const char* apos_data = strchr(linha, ',');
  return apos_data && sscanf(apos_data + 1, "%f,%f", lat, lon) == 2;
}

// MAIOR "trilha_NNNN.csv" JA NO CARTAO - cada gravacao e um arquivo novo, e o
// numero tem que vir do que ja existe no cartao, nao de uma variavel que
// nasce zerada a cada boot. MONTA o cartao: sem isso, tab5_sd_pasta() via
// cartao ausente, devolvia false, e todo INICIAR comecava do zero em
// "trilha_0001.csv" - sobrescrevendo a sessao anterior.
static uint32_t maior_trilha_existente(void)
{
  uint32_t maior = 0;
  if (!tab5_sd_montar()) { return 0; }
  char pasta[128];
  if (tab5_sd_pasta(TRILHA_PASTA, pasta, sizeof(pasta))) {
    maior = tab5_arq_maior_numero(pasta, "trilha_%04lu.csv");
  }
  tab5_sd_desmontar();
  return maior;
}

// ---------------------------------------------------------------------------
// A GRAVACAO (o fundo)
// ---------------------------------------------------------------------------
// CONTINUAR: o tracado e a contagem voltam do proprio arquivo - lido aqui, na
// tarefa de fundo (um dia inteiro sao ~17 mil linhas: segundos de cartao,
// que na interface seriam segundos de tela parada).
static void carregar_existente(fundo_t* f)
{
  char caminho[160];
  if (!caminho_da_trilha(s_numero, caminho, sizeof(caminho))) { return; }
  tab5_linhas_t* l = tab5_linhas_abrir(caminho);   // grava o pendente antes de ler
  if (!l) { return; }
  char linha[112];
  uint32_t n = 0, voltas = 0;
  while (tab5_linhas_proxima(l, linha, sizeof(linha))) {
    if ((++voltas % 64) == 0 && !fundo_ceder(f)) { break; }
    uint32_t ep;
    float lat, lon;
    if (!ler_ponto(linha, &ep, &lat, &lon)) { continue; }
    const uint32_t dia = tab5_trilha_dia_local(ep);
    mapa_adicionar(lat, lon, dia);
    s_dia = dia;
    ++n;
  }
  tab5_linhas_fechar(l);
  s_pontos = n;
  ESP_LOGI(TAG, "continuando %s: %lu ponto(s) ja no arquivo", s_nome, (unsigned long)n);
}

// MEIA-NOITE: a trilha do dia fecha (vira historico) e a do dia seguinte
// abre - o numero seguinte ao maior que ja existe, como num INICIAR.
static void virar_trilha(uint32_t dia_novo)
{
  uint32_t proximo = s_numero + 1;
  const uint32_t m = maior_trilha_existente() + 1;
  if (m > proximo) { proximo = m; }
  ESP_LOGI(TAG, "virou o dia: %s fica no historico (%lu ponto(s)), segue em trilha_%04lu.csv",
           s_nome, (unsigned long)s_pontos, (unsigned long)proximo);
  travar_mapa();
  s_numero = proximo;
  snprintf(s_nome, sizeof(s_nome), "trilha_%04lu.csv", (unsigned long)proximo);
  destravar_mapa();
  s_pontos = 0;
  s_dia = dia_novo;
}

// O LOOP - roda na tarefa que o kernel cria (kernel/fundo.h). Dorme
// AMOSTRA_MS, olha o GPS, grava se tiver fix. fundo_esperar() volta NA HORA
// quando pedem para parar.
static void laco_trilha(fundo_t* f)
{
  if (s_carregar) {
    s_carregar = false;
    carregar_existente(f);
    char resumo[NOTIF_RESUMO];
    snprintf(resumo, sizeof(resumo), "%lu ponto(s)", (unsigned long)s_pontos);
    fundo_estado(f, (int)s_pontos, resumo);
  }
  while (fundo_esperar(f, AMOSTRA_MS)) {
    const nmea_posicao_t pos  = tab5_gps_posicao();
    const nmea_tempo_t   hora = tab5_gps_tempo();

    // SEM FIX, SEM PONTO - nao vale a pena gravar lat/lon zerados so para
    // dizer "tentei". A proxima volta tenta de novo sozinha.
    if (!pos.valida || pos.qualidade == 0 || !hora.valida) { continue; }

    // O DIA LOCAL DESTE PONTO. Mudou (e ja se sabia o dia da trilha): fecha
    // esta e abre a do dia novo ANTES de gravar o ponto.
    char carimbo[24];
    snprintf(carimbo, sizeof(carimbo), "%04u-%02u-%02uT%02u:%02u:%02uZ",
             (unsigned)hora.ano, (unsigned)hora.mes, (unsigned)hora.dia,
             (unsigned)hora.hora, (unsigned)hora.min, (unsigned)hora.seg);
    uint32_t ep = 0;
    const uint32_t dia_chave = tab5_trilha_ler_carimbo(carimbo, &ep) ? tab5_trilha_dia_local(ep) : 0;
    if (dia_chave && s_dia && dia_chave != s_dia) { virar_trilha(dia_chave); }
    if (dia_chave) { s_dia = dia_chave; }

    // Pelo gravador do sistema (hal/tab5_sd.hpp): entrega a linha e segue -
    // o loop nao monta mais o cartao a cada ponto. O cabecalho vai junto e so
    // e escrito se o arquivo da sessao estiver vazio.
    char linha[112];
    snprintf(linha, sizeof(linha), "%s,%.6f,%.6f,%.1f,%u\n",
             carimbo, pos.lat, pos.lon, pos.altitude_m, (unsigned)pos.qualidade);
    if (tab5_sd_anexar(TRILHA_PASTA, s_nome, linha, "data_hora_utc,lat,lon,altitude_m,qualidade\n")) {
      mapa_adicionar(pos.lat, pos.lon, dia_chave);
      ++s_pontos;   // palavra sozinha, sem trava - ver o aviso no topo do arquivo
      char resumo[NOTIF_RESUMO];
      snprintf(resumo, sizeof(resumo), "%lu ponto(s)", (unsigned long)s_pontos);
      fundo_estado(f, (int)s_pontos, resumo);
    }
  }
}

// Prepara a sessao, na interface, antes de o kernel subir o loop.
static bool ao_iniciar(void)
{
  uint32_t proximo = s_continuar;
  s_carregar = (s_continuar != 0);
  s_continuar = 0;   // o pedido vale para UM inicio
  if (!proximo) { proximo = maior_trilha_existente() + 1; }
  travar_mapa();
  s_numero = proximo;
  snprintf(s_nome, sizeof(s_nome), "trilha_%04lu.csv", (unsigned long)proximo);
  destravar_mapa();
  s_pontos = 0;
  s_dia = 0;

  // Mapa NOVO por sessao (nao acumula caminhadas diferentes juntas) - mas so
  // limpo aqui, nunca em ao_parar(): assim da para olhar o tracado
  // completo depois de apertar PARAR, ate a proxima vez que apertar INICIAR.
  travar_mapa();
  mapa_zerar();
  s_mapa_dia = 0;   // 0 = "nenhum tracado ainda" - o 1o ponto define o dia, sem logar "virada"
  destravar_mapa();

  ESP_LOGI(TAG, "%s em %s/%s", s_carregar ? "continuando" : "gravando", TRILHA_PASTA, s_nome);
  return true;
}

// Depois que o loop saiu: o arquivo ja esta fechado (cada ponto abre e fecha).
static void ao_parar(void)
{
  ESP_LOGI(TAG, "parado - %lu ponto(s) em %s", (unsigned long)s_pontos, s_nome);
  travar_mapa();
  s_nome[0] = '\0';
  s_numero = 0;
  destravar_mapa();
}

// A DESCRICAO QUE O KERNEL RODA. Perfil vazio de proposito: o GPS e servico
// (a energia do Grove e decidida pelo kernel), e o cartao se empresta a cada
// ponto. Prioridade, pastilha, parada e limpeza sao do sistema.
const fundo_def_t TRILHA_FUNDO = {
  .nome       = "Trilha",
  .corpo      = laco_trilha,
  .pilha      = 4096,
  .perfil     = { },
  .ao_iniciar = ao_iniciar,
  .ao_parar   = ao_parar,
  .com_som    = ajustes_beep_trilha,
};

uint32_t tab5_trilha_pontos(void) { return s_pontos; }

void     tab5_trilha_continuar(uint32_t numero) { s_continuar = numero; }
uint32_t tab5_trilha_numero(void) { return sistema_fundo_ativo(&TRILHA_FUNDO) ? s_numero : 0; }

// ---------------------------------------------------------------------------
// CARIMBO E DIA
// ---------------------------------------------------------------------------
// Dias desde 1970-01-01 de uma data civil (Howard Hinnant) - o newlib nao tem
// timegm(), e o mktime() usaria o fuso do sistema.
static int32_t dias_de_civil(int y, unsigned m, unsigned d)
{
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

bool tab5_trilha_ler_carimbo(const char* linha, uint32_t* epoch_utc)
{
  unsigned a, me, d, h, mi, se;
  if (!linha || sscanf(linha, "%4u-%2u-%2uT%2u:%2u:%2u", &a, &me, &d, &h, &mi, &se) != 6) { return false; }
  if (a < 2000 || me < 1 || me > 12 || d < 1 || d > 31) { return false; }
  *epoch_utc = (uint32_t)dias_de_civil((int)a, me, d) * 86400u + h * 3600u + mi * 60u + se;
  return true;
}

uint32_t tab5_trilha_dia_local(uint32_t epoch_utc)
{
  if (epoch_utc < 1577836800u) { return 0; }   // antes de 2020: relogio sem acerto
  struct tm t;
  tab5_hora_local(epoch_utc, &t);
  return (uint32_t)(t.tm_year + 1900) * 10000 + (uint32_t)(t.tm_mon + 1) * 100 + (uint32_t)t.tm_mday;
}

void tab5_trilha_definir_beep(bool tocar)
{
  ajustes_definir_beep_trilha(tocar);
  // A fonte da notificacao e a do kernel, pelo nome (registro idempotente).
  notif_silenciar(notif_registrar(TRILHA_FUNDO.nome), !tocar);
}

bool tab5_trilha_beep_ligado(void) { return ajustes_beep_trilha(); }

// ---------------------------------------------------------------------------
// O HISTORICO
// ---------------------------------------------------------------------------
// "06/10 08:15" do primeiro ponto da trilha N (hora local); "" se nao der.
static void quando_comecou(uint32_t numero, char* out, size_t n)
{
  out[0] = 0;
  char caminho[160];
  if (!caminho_da_trilha(numero, caminho, sizeof(caminho))) { return; }
  tab5_linhas_t* l = tab5_linhas_abrir(caminho);
  if (!l) { return; }
  char linha[96];
  uint32_t ep;
  for (int k = 0; k < 3 && tab5_linhas_proxima(l, linha, sizeof(linha)); ++k) {   // pula o cabecalho
    if (tab5_trilha_ler_carimbo(linha, &ep) && tab5_trilha_dia_local(ep)) {
      struct tm t;
      tab5_hora_local(ep, &t);
      snprintf(out, n, "%02d/%02d %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
      break;
    }
  }
  tab5_linhas_fechar(l);
}

// O que a varredura da pasta precisa levar e trazer.
typedef struct {
  uint32_t* numeros;
  int       max, n;
  uint32_t  exceto_a, exceto_b;
} varredura_t;

static bool visita_trilha(const tab5_arq_item_t* it, void* ctx)
{
  varredura_t* v = (varredura_t*)ctx;
  unsigned long num;
  if (sscanf(it->nome, "trilha_%04lu.csv", &num) == 1
   && (uint32_t)num != v->exceto_a && (uint32_t)num != v->exceto_b) {
    v->numeros[v->n++] = (uint32_t)num;
  }
  return v->n < v->max;
}

static int maior_primeiro(const void* a, const void* b)
{
  const uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
  return (x < y) ? 1 : (x > y) ? -1 : 0;
}

int tab5_trilha_listar(uint32_t* numeros, char (*quando)[TAB5_TRILHA_QUANDO_TAM], int max,
                       int com_data, uint32_t exceto_a, uint32_t exceto_b,
                       tab5_trilha_cancelado_t cancelado)
{
  varredura_t v = { numeros, max, 0, exceto_a, exceto_b };
  char pasta[128];
  // Segura o cartao durante a lista toda: quando_comecou abre um arquivo por
  // trilha, e montar de verdade a cada um seria lento.
  const bool montou = tab5_sd_montar();
  if (montou && tab5_sd_pasta(TRILHA_PASTA, pasta, sizeof(pasta))) {
    tab5_arq_percorrer(pasta, visita_trilha, &v);
  }
  int n = v.n;

  // MAIOR PRIMEIRO: a sessao mais recente no topo.
  if (n > 1) { qsort(numeros, n, sizeof(uint32_t), maior_primeiro); }
  for (int i = 0; i < n; ++i) {
    if (quando) {
      if (i < com_data && montou) { quando_comecou(numeros[i], quando[i], TAB5_TRILHA_QUANDO_TAM); }
      else                        { quando[i][0] = 0; }
    }
    if ((i % 16) == 15 && cancelado && cancelado()) { n = i + 1; break; }
  }
  if (montou) { tab5_sd_desmontar(); }
  return n;
}

void tab5_trilha_ler(uint32_t numero, tab5_trilha_leitura_t* r, tab5_trilha_cancelado_t cancelado)
{
  r->n = r->total = r->primeiro = r->ultimo = 0;
  char caminho[160];
  if (!caminho_da_trilha(numero, caminho, sizeof(caminho))) { return; }
  tab5_linhas_t* l = tab5_linhas_abrir(caminho);
  if (!l) { return; }

  tab5_trilha_decimador_t dec;
  tab5_trilha_decimar_iniciar(&dec, r->pts, r->max);
  char linha[96];
  uint32_t voltas = 0;
  while (tab5_linhas_proxima(l, linha, sizeof(linha))) {
    if ((++voltas % 64) == 0) {
      vTaskDelay(1);                                   // nao segurar o core num arquivo grande
      if (cancelado && cancelado()) { break; }         // o app fechou: o resultado nao serve mais
    }
    uint32_t ep;
    float lat, lon;
    if (!ler_ponto(linha, &ep, &lat, &lon)) { continue; }   // o cabecalho, ou linha cortada

    if (!r->primeiro) { r->primeiro = ep; }
    r->ultimo = ep;
    ++r->total;
    tab5_trilha_decimar_somar(&dec, lat, lon);
  }
  tab5_linhas_fechar(l);
  r->n = dec.n;
}

uint32_t tab5_trilha_de_hoje(tab5_trilha_leitura_t* r, tab5_trilha_cancelado_t cancelado)
{
  const uint32_t maior = maior_trilha_existente();
  const uint32_t hoje = tab5_trilha_dia_local((uint32_t)time(nullptr));
  if (!maior || !hoje) { return 0; }
  tab5_trilha_ler(maior, r, cancelado);
  return (r->total && tab5_trilha_dia_local(r->ultimo) == hoje) ? maior : 0;
}
