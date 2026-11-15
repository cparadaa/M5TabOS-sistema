// Console serial - um punhado de comandos digitados pelo monitor.
//
// Nasceu de uma necessidade concreta: trocar a rede WiFi exigia desligar o
// aparelho, tirar o cartao, levar no PC, editar, e remontar tudo. Para uma
// linha de texto. Agora e um comando.
//
// Funciona dentro do proprio `idf.py monitor` - ele encaminha o que voce digita
// para a serial. Digite "help" para ver os comandos.
//
// Nao substitui o cartao no PC para arquivo grande; e para ajuste pontual.

#pragma once

#include <stddef.h>
#include <stdint.h>

// Cria a tarefa do console. Chamar depois de tab5_sd_iniciar().
void console_iniciar(void);

// SESSAO DE COMANDOS - quem digita: a serial (a do proprio console) ou o
// Terminal na tela (services/tab5_terminal). Cada uma tem a sua pasta atual
// e o seu jeito de interromper. O comando roda na tarefa de quem chamou e
// escreve no stdout DESSA tarefa (no ESP-IDF o stdout e de cada tarefa): o
// Terminal troca o dele por um FILE* que guarda as linhas.
typedef struct {
  char cwd[160];                 // pasta atual; "/sdcard" no comeco, "/" = os discos
  bool (*interrompido)(void);    // Ctrl+C / parada pedida; nullptr = nunca
  // Uma linha de quem digita (ver console_ler_linha). nullptr = nao sabe.
  int  (*ler_linha)(const char* pergunta, char* out, size_t n, bool oculto, uint32_t espera_ms);
  // Ha um TERMINAL DE VERDADE na frente (o app Terminal, a pagina /terminal/):
  // 'ssh usuario@host' abre a sessao SSH compartilhada (services/tab5_ssh) e
  // volta na hora - a tela vira o terminal remoto. Sem ele (a serial), o ssh
  // e o shell linha a linha.
  bool interativo;
} console_sessao_t;

// Roda UMA linha como o console serial rodaria ('help', 'ls', 'ping'...), na
// tarefa de quem chama. A serial e o Terminal rodam ao mesmo tempo, cada um
// com a sua sessao (achada pela tarefa). O MESMO comando nos dois no mesmo
// instante divide o estado proprio dele (o ping conta numa variavel so):
// raro, e o pior caso e um numero errado na tela. 'linha' e copiada.
void console_executar(console_sessao_t* sessao, const char* linha);

// Comando longo (ping, listagem grande, laco de teste) pergunta isto a cada
// volta: true = a sessao pediu para parar.
bool console_interrompido(void);

// COMANDO QUE PERGUNTA (a senha do ssh, as linhas do shell remoto): uma linha
// de quem digita nesta sessao. 'pergunta' aparece antes (na serial) ou na
// linha de comando (no Terminal); 'oculto' = senha (sem eco, mascarada).
// Espera ate espera_ms: 1 = veio a linha; 0 = ainda nao (chame de novo - o
// que foi digitado ate ali fica guardado); -1 = interrompido ou a sessao nao
// sabe perguntar.
int console_ler_linha(const char* pergunta, char* out, size_t n, bool oculto, uint32_t espera_ms);
