// TOCADOR DE MUSICA - a capa: a da etiqueta ID3, a da pasta, o cache no cartao
// e a tarefa propria que resolve o pedido. A busca na internet esta em
// tab5_musica_rede.cpp. Ver tab5_musica_interno.hpp.
#include "tab5_musica_interno.hpp"
#include "ajustes.h"
#include "app.h"
#include "tab5_sd.hpp"
#include "tab5_volumes.hpp"
#include "tab5_wifi.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

static const char* TAG = "musica";

// ---------------------------------------------------------------------------
// A CAPA (ver tab5_musica.hpp)
// ---------------------------------------------------------------------------
// Numa tarefa propria (pilha na PSRAM, nasce no primeiro pedido e fica
// esperando o proximo): a busca pela internet leva segundos e nao pode
// segurar nem o som nem a interface. O laco do som so deixa o pedido.
static capa_ped_t          s_capa_ped = { };     // o ultimo pedido (= a musica que toca)
static bool                s_capa_novo = false;  // ha pedido que a tarefa ainda nao pegou
static tab5_musica_capa_t  s_capa_estado = TAB5_MUSICA_CAPA_NENHUMA;
static char                s_capa_arq[200] = "";
static uint32_t            s_capa_versao = 0;
static TaskHandle_t        s_capa_tarefa = nullptr;

#define CAPA_PILHA       12288                // TLS + HTTP

tab5_musica_capa_t tab5_musica_capa(char* arquivo, size_t tam, uint32_t* versao)
{
  g_musica_trava.travar();
  const tab5_musica_capa_t e = s_capa_estado;
  if (arquivo && tam) { snprintf(arquivo, tam, "%s", e == TAB5_MUSICA_CAPA_PRONTA ? s_capa_arq : ""); }
  if (versao) { *versao = s_capa_versao; }
  g_musica_trava.destravar();
  return e;
}

// A leitura da capa para a interface: pelas portas do servico de volumes
// (segura o disco e grava o pendente antes), o app nao abre arquivo.
uint8_t* tab5_musica_capa_ler(const char* arquivo, size_t* n)
{
  return tab5_volume_ler_tudo(arquivo, 3 * 1024 * 1024, n);
}

// JPEG que o decodificador da tela (TJpgDec) abre: so o "baseline". Uma capa
// progressiva (comum dentro do MP3) sairia em branco - melhor procurar outra.
static bool jpeg_baseline(const uint8_t* b, size_t n)
{
  if (n < 4 || b[0] != 0xFF || b[1] != 0xD8) { return false; }
  size_t i = 2;
  while (i + 4 <= n) {
    if (b[i] != 0xFF) { return false; }
    const uint8_t m = b[i + 1];
    if (m == 0xC0 || m == 0xC1) { return true; }
    if (m >= 0xC2 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) { return false; }   // progressivo, aritmetico...
    if (m == 0xD9 || m == 0xDA) { return false; }
    i += 2 + ((size_t)b[i + 2] << 8 | b[i + 3]);
  }
  return false;
}

static bool e_png(const uint8_t* b, size_t n) { return n > 8 && !memcmp(b, "\x89PNG", 4); }

// Grava a imagem no cache: base + ".jpg"/".png". 'out' = o arquivo.
static bool capa_gravar(const char* base, const uint8_t* img, size_t n, char* out, size_t tam)
{
  const bool png = e_png(img, n);
  if (!png && !jpeg_baseline(img, n)) { return false; }
  snprintf(out, tam, "%s.%s", base, png ? "png" : "jpg");
  FILE* f = fopen(out, "wb");
  if (!f) { return false; }
  const bool ok = fwrite(img, 1, n, f) == n;
  if (fclose(f) != 0 || !ok) { unlink(out); return false; }
  return true;
}

