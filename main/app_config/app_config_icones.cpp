// APP: Ajustes - os icones: o quadrado colorido de cada linha da lista (Wi-Fi,
// Bluetooth, ESP-NOW, Mesh, Tela, Teclado, Grove, USB, ...) e o icone do app na
// tela inicial (as engrenagens que engrenam). Partido de app_config.cpp (o app);
// o que as partes compartilham esta em app_config_interno.hpp.

#include "math.h"
#include "app.h"
#include "app_config_interno.hpp"
#include "tab5_tema.hpp"

// ---------------------------------------------------------------------------
// ICONES DA LISTA - quadrado colorido com um desenho simples, como qualquer
// tela de ajustes de celular. A cor vem do tema (TEMA_APP_*), o desenho e
// sempre branco e o canto tem o raio da maquete (14 px em 48).
// Tudo em fracao do lado (l).
// ---------------------------------------------------------------------------
static void fundo_icone(lgfx::LovyanGFX& d, int x, int y, int l, uint16_t cor)
{
  d.fillRoundRect(x, y, l, l, l * 7 / 24, cor);
}

void ic_wifi(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_AZUL);
  const int cx = x + l / 2, cy = y + l * 3 / 4;
  for (int k = 0; k < 3; ++k) {
    const int r = l * (18 + 12 * k) / 100;
    d.fillArc(cx, cy, r, r - l / 14, 225, 315, TEMA_SOBRE_DESTAQUE);
  }
  d.fillCircle(cx, cy - 1, l / 14, TEMA_SOBRE_DESTAQUE);
}

void ic_bt(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_ROXO);
  const int cx = x + l / 2, t = y + l / 6, b = y + l * 5 / 6, m = y + l / 2;
  const int dx = l / 5, q = (b - t) / 4;
  for (int k = -1; k <= 1; ++k) {
    d.drawLine(cx + k, t, cx + k, b, TEMA_SOBRE_DESTAQUE);
    d.drawLine(cx + k, t, cx + dx + k, t + q, TEMA_SOBRE_DESTAQUE);
    d.drawLine(cx + dx + k, t + q, cx - dx + k, m + q, TEMA_SOBRE_DESTAQUE);
    d.drawLine(cx + k, b, cx + dx + k, b - q, TEMA_SOBRE_DESTAQUE);
    d.drawLine(cx + dx + k, b - q, cx - dx + k, m - q, TEMA_SOBRE_DESTAQUE);
  }
}

void ic_espnow(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_VERDE);
  const int cy = y + l / 2;
  d.fillCircle(x + l / 4, cy, l / 9, TEMA_SOBRE_DESTAQUE);
  d.fillCircle(x + l * 3 / 4, cy, l / 9, TEMA_SOBRE_DESTAQUE);
  for (int k = 0; k < 3; ++k) {
    d.fillCircle(x + l * (38 + 12 * k) / 100, cy, l / 28, TEMA_SOBRE_DESTAQUE);
  }
}

void ic_mesh(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_LIMA);
  const int ax = x + l / 4, ay = y + l * 3 / 4, bx = x + l / 2, by = y + l / 4;
  const int cx = x + l * 3 / 4, cy = y + l * 3 / 4;
  for (int k = -1; k <= 1; ++k) {
    d.drawLine(ax, ay + k, bx, by + k, TEMA_SOBRE_DESTAQUE);
    d.drawLine(bx, by + k, cx, cy + k, TEMA_SOBRE_DESTAQUE);
    d.drawLine(ax, ay + k, cx, cy + k, TEMA_SOBRE_DESTAQUE);
  }
  d.fillCircle(ax, ay, l / 10, TEMA_SOBRE_DESTAQUE);
  d.fillCircle(bx, by, l / 10, TEMA_SOBRE_DESTAQUE);
  d.fillCircle(cx, cy, l / 10, TEMA_SOBRE_DESTAQUE);
}

void ic_tela(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_LARANJA);
  const int cx = x + l / 2, cy = y + l / 2;
  d.fillCircle(cx, cy, l / 6, TEMA_SOBRE_DESTAQUE);
  for (int k = 0; k < 8; ++k) {
    const float ang = k * (float)M_PI / 4.0f;
    const int r0 = l * 26 / 100, r1 = l * 36 / 100;
    for (int e = -1; e <= 1; ++e) {
      d.drawLine(cx + (int)(r0 * cosf(ang)) + e, cy + (int)(r0 * sinf(ang)),
                 cx + (int)(r1 * cosf(ang)) + e, cy + (int)(r1 * sinf(ang)), TEMA_SOBRE_DESTAQUE);
    }
  }
}

