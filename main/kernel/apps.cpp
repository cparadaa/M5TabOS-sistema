// A TABELA DE APLICATIVOS.
//
// Ninguem edita este arquivo. A lista fixa vem de apps_lista.inc, que o
// main/CMakeLists.txt gera a cada build a partir das pastas main/app_<nome>/:
// cada uma vira uma linha APP(APP_<NOME>). Criar um app e criar a pasta e
// definir nela o 'extern const app_t APP_<NOME>'; tirar e apagar a pasta.
//
// Depois dos fixos, o boot acrescenta os apps lidos do cartao (os scripts
// Berry, services/berry_apps) com sistema_apps_registrar() - antes da home
// montar a ordem dos icones. Depois do boot, so a interface mexe (os apps do
// cartao relidos ao voltar do pendrive).

#include "app.h"

#define APP(simbolo) extern const app_t simbolo;
#include "apps_lista.inc"
#undef APP

static const app_t* const FIXOS[] = {
#define APP(simbolo) &simbolo,
#include "apps_lista.inc"
#undef APP
};

const app_t* SISTEMA_APPS[SISTEMA_APPS_MAX] = {
#define APP(simbolo) &simbolo,
#include "apps_lista.inc"
#undef APP
};

int SISTEMA_NUM_APPS = sizeof(FIXOS) / sizeof(FIXOS[0]);

bool sistema_apps_registrar(const app_t* app)
{
  if (!app || SISTEMA_NUM_APPS >= SISTEMA_APPS_MAX) { return false; }
  SISTEMA_APPS[SISTEMA_NUM_APPS++] = app;
  return true;
}

void sistema_apps_truncar(int n)
{
  const int fixos = sizeof(FIXOS) / sizeof(FIXOS[0]);
  if (n < fixos) { n = fixos; }
  if (n < SISTEMA_NUM_APPS) { SISTEMA_NUM_APPS = n; }
}
