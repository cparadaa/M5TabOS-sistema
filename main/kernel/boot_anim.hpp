// ANIMACAO DE BOOT - a que aparece ao LIGAR o Tab5 (nao confundir com a
// "abertura" de um app, a animacao de crescer do icone).
//
// Uma teia viva: sete nos principais FIXOS (o desenho da malha, um pouco
// mais destacados) e pontos de fundo que passeiam, se ligando e se soltando
// entre si e com os nos. Dos nos saem caracteres que voam ate o centro, o
// nome "M5tabOS" e decifrado letra por letra e aparece "[ conectado ]".
// A maquete aprovada esta na entrada de 2026-11-15 do docs/PASSAGEM.md.
//
// QUANDO: so no boot, chamada pela tarefa de interface (kernel/core.cpp)
// depois que o painel assumiu o relogio final e a tela virtual existe. Ao
// acordar do repouso NAO aparece. Um toque na tela pula o resto.
//
// CUSTO PARA O BOOT: nenhum a mais. A animacao ocupa o tempo em que a
// tarefa de interface so esperaria; o resto do boot (USB, pendrive, radio)
// segue em paralelo na app_main.
//
// DESENVOLVIMENTO: a chave TAB5_MOSTRAR_ANIM_BOOT (kernel/internal.hpp)
// desliga a animacao inteira - quem reinicia o aparelho toda hora para
// testar nao precisa assistir a ela a cada vez.
#pragma once

// Mostra a animacao inteira (~1,9 s, ou ate um toque) e devolve a tela ja
// pronta para o primeiro desenho do sistema.
void boot_anim_mostrar(void);
