// SSH (cliente) - entrar noutra maquina pelo Tab5: rodar um comando la e ver a
// saida, ou um shell linha a linha. Motor: libssh2 1.11 com o mbedTLS do
// sistema (components/libssh2).
//
// O que ja vale: senha (e "keyboard-interactive", que muitos servidores usam
// para a senha), saida e erro do outro lado, o codigo de saida do comando, a
// impressao digital da chave do servidor (SHA256, a mesma do 'ssh' do PC).
//
// SERVIDORES CONHECIDOS (como o known_hosts do PC): na primeira conexao a
// impressao digital e guardada em /sdcard/apps/SSH/known_hosts; nas seguintes
// ela tem que bater. MUDOU = recusa antes de pedir a senha (pode ser outro
// aparelho no lugar do servidor); tab5_ssh_esquecer() tira a linha quando a
// troca foi de proposito (servidor reinstalado).
//
// Dois jeitos de usar:
//   tab5_ssh_rodar    um comando, ou um shell "dumb" linha a linha (console,
//                     comando 'ssh' do Terminal);
//   A SESSAO (abaixo) um terminal DE VERDADE (xterm-256color: nano, top, vim)
//                     que mora no servico e e COMPARTILHADO: o app Terminal e
//                     o navegador do PC (services/tab5_terminal_web) entram e saem,
//                     e a sessao continua - comeca no PC, segue no aparelho.
// Ainda nao: chave privada.
//
// QUEM CHAMA: bloqueia (rede e criptografia) - nunca da tarefa de interface.
// Memoria: as alocacoes do libssh2 vao para a PSRAM; o mbedTLS do sistema ja
// aloca la. Pilha: a vaga de 6 KB do pool basta (o Terminal roda nela).
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
  const char* usuario;
  const char* host;
  int         porta;      // 0 = 22
  const char* comando;    // nullptr ou "" = shell linha a linha
  // Pede uma linha a quem usa - a senha (oculto) e, no shell, o que digitar.
  // Mesmo contrato de console_ler_linha: 1 = linha, 0 = ainda nao, -1 = pare.
  int  (*ler_linha)(const char* pergunta, char* out, size_t n, bool oculto, uint32_t espera_ms);
  // O que chega do outro lado (stdout e stderr) e os recados do cliente.
  void (*saida)(const char* d, int n, void* ctx);
  void* ctx;
  bool (*parar)(void);    // Ctrl+C; nullptr = nunca
} tab5_ssh_t;

// Conecta, autentica, roda e desconecta. Devolve o codigo de saida do comando
// remoto (0 = deu certo; no shell, o do 'exit'), ou -1 com o motivo em 'erro'.
int tab5_ssh_rodar(const tab5_ssh_t* s, char* erro, size_t erro_n);

// Esquece a impressao digital guardada de host:porta. false = nao havia.
// Toca no cartao: fora da interface.
bool tab5_ssh_esquecer(const char* host, int porta);

// ===========================================================================
// A SESSAO INTERATIVA - um terminal de verdade, compartilhado
// ===========================================================================
// UMA por vez, numa vaga do pool (6 KB): o app e a pagina sao ESPECTADORES.
//   - a TELA mora aqui, num emulador (components/libvterm): o app Terminal le
//     as celulas (tab5_ssh_sessao_celulas) e desenha;
//   - o navegador recebe o FLUXO CRU (xterm.js), por assinatura, e ao entrar
//     um retrato da tela atual em sequencias de escape (tab5_ssh_sessao_retrato);
//   - o TAMANHO (colunas x linhas) e o de quem digitou por ultimo, como no
//     tmux: cada espectador manda o seu com tab5_ssh_sessao_tamanho.
// As perguntas da conexao (servidor novo: aceitar a impressao digital? a
// senha) ficam no estado, e qualquer espectador responde.
// Tudo seguro de qualquer tarefa e sem bloquear (a escrita vai por uma fila);
// os retornos de assinatura rodam na tarefa da sessao e nao devem demorar.
typedef enum {
  TAB5_SSH_LIVRE = 0,     // nenhuma sessao
  TAB5_SSH_CONECTANDO,
  TAB5_SSH_CONFIRMAR,     // servidor novo: esperando aceitar a impressao digital
  TAB5_SSH_SENHA,         // esperando a senha
  TAB5_SSH_ABERTA,
  TAB5_SSH_FIM,           // terminou (msg diz por que); abrir de novo comeca outra
} tab5_ssh_estado_t;

typedef struct {
  tab5_ssh_estado_t estado;
  char     usuario[32], host[64];
  int      porta;
  char     impressao[64];   // SHA256:... (CONFIRMAR e ABERTA)
  char     msg[96];         // o que aconteceu por ultimo ("senha recusada", "a conexao caiu")
  int      cols, linhas;
  uint32_t versao;          // muda a cada mudanca de estado
  uint32_t tela;            // muda a cada mudanca na tela (o app redesenha)
} tab5_ssh_sessao_info_t;

// Abre (CONECTANDO). false = ja ha sessao, ou o pool nao deu tarefa.
bool tab5_ssh_sessao_abrir(const char* usuario, const char* host, int porta, int cols, int linhas);
void tab5_ssh_sessao_info(tab5_ssh_sessao_info_t* out);
void tab5_ssh_sessao_confirmar(bool aceitar);   // em CONFIRMAR
void tab5_ssh_sessao_senha(const char* senha);  // em SENHA (copiada e zerada depois)
void tab5_ssh_sessao_escrever(const void* d, size_t n);   // teclas (bytes para o PTY)
void tab5_ssh_sessao_tamanho(int cols, int linhas);
void tab5_ssh_sessao_fechar(void);              // encerra (qualquer estado)
void tab5_ssh_sessao_esquecer_fim(void);        // FIM -> LIVRE (quem mostrou a mensagem)

// O fluxo cru do servidor, para quem tem o proprio emulador (o navegador).
typedef void (*tab5_ssh_saida_t)(const char* d, size_t n, void* ctx);
int  tab5_ssh_sessao_assinar(tab5_ssh_saida_t fn, void* ctx);   // id, ou -1 (sem vaga)
void tab5_ssh_sessao_desassinar(int id);

// A tela atual em sequencias de escape (limpa, pinta celula a celula com as
// cores, poe o cursor): quem entra no meio ve o que esta la. Devolve os
// bytes (0 = sem sessao aberta). 'out' na PSRAM, 'max' de uns 32 KB.
size_t tab5_ssh_sessao_retrato(char* out, size_t max);

// A TELA para desenhar (o app Terminal): uma linha de celulas. Cores em
// RGB565, ja resolvidas da paleta. false = linha fora da tela ou sem sessao.
typedef struct {
  uint32_t cp;          // 0 = vazia; o segundo meio de um caractere largo tambem e 0
  uint16_t fg, bg;
  uint8_t  largura;     // 1 ou 2
  bool     negrito, sublinhado;
} tab5_ssh_celula_t;
bool tab5_ssh_sessao_celulas(int linha, tab5_ssh_celula_t* out, int max);
// O cursor (linha, coluna) e se ele aparece agora.
void tab5_ssh_sessao_cursor(int* linha, int* coluna, bool* visivel);
