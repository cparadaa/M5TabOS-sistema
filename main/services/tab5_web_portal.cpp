// O portal do navegador - ver tab5_web_portal.hpp.
#include "tab5_web_portal.hpp"

#include <stdio.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "app.h"
#include "tab5_power.hpp"
#include "tab5_rede.hpp"
#include "tab5_wifi.hpp"

extern const char PORTAL_INICIO[] asm("_binary_tab5_web_portal_html_start");
extern const char PORTAL_FIM[]    asm("_binary_tab5_web_portal_html_end");

#define SISTEMA_MAX 2048   // a resposta do api/sistema (cabe com folga: 4 partes, poucos apps)

// ---------------------------------------------------------------------------
// O CATALOGO: os apps desta variante que tem pagina no PC (app_t.no_pc)
// ---------------------------------------------------------------------------
static const app_t* app_do_pc(const char* nome)
{
  for (int i = 0; nome && i < SISTEMA_NUM_APPS; ++i) {
    const app_t* a = SISTEMA_APPS[i];
    if (a && a->no_pc && !strcasecmp(a->nome, nome)) { return a; }
  }
  return nullptr;
}

bool tab5_web_abrir_app(const char* nome)
{
  const app_t* a = app_do_pc(nome);
  return a && a->no_pc();
}

int tab5_web_catalogo(const char** out, int max)
{
  int n = 0;
  for (int i = 0; i < SISTEMA_NUM_APPS && n < max; ++i) {
    const app_t* a = SISTEMA_APPS[i];
    if (a && a->no_pc) { out[n++] = a->nome; }
  }
  return n;
}

// ---------------------------------------------------------------------------
// AS ROTAS DA RAIZ
// ---------------------------------------------------------------------------
static esp_err_t pagina(httpd_req_t* req)
{
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, PORTAL_INICIO, PORTAL_FIM - PORTAL_INICIO - 1);   // TEXT poe um '\0' no fim
}

// A visao geral: o que a barra de status do Tab5 mostra, mais os apps.
static esp_err_t sistema(httpd_req_t* req)
{
  char* out = (char*)heap_caps_malloc(SISTEMA_MAX, MALLOC_CAP_SPIRAM);
  if (!out) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sem memoria"); return ESP_FAIL; }
  tab5_rede_ip_t eu = {};
  tab5_rede_ip(&eu);
  const tab5_medida_t m = tab5_power_ultima();
  char nome[80], ssid[80], versao[80];
  tab5_web_json_texto(nome, sizeof(nome), eu.nome[0] ? eu.nome : "Tab5");
  tab5_web_json_texto(ssid, sizeof(ssid), tab5_wifi_ssid() ? tab5_wifi_ssid() : "");
  tab5_web_json_texto(versao, sizeof(versao), esp_app_get_description()->version);
  int k = snprintf(out, SISTEMA_MAX,
                   "{\"nome\":%s,\"versao\":%s,\"ip\":\"%s\",\"ssid\":%s,"
                   "\"bateria\":{\"pct\":%d,\"volts\":%.2f,\"mA\":%.0f},"
                   "\"ligado_s\":%llu,\"cpu_mhz\":%d,"
                   "\"mem\":{\"interna_livre\":%u,\"interna_total\":%u,\"psram_livre\":%u,\"psram_total\":%u},"
                   "\"ocioso_min\":%d,\"apps\":[",
                   nome, versao, eu.ip, ssid, m.pct, m.volts, m.mA,
                   (unsigned long long)(esp_timer_get_time() / 1000000), tab5_power_cpu_mhz(),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_total_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned)heap_caps_get_total_size(MALLOC_CAP_SPIRAM),
                   WEB_OCIOSO_MS / 60000);
  // Os do catalogo, abertos ou nao; e o ultimo que abriu (a pagina comeca nele).
  const char* cat[SISTEMA_APPS_MAX];
  const int nc = tab5_web_catalogo(cat, SISTEMA_APPS_MAX);
  const char* abertos[WEB_PARTES_MAX];
  const int na = tab5_web_abertos(abertos, WEB_PARTES_MAX);
  for (int i = 0; i < nc && k < SISTEMA_MAX - 160; ++i) {
    char pasta[24] = "", nm[64];
    tab5_web_json_texto(nm, sizeof(nm), cat[i]);
    const bool aberto = tab5_web_pasta(cat[i], pasta, sizeof(pasta));
    k += snprintf(out + k, SISTEMA_MAX - k, "%s{\"nome\":%s,\"aberto\":%s,\"pasta\":\"%s\"}",
                  i ? "," : "", nm, aberto ? "true" : "false", pasta);
  }
  char ult[64] = "null";
  if (na) { tab5_web_json_texto(ult, sizeof(ult), abertos[na - 1]); }
  snprintf(out + k, SISTEMA_MAX - k, "],\"ultimo\":%s}", ult);
  const esp_err_t r = tab5_web_json(req, out);
  heap_caps_free(out);
  return r;
}

static esp_err_t abrir(httpd_req_t* req)
{
  char nome[32], pasta[24];
  if (!tab5_web_param(req, "app", nome, sizeof(nome)) || !app_do_pc(nome)) {
    httpd_resp_set_status(req, "404 Not Found");
    return tab5_web_json(req, "{\"ok\":false,\"msg\":\"esse app nao abre no PC\"}");
  }
  if (!tab5_web_abrir_app(nome) || !tab5_web_pasta(nome, pasta, sizeof(pasta))) {
    return tab5_web_json(req, "{\"ok\":false,\"msg\":\"o Tab5 nao conseguiu abrir (sem vaga?)\"}");
  }
  char js[64];
  snprintf(js, sizeof(js), "{\"ok\":true,\"pasta\":\"%s\"}", pasta);
  return tab5_web_json(req, js);
}

static esp_err_t desligar(httpd_req_t* req)
{
  const esp_err_t r = tab5_web_json(req, "{\"ok\":true}");
  tab5_web_desligar();   // a parada espera esta resposta terminar
  return r;
}

static const tab5_web_rota_t ROTAS[] = {
  { "/",             HTTP_GET,  pagina,   false, nullptr },
  { "/api/sistema",  HTTP_GET,  sistema,  false, nullptr },
  { "/api/abrir",    HTTP_POST, abrir,    false, nullptr },
  { "/api/desligar", HTTP_POST, desligar, false, nullptr },
};

const tab5_web_rota_t* tab5_web_portal_rotas(int* n)
{
  *n = sizeof(ROTAS) / sizeof(ROTAS[0]);
  return ROTAS;
}