// A imagem dentro da etiqueta ID3v2 (quadro APIC; a capa da frente se houver
// mais de uma). Com o cartao montado.
static bool capa_embutida(const char* abs, const char* base, char* out, size_t tam)
{
  const int fd = open(abs, O_RDONLY);
  if (fd < 0) { return false; }
  bool ok = false;
  uint8_t h[10];
  if (read(fd, h, 10) == 10 && !memcmp(h, "ID3", 3) && h[3] >= 3 && h[3] <= 4 && !(h[5] & 0xC0)) {
    const uint32_t tag = syncsafe(h + 6);
    uint32_t pos = 10;
    while (!ok && pos + 10 <= tag + 10) {
      uint8_t q[10];
      lseek(fd, pos, SEEK_SET);
      if (read(fd, q, 10) != 10 || !q[0]) { break; }
      const uint32_t n = (h[3] == 4) ? syncsafe(q + 4) : be32(q + 4);
      if (!n || n > tag) { break; }
      // Compressao/cifra/unsync no quadro = nao da para usar cru. v2.4 com
      // "tamanho dos dados" (0x01) tem 4 bytes a mais na frente.
      const bool cru = (h[3] == 3) ? !(q[9] & 0xC0) : !(q[9] & 0x0E);
      const size_t extra = (h[3] == 4 && (q[9] & 0x01)) ? 4 : 0;
      if (!memcmp(q, "APIC", 4) && cru && n > 16 + extra && n <= CAPA_MAX_BYTES) {
        uint8_t* d = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
        if (d && (uint32_t)read(fd, d, n) == n) {
          const uint8_t enc = d[extra];
          size_t i = extra + 1;
          while (i < n && d[i]) { ++i; }   // tipo MIME (latin-1)
          ++i;
          ++i;                              // tipo de imagem (3 = capa da frente)
          if (enc == 1 || enc == 2) {       // descricao em UTF-16: termina em 00 00 alinhado
            while (i + 1 < n && (d[i] || d[i + 1])) { i += 2; }
            i += 2;
          } else {
            while (i < n && d[i]) { ++i; }
            ++i;
          }
          if (i < n) { ok = capa_gravar(base, d + i, n - i, out, tam); }
        }
        heap_caps_free(d);
      }
      pos += 10 + n;
    }
  }
  close(fd);
  return ok;
}

// cover.jpg, folder.jpg, front.jpg, capa.jpg, albumart*.jpg na pasta da musica.
static bool capa_da_pasta(const char* abs, char* out, size_t tam)
{
  char pasta[160 + TAB5_MUSICA_CAMINHO_MAX + 2];
  snprintf(pasta, sizeof(pasta), "%s", abs);
  char* barra = strrchr(pasta, '/');
  if (!barra) { return false; }
  *barra = '\0';
  DIR* d = opendir(pasta);
  if (!d) { return false; }
  static const char* const NOMES[] = { "cover", "folder", "front", "capa", "albumart" };
  bool ok = false;
  struct dirent* e;
  while (!ok && (e = readdir(d)) != nullptr) {
    if (e->d_name[0] == '.' || e->d_type == DT_DIR) { continue; }
    if (!termina_com(e->d_name, ".jpg") && !termina_com(e->d_name, ".jpeg") && !termina_com(e->d_name, ".png")) { continue; }
    for (const char* nm : NOMES) {
      if (!strncasecmp(e->d_name, nm, strlen(nm))) {
        snprintf(out, tam, "%s/%s", pasta, e->d_name);
        ok = true;
        break;
      }
    }
  }
  closedir(d);
  return ok;
}

