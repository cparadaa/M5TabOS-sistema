// Comandos do console - BANCADA: grove, i2c, camera, foto.
// (Partido de console.cpp; a tabela COMANDOS e a ajuda continuam la.)

#include "tab5_bmp.hpp"
#include "tab5_camera.hpp"
#include "tab5_camera_ipa.h"
#include "hal/efuse_hal.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "tab5_grove.hpp"
#include "tab5_teclado.hpp"
#include "ajustes.h"
#include "internal.hpp"   // sistema_trocar_relogio_painel
#include "console/console_interno.hpp"

// ---------------------------------------------------------------------------
// i2c <sda> <scl>  - descobre quem responde num par de pinos
// ---------------------------------------------------------------------------
//
// Existe para NAO adivinhar protocolo de hardware novo. O conector lateral do
// Tab5 expoe G49, G50, G0, G1 e EXT 5V; se o teclado falar I2C, o endereco dele
// aparece aqui e a integracao vira trivial. Se nada responder em nenhuma
// combinacao, e sinal de que nao e I2C - e ai a hipotese seguinte e UART.
//
// Use o par nos DOIS sentidos: nao ha como saber de fora quem e SDA e quem e
// SCL, e trocado nao responde nada.
//
//   i2c 49 50        i2c 50 49        i2c 0 1        i2c 1 0
//
// Rode a partir da tela inicial: o comando mexe no barramento externo, que e o
// mesmo que o Azimutal toma emprestado como UART do GPS.
int cmd_i2c(int argc, char** argv)
{
  if (argc < 3) { printf("%s", tr("uso: i2c <sda> <scl>   (ex: i2c 49 50)\n")); return 1; }

  // O AVISO VIROU TRAVA. Ate aqui, "rode a partir da tela inicial" era so um
  // comentario no fonte - o comando reconfigurava o barramento externo por
  // baixo de quem quer que o estivesse usando, sem perguntar. O Azimutal usa
  // esse mesmo barramento como UART do GPS, e o teclado do ExtPort1 tambem
  // esta nele: rodar isto com qualquer um dos dois aberto arranca o
  // barramento da tarefa dona no meio de uma transacao.
  //
  // A CONSISTENCIA que valia a pena: uma regra de bancada documentada em
  // comentario e uma regra que so vale enquanto alguem lembra de ler o
  // comentario. Fazendo o proprio comando checar, ela vale sempre.
  const char* app = sistema_app_atual();
  if (app) {
    printf(tr("nao da para varrer o I2C com '%s' aberto - ele pode estar usando\n"
           "o mesmo barramento externo. Volte para a tela inicial e tente de novo.\n"), app);
    return 1;
  }

  const int sda = atoi(argv[1]);
  const int scl = atoi(argv[2]);

  // O teclado fisico e lido pela tarefa de interface no Ex_I2C: reabrir o
  // barramento por baixo dela arrancaria a leitura no meio de uma transacao.
  if (tab5_teclado_presente()) {
    printf("%s", tr("nao da para varrer o I2C com o teclado conectado - ele usa o\n"
                    "barramento externo. Desconecte o teclado e tente de novo.\n"));
    return 1;
  }

  // O barramento EXTERNO (Ex_I2C, o do Grove) e o unico que podemos reaproveitar.
  // No Tab5 o INTERNO (toque, IMU, INA226, expansores, RTC) e o In_I2C
  // (I2C_NUM_1) e e propriedade da tarefa de interface - encostar nele daqui
  // seria a corrida que o sistema inteiro evita. Por isso a porta usada e
  // sempre a do proprio Ex_I2C, nunca uma fixa.
  const i2c_port_t porta = M5.Ex_I2C.getPort();
  if (porta == M5.In_I2C.getPort()) {
    printf("%s", tr("o barramento externo usa a mesma porta do interno - recusado\n"));
    return 1;
  }
  // Porta e pinos originais: e exatamente isto que volta no fim.
  const int sda_orig = M5.Ex_I2C.getSDA();
  const int scl_orig = M5.Ex_I2C.getSCL();

  M5.Ex_I2C.release();

  if (!M5.Ex_I2C.begin(porta, sda, scl))
  {
    printf(tr("nao consegui abrir I2C com SDA=%d SCL=%d\n"), sda, scl);
    M5.Ex_I2C.release();
    M5.Ex_I2C.begin(porta, sda_orig, scl_orig);
    return 1;
  }

  printf(tr("varrendo I2C em SDA=%d SCL=%d ...\n"), sda, scl);
  bool achou[128] = { };
  M5.Ex_I2C.scanID(achou);

  int n = 0;
  for (int a = 0; a < 128; ++a)
  {
    if (achou[a]) { printf(tr("  0x%02X responde\n"), (unsigned)a); ++n; }
  }
  if (n == 0) { printf("%s", tr("  ninguem respondeu\n")); }
  else        { printf(tr("  %d dispositivo(s)\n"), n); }

  // Devolve o barramento externo a porta e aos pinos de antes.
  M5.Ex_I2C.release();
  M5.Ex_I2C.begin(porta, sda_orig, scl_orig);
  return 0;
}

