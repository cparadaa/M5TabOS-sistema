// SHELL - a home: as paginas, a ordem dos icones (gravada na NVS), mover e
// remover icone, as bolinhas de pagina e o desenho dos icones. Partido de
// shell.cpp; o que as partes compartilham esta em shell_interno.hpp.

#include "esp_attr.h"
#include "bloqueio_anim.hpp"
#include "tela_virtual.hpp"
#include "ajustes.h"
#include "tab5_tela.hpp"
#include "tab5_ui.hpp"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>
#include <strings.h>   // strncasecmp
#include <stdio.h>
#include "shell_interno.hpp"
#include "idioma.h"
#include "tab5_hora.hpp"
#include "tab5_wifi.hpp"
#include "tab5_mesh.hpp"
#include "tab5_tema.hpp"
#include <time.h>

static const char *TAG = "shell";

// GEOMETRIA DA HOME (px). De cima para baixo: o cartao do relogio, a grade de
// icones (cada um com o rotulo embaixo) e a faixa das bolinhas de pagina.
// Colunas e linhas por pagina saem do espaco livre (home_por_pagina), nao de
// numeros fixos.
static const int CARTAO_Y   = TEMA_ESPACO_4;    // do topo da area ate o cartao
static const int CARTAO_PAD = 28;               // respiro dentro do cartao
static const int CARTAO_A   = 2 * CARTAO_PAD + TEMA_LETRA_GRANDE + 4 + TEMA_LETRA_CORPO;   // hora + data
static const int GRADE_TOPO = CARTAO_Y + CARTAO_A + TEMA_ESPACO_5;   // onde comeca a 1a linha de icones

// Rotulo (nome do app): 10 px abaixo do icone, letra de 24 px. A reserva e o
// que a linha ocupa alem do quadrado do icone; entre uma linha e a outra
// sobram ESPACO_LINHAS.
static const int ROTULO_ESPACO  = 10;
static const int ROTULO_LETRA   = 24;
static const int ROTULO_LETRA_MIN = 20;
static const int ROTULO_RESERVA = ROTULO_ESPACO + ROTULO_LETRA;
static const int ESPACO_LINHAS  = 28;

// Faixa embaixo dos icones para as bolinhas de pagina.
static const int HOME_BOLINHAS_A = 32;

// Vao horizontal entre icones: a grade ocupa a largura menos a margem dos dois
// lados, e o que sobra se reparte entre as colunas.
static int vao_colunas(const area_t& a)
{
  const int cols = icone_cols(a);
  return (a.largura - 2 * TEMA_MARGEM - cols * ICONE_LADO) / (cols - 1);
}

static void celula_do_icone(int i, const area_t& a, int* px, int* py)
{
  const int cols = icone_cols(a);
  const int col = i % cols;
  const int lin = i / cols;
  *px = a.x + TEMA_MARGEM + col * (ICONE_LADO + vao_colunas(a));
  *py = a.y + GRADE_TOPO + lin * (ICONE_LADO + ROTULO_RESERVA + ESPACO_LINHAS);
}

// ---------------------------------------------------------------------------
// PAGINAS DA HOME
// ---------------------------------------------------------------------------
// A pagina p mostra os apps [p*por_pagina, (p+1)*por_pagina). celula_do_icone()
// continua recebendo o indice DENTRO da pagina, entao desenho e toque (que
// usam a mesma funcao) seguem sempre de acordo. Nada de apps.cpp muda: o
// numero de paginas sai de SISTEMA_NUM_APPS.
static int s_pagina_home = 0;

static int home_por_pagina(const area_t& a)
{
  int linhas = 0;
  while (linhas < TAB5_HOME_LINHAS_MAX) {
    int px, py;
    celula_do_icone(linhas * icone_cols(a), a, &px, &py);
    if (py + ICONE_LADO + ROTULO_RESERVA > a.y + a.altura - HOME_BOLINHAS_A) { break; }
    ++linhas;
  }
  return (linhas < 1 ? 1 : linhas) * icone_cols(a);
}

