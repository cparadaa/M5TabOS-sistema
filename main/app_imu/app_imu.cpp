// APP: IMU - visualizador ao vivo dos eixos do BMI270 (acelerometro + giroscopio).
//
// PRA QUE SERVE: nenhum app deste sistema usava a IMU ainda. Este e o
// playground - mostra os 6 eixos (3 do acelerometro, 3 do giroscopio) ao
// vivo, para sentir o que o chip mede na mao antes de decidir o que fazer
// com isso depois (a ideia adiada e girar a tela sozinha, feito celular).
//
// SEM TAREFA PROPRIA, DE PROPOSITO: ao contrario do Mesh/GPS, ninguem
// precisa da IMU com o app FECHADO - ela so importa enquanto alguem esta
// olhando a tela e mexendo o aparelho. desenhar() le o sensor direto, na
// PROPRIA tarefa de interface, que e a unica autorizada a falar com o
// barramento I2C interno (o mesmo do touch e do INA226 - ver o aviso em
// tab5_power.hpp). Uma tarefa e uma queue aqui seriam abstracao sem motivo,
// no mesmo espirito do app Exemplo: nao existe produtor em segundo plano
// para alimentar.
//
// perfil.imu = true E UM FLAG DE FASE 1 (ver tab5_power.hpp): uma vez que
// este app existe na tabela, o BMI270 e ligado no boot para a SESSAO
// INTEIRA. O M5Unified o deixa com giroscopio e sem economia; o servico
// hal/tab5_imu corta isso quando ninguem le o giroscopio (so o acelerometro,
// em economia). Este app le o giroscopio: pede ao abrir e solta ao fechar
// (tab5_imu_giroscopio).

#include "app.h"
#include "idioma.h"
#include "pintura.hpp"
#include <math.h>
#include <stdio.h>
#include "esp_log.h"
#include "tab5_ui.hpp"
#include "tab5_tema.hpp"
#include "tab5_imu.hpp"

static const char* TAG = "app.imu";

// CONTRASTE PROPRIO, NAO SIS_BORDA/SIS_LINHA: moldura e cruzeta ficavam
// FRACAS DEMAIS contra o fundo, SIS_LINHA e rgb(56,60,60), quase invisivel).
// SIS_BORDA/SIS_LINHA sao constantes do SISTEMA (app.h), usadas por varios
// apps para bordas discretas de proposito - mudar as duas mudaria todo
// mundo. Este app e um mostrador de instrumento (nivel + barras de eixo),
// onde a grade PRECISA se ver por cima do fundo - por isso IMU_BORDA so daqui.
// Moldura no tom de texto desativado do tema (mais clara que TEMA_LINHA, que
// some contra o fundo); a cruzeta usa o mesmo tom: precisa se ver para o nivel
// ser lido, e TEMA_LINHA sumia no escuro.
static const uint16_t IMU_BORDA = TEMA_TEXTO_3;

static bool ao_abrir(void)
{
  if (!tab5_imu_disponivel()) {
    // Nao recusa a abertura: desenhar() mostra o aviso na tela mesmo assim.
    // Isso so aconteceria se perfil.imu tivesse ficado false por engano -
    // ver sistema_perfil_de_boot(), que uniao o campo de TODOS os apps.
    ESP_LOGW(TAG, "IMU nao esta habilitada - perfil.imu chegou a false no boot?");
  }
  // O giroscopio liga ja (este app le direto do sensor a cada quadro).
  tab5_imu_giroscopio(TAB5_GIRO_APP_IMU, true);
  tab5_imu_medir();
  ESP_LOGI(TAG, "aberto");
  return true;
}

// FUNDO DO NIVEL, CACHEADO - ver o aviso grande logo abaixo, junto de
// desenhar_nivel(). Criado na primeira chamada, liberado ao fechar o app.
static M5Canvas* s_nivel_fundo   = nullptr;
static int       s_nivel_fundo_r = -1;   // raio com que o fundo foi desenhado

// FUNDO DAS BARRAS DE EIXO, CACHEADO - ver o aviso grande junto de
// desenhar_barra(). Declarado aqui (nao junto da funcao, mais abaixo) pela
// mesma razao do nivel: ao_fechar() precisa liberar os dois.
static M5Canvas* s_barra_fundo   = nullptr;
static int       s_barra_fundo_w = -1, s_barra_fundo_h = -1;

