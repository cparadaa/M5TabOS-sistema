// APP: Camera - preview ao vivo do SC2356 (MIPI-CSI) + captura para o cartao.
//
// PRIMEIRA VERSAO, escopo deliberadamente pequeno - ver o aviso grande em
// hal/tab5_camera.hpp para o que ja foi verificado (sensor, pinos, barramento
// I2C compartilhado com M5.In_I2C, o exemplo oficial que este codigo segue).
//
// A foto sai com o quadro inteiro do sensor (1200x1600); o preview usa a
// mesma imagem em escala uniforme, sem esticar (ver hal/tab5_camera.cpp).
//
// O QUE FICA PARA DEPOIS, de proposito:
//   - Galeria (ver as fotos tiradas sem sair para o app Arquivos).
//   - Zoom/foco/ajustes de exposicao.
// Nada disso e dificil depois - e so mais do que cabia numa unica resposta
// sem virar uma parede de codigo nao testada em hardware nenhum.

#include "app.h"
#include "idioma.h"
#include "tab5_camera.hpp"
#include "tab5_sd.hpp"
#include "tab5_ui.hpp"
#include "tab5_tema.hpp"
#include "tab5_bmp.hpp"
#include "pintura.hpp"
#include "trabalho.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include "tab5_arquivos.hpp"   // listar pasta e do sistema
#include "tab5_camera_web.hpp"

static const char *TAG = "app.camera";

static uint16_t* s_foto   = nullptr;   // a foto sendo capturada (TAB5_CAM_FOTO_L x _A, PSRAM) - do app ate virar trabalho
static char      s_foto_nome[32];
static volatile bool s_gravou_ok = false;   // escrito pelo trabalho (estatica: existe mesmo com o app fechado)
static int       s_largura, s_altura;
static uint32_t  s_fotos = 0;

// CAPTURANDO: a camera esta tirando a foto para s_foto (um frame, ~33 ms).
// GRAVANDO:   um TRABALHO SOLTO grava a foto no cartao (segundos).
// O app NAO trava a navegacao em nenhum dos dois. Na hora de gravar, o buffer
// PASSA A SER DO TRABALHO (sistema_trabalho_solto): ele grava e libera. Por
// isso fechar o app no meio da gravacao e instantaneo - antes, o fechamento
// esperava a gravacao inteira e a interface congelava.
typedef enum { ESTADO_NORMAL, ESTADO_CAPTURANDO, ESTADO_GRAVANDO, ESTADO_FLASH } estado_botao_t;
static estado_botao_t s_estado = ESTADO_NORMAL;
static bool     s_flash_sucesso = false;
static uint32_t s_flash_ate_ms = 0;   // pisca a borda do botao por um instante apos o resultado

// PINTURA POR DIFERENCA: o botao so e repintado quando o texto dele muda (ou
// numa passada completa). Antes ele era desenhado inteiro - texto grande, a
// cada quadro da camera, ~30 vezes por segundo - sem mudar nada.
static pintura_t s_p;
static int       s_botao_pintado = -1;
static bool      s_tem_quadro = false;   // algum quadro ja chegou desde que a camera (re)ligou

// Botao CAPTURAR: faixa inferior fixa, independente do tamanho da area (varia
// pouco entre paisagem/retrato, mas o calculo por fracao evita numero magico).
#define BOTAO_A 96

// MAIOR "foto_NNNN.bmp" JA NO CARTAO - defeito observado: s_fotos
// comecava do zero a cada ABERTURA do app, entao toda sessao nova sobrescrevia
// "foto_0001.bmp" da sessao anterior. A numeracao tem que vir do que ja existe
// no cartao, no MESMO espirito que tab5_mesh_carregar_do_cartao() ja usa para
// reconstruir a lista de conversas a partir dos arquivos - o cartao e a fonte
// da verdade, nao uma variavel na RAM que morre quando o app fecha.
static uint32_t maior_foto_existente(void)
{
  char pasta[128];
  if (!tab5_sd_pasta("Camera", pasta, sizeof(pasta))) { return 0; }
  return tab5_arq_maior_numero(pasta, "foto_%04lu.bmp");   // 0 = primeira foto de todas
}

