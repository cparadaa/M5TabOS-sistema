// FERRAMENTAS DE REDE - olhar e consultar a rede: o endereco do aparelho, DNS,
// a tabela ARP (e a varredura da rede local), traceroute, teste de porta TCP e
// HTTP/HTTPS. O Terminal mostra (comandos ip, dns, arp, traceroute, tcp, http,
// busca no console.cpp); o Berry e outros apps usam o mesmo motor.
//
// O Wi-Fi e do C6 (esp_hosted), mas a pilha IP (lwIP) roda AQUI no P4: ARP,
// ICMP e sockets sao daqui, e funcionam com qualquer rede em que o Wi-Fi
// estiver (hal/tab5_wifi).
//
// QUEM CHAMA: tudo bloqueia (rede) - nunca da tarefa de interface. Os laços
// longos (varredura, traceroute, HTTP) perguntam 'parar' a cada volta e
// entregam o resultado aos poucos por callback, para quem mostra ir mostrando.
// Memoria: buffers de HTTP na PSRAM (o mbedTLS do sistema ja aloca la).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TAB5_REDE_IP_TXT 46   // cabe um IPv6 em texto

typedef bool (*tab5_rede_parar_t)(void);   // true = pare (Ctrl+C); nullptr = nunca

// ---- o aparelho na rede ----------------------------------------------------
typedef struct {
  bool     conectado;               // tem IPv4
  char     ip[16], mascara[16], gateway[16];
  char     dns[2][TAB5_REDE_IP_TXT];
  char     ipv6[TAB5_REDE_IP_TXT];  // link-local, "" = nenhum
  uint8_t  mac[6];
  char     nome[33];                // o hostname
} tab5_rede_ip_t;
bool tab5_rede_ip(tab5_rede_ip_t* out);   // false = sem interface de Wi-Fi

// ---- DNS ---------------------------------------------------------------------
// Os enderecos de 'nome' (IPv4 e IPv6). Devolve quantos (0 = nao resolveu);
// 'ms' (pode ser nullptr) = quanto levou.
int tab5_rede_dns(const char* nome, char ips[][TAB5_REDE_IP_TXT], int max, uint32_t* ms);

// ---- ARP ---------------------------------------------------------------------
typedef struct { char ip[16]; uint8_t mac[6]; } tab5_rede_vizinho_t;
// A tabela ARP agora (quem falou com o aparelho ha pouco). Devolve quantos.
int tab5_rede_arp(tab5_rede_vizinho_t* out, int max);
// VARRE a rede local (/24 do aparelho): pergunta "quem tem o IP x?" a cada
// endereco e entrega quem respondeu. Uns 15 s. Devolve quantos achou; -1 =
// sem rede.
typedef void (*tab5_rede_achou_t)(const tab5_rede_vizinho_t* v, void* ctx);
int tab5_rede_arp_varrer(tab5_rede_achou_t achou, void* ctx, tab5_rede_parar_t parar);

// ---- traceroute --------------------------------------------------------------
// Um salto: ttl, quem respondeu ("" = ninguem) e o tempo de cada sonda (-1 =
// sem resposta). 'fim' = respondeu o proprio alvo.
typedef struct { int ttl; char ip[16]; int ms[3]; bool fim; } tab5_rede_salto_t;
typedef void (*tab5_rede_salto_cb_t)(const tab5_rede_salto_t* s, void* ctx);
// Devolve quantos saltos ate o alvo (0 = nao chegou; -1 = nao resolveu/sem rede).
int tab5_rede_traceroute(const char* alvo, int max_saltos, tab5_rede_salto_cb_t cb, void* ctx,
                         tab5_rede_parar_t parar);

// ---- TCP ---------------------------------------------------------------------
// Conecta com prazo e devolve o socket JA CONECTADO, em modo bloqueante (quem
// usa fecha com close()). <0 = falhou: -1 nao resolveu, -errno do socket.
int tab5_rede_conectar(const char* host, int porta, uint32_t timeout_ms, uint32_t* ms);
// So abre e fecha: a porta esta aberta? 0 = conectou; <0 como acima.
int tab5_rede_tcp(const char* host, int porta, uint32_t timeout_ms, uint32_t* ms);

// ---- HTTP / HTTPS ------------------------------------------------------------
// GET (ou HEAD) em 'url', seguindo redirecionamentos (ate 5). O corpo chega
// aos pedacos em 'dados' (pode ser nullptr); devolver false para de ler.
// HTTPS pelo pacote de certificados do sistema.
typedef struct {
  int      status;          // 200, 404... 0 = nao conectou
  int64_t  tamanho;         // Content-Length; -1 = nao disse
  int64_t  recebido;        // bytes de corpo entregues
  char     tipo[64];        // Content-Type
  char     final[192];      // a URL depois dos redirecionamentos
  int      redirecionou;    // quantos seguiu (0 = a URL pedida respondeu)
  char     erro[64];        // o motivo, se status == 0
  uint32_t ms;
} tab5_rede_http_t;
typedef bool (*tab5_rede_dados_t)(const char* d, int n, void* ctx);
bool tab5_rede_http(const char* url, bool so_cabecalho, tab5_rede_dados_t dados, void* ctx,
                    tab5_rede_http_t* info, tab5_rede_parar_t parar);

// GET inteiro num buffer da PSRAM (para JSON curto: busca, APIs). Devolve o
// buffer (com '\0'; libere com free) ou nullptr; 'max' limita o tamanho.
char* tab5_rede_http_texto(const char* url, size_t max, tab5_rede_http_t* info, tab5_rede_parar_t parar);

// Texto -> URL ("a b&c" -> "a+b%26c").
void tab5_rede_url_codificar(const char* s, char* out, size_t tam);