static int home_paginas(const area_t& a)
{
  const int pp = home_por_pagina(a);
  return (SISTEMA_NUM_APPS + pp - 1) / pp;
}

bool home_mudar_pagina(int delta)
{
  const int n = home_paginas(area_t(sistema_area()));
  int nova = s_pagina_home + delta;
  if (nova < 0 || nova >= n) { return false; }
  s_pagina_home = nova;
  s_sujo_layout = true;   // mudou a POSICAO dos icones: repintura completa
  return true;
}

// ---------------------------------------------------------------------------
// ORDEM DOS ICONES (ver sistema_home_app em app.h)
// ---------------------------------------------------------------------------
// s_ordem[pos] = indice em SISTEMA_APPS. Desenho e toque leem daqui; so
// mover/carregar escrevem, e sempre dentro de s_ordem_mux - o console mexe de
// outra tarefa. Ler sem trava e seguro: cada entrada e um byte, e o pior caso
// e um quadro com a ordem velha, logo repintado (mover pede redesenho).
#define HOME_APPS_MAX 64
static uint8_t s_ordem[HOME_APPS_MAX];
static int     s_n_ordem = 0;
static portMUX_TYPE s_ordem_mux = portMUX_INITIALIZER_UNLOCKED;

static int home_n(void) { return s_n_ordem ? s_n_ordem : SISTEMA_NUM_APPS; }

const app_t* sistema_home_app(int pos)
{
  if (pos < 0 || pos >= home_n()) { return nullptr; }
  return SISTEMA_APPS[s_n_ordem ? s_ordem[pos] : pos];
}

int sistema_home_posicao(const app_t* app)
{
  for (int i = 0; i < home_n(); ++i) { if (sistema_home_app(i) == app) { return i; } }
  return -1;
}

// Monta a ordem a partir dos nomes: os conhecidos na ordem dada (repetido
// vale uma vez), e depois os que faltaram na ordem de apps.cpp.
static void ordem_de_nomes(const char* nomes)
{
  uint8_t nova[HOME_APPS_MAX];
  bool usado[HOME_APPS_MAX] = {};
  const int total = SISTEMA_NUM_APPS < HOME_APPS_MAX ? SISTEMA_NUM_APPS : HOME_APPS_MAX;
  if (SISTEMA_NUM_APPS > HOME_APPS_MAX) {
    ESP_LOGE(TAG, "%d apps, a home so ordena %d (HOME_APPS_MAX)", SISTEMA_NUM_APPS, HOME_APPS_MAX);
  }
  int n = 0;
  const char* p = nomes ? nomes : "";
  while (*p) {
    const char* fim = strchr(p, ',');
    const size_t len = fim ? (size_t)(fim - p) : strlen(p);
    for (int i = 0; i < total; ++i) {
      const char* nome = SISTEMA_APPS[i]->nome;
      if (SISTEMA_APPS[i]->fora_da_home) { continue; }   // nao tem icone (app.h)
      if (!usado[i] && strlen(nome) == len && strncasecmp(nome, p, len) == 0) {
        usado[i] = true; nova[n++] = (uint8_t)i; break;
      }
    }
    if (!fim) { break; }
    p = fim + 1;
  }
  for (int i = 0; i < total; ++i) { if (!usado[i] && !SISTEMA_APPS[i]->fora_da_home) { nova[n++] = (uint8_t)i; } }

  taskENTER_CRITICAL(&s_ordem_mux);
  memcpy(s_ordem, nova, n);
  s_n_ordem = n;
  taskEXIT_CRITICAL(&s_ordem_mux);
}

// UM buffer para gravar e para carregar a ordem (os dois so rodam na
// interface, nunca ao mesmo tempo): eram dois de 1 KB na RAM interna.
EXT_RAM_BSS_ATTR static char s_ordem_nomes[1024];   // so a interface (PSRAM: ver main/linker.lf)