// A CONTAGEM E DO WORKER: o readdir da pasta Camera rodava
// no ao_abrir, na tarefa de interface - com centenas de fotos, a camera
// demorava a aparecer. O numero so e preciso na hora de CAPTURAR; ate ele
// chegar (AVISO_TRABALHO), o botao espera.
static volatile uint32_t s_fotos_lidas = 0;
static volatile bool     s_fotos_prontas = false;   // o trabalho terminou (resultado em s_fotos_lidas)
static bool              s_fotos_ok = false;        // s_fotos ja vale

static void trabalho_contar(void*)
{
  s_fotos_lidas = maior_foto_existente();
  s_fotos_prontas = true;
}

// O retangulo do preview, em coordenadas de TELA.
static area_t retangulo_preview(void)
{
  const area_t a = sistema_area();
  return area_t{ a.x, a.y, s_largura, s_altura };
}

// Liga o sensor e aponta o preview DIRETO para a tela virtual: o PPA da
// camera escreve o frame no lugar, e cada frame pede um redesenho - que so
// avisa o compositor (sistema_superficie_mudou). Nenhuma copia pela CPU.
static bool ligar_camera(void)
{
  if (!tab5_camera_iniciar("Camera")) { return false; }
  superficie_t sup;
  if (!sistema_superficie(retangulo_preview(), &sup)) {
    ESP_LOGE(TAG, "sem superficie de DMA para o preview");
    tab5_camera_parar("Camera");
    return false;
  }
  const tab5_cam_destino_t d = { sup.buf, sup.bytes, sup.pic_l, sup.pic_a, sup.x, sup.y,
                                 s_largura, s_altura, sistema_pedir_redesenho };
  tab5_camera_definir_destino(&d);
  return true;
}

static bool ao_abrir(void)
{
  const area_t a = sistema_area();
  s_largura = a.largura;
  // Multiplo de 8: exigencia da superficie de DMA (sistema_superficie).
  s_altura  = (a.altura - BOTAO_A - 12) & ~7;
  if (s_largura <= 0 || s_altura <= 0) { return false; }

  if (!ligar_camera()) { return false; }

  s_fotos_ok = false;
  s_fotos_prontas = false;
  if (!sistema_trabalho(trabalho_contar, nullptr)) { s_fotos = 0; s_fotos_ok = true; }
  s_estado = ESTADO_NORMAL;
  s_flash_ate_ms = 0;
  s_botao_pintado = -1;
  s_tem_quadro = false;
  ESP_LOGI(TAG, "aberto (preview %dx%d)", s_largura, s_altura);
  return true;
}

static void ao_fechar(void)
{
  tab5_camera_parar("Camera");   // espera a tarefa da camera: nenhum PPA escrevendo (tela, s_foto) depois daqui
  // So existe se a captura nao chegou a virar trabalho; a que virou e do trabalho.
  if (s_foto) { heap_caps_free(s_foto); s_foto = nullptr; }
  ESP_LOGI(TAG, "fechado");
}

// REPOUSO: sem gancho. Com a tela apagada o SISTEMA pausa a camera deste app
// e a religa ao acordar, com o mesmo destino (kernel/core.cpp,
// tab5_camera_pausar). Uma foto que estava sendo capturada volta como falha
// (quadro_pronto com ok = false) e o botao sai de "CAPTURANDO..." sozinho.

// O PEDIDO DE GRAVACAO leva tudo o que precisa: o trabalho e solto e pode
// rodar com o app ja fechado (ou reaberto, com outro s_foto_nome).
typedef struct {
  uint16_t* px;
  char      nome[32];
} gravacao_t;