static tab5_musica_capa_t capa_resolver(const capa_ped_t* p, char* out, size_t tam)
{
  char raiz[160], base[200], abs[160 + TAB5_MUSICA_CAMINHO_MAX + 2];
  // A chave do cache: o disco (artista + album) - as musicas do mesmo disco
  // dividem a capa; sem album, artista + titulo; sem nada, o arquivo.
  uint32_t chave;
  if (p->album[0] && p->artista[0]) { chave = fnv(p->album, fnv(p->artista, fnv("d:"))); }
  else if (p->titulo[0])            { chave = fnv(p->titulo, fnv(p->artista, fnv("t:"))); }
  else                              { chave = fnv(p->caminho); }
  // O CACHE MORA NO CARTAO, a musica pode morar no pendrive: a capa de uma
  // musica do pendrive tambem fica guardada (no cartao), e sem cartao ainda
  // vale a imagem da pasta dela.
  const bool sd = tab5_sd_montar();
  const bool abs_c = absoluto(p->caminho);
  const bool disco = abs_c ? tab5_volume_montar(p->caminho) : sd;
  if (!disco) { if (sd) { tab5_sd_desmontar(); } return TAB5_MUSICA_CAPA_NENHUMA; }
  if (sd) { musica_raiz_montado(raiz, sizeof(raiz)); }
  if (abs_c) { snprintf(abs, sizeof(abs), "%s", p->caminho); }
  else       { snprintf(abs, sizeof(abs), "%s/%s", raiz, p->caminho); }
  struct stat st;
  bool sem = false;
  bool ok = false;
  if (sd) {
    snprintf(base, sizeof(base), "%s/.capas", raiz);
    mkdir(base, 0775);
    snprintf(base, sizeof(base), "%s/.capas/%08lx", raiz, (unsigned long)chave);
    for (const char* ext : { ".jpg", ".png" }) {
      snprintf(out, tam, "%s%s", base, ext);
      if (stat(out, &st) == 0 && st.st_size > 0) { ok = true; break; }
    }
    if (!ok) { ok = capa_embutida(abs, base, out, tam); }
  }
  if (!ok) { ok = capa_da_pasta(abs, out, tam); }
  if (!ok && sd) {
    char marca[220];
    snprintf(marca, sizeof(marca), "%s.sem", base);
    sem = stat(marca, &st) == 0;
  }
  if (abs_c) { tab5_volume_desmontar(p->caminho); }
  if (sd) { tab5_sd_desmontar(); }
  if (ok) { return TAB5_MUSICA_CAPA_PRONTA; }
  if (!sd || sem || !ajustes_capa_internet() || tab5_wifi_estado() != TAB5_WIFI_CONECTADO) { return TAB5_MUSICA_CAPA_NENHUMA; }

  // A INTERNET, com o cartao solto (segundos de rede nao seguram o pendrive).
  uint8_t* img = nullptr;
  size_t n = 0;
  const net_t r = musica_capa_internet(p, &img, &n);
  if (r == NET_FALHOU) { heap_caps_free(img); return TAB5_MUSICA_CAPA_NENHUMA; }
  if (!tab5_sd_montar()) { heap_caps_free(img); return TAB5_MUSICA_CAPA_NENHUMA; }
  if (r == NET_ACHOU) {
    ok = capa_gravar(base, img, n, out, tam);
    if (ok) { ESP_LOGI(TAG, "capa da internet: %s (%u bytes)", out, (unsigned)n); }
  }
  if (!ok) {
    // Nao tem (ou veio uma que a tela nao abre): marca, para nao perguntar
    // de novo a cada vez que esta musica tocar. Apagar .capas/ refaz a busca.
    char marca[220];
    snprintf(marca, sizeof(marca), "%s.sem", base);
    FILE* f = fopen(marca, "w");
    if (f) { fclose(f); }
  }
  tab5_sd_desmontar();
  heap_caps_free(img);
  return ok ? TAB5_MUSICA_CAPA_PRONTA : TAB5_MUSICA_CAPA_NENHUMA;
}

static void capa_tarefa(void*)
{
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    for (;;) {
      capa_ped_t p;
      g_musica_trava.travar();
      const bool tem = s_capa_novo;
      if (tem) { p = s_capa_ped; s_capa_novo = false; }
      g_musica_trava.destravar();
      if (!tem) { break; }
      char arq[200] = "";
      const tab5_musica_capa_t r = capa_resolver(&p, arq, sizeof(arq));
      g_musica_trava.travar();
      if (!s_capa_novo && !strcmp(s_capa_ped.caminho, p.caminho)) {   // ainda e a que toca
        s_capa_estado = r;
        snprintf(s_capa_arq, sizeof(s_capa_arq), "%s", arq);
        ++s_capa_versao;
      }
      g_musica_trava.destravar();
      if (r == TAB5_MUSICA_CAPA_PRONTA) { sistema_acordar_interface(); }
    }
  }
}

void musica_capa_pedir(const char* caminho, const char* titulo, const char* artista, const char* album)
{
  g_musica_trava.travar();
  if (!strcmp(s_capa_ped.caminho, caminho) && s_capa_estado != TAB5_MUSICA_CAPA_NENHUMA) {
    g_musica_trava.destravar();   // a mesma musica de novo (repetir uma): ja tem
    return;
  }
  snprintf(s_capa_ped.caminho, sizeof(s_capa_ped.caminho), "%s", caminho);
  snprintf(s_capa_ped.titulo, sizeof(s_capa_ped.titulo), "%s", titulo);
  snprintf(s_capa_ped.artista, sizeof(s_capa_ped.artista), "%s", artista);
  snprintf(s_capa_ped.album, sizeof(s_capa_ped.album), "%s", album);
  s_capa_novo = true;
  s_capa_estado = TAB5_MUSICA_CAPA_BUSCANDO;
  s_capa_arq[0] = '\0';
  ++s_capa_versao;
  TaskHandle_t t = s_capa_tarefa;
  g_musica_trava.destravar();
  if (!t) {
    if (xTaskCreatePinnedToCoreWithCaps(capa_tarefa, "capa", CAPA_PILHA, nullptr, 1, &t,
                                        tskNO_AFFINITY, MALLOC_CAP_SPIRAM) != pdPASS) {
      ESP_LOGW(TAG, "capa: a tarefa nao subiu");
      g_musica_trava.travar();
      s_capa_estado = TAB5_MUSICA_CAPA_NENHUMA;
      ++s_capa_versao;
      g_musica_trava.destravar();
      return;
    }
    s_capa_tarefa = t;
  }
  xTaskNotifyGive(t);
}