static void ordem_gravar(void)
{
  char* const nomes = s_ordem_nomes;
  const size_t tam = sizeof(s_ordem_nomes);
  size_t k = 0;
  nomes[0] = 0;
  for (int i = 0; i < home_n(); ++i) {
    const char* nome = sistema_home_app(i)->nome;
    const size_t len = strlen(nome);
    if (k + len + 2 > tam) { break; }
    if (k) { nomes[k++] = ','; }
    memcpy(nomes + k, nome, len);
    k += len;
    nomes[k] = 0;
  }
  ajustes_definir_ordem_apps(nomes);
}

void home_ordem_carregar(void)
{
  ajustes_ordem_apps(s_ordem_nomes, sizeof(s_ordem_nomes));
  ordem_de_nomes(s_ordem_nomes);
}

// Tira o app de 'de' e encaixa em 'para'; os do meio andam uma casa.
static void ordem_mover(int de, int para)
{
  taskENTER_CRITICAL(&s_ordem_mux);
  if (s_n_ordem && de >= 0 && de < s_n_ordem && para >= 0 && para < s_n_ordem && de != para) {
    const uint8_t v = s_ordem[de];
    if (de < para) { memmove(&s_ordem[de], &s_ordem[de + 1], para - de); }
    else           { memmove(&s_ordem[para + 1], &s_ordem[para], de - para); }
    s_ordem[para] = v;
  }
  taskEXIT_CRITICAL(&s_ordem_mux);
}

bool sistema_home_mover(const app_t* app, int pos)
{
  const int de = sistema_home_posicao(app);
  if (de < 0 || pos < 0 || pos >= home_n()) { return false; }
  ordem_mover(de, pos);
  ordem_gravar();
  sistema_pedir_redesenho_completo();
  return true;
}

void sistema_home_ordem_padrao(void)
{
  ordem_de_nomes("");
  ajustes_definir_ordem_apps("");
  sistema_pedir_redesenho_completo();
}

// Que posicao (na ordem) esta sob o dedo, na pagina atual; -1 = nenhuma.
// Sem folga e o quadrado do icone (o clique). Com folga a celula inteira,
// rotulo e meio vao incluidos, e uma celula vazia no fim da ultima pagina
// vale a ultima posicao - e o alvo do arraste, que nao pode cair em buraco.
int home_pos_em(const area_t& a, int tx, int ty, bool folga)
{
  const int por_pagina = home_por_pagina(a);
  const int primeiro = s_pagina_home * por_pagina;
  const int fx = folga ? vao_colunas(a) / 2 : 0;
  const int fy_cima  = folga ? ESPACO_LINHAS / 2 : 0;
  const int fy_baixo = folga ? ROTULO_RESERVA + ESPACO_LINHAS / 2 : 0;
  for (int k = 0; k < por_pagina; ++k) {
    int px, py;
    celula_do_icone(k, a, &px, &py);
    if (tx >= px - fx && tx < px + ICONE_LADO + fx && ty >= py - fy_cima && ty < py + ICONE_LADO + fy_baixo) {
      const int pos = primeiro + k;
      if (pos < home_n()) { return pos; }
      return folga ? home_n() - 1 : -1;
    }
  }
  return -1;
}

// ---------------------------------------------------------------------------
// MOVER ICONE (segurar e arrastar) - ver home_segurar em internal.hpp
// ---------------------------------------------------------------------------
// Repinta a home so quando o icone TROCA de casa, nunca a cada amostra do
// dedo: cada repintura completa sao ~70KB por icone (ver desenhar_home).
// Para outra pagina: segurar o dedo na beirada lateral por HOME_BORDA_MS.
#define HOME_SEGURAR_MS 600
#define HOME_BORDA_PX   60
#define HOME_BORDA_MS   700