// No worker do sistema: grava a foto (quadro inteiro) e libera tudo.
static void trabalho_gravar(void* arg)
{
  gravacao_t* g = (gravacao_t*)arg;
  s_gravou_ok = tab5_bmp_salvar("Camera", g->nome, g->px, TAB5_CAM_FOTO_L, TAB5_CAM_FOTO_A);
  heap_caps_free(g->px);
  heap_caps_free(g);
}

// Entrega s_foto a um trabalho solto. false = nao deu (s_foto continua do app).
static bool gravar_foto(void)
{
  gravacao_t* g = (gravacao_t*)heap_caps_malloc(sizeof(gravacao_t), MALLOC_CAP_DEFAULT);
  if (!g) { return false; }
  g->px = s_foto;
  snprintf(g->nome, sizeof(g->nome), "%s", s_foto_nome);
  if (!sistema_trabalho_solto(trabalho_gravar, g)) { heap_caps_free(g); return false; }
  s_foto = nullptr;
  return true;
}

static void ao_aviso(uint32_t av)
{
  if ((av & AVISO_TRABALHO) && s_fotos_prontas && !s_fotos_ok) {
    s_fotos = s_fotos_lidas;
    s_fotos_ok = true;
    ESP_LOGI(TAG, "%u foto(s) ja no cartao", (unsigned)s_fotos);
  }
  if ((av & AVISO_TRABALHO) && s_estado == ESTADO_GRAVANDO) {
    s_estado = ESTADO_FLASH;
    s_flash_sucesso = s_gravou_ok;
    s_flash_ate_ms = pdTICKS_TO_MS(xTaskGetTickCount()) + 600;
  }
  sistema_pedir_redesenho();
}

static void desenhar(lgfx::LovyanGFX& d, const area_t& a)
{
  pintura_iniciar(&s_p);
  // O frame ja esta na tela virtual (o PPA da camera o escreveu la); so
  // falta o compositor saber que a area mudou.
  if (tab5_camera_quadro_escrito()) {
    s_tem_quadro = true;
    sistema_superficie_mudou(retangulo_preview());
  } else if (sistema_area_foi_limpa()) {
    // Ainda sem nenhum frame (sensor acabou de subir) - avisa em vez de
    // deixar a area preta parecendo travado.
    tab5_texto(d, 2);
    d.setTextDatum(middle_center);
    d.setTextColor(TEMA_TEXTO_2, TEMA_FUNDO);
    d.drawString(tr("iniciando a câmera..."), a.x + s_largura / 2, a.y + s_altura / 2);
    d.setTextDatum(top_left);
  }

  // A FOTO CHEGOU AO BUFFER: a gravacao vai para o worker do sistema.
  if (s_estado == ESTADO_CAPTURANDO) {
    bool ok = false;
    if (tab5_camera_quadro_pronto(s_foto, &ok)) {
      if (ok && gravar_foto()) {
        s_estado = ESTADO_GRAVANDO;
      } else {
        s_estado = ESTADO_FLASH;
        s_flash_sucesso = false;
        s_flash_ate_ms = pdTICKS_TO_MS(xTaskGetTickCount()) + 600;
      }
    }
  } else if (s_estado == ESTADO_FLASH && pdTICKS_TO_MS(xTaskGetTickCount()) >= s_flash_ate_ms) {
    s_estado = ESTADO_NORMAL;
  }

  // ---- barra inferior: botao CAPTURAR + contador -----------------------
  // So quando o que ele diz mudou: o quadro novo da camera nao mexe nele.
  const int visual = (s_estado == ESTADO_FLASH) ? (s_flash_sucesso ? 10 : 11) : (int)s_estado;
  if (!pintura_mudou_i(&s_p, &s_botao_pintado, visual)) { return; }
  const int by = a.y + s_altura + 12;
  const char* texto = tr("CAPTURAR");
  // Botao principal em destaque; ocupado em cinza neutro; resultado em verde/vermelho.
  // A tinta escura no verde, porque o branco nele fica fraco para ler.
  uint16_t    cor    = TEMA_DESTAQUE;
  uint16_t    tinta  = TEMA_SOBRE_DESTAQUE;
  if (s_estado == ESTADO_CAPTURANDO)                    { texto = tr("CAPTURANDO...");      cor = TEMA_SUPERFICIE_ALTA; tinta = TEMA_TEXTO; }
  else if (s_estado == ESTADO_GRAVANDO)                 { texto = tr("SALVANDO...");        cor = TEMA_SUPERFICIE_ALTA; tinta = TEMA_TEXTO; }
  else if (s_estado == ESTADO_FLASH && s_flash_sucesso) { texto = tr("CAPTURADO!");         cor = TEMA_SUCESSO;         tinta = TEMA_FUNDO; }
  else if (s_estado == ESTADO_FLASH)                    { texto = tr("FALHOU");             cor = TEMA_PERIGO;          tinta = TEMA_SOBRE_DESTAQUE; }

  tab5_botao(d, a.x + 16, by, a.largura - 32, BOTAO_A - 12,
             texto, cor, tinta, TAB5_BOTAO_TEXTO_GRANDE);

  // ENQUANTO HOUVER PEDIDO PENDENTE, o app precisa continuar conferindo -
  // periodo() abaixo liga o relogio de 100 ms so nessa hora.
}

