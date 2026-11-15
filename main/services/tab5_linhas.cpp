#include "tab5_linhas.hpp"
#include "tab5_volumes.hpp"

#include "esp_heap_caps.h"
#include <cstring>

// Quanto pedir ao leitor por vez. O leitor ja guarda blocos de 64 KB; este
// pedaco so evita uma chamada por byte.
#define LINHAS_BLOCO 1024

struct tab5_linhas {
  tab5_leitor_t* leitor;
  char           buf[LINHAS_BLOCO];
  int            pos;    // proximo byte de buf a entregar
  int            n;      // bytes validos em buf
  bool           fim;    // o leitor nao tem mais nada
};

tab5_linhas_t* tab5_linhas_abrir(const char* caminho)
{
  tab5_leitor_t* r = tab5_leitor_abrir(caminho);
  if (!r) { return nullptr; }
  tab5_linhas_t* l = (tab5_linhas_t*)heap_caps_calloc(1, sizeof(*l), MALLOC_CAP_SPIRAM);
  if (!l) { tab5_leitor_fechar(r); return nullptr; }
  l->leitor = r;
  return l;
}

// Proximo byte do arquivo, ou -1 no fim.
static int proximo_byte(tab5_linhas_t* l)
{
  if (l->pos >= l->n) {
    if (l->fim) { return -1; }
    const int lidos = tab5_leitor_ler(l->leitor, l->buf, sizeof(l->buf));
    if (lidos <= 0) { l->fim = true; return -1; }
    l->pos = 0;
    l->n = lidos;
  }
  return (unsigned char)l->buf[l->pos++];
}

bool tab5_linhas_proxima(tab5_linhas_t* l, char* out, size_t tam, bool* cortada)
{
  if (cortada) { *cortada = false; }
  if (!l || !out || tam == 0) { return false; }
  size_t k = 0;
  bool leu_algo = false;
  int c;
  while ((c = proximo_byte(l)) >= 0) {
    leu_algo = true;
    if (c == '\n') { break; }
    if (c == '\r') { continue; }
    if (k + 1 < tam) { out[k++] = (char)c; }   // alem do limite: descarta o resto da linha
    else if (cortada) { *cortada = true; }
  }
  out[k] = '\0';
  return leu_algo;   // a ultima linha sem '\n' tambem conta
}

bool tab5_linhas_ir(tab5_linhas_t* l, uint64_t pos)
{
  if (!l || !tab5_leitor_ir(l->leitor, pos)) { return false; }
  l->pos = 0;   // o pedaco guardado e do lugar antigo: descarta
  l->n = 0;
  l->fim = false;
  return true;
}

uint64_t tab5_linhas_pos(const tab5_linhas_t* l)
{
  // O leitor ja andou ate o fim do pedaco; recua o que ainda nao foi entregue.
  return l ? tab5_leitor_pos(l->leitor) - (uint64_t)(l->n - l->pos) : 0;
}

void tab5_linhas_fechar(tab5_linhas_t* l)
{
  if (!l) { return; }
  tab5_leitor_fechar(l->leitor);
  heap_caps_free(l);
}