static int      s_mover = -1;          // posicao do icone levantado; -1 = nenhum
static bool     s_mover_mudou = false;
static uint32_t s_borda_desde = 0;

// REMOVER UM APP DE FORA (script do cartao): com o icone dele levantado,
// aparece uma faixa no topo; soltar nela pede confirmacao. Os apps do sistema
// nao mostram a faixa (sistema_apps_de_fora_removivel). Quem remove de fato e
// o dono dos apps de fora - o kernel so pergunta e pede.
#define HOME_REMOVER_A  110
static bool         s_sobre_remover = false;   // o dedo esta na faixa
const app_t* s_remover_app = nullptr;   // esperando o "Remover" da confirmacao
static area_t       s_rem_bt_sim = {}, s_rem_bt_nao = {};

bool home_segurar(int tx, int ty, uint32_t agora, uint32_t segurando_ms, int desvio)
{
  const area_t a = sistema_area();
  ty -= TOQUE_DY;   // a mesma correcao do clique (tratar_toque)

  if (s_mover < 0) {
    if (segurando_ms < HOME_SEGURAR_MS || desvio > ROLAGEM_ARRASTE_MIN) { return false; }
    const int pos = home_pos_em(a, tx, ty, false);
    if (pos < 0) { return false; }
    s_mover = pos;
    s_mover_mudou = false;
    s_borda_desde = 0;
    s_sujo_layout = true;
    ESP_LOGI(TAG, "movendo '%s' (posicao %d)", sistema_home_app(pos)->nome, pos + 1);
    return true;
  }

  // Na faixa de remover o icone nao troca de casa: so acende a faixa.
  const bool sobre = ty < a.y + HOME_REMOVER_A && sistema_apps_de_fora_removivel(sistema_home_app(s_mover));
  if (sobre != s_sobre_remover) { s_sobre_remover = sobre; s_sujo_layout = true; }
  if (sobre) { s_borda_desde = 0; return true; }

  const int paginas = home_paginas(a);
  const int lado = (tx < a.x + HOME_BORDA_PX && s_pagina_home > 0) ? -1
                 : (tx >= a.x + a.largura - HOME_BORDA_PX && s_pagina_home < paginas - 1) ? +1 : 0;
  if (lado) {
    if (!s_borda_desde) { s_borda_desde = agora; return true; }
    if (agora - s_borda_desde < HOME_BORDA_MS) { return true; }
    s_borda_desde = agora;   // segurando ali, a proxima pagina vem depois de outro prazo
    const int pp = home_por_pagina(a);
    s_pagina_home += lado;
    // Entra pela beirada de onde veio: indo para a direita, primeira casa.
    int para = (lado > 0) ? s_pagina_home * pp : (s_pagina_home + 1) * pp - 1;
    if (para >= home_n()) { para = home_n() - 1; }
    ordem_mover(s_mover, para);
    s_mover = para;
    s_mover_mudou = true;
    s_sujo_layout = true;
    return true;
  }
  s_borda_desde = 0;

  const int pos = home_pos_em(a, tx, ty, true);
  if (pos >= 0 && pos != s_mover) {
    ordem_mover(s_mover, pos);
    s_mover = pos;
    s_mover_mudou = true;
    s_sujo_layout = true;
  }
  return true;
}

bool home_soltar(void)
{
  if (s_mover < 0) { return false; }
  if (s_sobre_remover) {
    s_remover_app = sistema_home_app(s_mover);
    s_sobre_remover = false;
  }
  if (s_mover_mudou) {
    ESP_LOGI(TAG, "'%s' agora na posicao %d", sistema_home_app(s_mover)->nome, s_mover + 1);
    ordem_gravar();
  }
  s_mover = -1;
  s_sujo_layout = true;
  return true;
}