// ---------------------------------------------------------------------------
// 'foto' - PROVA DE QUE A CAMERA VIROU SERVICO, do MESMO jeito
// que 'concorrencia'/'simulacao' acima provam GPS/IMU: liga a camera, tira
// UMA foto e desliga de novo, inteiramente de FORA do app Camera - se isto
// funcionar com o app Camera FECHADO, e funcionar de novo com ele ABERTO ao
// mesmo tempo (ver a contagem de referencia em tab5_camera.hpp), a
// independencia entre servico e app esta provada em hardware, nao so no
// papel.
//
// A FOTO sai com o quadro inteiro do sensor (TAB5_CAM_FOTO_L x _A); este
// comando nao tem preview.
void cmd_foto(void)
{

  // Da tarefa do console, direto: o SCCB do sensor passa pela trava do I2C
  // interno (hal/tab5_sccb.cpp). O console aparece usando a camera nos
  // recursos (energia quem) enquanto ela estiver ligada por ele.
  printf("%s", tr("ligando a camera...\n"));
  if (!tab5_camera_iniciar("console")) {
    printf("%s", tr("falhou - confira o log (sensor ausente?)\n"));
    return;
  }

  // A camera so CAPTURA (um frame, por PPA, na tarefa dela); gravar e daqui,
  // da tarefa do console - ver services/tab5_bmp e tab5_camera_pedir_quadro.
  uint16_t* foto = tab5_camera_quadro_alocar(TAB5_CAM_FOTO_L, TAB5_CAM_FOTO_A);
  bool ok = false;
  if (!foto) {
    printf("%s", tr("sem PSRAM para a foto\n"));
  } else if (!tab5_camera_pedir_quadro(foto, TAB5_CAM_FOTO_L, TAB5_CAM_FOTO_A)) {
    printf("%s", tr("nao consegui pedir a foto (outra ainda pendente?)\n"));
  } else {
    bool pronta = false;
    for (int i = 0; i < 50 && !pronta; ++i) {    // 5 s: o primeiro frame pode demorar
      vTaskDelay(pdMS_TO_TICKS(100));
      pronta = tab5_camera_quadro_pronto(foto, &ok);
    }
    if (!pronta)  { printf("%s", tr("a camera nao entregou quadro em 5 s (confira o log)\n")); }
    else if (ok)  { ok = tab5_bmp_salvar("Console", "console", foto, TAB5_CAM_FOTO_L, TAB5_CAM_FOTO_A);
                    printf("%s", ok ? tr("foto salva: /sdcard/apps/Console/console.bmp\n") : tr("a gravacao falhou (confira o log)\n")); }
    else          { printf("%s", tr("a captura falhou (confira o log)\n")); }
  }

  // SEMPRE solta o interesse que este comando pediu - mesmo em erro. Se mais
  // alguem (o app Camera, por exemplo) ainda estiver usando, a contagem de
  // referencia mantem o sensor de pe; se este comando era o unico
  // interessado, ele desliga de verdade agora.
  tab5_camera_parar("console");   // espera a tarefa da camera - so depois o buffer pode sair
  if (foto) { heap_caps_free(foto); }
}

