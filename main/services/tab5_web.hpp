// SERVIDOR WEB DO SISTEMA - uma porta aberta so de proposito, com senha.
//
// Para o que o PC faz melhor pelo navegador: editar os scripts Berry
// (services/berry_web), conversar pelo Mesh (services/tab5_mesh_web), e
// depois ver a camera e mandar/baixar arquivos.
//
// UM SERVIDOR, UMA PASTA POR APP. Cada app ABRE a sua parte com um prefixo
// ("editor" -> http://<ip>/editor/, "mesh" -> /mesh/, "terminal" -> /terminal/)
// e as rotas dele, relativas a pasta ("/" e a pagina, "/api/x"). Varios
// abertos ao mesmo tempo, a mesma senha para todos. A RAIZ (http://<ip>/) e o
// PORTAL (tab5_web_portal): a visao geral do aparelho e todos os apps que tem
// pagina no PC (app_t.no_pc), abertos ou nao - um clique abre o que faltar.
// Pagina de app fechado volta ao portal. O servidor sobe quando a primeira
// parte abre e cai quando a ultima fecha. Ate WEB_PARTES_MAX partes.
//
// O PADRAO E FECHADO. Nenhuma porta fica aberta no boot. Uma parte abre
// quando a pessoa pede no aparelho e RODA DE FUNDO - sair do app, apagar ou
// bloquear a tela nao a fecha. Fecha:
//   - quando o app fecha a dele (botao Desligar);
//   - depois de WEB_OCIOSO_MS sem nenhum pedido A ELA (quem usa o navegador
//     salva sozinho enquanto edita: ao cair, o que foi feito ja esta
//     gravado) - o Mesh parado nao derruba o Editor em uso.
// E o servidor inteiro cai se o Wi-Fi cair ou depois de WEB_ERROS_MAX
// senhas erradas.
// Ligado, ele declara RECURSO_REDE (kernel/recursos.h): a politica dele
// proibe o sono leve (com a tela apagada o SDIO do C6 nao acorda o P4, e o
// navegador ficaria esperando) e segura o Wi-Fi contra o cochilo (que o
// desligava depois de 3 min de tela apagada, e o servidor caia junto) - a tela pode
// apagar - e publica um aviso na barra com os apps abertos: a pessoa sempre
// ve que ha uma porta aberta, e o toque nele abre o ultimo app que abriu.
//
// O PORTEIRO (cada pedido, antes da rota):
//   1. so da REDE LOCAL: o IPv4 de quem pede tem que estar na sub-rede do
//      aparelho (ou ser IPv6 link-local);
//   2. o Host tem que ser o IP do aparelho (um nome de dominio no Host e o
//      truque de "DNS rebinding" de uma pagina de fora);
//   3. SENHA: autenticacao Basic - o navegador mostra a janela dele. Qualquer
//      usuario; a senha e curta, sorteada a cada ligar e mostrada na tela;
//   4. pedido que muda algo (POST) precisa do cabecalho X-Tab5: uma pagina
//      de outro site nao consegue mandar esse cabecalho sem a permissao de
//      CORS, que o servidor nunca da.
//
// MEMORIA: o httpd do ESP-IDF, UMA tarefa (pilha interna de WEB_PILHA, so
// enquanto ligado - as rotas gravam no cartao, e com ele emprestado a
// gravacao vai a flash: pilha na PSRAM daria o assert do cache). Corpos e
// respostas grandes ficam na PSRAM de quem trata a rota.
#pragma once

#include <stddef.h>
#include <stdbool.h>
#include "esp_http_server.h"

#define WEB_OCIOSO_MS  (10 * 60 * 1000)   // sem pedido nenhum: desliga
#define WEB_ERROS_MAX  20
// 6 KB ESTOUROU (28/10/2026: Breakpoint do fim de pilha num snprintf, com o
// Mesh lendo o historico do cartao). As rotas guardam buffer grande na
// PSRAM; isto e a folga para o httpd, o cartao (FATFS) e o printf.
#define WEB_PILHA      8192

#define WEB_PARTES_MAX 4