// Indicador de pagina: pontos centrados na faixa acima da barra de navegacao.
// A pagina atual e uma pilula de 28x8; as outras, pontos de 8x8.
static void desenhar_bolinhas(lgfx::LovyanGFX& d, const area_t& a)
{
  const int n = home_paginas(a);
  if (n < 2) { return; }
  const int atual_l = 28, ponto_l = 8, alt = 8, vao = 8;
  const int total = atual_l + (n - 1) * (ponto_l + vao);
  int x = a.x + a.largura / 2 - total / 2;
  const int y = a.y + a.altura - HOME_BOLINHAS_A / 2 - alt / 2;
  for (int i = 0; i < n; ++i) {
    const bool atual = (i == s_pagina_home);
    const int l = atual ? atual_l : ponto_l;
    d.fillRoundRect(x, y, l, alt, alt / 2, atual ? TEMA_TEXTO : TEMA_LINHA);
    x += l + vao;
  }
}

// ---------------------------------------------------------------------------
// CARTAO DO RELOGIO (topo da home)
// ---------------------------------------------------------------------------
// A esquerda a hora e a data por extenso; a direita duas pilulas com o estado
// do Wi-Fi e do Mesh. Tudo vem dos servicos que ja existem (tab5_hora,
// tab5_wifi, tab5_mesh): aqui so se le e se desenha.
static const int PILULA_A = 40;   // altura de cada pilula

static char s_cartao_chave[96] = "";   // o que o cartao mostra agora (ver home_cartao_atualizar)

// Os textos das duas pilulas. 'mesh' fica vazio quando o servico nao esta ativo
// (a pilula some). 'wifi_ok' diz se o ponto e verde.
static void cartao_textos(char* wifi, size_t nw, bool* wifi_ok, char* mesh, size_t nm)
{
  *wifi_ok = (tab5_wifi_estado() == TAB5_WIFI_CONECTADO) && tab5_wifi_ssid() && tab5_wifi_ssid()[0];
  if (*wifi_ok) { snprintf(wifi, nw, "Wi-Fi %s", tab5_wifi_ssid()); }
  else          { snprintf(wifi, nw, "%s", tr("Wi-Fi desligado")); }
  if (tab5_mesh_servico_ativo()) { snprintf(mesh, nm, tr("Mesh · %d nós"), tab5_mesh_num_nos()); }
  else                           { mesh[0] = 0; }
}

// Uma pilula encostada na direita (x_dir), com o pontinho colorido e o texto
// cortado para caber em 'larg_max'.
static void desenhar_pilula(lgfx::LovyanGFX& d, int x_dir, int y, int larg_max, uint16_t cor_ponto, const char* txt)
{
  const int pad = 16, ponto = 12, espaco = 10;
  char t[64];
  snprintf(t, sizeof(t), "%s", txt);
  d.setFont(tab5_fonte_suave(false, ROTULO_LETRA));
  d.setTextSize(1);
  tab5_cortar_ao_caber(d, t, larg_max - 2 * pad - ponto - espaco);
  const int l = 2 * pad + ponto + espaco + (int)d.textWidth(t);
  const int x = x_dir - l;
  d.fillRoundRect(x, y, l, PILULA_A, PILULA_A / 2, TEMA_SUPERFICIE_ALTA);
  d.fillCircle(x + pad + ponto / 2, y + PILULA_A / 2, ponto / 2, cor_ponto);
  d.setTextDatum(middle_left);
  d.setTextColor(TEMA_TEXTO, TEMA_SUPERFICIE_ALTA);
  d.drawString(t, x + pad + ponto + espaco, y + PILULA_A / 2);
}

// A chave resume o que o cartao mostra: se nao mudou, nao ha o que repintar.
static void cartao_chave(char* out, size_t n)
{
  char hora[8], wifi[48], mesh[48];
  bool ok;
  hora_agora(hora, sizeof(hora));
  cartao_textos(wifi, sizeof(wifi), &ok, mesh, sizeof(mesh));
  struct tm lt = {};
  const uint32_t t = tab5_hora_epoch();
  if (t) { tab5_hora_local(t, &lt); }
  snprintf(out, n, "%s|%d|%s|%s|%d", hora, lt.tm_mday, wifi, mesh, idioma_atual());
}