// 'camera' - O QUE O ISP ESTA FAZENDO COM A COR. Imprime o estado do IPA
// proprio (hal/tab5_camera_ipa.c: ganhos do AWB, a matriz da calibracao e a
// que foi ao ISP, o verde que um branco saturado teria com cada uma) e mede um
// quadro inteiro: media do centro, media das sombras, fracao de pixel
// estourado e de pixel magenta. Com a lente tampada, a media do centro e o
// preto que o sensor entrega - o teste direto do pedestal (tab5_camera.cpp,
// zerar_pedestal). 'camera reg' fala com o sensor enquanto o app Camera esta
// aberto, para experimentar um registrador ao vivo.
void cmd_camera(int argc, char** argv)
{
  if (argc >= 3 && strcmp(argv[1], "reg") == 0) {
    const long reg   = strtol(argv[2], nullptr, 16);
    const int  valor = (argc >= 4) ? (int)strtol(argv[3], nullptr, 16) : -1;
    uint8_t lido = 0;
    if (!tab5_camera_reg((uint16_t)reg, valor, &lido)) {
      printf("%s", tr("camera desligada (abra o app Camera) ou o SCCB falhou\n"));
      return;
    }
    printf(tr("sensor 0x%04lx = 0x%02x\n"), reg, lido);
    return;
  }

  const unsigned rev = efuse_hal_chip_revision();
  printf(tr("P4 revisao v%u.%u - %s\n"), rev / 100, rev % 100,
         rev >= 300 ? tr("ISP com WBG e BLC (balanco e preto no hardware)")
                    : tr("ISP sem WBG nem BLC (balanco na CCM, preto pelo sensor)"));

  // Ligada por outro (o app Camera, o navegador): so observa - o AE/AWB ja
  // estao assentados.
  const bool ja_ligada = tab5_camera_ativa();
  if (!ja_ligada) {
    printf("%s", tr("ligando a camera e esperando o AE/AWB assentarem...\n"));
    if (!tab5_camera_iniciar("console")) { printf("%s", tr("falhou - confira o log\n")); return; }
    vTaskDelay(pdMS_TO_TICKS(2500));
  }

  tab5_cam_ipa_estado_t e;
  if (!tab5_camera_ipa_estado(&e)) {
    printf("%s", tr("o IPA proprio nao rodou nenhum quadro (fora do pipeline?)\n"));
  } else {
    printf(tr("quadros do ISP   : %lu\n"), (unsigned long)e.quadros);
    printf(tr("exposicao, ganho : %lu us, %.2fx\n"), (unsigned long)e.exposicao_us, (double)e.ganho_sensor);
    printf(tr("ganhos do AWB    : R %.2f  B %.2f\n"), (double)e.ganho_r, (double)e.ganho_b);
    printf("%s", tr("CCM calibracao   |  CCM no ISP\n"));
    for (int i = 0; i < 3; ++i) {
      printf("  %6.2f %6.2f %6.2f  |  %6.2f %6.2f %6.2f\n",
             (double)e.ccm_awb[i][0], (double)e.ccm_awb[i][1], (double)e.ccm_awb[i][2],
             (double)e.ccm_isp[i][0], (double)e.ccm_isp[i][1], (double)e.ccm_isp[i][2]);
    }
    printf(tr("verde do branco saturado: %.2f -> %.2f (1 = branco; abaixo de ~0,9 sai rosa)\n"),
           (double)e.g_branco_antes, (double)e.g_branco_depois);
    printf(tr("saturacao        : %lu (128 = 1,0)\n"), (unsigned long)e.saturacao);
    uint32_t total = 0;
    for (int i = 0; i < 16; ++i) { total += e.hist[i]; }
    if (total) {
      printf("%s", tr("histograma do Y  :"));
      for (int i = 0; i < 16; ++i) { printf(" %lu", (unsigned long)(e.hist[i] * 100 / total)); }
      printf("%s", tr(" (%% por faixa de 16)\n"));
    }
  }
  uint8_t preto = 0;
  if (tab5_camera_reg(0x3908, -1, &preto)) { printf(tr("alvo do preto    : 0x%02x (fabrica 0x41)\n"), preto); }

  uint16_t* foto = tab5_camera_quadro_alocar(TAB5_CAM_FOTO_L, TAB5_CAM_FOTO_A);
  bool ok = false, pronta = false;
  if (foto && tab5_camera_pedir_quadro(foto, TAB5_CAM_FOTO_L, TAB5_CAM_FOTO_A)) {
    for (int i = 0; i < 50 && !pronta; ++i) { vTaskDelay(pdMS_TO_TICKS(100)); pronta = tab5_camera_quadro_pronto(foto, &ok); }
  }
  if (pronta && ok) {
    const int L = TAB5_CAM_FOTO_L, A = TAB5_CAM_FOTO_A;
    uint64_t c[3] = { }, sombra[3] = { };
    uint32_t n_c = 0, n_sombra = 0, n = 0, estouro = 0, magenta = 0;
    for (int y = 0; y < A; y += 2) {
      for (int x = 0; x < L; x += 2) {
        const uint16_t p = foto[y * L + x];
        const int r = (p >> 11) << 3, g = ((p >> 5) & 0x3F) << 2, b = (p & 0x1F) << 3;
        ++n;
        if (r >= 248 && g >= 248 && b >= 248) { ++estouro; }
        const int menor_rb = r < b ? r : b;
        if (menor_rb >= 200 && g + 40 < menor_rb) { ++magenta; }
        if (r + g + b < 96) { sombra[0] += r; sombra[1] += g; sombra[2] += b; ++n_sombra; }
        if (abs(x - L / 2) < 100 && abs(y - A / 2) < 100) { c[0] += r; c[1] += g; c[2] += b; ++n_c; }
      }
    }
    printf(tr("centro (200x200) : R %llu G %llu B %llu\n"), c[0] / n_c, c[1] / n_c, c[2] / n_c);
    if (n_sombra) {
      printf(tr("sombras (%lu%%)    : R %llu G %llu B %llu (neutro = tres iguais)\n"),
             (unsigned long)(n_sombra * 100 / n), sombra[0] / n_sombra, sombra[1] / n_sombra, sombra[2] / n_sombra);
    }
    printf(tr("estourado        : %.2f%% do quadro\n"), estouro * 100.0 / n);
    printf(tr("magenta no branco: %.2f%% do quadro\n"), magenta * 100.0 / n);
  } else {
    printf("%s", tr("nao veio quadro para medir\n"));
  }

  if (!ja_ligada) { tab5_camera_parar("console"); }   // espera a tarefa: so depois o buffer sai
  if (foto) { heap_caps_free(foto); }
}

