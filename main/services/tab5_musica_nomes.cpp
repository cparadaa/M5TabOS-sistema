// TOCADOR DE MUSICA - nomes: UTF-8 e o CP437 das playlists antigas, e o titulo
// (e o artista) tirado do nome do arquivo. Ver tab5_musica_interno.hpp.
#include "tab5_musica_interno.hpp"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// NOMES: o FAT daqui fala UTF-8 (CONFIG_FATFS_API_ENCODING_UTF_8): o nome
// que o readdir() entrega e o mesmo que a tela mostra e que o open() aceita.
// Ate a versao anterior ele falava CP437 - e as playlists gravadas naquela
// epoca guardam os caminhos em CP437. Por isso tab5_musica_nome_utf8() (e a
// leitura do .m3u) ainda entendem CP437: texto que NAO e UTF-8 valido so
// pode ter vindo de la, e e convertido.
// ---------------------------------------------------------------------------
static const uint16_t CP437_ALTO[128] = {
  0x00C7,0x00FC,0x00E9,0x00E2,0x00E4,0x00E0,0x00E5,0x00E7,0x00EA,0x00EB,0x00E8,0x00EF,0x00EE,0x00EC,0x00C4,0x00C5,
  0x00C9,0x00E6,0x00C6,0x00F4,0x00F6,0x00F2,0x00FB,0x00F9,0x00FF,0x00D6,0x00DC,0x00A2,0x00A3,0x00A5,0x20A7,0x0192,
  0x00E1,0x00ED,0x00F3,0x00FA,0x00F1,0x00D1,0x00AA,0x00BA,0x00BF,0x2310,0x00AC,0x00BD,0x00BC,0x00A1,0x00AB,0x00BB,
  0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
  0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
  0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
  0x03B1,0x00DF,0x0393,0x03C0,0x03A3,0x03C3,0x00B5,0x03C4,0x03A6,0x0398,0x03A9,0x03B4,0x221E,0x03C6,0x03B5,0x2229,
  0x2261,0x00B1,0x2265,0x2264,0x2320,0x2321,0x00F7,0x2248,0x00B0,0x2219,0x00B7,0x221A,0x207F,0x00B2,0x25A0,0x00A0,
};

// Acrescenta um ponto de codigo em UTF-8; false = nao coube.
bool musica_utf8_por(char* out, size_t tam, size_t* k, uint32_t c)
{
  char b[4]; size_t n;
  if      (c < 0x80)  { b[0] = (char)c; n = 1; }
  else if (c < 0x800) { b[0] = (char)(0xC0 | (c >> 6)); b[1] = (char)(0x80 | (c & 0x3F)); n = 2; }
  else                { b[0] = (char)(0xE0 | (c >> 12)); b[1] = (char)(0x80 | ((c >> 6) & 0x3F));
                        b[2] = (char)(0x80 | (c & 0x3F)); n = 3; }
  if (*k + n >= tam) { return false; }
  memcpy(out + *k, b, n);
  *k += n;
  return true;
}

// O texto e UTF-8 valido? (ASCII puro tambem e.)
bool musica_utf8_valido(const char* s)
{
  for (const uint8_t* p = (const uint8_t*)s; *p; ) {
    if (*p < 0x80) { ++p; continue; }
    int extra = (*p >= 0xF0 && *p < 0xF8) ? 3 : (*p >= 0xE0) ? 2 : (*p >= 0xC2 && *p < 0xE0) ? 1 : -1;
    if (extra < 0 || (*p >= 0xF8)) { return false; }
    ++p;
    while (extra--) { if ((*p & 0xC0) != 0x80) { return false; } ++p; }
  }
  return true;
}

void tab5_musica_nome_utf8(const char* nome, char* out, size_t out_tam)
{
  if (!out_tam) { return; }
  if (musica_utf8_valido(nome)) {
    // Corta sem partir uma letra no meio.
    size_t n = strlen(nome);
    if (n >= out_tam) {
      n = out_tam - 1;
      while (n && ((uint8_t)nome[n] & 0xC0) == 0x80) { --n; }
    }
    memcpy(out, nome, n);
    out[n] = '\0';
    return;
  }
  size_t k = 0;   // CP437 (playlist antiga)
  for (const uint8_t* p = (const uint8_t*)nome; *p; ++p) {
    const uint32_t c = (*p < 0x80) ? *p : CP437_ALTO[*p - 0x80];
    if (!musica_utf8_por(out, out_tam, &k, c)) { break; }
  }
  out[k] = '\0';
}

static void tirar_parenteses(char* t)
{
  char r[160];
  size_t k = 0;
  int fundo = 0;
  for (const char* p = t; *p && k + 1 < sizeof(r); ++p) {
    if (*p == '(' || *p == '[') { ++fundo; continue; }
    if ((*p == ')' || *p == ']') && fundo > 0) { --fundo; continue; }
    if (!fundo) { r[k++] = *p; }
  }
  r[k] = '\0';
  // Espacos repetidos e nas pontas (e o " - " que ficou pendurado no fim).
  char* a = r;
  while (*a == ' ') { ++a; }
  size_t n = strlen(a);
  while (n && (a[n - 1] == ' ' || a[n - 1] == '-' || a[n - 1] == '_')) { a[--n] = '\0'; }
  if (!n) { return; }
  char* w = t;
  for (const char* p = a; *p; ++p) {
    if (*p == ' ' && w > t && w[-1] == ' ') { continue; }
    *w++ = *p;
  }
  *w = '\0';
}

// O titulo (e o artista, se o nome trouxer) a partir do nome do arquivo:
// "Artista - Titulo (Video).mp3" -> "Titulo" e "Artista". Sem " - ", o nome
// todo e o titulo. '_' vira espaco (nome baixado sem espacos).
void tab5_musica_titulo_do_arquivo(const char* nome, char* titulo, size_t tt, char* artista, size_t ta)
{
  char t[TAB5_MUSICA_NOME_MAX];
  snprintf(t, sizeof(t), "%s", nome);
  char* ponto = strrchr(t, '.');
  if (ponto && ponto != t) { *ponto = '\0'; }
  char u[160];
  tab5_musica_nome_utf8(t, u, sizeof(u));
  for (char* p = u; *p; ++p) { if (*p == '_') { *p = ' '; } }
  tirar_parenteses(u);
  char* sep = strstr(u, " - ");
  if (sep && sep != u && artista && ta) {
    *sep = '\0';
    snprintf(artista, ta, "%s", u);
    snprintf(titulo, tt, "%s", sep + 3);
  } else {
    snprintf(titulo, tt, "%s", u);
  }
}