static void desenhar_cartao_relogio(lgfx::LovyanGFX& d, const area_t& a)
{
  const int cx = a.x + TEMA_MARGEM, cy = a.y + CARTAO_Y, cl = a.largura - 2 * TEMA_MARGEM;
  d.fillRoundRect(cx, cy, cl, CARTAO_A, TEMA_RAIO_G, TEMA_SUPERFICIE);

  // Hora e data. Sem a hora acertada: "--:--" e sem data.
  char hora[8], data[48] = "";
  hora_agora(hora, sizeof(hora));
  const uint32_t t = tab5_hora_epoch();
  if (t) {
    struct tm lt;
    tab5_hora_local(t, &lt);
    idioma_data_por_extenso(&lt, data, sizeof(data));
  }
  d.setTextSize(1);
  d.setTextDatum(top_left);
  d.setFont(tab5_fonte_suave(false, TEMA_LETRA_GRANDE));
  d.setTextColor(TEMA_TEXTO, TEMA_SUPERFICIE);
  d.drawString(hora, cx + CARTAO_PAD, cy + CARTAO_PAD);
  d.setFont(tab5_fonte_suave(false, TEMA_LETRA_CORPO));
  d.setTextColor(TEMA_TEXTO_2, TEMA_SUPERFICIE);
  d.drawString(data, cx + CARTAO_PAD, cy + CARTAO_PAD + TEMA_LETRA_GRANDE + 4);
  const int larg_esq = (int)d.textWidth(data);

  // As pilulas, no que sobra a direita do texto, centradas na altura do cartao.
  char wifi[48], mesh[48];
  bool wifi_ok;
  cartao_textos(wifi, sizeof(wifi), &wifi_ok, mesh, sizeof(mesh));
  const int n = mesh[0] ? 2 : 1;
  const int x_dir = cx + cl - CARTAO_PAD;
  const int livre = x_dir - (cx + CARTAO_PAD + larg_esq + TEMA_ESPACO_5);
  int y = cy + (CARTAO_A - (n * PILULA_A + (n - 1) * TEMA_ESPACO_2)) / 2;
  desenhar_pilula(d, x_dir, y, livre, wifi_ok ? TEMA_SUCESSO : TEMA_TEXTO_3, wifi);
  if (mesh[0]) { desenhar_pilula(d, x_dir, y + PILULA_A + TEMA_ESPACO_2, livre, TEMA_DESTAQUE, mesh); }

  tab5_fonte_corpo(d);
  cartao_chave(s_cartao_chave, sizeof(s_cartao_chave));
}