void ic_teclado(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_GRAFITE);
  const int kx = x + l / 8, ky = y + l * 3 / 10, kl = l * 3 / 4, ka = l * 2 / 5;
  d.fillRoundRect(kx, ky, kl, ka, 4, TEMA_SOBRE_DESTAQUE);
  const uint16_t tec = TEMA_APP_GRAFITE;
  const int t = kl / 7;
  for (int f = 0; f < 2; ++f) {
    for (int c = 0; c < 5; ++c) { d.fillRect(kx + t / 2 + c * (t + t / 3), ky + 5 + f * (t + 2), t, t - 2, tec); }
  }
  d.fillRect(kx + kl / 4, ky + ka - t / 2 - 5, kl / 2, t / 2, tec);
}

void ic_grove(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_ROSA);
  const int cx = x + l / 2;
  d.fillRoundRect(cx - l / 5, y + l * 35 / 100, l * 2 / 5, l * 3 / 10, 5, TEMA_SOBRE_DESTAQUE);
  d.fillRect(cx - l / 8, y + l / 6, l / 14, l / 5, TEMA_SOBRE_DESTAQUE);
  d.fillRect(cx + l / 8 - l / 14, y + l / 6, l / 14, l / 5, TEMA_SOBRE_DESTAQUE);
  d.fillRect(cx - l / 20, y + l * 65 / 100, l / 10, l / 6, TEMA_SOBRE_DESTAQUE);
}

// O simbolo do USB: haste, triangulo na ponta, e os dois ramos (bola e
// quadrado) - o mesmo desenho gravado no plugue.
void ic_usb(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_CIANO);
  const int cx = x + l / 2, topo = y + l / 6, base = y + l * 80 / 100, e = l / 24 + 1;
  d.fillRect(cx - e / 2, topo + l / 10, e, base - topo - l / 10, TEMA_SOBRE_DESTAQUE);
  d.fillTriangle(cx, topo, cx - l / 10, topo + l / 8, cx + l / 10, topo + l / 8, TEMA_SOBRE_DESTAQUE);
  d.fillCircle(cx, base, l / 14, TEMA_SOBRE_DESTAQUE);
  const int ry = y + l * 45 / 100;
  d.drawLine(cx, ry + l / 8, cx - l / 5, ry, TEMA_SOBRE_DESTAQUE);
  d.drawLine(cx + 1, ry + l / 8, cx - l / 5 + 1, ry, TEMA_SOBRE_DESTAQUE);
  d.fillCircle(cx - l / 5, ry - l / 22, l / 18, TEMA_SOBRE_DESTAQUE);
  const int ry2 = y + l * 55 / 100;
  d.drawLine(cx, ry2 + l / 8, cx + l / 5, ry2, TEMA_SOBRE_DESTAQUE);
  d.drawLine(cx - 1, ry2 + l / 8, cx + l / 5 - 1, ry2, TEMA_SOBRE_DESTAQUE);
  d.fillRect(cx + l / 5 - l / 20, ry2 - l / 10, l / 10, l / 10, TEMA_SOBRE_DESTAQUE);
}

// Um monitor (o PC) com o sinal da rede.
void ic_pc(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_AZUL);
  const int mx = x + l / 5, my = y + l / 4, mw = l * 3 / 5, mh = l * 2 / 5;
  d.fillRoundRect(mx, my, mw, mh, l / 20, TEMA_SOBRE_DESTAQUE);
  d.fillRect(mx + l / 24, my + l / 24, mw - l / 12, mh - l / 12, TEMA_APP_AZUL);
  d.fillRect(x + l / 2 - l / 24, my + mh, l / 12, l / 10, TEMA_SOBRE_DESTAQUE);
  d.fillRect(x + l / 2 - l / 6, my + mh + l / 10, l / 3, l / 24 + 1, TEMA_SOBRE_DESTAQUE);
  for (int k = 1; k <= 3; ++k) {   // as ondas do Wi-Fi dentro da tela
    d.drawArc(x + l / 2, my + mh - l / 14, k * l / 14, k * l / 14 - 2, 225, 315, TEMA_SOBRE_DESTAQUE);
  }
}

