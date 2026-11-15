// TEXTO DE FORA, PRONTO PARA AS FONTES DO SISTEMA.
//
// As fontes suaves (ferramentas/gerar_fontes.py) tem ASCII, o Latin-1 do
// portugues e meia duzia de simbolos; o resto que tem desenho e o emoji de um
// codigo so (tab5_emoji). TODO o restante vira o "dummy" da M5GFX - um
// RETANGULO (IFont::drawCharDummy) - e e dai que vinham as mensagens "todas em
// quadrados" da ponte do Telegram: gente escrevendo do celular manda bandeira
// (dois indicadores regionais), tom de pele, letra "estilizada" (os geradores
// de fonte bonita usam os Simbolos Alfanumericos Matematicos, 𝗻𝗲𝗴𝗿𝗶𝘁𝗼 /
// 𝘪𝘵𝘢𝘭𝘪𝘤𝘰 / 𝓈𝒸𝓇𝒾𝓅𝓉), espaco especial, acento combinado, cirilico...
//
// Aqui cada codigo vira o que da para desenhar, nesta ordem:
//   1. a fonte ou o emoji tem -> fica como esta;
//   2. tem equivalente legivel -> troca (𝗔->A, ａ->a, Ⓐ->A, ł->l, Ж->Zh,
//      🇧🇷->BR, a+◌́->á, espaco fino->espaco, „->", ...);
//   3. e invisivel por natureza (largura zero, seletor, tom de pele, ZWJ) ->
//      some;
//   4. nada disso -> "?" (um ponto de interrogacao diz "havia algo aqui" sem
//      a parede de caixinhas).
//
// Puro: sem M5GFX, sem ESP-IDF - roda no Mac (testes/rodar_unicode.sh). Quem
// sabe o que a fonte e o emoji desenham e quem chama, pelo predicado.
#pragma once

#include <stddef.h>
#include <stdint.h>

// true = este codigo tem desenho proprio (fonte ou emoji).
typedef bool (*tab5_unicode_desenha_t)(uint32_t codigo);

// 'out' sempre termina em NUL; o corte cai sempre numa fronteira de caractere.
// Tambem colapsa as cadeias ZWJ no primeiro emoji (ver tab5_emoji_preparar) e
// troca quebras de linha e tabulacao por espaco.
void tab5_unicode_preparar(const char* in, char* out, size_t out_tam, tab5_unicode_desenha_t desenha);

// ---- POSICOES NUM TEXTO UTF-8 (cursor, corte) -------------------------------
// Sempre numa fronteira de caractere: um acento (2 bytes) ou emoji (4) nunca e
// partido ao meio.
static inline int tab5_utf8_anterior(const char* s, int i)
{
  if (i <= 0) { return 0; }
  --i;
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) { --i; }
  return i;
}

// O caractere seguinte a 'i', sem passar de 'fim' (-1 = ate o '\0').
static inline int tab5_utf8_seguinte(const char* s, int i, int fim = -1)
{
  if (fim >= 0 ? i >= fim : !s[i]) { return i; }
  ++i;
  while ((fim >= 0 ? i < fim : s[i] != 0) && ((unsigned char)s[i] & 0xC0) == 0x80) { ++i; }
  return i;
}

// Quantos dos primeiros 'n' bytes de 's' cabem sem partir um caractere.
// Decide SO pelos n bytes dados - nunca olha s[n]. (Olhava, 04/11/2026: o
// editor insere cada tecla de um buffer da pilha, e o byte depois dela era
// lixo; quando o lixo parecia continuacao de UTF-8, a tecla sumia - o Enter
// das Notas "nao pegava" quase nunca. O notas_ler, depois do fread, tambem
// olhava um byte nao lido.)
static inline int tab5_utf8_corte(const char* s, int n)
{
  if (n <= 0) { return 0; }
  int i = n - 1;   // o comeco do ultimo caractere
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) { --i; }
  const unsigned char b = (unsigned char)s[i];
  const int tam = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 1;
  return i + tam <= n ? n : i;
}