// ---------------------------------------------------------------------------
// BANCADA
// ---------------------------------------------------------------------------

void c_grove(int argc, char** argv)
{
  if (argc >= 2 && eh(argv[1], "trocar")) {
    // TROQUEI O MODULO NO CABO. Para o servico, devolve a porta, sonda de novo
    // e sobe o que couber. Feito pelo sistema, na tarefa de interface - daqui
    // nao da para mexer na UART de outra tarefa com seguranca.
    if (sistema_grove_redetectar()) {
      printf("%s", tr("pedido feito - a interface sonda no proximo ciclo (uns 4s).\n"
             "rode 'grove' depois para ver o que achou.\n"));
    } else {
      printf("%s", tr("um app esta usando o Grove agora - feche-o e tente de novo\n"));
    }
    return;
  }
  // 'grove cru <baud>', ou a forma antiga 'grove <baud>': despeja os bytes
  // crus, para OLHAR em vez de deduzir.
  const char* baud = nullptr;
  if (argc >= 3 && eh(argv[1], "cru"))          { baud = argv[2]; }
  else if (argc >= 2 && atoi(argv[1]) > 0)      { baud = argv[1]; }
  if (baud) { tab5_grove_despejar(atoi(baud), 2500); return; }
  if (argc >= 2) { mostrar_uso("grove"); return; }

  // SO RELATA - nao sonda. Sondar daqui era um bug silencioso: a sonda escreve
  // e le na UART, e com o servico Meshtastic de pe a porta e DELE. Duas
  // tarefas na mesma UART embaralham o fluxo e o enlace caia por causa do
  // comando que deveria so diagnosticar.
  const grove_dono_t dono = tab5_grove_dono();
  printf(tr("Grove: %s\n"), grove_nome(tab5_grove_tipo()));
  printf(tr("  energia : %s\n"), ajustes_grove() ? tr("ligada") : tr("desligada"));
  printf(tr("  porta   : %s\n"), (dono == GROVE_DONO_NINGUEM) ? tr("livre") : grove_dono_nome(dono));
  if (sistema_grove_sondando()) { printf("%s", tr("  sondagem em andamento...\n")); }
  printf("%s", tr("\ntrocou o modulo do cabo? use 'grove trocar'\n"));
}