static void ao_fechar(void)
{
  tab5_sprite_soltar(&s_nivel_fundo);
  s_nivel_fundo_r = -1;
  tab5_sprite_soltar(&s_barra_fundo);
  s_barra_fundo_w = s_barra_fundo_h = -1;
  tab5_imu_giroscopio(TAB5_GIRO_APP_IMU, false);   // volta a economia na proxima medida
  ESP_LOGI(TAG, "fechado");
}

// ---------------------------------------------------------------------------
// NIVEL (BOLHA)
// ---------------------------------------------------------------------------
// O ponto se move com a inclinacao lida do acelerometro em repouso: 1g
// dividido entre os eixos conforme o aparelho tomba. Clampado em +-1g - alem
// disso o aparelho ja esta de lado, e o ponto so precisa encostar na borda.
//
// isto redesenhava a caixa INTEIRA a cada frame (20Hz):
// apagava tudo e recomputava dois circulos + cruzeta via algoritmo de
// vetor, so para mover um ponto. Era uma troca CONSCIENTE (documentada aqui
// mesmo: "mais simples que guardar a posicao anterior", "dezenas de KB por
// frame, nada parecido com os ~1,6MB que causam a piscada azul") - nao
// estava errado, so nao tinha sido revisitado ainda no processo de
// otimizacao de energia desta sessao.
//
// A troca agora: o CIRCULO/CRUZETA (que nunca mudam, so o raio deles muda se
// a tela for redimensionada) vao para um M5Canvas pequeno, desenhados so
// quando o raio muda. A cada frame, em vez de recalcular os dois circulos e
// as duas linhas por algoritmo, so faz um pushSprite() do canvas (uma copia
// de bytes, sem trigonometria nem Bresenham) e desenha a bolinha por cima.
// O NUMERO DE BYTES ESCRITOS NA TELA CONTINUA O MESMO (a caixa inteira ainda
// e "revelada" a cada frame, e e assim que o rastro da bolinha antiga
// continua correto sem precisar restaurar so o pedaco onde ela estava) - o
// ganho e SO de CPU: uma copia e sempre mais barata que recalcular circulo e
// linha do zero, 20 vezes por segundo, enquanto o app estiver aberto.
static void desenhar_nivel(lgfx::LovyanGFX& d, int cx, int cy, int r, float ax, float ay)
{
  const int lado = 2 * r + 12;

  if (!s_nivel_fundo) {
    // SEM SPRITE, SEM CACHE - volta ao caminho antigo (redesenha tudo por
    // vetor). So aconteceria sem PSRAM livre, o que ja seria um problema
    // maior que este.
    s_nivel_fundo = tab5_sprite_novo(&d, lado, lado);
  }

  if (!s_nivel_fundo) {
    const uint16_t linha = TEMA_TEXTO_3;   // ver o aviso de IMU_BORDA no topo do arquivo
    d.fillRect(cx - r - 6, cy - r - 6, lado, lado, TEMA_FUNDO);
    d.drawCircle(cx, cy, r, IMU_BORDA);
    d.drawCircle(cx, cy, r / 2, linha);
    d.drawFastHLine(cx - r, cy, 2 * r, linha);
    d.drawFastVLine(cx, cy - r, 2 * r, linha);
  } else {
    if (s_nivel_fundo_r != r) {
      // SO QUANDO O RAIO MUDA (nunca, na pratica - a tela nao redimensiona
      // com o app aberto - mas e o mesmo cuidado do resto do sistema: nao
      // supor um layout fixo por sorte).
      const uint16_t linha = TEMA_TEXTO_3;
      s_nivel_fundo->fillSprite(TEMA_FUNDO);
      s_nivel_fundo->drawCircle(lado / 2, lado / 2, r, IMU_BORDA);
      s_nivel_fundo->drawCircle(lado / 2, lado / 2, r / 2, linha);
      s_nivel_fundo->drawFastHLine(lado / 2 - r, lado / 2, 2 * r, linha);
      s_nivel_fundo->drawFastVLine(lado / 2, lado / 2 - r, 2 * r, linha);
      s_nivel_fundo_r = r;
    }
    s_nivel_fundo->pushSprite(&d, cx - lado / 2, cy - lado / 2);
  }

  float px = ax, py = ay;
  if (px >  1.0f) { px =  1.0f; }
  if (px < -1.0f) { px = -1.0f; }
  if (py >  1.0f) { py =  1.0f; }
  if (py < -1.0f) { py = -1.0f; }

  // Tela cresce para BAIXO; inclinar o topo do aparelho "para tras" e Y
  // positivo no acelerometro, e deve mover o ponto para CIMA na tela -
  // por isso o sinal trocado em py.
  const int dx = cx + (int)(px * (r - 16));
  const int dy = cy - (int)(py * (r - 16));
  d.fillCircle(dx, dy, 11, TEMA_APP_LIMA);
  d.drawCircle(dx, dy, 11, TEMA_FUNDO);
}

