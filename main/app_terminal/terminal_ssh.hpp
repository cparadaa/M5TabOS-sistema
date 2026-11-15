// A TELA SSH DO TERMINAL - a sessao SSH compartilhada (services/tab5_ssh) no
// aparelho. So interface: a sessao, a tela (o emulador) e a conexao sao do
// servico; aqui se desenha a grade, se pergunta o que a conexao pede e se
// manda o que se digita. A mesma sessao aparece no navegador do PC
// (services/tab5_terminal_web): o botao "No PC" abre a parte /terminal/.
//
// Quem abre a sessao e o comando 'ssh usuario@host' digitado no Terminal (no
// aparelho ou no navegador); o app mostra esta tela ENQUANTO ela esta de pe
// (conectando, servidor novo, senha, aberta) e volta aos comandos quando ela
// termina - nao ha botao de trocar.
#pragma once

#include "app.h"

void ssh_tela_abrir(void);     // a tela entrou (a sessao subiu, ou o app abriu nela)
void ssh_tela_fechar(void);    // saiu (a sessao terminou ou o app fechou): a sessao segue no servico
void ssh_tela_desenhar(lgfx::LovyanGFX& d, const area_t& a, bool tudo);   // 'a' = a area do app
void ssh_tela_tocar(int x, int y);   // relativo a area
void ssh_tela_tocar_nivel(int x, int y, bool pressionado);
void ssh_tela_teclar(const char* txt, uint8_t codigo, uint8_t mod);
void ssh_tela_aviso(void);     // AVISO_SSH / AVISO_WEB: a tela ou o estado mudaram