void ic_sobre(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_GRAFITE);
  const int cx = x + l / 2;
  d.fillCircle(cx, y + l * 28 / 100, l / 12, TEMA_SOBRE_DESTAQUE);
  d.fillRoundRect(cx - l / 14, y + l * 42 / 100, l / 7, l * 38 / 100, 3, TEMA_SOBRE_DESTAQUE);
}

// ---------------------------------------------------------------------------
// LINHA DE ESTADO DE CADA PAGINA (o subtitulo na lista)
// ---------------------------------------------------------------------------
void ic_nuvem(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_CIANO);
  const int cy = y + l * 58 / 100;
  d.fillCircle(x + l * 36 / 100, cy, l * 16 / 100, TEMA_SOBRE_DESTAQUE);
  d.fillCircle(x + l * 56 / 100, cy - l * 8 / 100, l * 20 / 100, TEMA_SOBRE_DESTAQUE);
  d.fillCircle(x + l * 70 / 100, cy + l * 2 / 100, l * 13 / 100, TEMA_SOBRE_DESTAQUE);
  d.fillRect(x + l * 36 / 100, cy, l * 34 / 100, l * 15 / 100, TEMA_SOBRE_DESTAQUE);
}

void ic_hora(lgfx::LovyanGFX& d, int x, int y, int l)
{
  fundo_icone(d, x, y, l, TEMA_APP_VERMELHO);
  const int cx = x + l / 2, cy = y + l / 2, r = l * 32 / 100;
  d.fillArc(cx, cy, r, r - l / 14, 0, 360, TEMA_SOBRE_DESTAQUE);
  d.fillRect(cx - l / 40, cy - r * 7 / 10, l / 20, r * 7 / 10, TEMA_SOBRE_DESTAQUE);
  d.fillRect(cx, cy - l / 40, r * 6 / 10, l / 20, TEMA_SOBRE_DESTAQUE);
}

void ic_idioma(lgfx::LovyanGFX& d, int x, int y, int l)
{
  // Um globo: o contorno, o equador e dois meridianos.
  fundo_icone(d, x, y, l, TEMA_APP_AMARELO);
  const int cx = x + l / 2, cy = y + l / 2, r = l * 32 / 100, e = l / 18;
  d.fillArc(cx, cy, r, r - e, 0, 360, TEMA_SOBRE_DESTAQUE);
  d.fillRect(cx - r, cy - e / 2, 2 * r, e, TEMA_SOBRE_DESTAQUE);
  d.fillRect(cx - e / 2, cy - r, e, 2 * r, TEMA_SOBRE_DESTAQUE);
  for (int k = 0; k < e; ++k) { d.drawEllipse(cx, cy, r / 2 - k, r - 1, TEMA_SOBRE_DESTAQUE); }
}

void ic_energia(lgfx::LovyanGFX& d, int x, int y, int l)
{
  const app_t* e = app_energia();
  if (e && e->icone) { e->icone(d, x, y, l); }
}

// ---------------------------------------------------------------------------
// ICONE DA TELA INICIAL
// ---------------------------------------------------------------------------
// Primitivas e fracao do lado, como a marca do Mesh: sai limpo em qualquer
// tamanho, nao gasta flash e nao serrilha. A letra grande que estava aqui era
// a fonte embutida ampliada 6x - cada pixel dela virava um quadrado de 6.

// ENGRENAGENS QUE ENGRENAM DE VERDADE (terceira versao).
//
// A anterior tinha a composicao certa - grande, e duas menores ao
// lado, como na referencia do icone de ajustes classico - mas as tres se
// SOBREPUNHAM: a distancia entre centros era bem menor que a soma dos raios, e
// viravam um borrao so. Agora e engrenagem de verdade:
//   - MESMO PASSO de dente nas tres (PASSO, % do lado): so assim o dente de
//     uma cabe no vao da outra; o raio de cada uma sai do numero de dentes;
//   - distancia entre centros = soma dos raios primitivos (onde os dentes se
//     encontram);
//   - FASE calculada: a grande aponta um dente para a media, a media poe um
//     VAO ali; para a pequena, a fase compensa onde o dente da grande caiu.
// Dente TRAPEZIOIDAL (mais largo na raiz que na ponta), baixo e largo - o
// dente fino e comprido da primeira prova lia como sol, nao engrenagem.
// Prototipado em imagem antes (a mesma conta), conferido tambem a 96 px.
static const float ENG_PASSO = 10.5f;   // passo circular, % do lado
static const float ENG_DENTE = 6.0f;    // altura do dente, % do lado