// ---------------------------------------------------------------------------
// BARRA CENTRADA EM ZERO
// ---------------------------------------------------------------------------
// 'frac' ja vem em [-1, 1] (o valor dividido pela escala escolhida por quem
// chama). Preenche do centro para o lado que o sinal indicar - o mesmo
// desenho de um medidor de balanco de audio, que e a metafora certa aqui:
// o que importa e o AFASTAMENTO de zero, nos dois sentidos.
//
// FUNDO CACHEADO, (s_barra_fundo, declarado la em cima junto de
// s_nivel_fundo) - achado na mesma varredura que motivou o fix do nivel
// (desenhar_nivel(), ver o aviso grande la): esta funcao roda 6 vezes por
// frame a 20Hz (uma por eixo) e apagava + redesenhava borda e linha de
// centro TODA VEZ, com as SEIS chamadas usando exatamente o mesmo w/h - a
// mesma imagem estatica, seis vezes por frame, sempre recalculada do zero
// por algoritmo de retangulo/linha. Um sprite so, do tamanho de UMA barra,
// serve as seis (todas identicas) - so o preenchimento (que de fato muda por
// eixo) continua desenhado direto na tela.
static void desenhar_barra(lgfx::LovyanGFX& d, int x, int y, int w, int h,
                           float frac, uint16_t cor)
{
  const int cx = x + w / 2;

  if (s_barra_fundo && (s_barra_fundo_w != w || s_barra_fundo_h != h)) {
    tab5_sprite_soltar(&s_barra_fundo);
  }
  if (!s_barra_fundo) {
    // SEM PSRAM PARA O SPRITE (nullptr) - volta ao caminho antigo, mesma
    // defesa do nivel.
    s_barra_fundo = tab5_sprite_novo(&d, w, h);
    if (s_barra_fundo) {
      s_barra_fundo->fillSprite(TEMA_FUNDO);
      s_barra_fundo->drawRect(0, 0, w, h, IMU_BORDA);
      s_barra_fundo->drawFastVLine(w / 2, 0, h, TEMA_LINHA);
      s_barra_fundo_w = w;
      s_barra_fundo_h = h;
    }
  }

  if (s_barra_fundo) {
    s_barra_fundo->pushSprite(&d, x, y);
  } else {
    d.fillRect(x, y, w, h, TEMA_FUNDO);
    d.drawRect(x, y, w, h, IMU_BORDA);
    d.drawFastVLine(cx, y, h, TEMA_LINHA);
  }

  if (frac >  1.0f) { frac =  1.0f; }
  if (frac < -1.0f) { frac = -1.0f; }
  const int px = (int)(frac * (w / 2 - 3));
  if (px >= 0) { d.fillRect(cx, y + 3, px, h - 6, cor); }
  else         { d.fillRect(cx + px, y + 3, -px, h - 6, cor); }
}

typedef struct {
  const char* rotulo;
  float       valor;
  float       escala;    // valor que enche a barra inteira (1.0 == fundo de escala)
  uint16_t    cor;
  const char* unidade;
} eixo_t;

