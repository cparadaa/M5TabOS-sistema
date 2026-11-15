// A PONTE entre o nucleo e os apps que nao sao compilados junto (kernel/app.h,
// "APPS DE FORA"). E o UNICO arquivo fora de services/berry_* que conhece o
// Berry: na variante berry (M5_COM_BERRY=1) liga os apps .be do cartao; na
// completa as funcoes nao fazem nada, e o Berry nem entra no binario.
// Ver docs/VARIANTES.md.
#include "app.h"

#if M5_COM_BERRY
#include "berry_apps.hpp"

void sistema_apps_de_fora_iniciar(void) { berry_apps_iniciar(); }
bool sistema_apps_de_fora_atender(void) { return berry_apps_atender(); }
void sistema_apps_de_fora_reler(void)   { berry_apps_reler(); }
bool sistema_apps_de_fora_removivel(const app_t* app) { return berry_apps_eh_meu(app); }
void sistema_apps_de_fora_remover(const app_t* app)   { berry_apps_remover(app); }

#else

void sistema_apps_de_fora_iniciar(void) {}
bool sistema_apps_de_fora_atender(void) { return false; }
void sistema_apps_de_fora_reler(void)   {}
bool sistema_apps_de_fora_removivel(const app_t*) { return false; }
void sistema_apps_de_fora_remover(const app_t*)   {}

#endif