// O RITMO E O DA CAMERA. Cada quadro escrito ja pede um redesenho
// (tab5_cam_destino_t.avisar): um relogio de 100 ms POR CIMA disso so metia
// passadas sem quadro novo no meio das que tem - a interface ocupada na hora
// em que o quadro chega, e o preview andando aos solavancos. O app Camera em
// Berry, que so redesenha no quadro, ficava mais liso que este. O relogio
// fica para o que nao vem da camera: a foto em curso, o "CAPTURADO!" que
// some sozinho, e o primeiro quadro que ainda nao chegou.
static uint32_t periodo(void)
{
  return (s_estado != ESTADO_NORMAL || !s_tem_quadro) ? 100 : 0;
}

static void ao_tocar(int x, int y)
{
  const int by = s_altura + 12;
  if (y < by || y >= by + BOTAO_A - 12) { return; }
  // BLOQUEIA TAMBEM DURANTE O FLASH ("CAPTURADO!"/"FALHOU", 600ms) - nao so
  // durante ESTADO_CAPTURANDO. Achado ao revisar "e se apertar
  // repetidamente": um toque durante o flash passava pelo
  // guarda antigo (que so olhava CAPTURANDO), comecava uma SEGUNDA captura
  // e travava o INICIO de novo - visto de fora como "o botao nao volta ao
  // normal", quando na verdade tinha voltado e sido travado outra vez por um
  // toque que ninguem tinha certeza se tinha acontecido.
  if (s_estado != ESTADO_NORMAL) { return; }
  if (!s_fotos_ok) { return; }   // a numeracao ainda esta sendo lida do cartao (instantes)

  // Um buffer por foto: o anterior foi com o trabalho de gravacao.
  if (!s_foto) { s_foto = tab5_camera_quadro_alocar(TAB5_CAM_FOTO_L, TAB5_CAM_FOTO_A); }
  if (!s_foto) { ESP_LOGE(TAG, "sem PSRAM para a foto"); return; }
  if (!tab5_camera_pedir_quadro(s_foto, TAB5_CAM_FOTO_L, TAB5_CAM_FOTO_A)) { return; }
  snprintf(s_foto_nome, sizeof(s_foto_nome), "foto_%04lu", (unsigned long)(s_fotos + 1));
  ++s_fotos;
  s_estado = ESTADO_CAPTURANDO;

  // "CLIQUE" DO OBTURADOR - na hora do toque, como numa camera
  // de verdade: confirma "capturei ESTE instante". 600 Hz ("achei agudo").
  sistema_beep(600.0f, 40);
  sistema_pedir_redesenho();
}

