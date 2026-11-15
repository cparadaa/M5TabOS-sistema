// O TERMINAL NO NAVEGADOR DO PC - o MESMO Terminal do aparelho, na parte
// /terminal/ do servidor web (tab5_web), no mesmo esquema do Editor e do Mesh.
//
// Como no app Terminal, sao duas coisas que se revezam na mesma tela:
//   - OS COMANDOS do sistema (services/tab5_terminal: a mesma sessao da tela
//     do Tab5 - o que se digita num aparece no outro). HTTP simples:
//       GET  api/console?v=<versao>&desde=<linha>  o que mudou (JSON)
//       POST api/enviar   (corpo = a linha)         roda / responde a pergunta
//       POST api/parar                              Ctrl+C
//       POST api/limpar                             Ctrl+L
//   - A SESSAO SSH compartilhada (tab5_ssh), que o comando 'ssh usuario@host'
//     abre. O xterm.js vem da FLASH (services/xterm/*.gz): funciona com o PC
//     sem internet. Uma conexao WebSocket (ws) leva tudo:
//       Tab5 -> navegador  binario = o que o servidor SSH mandou (cru, para o
//                          xterm.js); texto = {"t":"estado",...} quando o
//                          estado ou o tamanho mudam. Ao entrar, o retrato da
//                          tela atual.
//       navegador -> Tab5  binario = teclas; texto = {"t":"ola"|"tamanho","c","l"},
//                          {"t":"senha","s"}, {"t":"confirmar","sim"}, {"t":"fechar"}.
// A pagina mostra o SSH enquanto a sessao esta de pe e volta aos comandos
// quando ela termina - a mesma regra da tela do Tab5. Fechar a aba nao
// encerra nada: o comando e a sessao sao dos servicos.
#pragma once

// Abre a parte do Terminal no servidor (/terminal/) e prepara a sessao de
// comandos. De qualquer tarefa (o portal chama de dentro de um pedido).
// false = ver tab5_web_abrir.
bool tab5_terminal_web_ligar(void);
