// O PORTAL - a raiz do servidor web (http://<ip>/), a "area de trabalho" do
// Tab5 no navegador do PC.
//
// Uma pagina so (tab5_web_portal.html, da flash): a barra lateral com os apps
// que tem pagina no PC (app_t.no_pc - abertos e fechados) e a visao geral do
// aparelho (bateria, rede, memoria, ha quanto tempo ligado). Escolher um app
// mostra a pagina dele ali dentro, sem sair do portal (o endereco guarda a
// escolha: http://<ip>/#Terminal); um app fechado abre com um clique - a
// senha ja foi dada, e quem a tem viu a tela do Tab5.
//
// As rotas sao da RAIZ, nao de uma parte: o servidor (tab5_web) as despacha
// antes das partes, com o mesmo porteiro.
//   GET  /               a pagina
//   GET  /api/sistema    a visao geral e os apps (JSON)
//   POST /api/abrir?app=Terminal   abre a parte do app (pelo catalogo)
//   POST /api/desligar   desliga o servidor inteiro
#pragma once

#include "tab5_web.hpp"

// As rotas da raiz (o vetor vive para sempre). 'n' = quantas.
const tab5_web_rota_t* tab5_web_portal_rotas(int* n);