// ---------------------------------------------------------------------------
// ICONE DA TELA INICIAL - corpo de maquina fotografica + lente, primitivas.
// ---------------------------------------------------------------------------
static void icone_camera(lgfx::LovyanGFX& d, int x, int y, int lado)
{
  const uint16_t fundo = d.color565(45, 40, 20);
  const uint16_t corpo = TFT_ORANGE;

  d.fillRoundRect(x, y, lado, lado, lado / 6, fundo);

  const int cx = x + lado / 2, cy = y + lado / 2 + (int)(0.06f * lado);
  const int corpo_l = (int)(0.62f * lado), corpo_a = (int)(0.40f * lado);
  d.fillRoundRect(cx - corpo_l / 2, cy - corpo_a / 2, corpo_l, corpo_a, lado / 14, corpo);

  // "Visor" saliente em cima, do lado esquerdo do corpo.
  const int vis_l = (int)(0.18f * lado), vis_a = (int)(0.10f * lado);
  d.fillRoundRect(cx - corpo_l / 2 + (int)(0.06f * lado), cy - corpo_a / 2 - vis_a + 2,
                   vis_l, vis_a, lado / 20, corpo);

  // Lente: dois circulos concentricos.
  const int r_ext = (int)(0.16f * lado), r_int = (int)(0.10f * lado);
  d.fillCircle(cx, cy, r_ext, fundo);
  d.drawCircle(cx, cy, r_ext, corpo);
  d.fillCircle(cx, cy, r_int, corpo);
}

extern const app_t APP_CAMERA = {
  .nome  = "Camera",
  .cor   = TFT_ORANGE,
  // SEM .camera=true AQUI - a camera virou servico (contagem
  // de referencia, ver o aviso grande em hal/tab5_camera.hpp) e liga o
  // proprio CAM_RST sozinha dentro de tab5_camera_iniciar(), no mesmo
  // espirito que o Grove ja faz para GPS/Mesh. Declarar aqui tambem seria
  // so redundante (o kernel ainda aplicaria antes de ao_abrir(), mas quem
  // garante a energia de verdade agora e o proprio servico).
  //
  // .alto_falante=true (junto do clique do obturador em
  // ao_tocar()): ao contrario da camera, o alto-falante NAO tem como se
  // auto-declarar depois do boot (a reserva dos pinos I2S do ES8388 e
  // decisao de FASE 1, antes do M5.begin() - ver o aviso grande em
  // ajustes.h). Este app usa som de verdade agora, entao declara - mesmo
  // que o app Ajustes ja declare o mesmo hoje, e uma uniao (OR), nao concorrencia.
  .perfil = { .alto_falante = true },
  .usa_sd = true,
  .exige_sd = true,         // sem cartao nao ha o que fazer (ver app.h)
  .orientacao = ORI_FIXO_EM_PE,   // o preview e DMA na tela em rotacao 0: nem o teclado deita
  .ao_abrir   = ao_abrir,
  .ao_fechar  = ao_fechar,
  .desenhar  = desenhar,
  .ao_tocar  = ao_tocar,
  // O PREVIEW NAO DEPENDE DO RELOGIO: cada frame escrito pela
  // camera pede um redesenho na hora (tab5_cam_destino_t.avisar). O periodo
  // so conduz o botao (CAPTURANDO/SALVANDO/flash) quando nao ha frame.
  .periodo_ms = 100,
  .periodo_agora = periodo,           // 0 com o preview andando (o quadro e o relogio)
  .avisos    = AVISO_TRABALHO,   // a gravacao da foto terminou
  .ao_aviso  = ao_aviso,
  .icone = icone_camera,
  .ao_girar = nullptr,
  .comandos = nullptr,
  .fora_da_home = false,
  .no_pc = tab5_camera_web_ligar,   // a camera ao vivo no navegador (/camera/)
  .fundo_tema = true,               // o sistema limpa a area com o fundo do tema
};
