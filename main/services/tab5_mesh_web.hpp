// O MESH NO NAVEGADOR DO PC - as rotas do servidor web (tab5_web) para
// conversar pela malha: os canais e as conversas diretas a esquerda, as
// mensagens a direita, uma caixa para escrever. A pagina (tab5_mesh_web.html,
// na flash) pergunta /api/estado a cada 2 s e so rele o que mudou.
//
// So interface sobre o servico tab5_mesh: conversas, historico (do cartao,
// pela chave), envio e o estado de entrega sao os dele - o mesmo que o app
// Mesh mostra. Ler uma conversa pelo PC a marca como lida, como no app.
//
// As rotas, em http://<ip>/mesh/ (conversa = c=1&idx=<canal> ou no=<numero>):
//   GET  /                   a pagina
//   GET  /api/estado         {"eu","eu_nome","no_ar","conv","msgs","envios","nao_lidas"}
//   GET  /api/conversas      [{"canal","idx","no","nome","sub","nao_lidas","ultima"}]
//   GET  /api/canais         [{"idx","nome","visivel"}]
//   GET  /api/nos            [{"num","id","nome","longo","hw","visto","idade","saltos","snr10",
//                              "bat","mv","mqtt","fav","lat","lon"}]   (lat/lon: graus x 1e7)
//   GET  /api/msgs?conversa  [{"de","nome","texto","quando","minha","envio","erro","mqtt","saltos","snr10","rssi"}]
//   GET  /api/saude          {"uso_canal10","airtime10","ruido","online","total","tx","rx",...,"enlace"}
//   POST /api/enviar?conversa   corpo = o texto (ate MESH_PAYLOAD_MAX bytes)
//   POST /api/favorito?no=N&sim=1|0
// Numeros com casa decimal vao como inteiros (snr10 = SNR x 10): sem %f, que
// e o que mais gasta a pilha do printf (a do httpd ja estourou uma vez).
#pragma once

// Abre a parte do Mesh no servidor (/mesh/). Da tarefa de interface.
// false = ver tab5_web_abrir.
bool tab5_mesh_web_ligar(void);