static float eng_raio(int n) { return (float)n * ENG_PASSO / (2.0f * (float)M_PI); }

static void desenhar_engrenagem(lgfx::LovyanGFX& d, int x0, int y0, int lado,
                                float cx, float cy, int n, float fase_graus,
                                uint16_t escuro, uint16_t fundo)
{
  const float k  = (float)lado / 100.0f;   // % do lado -> pixels
  const float rp = eng_raio(n);
  const float rr = rp - ENG_DENTE / 2.0f;  // raiz
  const float rt = rp + ENG_DENTE / 2.0f;  // ponta
  auto px = [&](float v) { return x0 + (int)lroundf(v * k); };
  auto py = [&](float v) { return y0 + (int)lroundf(v * k); };

  d.fillCircle(px(cx), py(cy), (int)lroundf(rr * k), escuro);
  const float passo = 360.0f / (float)n;
  const float hb = passo * 0.36f * (float)M_PI / 180.0f;   // meia largura na raiz
  const float ht = passo * 0.22f * (float)M_PI / 180.0f;   // meia largura na ponta
  for (int i = 0; i < n; ++i) {
    const float a = (fase_graus + (float)i * passo) * (float)M_PI / 180.0f;
    const int x1 = px(cx + rr * cosf(a - hb)), y1 = py(cy + rr * sinf(a - hb));
    const int x2 = px(cx + rt * cosf(a - ht)), y2 = py(cy + rt * sinf(a - ht));
    const int x3 = px(cx + rt * cosf(a + ht)), y3 = py(cy + rt * sinf(a + ht));
    const int x4 = px(cx + rr * cosf(a + hb)), y4 = py(cy + rr * sinf(a + hb));
    d.fillTriangle(x1, y1, x2, y2, x3, y3, escuro);
    d.fillTriangle(x1, y1, x3, y3, x4, y4, escuro);
  }
  // Furo do meio, na cor do fundo.
  d.fillCircle(px(cx), py(cy), (int)lroundf(rr * 0.40f * k), fundo);
}

void icone_config(lgfx::LovyanGFX& d, int x, int y, int lado)
{
  const uint16_t fundo  = TEMA_APP_AMARELO;
  const uint16_t escuro = TEMA_FUNDO;
  d.fillRoundRect(x, y, lado, lado, lado / 6, fundo);

  // Grande embaixo a esquerda; a media engrena nela para cima-direita (-40
  // graus), a pequena para baixo-direita (+20). Folgas conferidas: nenhuma
  // encosta na borda arredondada, e media e pequena nao se tocam.
  const int   nG = 14, nM = 9, nP = 6;
  const float gx = 40.0f, gy = 58.0f;
  const float t1 = -40.0f, t2 = 20.0f;
  const float rG = eng_raio(nG), rM = eng_raio(nM), rP = eng_raio(nP);
  const float r2g = (float)M_PI / 180.0f;

  const float mx = gx + (rG + rM) * cosf(t1 * r2g), my = gy + (rG + rM) * sinf(t1 * r2g);
  const float sx = gx + (rG + rP) * cosf(t2 * r2g), sy = gy + (rG + rP) * sinf(t2 * r2g);

  const float fG = t1;                              // dente da grande apontando para a media
  const float fM = t1 + 180.0f + 180.0f / nM;       // vao da media no mesmo ponto
  const float pG = 360.0f / nG;
  const float dlt = t2 - (fG + roundf((t2 - fG) / pG) * pG);   // onde o dente da grande caiu
  const float fP = (t2 + 180.0f - dlt * rG / rP) + 180.0f / nP;

  desenhar_engrenagem(d, x, y, lado, gx, gy, nG, fG, escuro, fundo);
  desenhar_engrenagem(d, x, y, lado, mx, my, nM, fM, escuro, fundo);
  desenhar_engrenagem(d, x, y, lado, sx, sy, nP, fP, escuro, fundo);
}