void desenhar_home(lgfx::LovyanGFX& d, const area_t& a)
{
  // PAPEL DE PAREDE (Ajustes > Tela). So nesta passada - a home e estatica e
  // so e desenhada inteira, com a area recem-limpa. A semente e tirada uma
  // vez por boot - e de novo quando o usuario volta a escolher "Sorteio" -,
  // entao o desenho nao muda a cada volta para a home.
  static uint32_t s_semente_parede = 0;
  static uint8_t  s_parede_ant = 0xFF;
  const uint8_t parede = ajustes_parede();
  if (parede == AJUSTES_PAREDE_SORTEIO && s_parede_ant != AJUSTES_PAREDE_SORTEIO) { s_semente_parede = 0; }
  s_parede_ant = parede;
  while (!s_semente_parede) { s_semente_parede = esp_random(); }
  if (parede != AJUSTES_PAREDE_NENHUM) {
    const int estilo = (parede == AJUSTES_PAREDE_SORTEIO)
                     ? (int)(s_semente_parede % AJUSTES_PAREDE_ESTILOS) : parede - 1;
    bq_fundo_parede(d, a, estilo, s_semente_parede);
  } else {
    // Sem papel de parede: o fundo do tema (em faixas, ver tab5_tela_limpar).
    tab5_tela_limpar(a.x, a.y, a.largura, a.altura, TEMA_FUNDO);
  }

  desenhar_cartao_relogio(d, a);

  const int por_pagina = home_por_pagina(a);
  if (s_pagina_home >= home_paginas(a)) { s_pagina_home = 0; }
  const int primeiro = s_pagina_home * por_pagina;
  // Largura que o rotulo pode usar: o icone e o vao ate o vizinho, com uma
  // folga de 8 px para dois nomes lado a lado nao se encostarem.
  const int largura_disponivel = ICONE_LADO + vao_colunas(a) - 8;
  for (int k = 0; k < por_pagina && primeiro + k < home_n(); ++k) {
    int px, py;
    celula_do_icone(k, a, &px, &py);

    // RESPIRO ENTRE ICONES: o desenho de cada icone e do app (app_t.icone); a
    // pausa entre eles, se precisar, e do loop do sistema - quem escreve app
    // desenha UM icone e nao precisa saber disso.
    const app_t* app = sistema_home_app(primeiro + k);
    // O ICONE LEVANTADO (segurar e arrastar): moldura em volta, no vao entre
    // icones - o desenho do icone em si nao muda.
    if (primeiro + k == s_mover) {
      for (int e = 0; e < 4; ++e) {
        d.drawRoundRect(px - 8 + e, py - 8 + e, ICONE_LADO + 16 - 2 * e, ICONE_LADO + 16 - 2 * e,
                        TEMA_RAIO_ICONE + 8 - e, TEMA_TEXTO);
      }
    }
    if (app->icone) {
      app->icone(d, px, py, ICONE_LADO);
    } else {
      // App sem icone proprio: quadrado na cor do app com a inicial.
      d.fillRoundRect(px, py, ICONE_LADO, ICONE_LADO, TEMA_RAIO_ICONE, app->cor);
      char inicial[2] = { app->nome[0], 0 };
      d.setFont(tab5_fonte_suave(true, TEMA_LETRA_GRANDE));
      d.setTextSize(1);
      d.setTextDatum(middle_center);
      d.setTextColor(TEMA_SOBRE_DESTAQUE, app->cor);
      d.drawString(inicial, px + ICONE_LADO / 2, py + ICONE_LADO / 2);
    }

    // NOME DO APP: Roboto regular, centrado abaixo do icone. SEM COR DE FUNDO:
    // com ela o nome saia numa tarja por cima do papel de parede.
    // LARGURA MEDIDA DE VERDADE: se nao couber, a letra desce de 2 em 2 px (ate
    // ROTULO_LETRA_MIN) em alturas nativas - e melhor que cortar o nome, que
    // deixaria de identificar o app.
    int alt_nome = ROTULO_LETRA;
    d.setFont(tab5_fonte_suave(false, alt_nome));
    d.setTextSize(1);
    d.setTextDatum(top_center);
    d.setTextColor(TEMA_TEXTO);
    while ((int)d.textWidth(tr(app->nome)) > largura_disponivel && alt_nome > ROTULO_LETRA_MIN) {
      alt_nome -= 2;
      d.setFont(tab5_fonte_suave(false, alt_nome));
    }
    d.drawString(tr(app->nome), px + ICONE_LADO / 2, py + ICONE_LADO + ROTULO_ESPACO);
    tab5_fonte_corpo(d);
  }
  desenhar_bolinhas(d, a);

  // A FAIXA DE REMOVER, com um app de fora levantado.
  if (s_mover >= 0 && sistema_apps_de_fora_removivel(sistema_home_app(s_mover))) {
    d.fillRoundRect(a.x + TEMA_MARGEM, a.y + 8, a.largura - 2 * TEMA_MARGEM, HOME_REMOVER_A - 16, TEMA_RAIO_G,
                    s_sobre_remover ? TEMA_PERIGO : TEMA_SUPERFICIE_ALTA);
    tab5_titulo(d, 30);
    d.setTextDatum(middle_center);
    d.setTextColor(s_sobre_remover ? TEMA_SOBRE_DESTAQUE : TEMA_PERIGO);
    d.drawString(s_sobre_remover ? tr("Solte para remover") : tr("Arraste aqui para remover"),
                 a.x + a.largura / 2, a.y + HOME_REMOVER_A / 2);
    tab5_fonte_corpo(d);
  }
  // A CONFIRMACAO, depois de soltar na faixa.
  if (s_remover_app) {
    const int bl = a.largura - 80, bh = 330, bx = a.x + 40, by = a.y + (a.altura - bh) / 2;
    d.fillRoundRect(bx, by, bl, bh, TEMA_RAIO_G, TEMA_SUPERFICIE);
    d.drawRoundRect(bx, by, bl, bh, TEMA_RAIO_G, TEMA_LINHA);
    tab5_titulo(d, 34);
    d.setTextDatum(top_left);
    d.setTextColor(TEMA_TEXTO, TEMA_SUPERFICIE);
    char t[64];
    snprintf(t, sizeof(t), tr("Remover \"%s\"?"), tr(s_remover_app->nome));
    d.drawString(t, bx + 30, by + 34);
    d.setFont(tab5_fonte_suave(false, 26));
    d.setTextSize(1);
    d.setTextColor(TEMA_TEXTO_2, TEMA_SUPERFICIE);
    d.drawString(tr("O app e os dados dele vão para a"), bx + 30, by + 100);
    d.drawString(tr("lixeira do cartão (Arquivos recupera)."), bx + 30, by + 136);
    const int bw = (bl - 90) / 2;
    s_rem_bt_nao = { bx + 30, by + bh - 110, bw, 80 };
    s_rem_bt_sim = { bx + 60 + bw, by + bh - 110, bw, 80 };
    tab5_botao(d, s_rem_bt_nao.x, s_rem_bt_nao.y, bw, 80, tr("Cancelar"), TEMA_SUPERFICIE_ALTA, TEMA_TEXTO);
    tab5_botao(d, s_rem_bt_sim.x, s_rem_bt_sim.y, bw, 80, tr("Remover"), TEMA_PERIGO, TEMA_SOBRE_DESTAQUE);
    tab5_fonte_corpo(d);
  }
}