static void desenhar(lgfx::LovyanGFX& d, const area_t& a)
{
  static pintura_t s_p;
  pintura_iniciar(&s_p);

  // O sistema tenta subir de novo uma IMU que falhou no boot (tab5_imu_medir):
  // quando ela aparece, o aviso sai numa repintura completa.
  static bool s_sem_imu = false;
  if (!tab5_imu_disponivel()) {
    if (pintura_completa(&s_p) || !s_sem_imu) {
      s_sem_imu = true;
      tab5_texto(d, 2.3f);
      d.setTextDatum(middle_center);
      d.setTextColor(TEMA_PERIGO, TEMA_FUNDO);
      d.drawString(tr("IMU não respondeu"), a.x + a.largura / 2, a.y + a.altura / 2);
    }
    return;
  }
  if (s_sem_imu) {
    s_sem_imu = false;
    sistema_pedir_redesenho_completo();
    return;
  }

  // UMA leitura por frame, na tarefa de interface - mesma regra do INA226 e
  // do teclado (ver o aviso grande em tab5_power.hpp sobre o barramento
  // interno sem mutex).
  //
  // POR QUE NAO tab5_imu_ultima() (ver hal/tab5_imu.hpp): aquele
  // cache existe para OUTROS apps perguntarem "como o aparelho esta" sem
  // tocar no I2C - mas so e alimentado 1x/segundo, rapido demais para
  // energia/temperatura, devagar demais para um nivel bolha nao parecer
  // travado. Este app le por tab5_imu_amostra(): e o unico lugar que precisa
  // da leitura rapida, e ja e seguro (mesma tarefa).
  //
  // Sem amostra nova neste quadro, ficam os valores do quadro anterior (como
  // o M5Unified ja fazia): zerar faria o nivel piscar.
  static float s_a[3] = {0, 0, 0}, s_g[3] = {0, 0, 0};
  tab5_imu_amostra(s_a, s_g);
  const float ax = s_a[0], ay = s_a[1], az = s_a[2];
  const float gx = s_g[0], gy = s_g[1], gz = s_g[2];

  // DEITADO (teclado fisico encaixado, kernel/orientacao): duas colunas - o
  // nivel a esquerda, as barras a direita. Empilhado, as barras passavam da
  // altura da area (~584 px) e escreviam por cima da navegacao.
  const bool deitado = a.largura > a.altura;
  const int nivel_r  = 130;
  const int nivel_cx = deitado ? a.x + 40 + nivel_r : a.x + a.largura / 2;
  const int nivel_cy = a.y + 24 + nivel_r;
  // O NIVEL E DA TELA, nao do chip: deitada (rotacao 3, kernel/orientacao)
  // o "para a direita" da tela e o +Y do chip e o "para cima" e o -X. Sem
  // girar os eixos, a bolha andava 90 graus fora do lado que se inclinava.
  // As barras abaixo seguem mostrando os eixos do chip (sao medidas).
  desenhar_nivel(d, nivel_cx, nivel_cy, nivel_r, deitado ? ay : ax, deitado ? -ax : ay);

  if (pintura_completa(&s_p)) {
    tab5_texto(d, 2.3f);
    d.setTextDatum(top_center);
    d.setTextColor(TEMA_TEXTO_2, TEMA_FUNDO);
    d.drawString(tr("incline o aparelho"), nivel_cx, nivel_cy + nivel_r + 6);
  }

  // Escala do acelerometro em g (1g = repouso na vertical) e do giroscopio em
  // graus/segundo. Os numeros sao teto de TELA, nao teto do sensor: passar
  // deles so encosta a barra na borda, o valor numerico continua exato.
  const eixo_t eixos[6] = {
    // Cores de DADO (eixos X/Y/Z em vermelho/verde/azul, giro em laranja/amarelo/roxo),
    // suavizadas para os tons do tema sem perder a distincao entre os seis.
    { "AX", ax,   2.0f, TEMA_APP_VERMELHO, "g"   },
    { "AY", ay,   2.0f, TEMA_APP_VERDE,    "g"   },
    { "AZ", az,   2.0f, TEMA_APP_AZUL,     "g"   },
    { "GX", gx, 250.0f, TEMA_APP_LARANJA,  "dps" },
    { "GY", gy, 250.0f, TEMA_APP_AMARELO,  "dps" },
    { "GZ", gz, 250.0f, TEMA_APP_ROXO,     "dps" },
  };

  const int col_x      = deitado ? nivel_cx + nivel_r + 40 : a.x;   // a coluna das barras
  const int y0         = deitado ? a.y + 40 : nivel_cy + nivel_r + 46;
  const int alt_linha  = 72;
  const int barra_x    = col_x + 100;
  const int barra_w    = a.x + a.largura - barra_x - 150;
  const int barra_h    = 36;

  for (int i = 0; i < 6; ++i) {
    const int cy = y0 + i * alt_linha;

    // Rotulo: nunca muda, so entra na passada completa.
    if (pintura_completa(&s_p)) {
      tab5_texto(d, 2.3f);
      d.setTextDatum(middle_left);
      d.setTextColor(TEMA_TEXTO, TEMA_FUNDO);
      d.drawString(eixos[i].rotulo, col_x + 16, cy + barra_h / 2);
    }

    desenhar_barra(d, barra_x, cy, barra_w, barra_h,
                   eixos[i].valor / eixos[i].escala, eixos[i].cor);

    char buf[24];
    snprintf(buf, sizeof(buf), "%+6.2f %s", (double)eixos[i].valor, eixos[i].unidade);
    tab5_texto(d, 2.3f);
    d.setTextDatum(middle_left);
    d.setTextColor(TEMA_TEXTO, TEMA_FUNDO);
    d.drawString(buf, barra_x + barra_w + 12, cy + barra_h / 2);
  }
}

