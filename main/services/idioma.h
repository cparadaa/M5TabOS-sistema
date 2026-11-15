// IDIOMA DA INTERFACE - escolhido em Ajustes > Idioma (e nas boas-vindas).
//
// A CHAVE E O PROPRIO TEXTO EM PORTUGUES, como no gettext:
//
//     d.drawString(tr("Cancelar"), x, y);
//     snprintf(t, sizeof(t), tr("Remover \"%s\"?"), app->nome);
//
// Em portugues tr() devolve o mesmo ponteiro, sem busca. Nos outros procura a
// frase; FRASE SEM TRADUCAO SAI EM PORTUGUES.
//
// CADA IDIOMA E UM ARQUIVO .po (o formato do gettext - o Poedit e qualquer
// ferramenta de traducao abrem), nunca codigo:
//   - idiomas/en.po vai EMBUTIDO no firmware;
//   - qualquer .po em /sdcard/apps/Sistema/idiomas/ entra na lista no proximo
//     boot (um fr.po ali = frances, sem compilar nada). Um en.po no cartao
//     substitui o embutido - corrigir uma traducao tambem nao pede firmware.
// O cabecalho do .po diz o codigo ("Language: fr") e o nome que aparece na
// lista ("X-Language-Name: Français"). ferramentas/idiomas.py junta todos os
// tr("...") do codigo em idiomas/M5tabOS.pot e atualiza os .po.
//
// Todos os idiomas sao lidos UMA vez, no boot, para a PSRAM, e ficam ate o
// proximo boot: trocar e instantaneo, e um texto devolvido por tr() nunca
// deixa de existir.
//
// O que NAO passa por aqui: o console serial, os logs e o .nome do app_t (que
// tambem e pasta e comando) - o nome DESENHADO e tr(app->nome).

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#define IDIOMAS_PASTA "Sistema/idiomas"   // dentro de /sdcard/apps (tab5_sd)

// Le os idiomas (o embutido e os do cartao) e aplica o escolhido. Uma vez, no
// boot, depois do cartao (tab5_sd_iniciar).
void idioma_iniciar(void);

// A lista: 0 e sempre o portugues.
int         idioma_n(void);
const char* idioma_codigo(int i);   // "pt", "en", "fr"...
const char* idioma_nome(int i);     // no proprio idioma: "Português", "English"
int         idioma_atual(void);
// Grava e vale na hora. Quem esta na tela repinta: a barra percebe pela versao.
void        idioma_definir(int i);
// Muda a cada troca. Quem pinta por diferenca compara com a que desenhou.
uint32_t    idioma_versao(void);

// O texto no idioma atual. 'pt' e a chave; nullptr devolve nullptr.
const char* tr(const char* pt);

// MARCA SEM TRADUZIR: para texto guardado numa tabela e traduzido so na hora
// de desenhar (tr(TABELA[i])). Nao faz nada - e o que ferramentas/idiomas.py
// procura, alem do tr("...").
#define N_(s) (s)

// "terça-feira, 21 de outubro" / "Tuesday, October 21" - a ordem tambem vem
// do .po (a frase "{dia}, {n} de {mes}").
void idioma_data_por_extenso(const struct tm* t, char* out, size_t n);
