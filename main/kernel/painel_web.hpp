// O PAINEL "NO PC" - a tela do servidor web (services/tab5_web) num app.
//
// Quem usa o navegador do PC (o Editor, o Mesh) mostra a MESMA coisa: com a
// parte do app fechada, o que e e o botao Ligar (e quem mais esta aberto);
// aberta, o endereco do PORTAL (http://<ip>/, que abre direto neste app), a
// senha, quem conectou e o Desligar - que fecha so a parte dele. O app so diz o retangulo, quem e o dono e como abrir
// as rotas dele.
//
// Tudo da tarefa de interface. Redesenhar: quando painel_web_assinatura()
// mudar (o app declara AVISO_WEB e compara no ao_aviso ou no desenho).
#pragma once

#include <M5Unified.h>
#include <stdint.h>

// Desenha o painel no retangulo (coordenadas de tela). Limpa o retangulo.
void painel_web_desenhar(lgfx::LovyanGFX& d, int x, int y, int l, int a, const char* dono);

// O toque (tx, ty, mesmo referencial do retangulo) caiu no botao? Entao
// abre (com 'ligar', as rotas do app) ou fecha a parte de 'dono', e avisa
// por recado quando nao deu (sem Wi-Fi, o servidor nao subiu). true = era o botao.
bool painel_web_tocar(int x, int y, int l, int a, int tx, int ty, const char* dono, bool (*ligar)(void));

// Muda quando o que o painel mostra muda (ligou, desligou, alguem conectou).
uint32_t painel_web_assinatura(void);