// Chamada pelo loop principal no tique da barra de status: se a hora (minuto),
// o Wi-Fi ou o Mesh mudaram desde a ultima pintura, repinta SO o cartao do
// relogio. Nao faz nada fora da home limpa (app aberto, bloqueio, central de
// avisos, icone levantado ou confirmacao na tela).
void home_cartao_atualizar(void)
{
  if (s_app || s_bloqueado || s_painel_avisos || s_mover >= 0 || s_remover_app) { return; }
  char chave[sizeof(s_cartao_chave)];
  cartao_chave(chave, sizeof(chave));
  if (strcmp(chave, s_cartao_chave) == 0) { return; }
  tela_virtual().startWrite();
  desenhar_cartao_relogio(tela_virtual(), sistema_area());
  tela_virtual().endWrite();
}

// O toque com a confirmacao de remover na tela: Remover remove; qualquer outro
// lugar cancela.
void home_remover_tocar(int tx, int ty)
{
  const app_t* app = s_remover_app;
  s_remover_app = nullptr;
  s_sujo_layout = true;
  if (tab5_dentro(tx, ty, s_rem_bt_sim.x, s_rem_bt_sim.y, s_rem_bt_sim.largura, s_rem_bt_sim.altura)) {
    ESP_LOGI(TAG, "removendo '%s'", app->nome);
    sistema_apps_de_fora_remover(app);
  }
}