// Uma rota: so roda depois do porteiro (acima). 'uri' e RELATIVA a pasta
// do app: "/" e a pagina dele, "/api/x" responde em /<prefixo>/api/x.
typedef struct {
  const char*    uri;      // exata, relativa ("/api/scripts")
  httpd_method_t metodo;   // HTTP_GET, HTTP_POST
  esp_err_t    (*fn)(httpd_req_t* req);
  // WEBSOCKET (metodo HTTP_GET): o aperto de mao passa pelo porteiro (rede
  // local, senha, e a ORIGEM igual ao endereco do Tab5 - o WebSocket nao leva
  // o X-Tab5) e 'fn' passa a receber os QUADROS daquela conexao
  // (httpd_ws_recv_frame). 'ws_fechou' (opcional): a conexao caiu ou a parte
  // fechou. Conexao aberta conta como uso: a parte nao fecha por prazo.
  bool           ws;
  void         (*ws_fechou)(int fd);
} tab5_web_rota_t;

// ABRE a parte de 'dono' (o nome do app, literal: o aviso da barra abre ele)
// em /<prefixo>/ com estas rotas (o vetor tem que viver ate fechar). Liga o
// servidor e sorteia a senha se for a primeira. Abrir de novo a mesma parte
// so renova o prazo. false = sem Wi-Fi conectado, sem vaga, ou o httpd nao
// subiu. De qualquer tarefa, sem bloquear - de dentro de um pedido tambem
// (o portal abrindo um app: o servidor ja esta de pe).
bool tab5_web_abrir(const char* dono, const char* prefixo, const tab5_web_rota_t* rotas, int n);

// FECHA a parte de 'dono'. A ultima a fechar desliga o servidor. De qualquer
// tarefa, sem bloquear. AVISO_WEB quando mudar.
void tab5_web_fechar(const char* dono);

// DESLIGA tudo (todas as partes). De qualquer tarefa, sem bloquear: os
// pedidos novos ja sao recusados e a parada de verdade (que espera o pedido
// em curso) e numa vaga do pool.
void tab5_web_desligar(void);

bool tab5_web_ligado(void);
bool tab5_web_aberto(const char* dono);       // a parte deste app esta aberta?
// Os apps abertos (nomes, na ordem em que abriram). Devolve quantos.
int  tab5_web_abertos(const char** out, int max);
// "http://192.168.1.7/editor/" (dono = nullptr: a raiz). false = sem IP.
bool tab5_web_endereco(const char* dono, char* out, size_t n);
// A pasta da parte aberta de 'dono' ("terminal"). false = nao esta aberta.
bool tab5_web_pasta(const char* dono, char* out, size_t n);

// O CATALOGO (tab5_web_portal): os apps desta variante com pagina no PC
// (app_t.no_pc), na ordem da tabela. Devolve quantos.
int  tab5_web_catalogo(const char** out, int max);
// Abre a parte do app pelo nome ("Terminal"), pelo catalogo - o portal e o
// comando 'web' do console. false = nao ha esse app no PC, ou nao abriu.
bool tab5_web_abrir_app(const char* nome);
void tab5_web_senha(char* out, size_t n);     // "" = desligado

// O ultimo pedido aceito (de qualquer parte): de quem (IP em texto) e quando
// (tab5_agora_ms). 'quem' "" = ninguem ainda.
void tab5_web_ultimo(char* quem, size_t n, uint32_t* quando_ms);

// ---- para as rotas -----------------------------------------------------------
// WEBSOCKET: manda um quadro (texto ou binario) para a conexao 'fd', de
// QUALQUER tarefa - espera o servidor mandar. false = a conexao ja caiu.
bool tab5_web_ws_enviar(int fd, const void* d, size_t n, bool texto);

// O corpo inteiro em 'buf' (com '\0'). Devolve o tamanho; -1 = maior que
// tam - 1 (a rota ja respondeu 413) ou a conexao caiu.
int tab5_web_corpo(httpd_req_t* req, char* buf, size_t tam);

// Um parametro da URL (?arq=Ola.be), ja decodificado (%20, +). false = nao veio.
bool tab5_web_param(httpd_req_t* req, const char* nome, char* out, size_t n);

// Resposta JSON (Content-Type certo, sem cache).
esp_err_t tab5_web_json(httpd_req_t* req, const char* json);

// Texto -> string JSON com aspas ("a\"b\n" -> "\"a\\\"b\\n\""). Corta no
// tamanho de 'out', sem quebrar escape.
void tab5_web_json_texto(char* out, size_t n, const char* s);
