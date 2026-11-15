// O EDITOR DE BERRY NO NAVEGADOR DO PC - as rotas do servidor web
// (tab5_web) para os scripts de apps/Berry.
//
// A pagina (berry_web.html, na flash) e uma so: a lista dos .be a esquerda e
// um editor com cores a direita - quem carrega o editor (CodeMirror) e o
// navegador do PC, de um CDN; sem internet no PC, vira uma caixa de texto
// simples. Salvar, Verificar (berry_verificar: o erro volta com a linha
// marcada) e Rodar (salva, verifica e abre o app no Tab5).
//
// As rotas, em http://<ip>/editor/ (a parte do Editor no servidor):
//   GET  /                   a pagina
//   GET  /api/scripts        [{"nome":"Ola.be","bytes":123}, ...]
//   GET  /api/script?arq=    o texto do script
//   POST /api/script?arq=    grava (corpo = o texto inteiro); com &auto=1 (a
//                            gravacao automatica da pagina) nao rele a pasta
//   POST /api/verificar      {"ok":false,"linha":3,"erro":"..."}
//   POST /api/rodar?arq=     grava, verifica e abre: {"ok":..., "aberto":...}
// O cartao e pelo berry_scripts (portas do tab5_sd), na tarefa do servidor.
#pragma once

// Abre a parte do Editor no servidor (/editor/). Da tarefa de interface.
// false = ver tab5_web_abrir.
// O servidor roda de fundo: fechar o Editor nao o desliga (ver tab5_web.hpp).
bool berry_web_ligar(void);