// ---------------------------------------------------------------------------
// ICONE DA TELA INICIAL
// ---------------------------------------------------------------------------
// Tres eixos, convencao comum de ferramenta 3D: X vermelho para a direita, Y
// verde para cima, Z azul como um circulo pontilhado (o eixo que "sai da
// tela" nao tem como virar uma seta plana).
static void icone_imu(lgfx::LovyanGFX& d, int x, int y, int lado)
{
  const uint16_t fundo = d.color565(32, 24, 40);
  d.fillRoundRect(x, y, lado, lado, lado / 6, fundo);

  const int cx = x + lado / 2;
  const int cy = y + lado / 2;
  const int r  = (int)(0.34f * lado);

  for (int ang = 0; ang < 360; ang += 30) {
    const float rad = (float)ang * 3.14159265f / 180.0f;
    const int px = cx + (int)(r * 0.55f * cosf(rad));
    const int py = cy + (int)(r * 0.55f * sinf(rad));
    d.fillCircle(px, py, lado / 60, TFT_SKYBLUE);
  }

  tab5_linha_grossa(d, cx, cy, cx + r, cy, lado / 30.0f, TFT_RED);
  d.fillTriangle(cx + r, cy - lado / 22, cx + r, cy + lado / 22, cx + r + lado / 14, cy, TFT_RED);

  tab5_linha_grossa(d, cx, cy, cx, cy - r, lado / 30.0f, TFT_GREEN);
  d.fillTriangle(cx - lado / 22, cy - r, cx + lado / 22, cy - r, cx, cy - r - lado / 14, TFT_GREEN);

  d.fillCircle(cx, cy, lado / 26, TFT_WHITE);
}

extern const app_t APP_IMU = {
  .nome  = "IMU",
  .cor   = TFT_PURPLE,
  // So a IMU. E FASE 1 (ver o aviso grande no topo do arquivo): uma vez
  // registrado, o BMI270 fica ligado o boot inteiro, nao so enquanto este
  // app esta em primeiro plano.
  .perfil = { .imu = true },
  .usa_sd = false,
  .orientacao = ORI_EM_PE,
  .ao_abrir  = ao_abrir,
  .ao_fechar = ao_fechar,
  .desenhar  = desenhar,
  .ao_tocar  = nullptr,
  .ao_tocar_nivel = nullptr,
  .ao_teclar = nullptr,
  // 50ms = 20Hz. Fluido o bastante para acompanhar um gesto de mao sem gastar
  // mais I2C do que precisa - o mesmo raciocinio do TECLADO_MS em core.cpp.
  .periodo_ms = 50,
  .altura_conteudo = 0,
  .icone = icone_imu,
  .fundo_tema = true,
};
