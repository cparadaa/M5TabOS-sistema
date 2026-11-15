// TOCADOR DE MUSICA - a capa pela internet: busca no iTunes (artista + titulo)
// e baixa a imagem. Ver tab5_musica_interno.hpp.
#include "tab5_musica_interno.hpp"
#include "tab5_rede.hpp"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "musica";

// ---- a internet ----
// GET pelo servico de rede (services/tab5_rede: HTTPS, redirecionamento) num
// buffer da PSRAM de ate 'max' bytes. So 200 e inteiro: chegou ao limite =
// truncado, descarta. Devolve o buffer (heap_caps_free) e o tamanho em 'n'.
static uint8_t* http_baixar(const char* url, size_t max, size_t* n)
{
  tab5_rede_http_t info;
  char* b = tab5_rede_http_texto(url, max, &info, nullptr);
  if (b && (info.status != 200 || info.recebido >= (int64_t)max)) {
    ESP_LOGW(TAG, "capa: %s respondeu %d (%lld B)", url, info.status, (long long)info.recebido);
    heap_caps_free(b);
    b = nullptr;
  } else if (!b) {
    ESP_LOGW(TAG, "capa: %s", info.erro);
  }
  *n = b ? (size_t)info.recebido : 0;
  return (uint8_t*)b;
}

// Para comparar nomes: so letras e numeros, minusculas, sem acento (UTF-8 de
// 2 bytes vira a letra base quando e latina; o resto some).
static void normalizar(const char* s, char* out, size_t tam)
{
  static const char DOBRA[] = "aaaaaaaceeeeiiiidnooooo/ouuuuyty";   // U+00E0..U+00FF (e as maiusculas, -0x20)
  size_t k = 0;
  for (const uint8_t* p = (const uint8_t*)s; *p && k + 1 < tam; ++p) {
    uint32_t c = *p;
    if (c == 0xC3 && p[1]) { c = 0xC0 + (p[1] & 0x3F); ++p; c = c >= 0xE0 ? c : c + 0x20; c = (uint8_t)DOBRA[c - 0xE0]; }
    else if (c >= 0x80) { continue; }
    if (c >= 'A' && c <= 'Z') { c += 32; }
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) { out[k++] = (char)c; }
  }
  out[k] = '\0';
}

// O artista que a busca devolveu e o nosso? Um contem o outro, sem acento e
// sem pontuacao ("Red Hot Chili Peppers" x "Red Hot Chili Peppers & ...").
static bool mesmo_artista(const char* nosso, const char* deles)
{
  char a[64], b[64];
  normalizar(nosso, a, sizeof(a));
  normalizar(deles, b, sizeof(b));
  return a[0] && b[0] && (strstr(a, b) || strstr(b, a));
}

// A busca do iTunes (sem chave): artista + titulo -> a capa 600x600 do disco.
// SO COM ARTISTA, e so se o artista que voltar for o mesmo: medido, uma
// musica sem etiqueta ("Passeio no Tab5") recebia a capa de qualquer coisa
// que a busca achasse com essas palavras.
net_t musica_capa_internet(const capa_ped_t* p, uint8_t** img, size_t* n)
{
  if (!p->artista[0]) { return NET_NAO_TEM; }
  char termo[160], q[400], url[520];
  snprintf(termo, sizeof(termo), "%s%s%s", p->artista, p->artista[0] ? " " : "", p->titulo);
  tab5_rede_url_codificar(termo, q, sizeof(q));
  snprintf(url, sizeof(url), "https://itunes.apple.com/search?media=music&entity=song&limit=1&term=%s", q);
  size_t n_json = 0;
  char* json = (char*)http_baixar(url, 64 * 1024, &n_json);
  if (!json) { return NET_FALHOU; }
  net_t r = NET_FALHOU;
  const char* chave = strstr(json, "\"artworkUrl100\":\"");
  if (!chave) {
    r = strstr(json, "\"resultCount\":0") ? NET_NAO_TEM : NET_FALHOU;
  } else {
    // O artista do resultado (vem antes da arte no JSON do iTunes).
    char deles[96] = "";
    const char* ar = strstr(json, "\"artistName\":\"");
    if (ar) {
      ar += 14;
      const char* fa = strchr(ar, '"');
      if (fa && (size_t)(fa - ar) < sizeof(deles)) { memcpy(deles, ar, (size_t)(fa - ar)); deles[fa - ar] = '\0'; }
    }
    if (!mesmo_artista(p->artista, deles)) {
      ESP_LOGI(TAG, "capa: a busca devolveu outro artista (\"%s\") - sem capa", deles);
      heap_caps_free(json);
      return NET_NAO_TEM;
    }
    chave += 17;
    const char* fim = strchr(chave, '"');
    char arte[400];
    size_t k = fim ? (size_t)(fim - chave) : 0;
    if (k && k < sizeof(arte)) {
      memcpy(arte, chave, k);
      arte[k] = '\0';
      char* t = strstr(arte, "100x100");
      if (t) { memcpy(t, "600x600", 7); }
      heap_caps_free(json);
      json = nullptr;
      size_t nb = 0;
      uint8_t* b = http_baixar(arte, CAPA_MAX_BYTES, &nb);
      if (b && nb > 100) { *img = b; *n = nb; r = NET_ACHOU; }
      else               { heap_caps_free(b); }
    }
  }
  heap_caps_free(json);
  return r;
}

