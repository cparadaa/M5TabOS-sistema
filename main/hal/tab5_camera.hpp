// Servico de CAMERA - sensor SC2356 (SC202CS) do Tab5, MIPI-CSI, 1600x1200.
//
// PESQUISADO, nao inventado: nao existia NENHUM driver de
// camera neste projeto ate aqui - so o CAM_RST (tab5_power_camera). Os fatos
// abaixo vieram do BSP OFICIAL da Espressif para esta placa exata
// (github.com/espressif/esp-bsp, bsp/m5stack_tab5), nao de suposicao:
//
//   - Sensor: SC2356 / SC202CS, MIPI-CSI. Kconfig: CONFIG_CAMERA_SC202CS.
//   - Pino de reset: NENHUM GPIO direto - e o MESMO CAM_RST que
//     tab5_power_camera() ja controla (expansor 0, bit 6). o
//     sistema de apps ligava isso sozinho via perfil.camera=true; desde que
//     a camera virou servico (ver o aviso grande logo abaixo), quem liga e
//     DESLIGA o pino e o proprio tab5_camera_iniciar()/parar().
//   - SCCB (o I2C do sensor, para configura-lo): G32/G31 - EXATAMENTE os
//     mesmos pinos que M5Unified ja usa para o barramento INTERNO
//     (M5.In_I2C: touch, IMU, PMIC, expansores - ver M5Unified.cpp,
//     board_M5Tab5). Ou seja, a camera e so mais um endereco no MESMO
//     barramento que o resto do sistema ja possui e protege com a regra "so a
//     tarefa de interface fala I2C".
//
// COMO ISTO NAO COLIDE COM O QUE JA EXISTE: em vez de abrir um segundo
// barramento nos mesmos pinos (proibido - o ESP-IDF nao deixa dois donos na
// mesma GPIO), tab5_camera_iniciar() pega o handle que o M5Unified ja
// instalou com i2c_master_get_bus_handle() - uma funcao do proprio IDF feita
// exatamente para "quero o barramento que outro componente ja abriu", sem
// inicializar nada de novo. Confirmado: M5GFX usa i2c_new_master_bus() (o
// driver NOVO) por baixo do M5.In_I2C - por isso o handle e do tipo certo.
//
// O RESTO (V4L2: open/ioctl/mmap em /dev/videoN) e o mesmo padrao que o
// exemplo oficial da Espressif usa para ESTA placa
// (esp-bsp/examples/display_camera_video) - so trocamos LVGL por M5GFX e
// callback assincrono por tarefa_app_t (o molde que este projeto ja usa para
// todo servico de fundo, ver kernel/tarefa_app.h).
#pragma once

#include <stddef.h>

#include <stdbool.h>
#include <stdint.h>

// -------------------------------------------------------------------------
// VIROU SERVICO, NAO MAIS "RECURSO DO APP CAMERA"
// ("quais sensores internos podem virar servico?" -> a camera era a unica
// peca de hardware caro que ainda so existia enquanto UM app especifico
// estava na tela).
// -------------------------------------------------------------------------
// CONTAGEM DE REFERENCIA, mesmo molde de tab5_sd.hpp: tab5_camera_iniciar()
// pode ser chamada por MAIS DE UM consumidor ao mesmo tempo (o app Camera E,
// por exemplo, o comando de console 'foto', ou um futuro segundo app) - a
// primeira chamada liga o sensor de verdade, as seguintes so contam mais um
// interessado, e o sensor so desliga quando o ULTIMO chamar
// tab5_camera_parar(). Abrir o app Camera enquanto o comando 'foto' ainda
// segura a camera (ou vice-versa) e seguro: nenhum dos dois derruba o outro.
//
// O SENSOR TEM UM MODO SO (1600x1200, ver a foto abaixo); O TAMANHO E DE
// QUEM USA. Ligar nao escolhe resolucao: o preview tem o tamanho do destino
// (tab5_camera_definir_destino) e cada quadro pedido, o seu
// (tab5_camera_pedir_quadro). Assim o app Camera, um app Berry, o console e
// o navegador do PC (services/tab5_camera_web) usam a camera ao mesmo tempo
// sem um recusar o outro. (Antes iniciar() levava o tamanho do preview e
// recusava um segundo consumidor com outro tamanho.)
//
// A CAMERA SE ALIMENTA SOZINHA AGORA - antes, CAM_RST (o pino de reset do
// sensor) so era ligado porque o app Camera declarava perfil.camera=true, e
// o KERNEL aplicava isso antes de ao_abrir() rodar (ver tab5_power.hpp). Um
// servico de verdade nao pode depender de UM app especifico estar aberto
// para ter energia - por isso tab5_camera_iniciar() agora chama
// tab5_power_camera(true) ELA MESMA, no mesmo espirito que tab5_grove.cpp ja
// liga o trilho de 5V sozinho para GPS/Mesh (ver o aviso la). O app Camera
// NAO declara mais perfil.camera - ver o aviso em app_camera.cpp.
//
// O UNICO PONTO DELICADO: core.cpp ainda reaplica o perfil de energia
// (imu/mic/camera/etc) toda vez que um app abre ou fecha, como uniao do que
// TODOS os apps abertos hoje declaram - e SEM SABER que este servico existe,
// aquela reaplicacao desligaria CAM_RST no meio de uma sessao pedida por
// fora de um app (o comando 'foto', por exemplo) so porque o app QUE ABRIU
// NAQUELE INSTANTE nao pede camera. tab5_camera_ativa() (abaixo) existe
// exatamente para tapar esse buraco: sistema_impor_preferencias() faz OR com
// ela, do mesmo jeito que ja faz com ajustes_grove() para o Grove.
//
// SO DA TAREFA DE INTERFACE, DIRETO - achado ao vivo com o
// comando 'foto': estas duas funcoes tocam SCCB (o I2C interno, o MESMO
// barramento de touch/IMU/energia/RTC), e a regra deste projeto e "so a
// tarefa de interface fala I2C" (sem trava nenhuma no driver). app_camera.cpp
// pode chamar direto porque ao_abrir()/ao_fechar() JA rodam nessa tarefa.
// QUALQUER outro chamador (um comando de console, uma futura tarefa de
// fundo) DEVE passar por sistema_camera_iniciar()/sistema_camera_parar()
// (kernel/app.h) em vez de chamar estas aqui - a ponte manda o pedido para a
// tarefa certa. Chamar estas direto de outra tarefa e o que produziu "Set
// format regs fail" na primeira tentativa do 'foto'.

// Liga o sensor (ou so soma mais um interessado, se ja estiver ligado - ver
// o aviso grande acima sobre contagem de referencia): barramento I2C
// compartilhado, esp_video_init, abre o dispositivo, aloca os buffers de
// captura, registra o PPA (escala + rotaciona 270 graus - o sensor esta
// montado de lado na placa) e sobe a tarefa que produz frames.
//
// De qualquer tarefa: o SCCB passa pela trava do I2C interno
// (hal/tab5_sccb.cpp). Nao depende de perfil.camera, ela mesma liga CAM_RST.
// Devolve false se qualquer passo falhar (o log explica qual) - nesse caso
// nao ha nada para desligar.
// 'quem' (app, "navegador", "console") e quem aparece usando o recurso
// CAMERA (kernel/recursos.h) - o mesmo nome no parar.
bool tab5_camera_iniciar(const char* quem);

// Descarta UM interesse (ver a contagem de referencia acima). So quando o
// ULTIMO cai e que a tarefa desce de verdade (tarefa_app_encerrar, com
// timeout - ver o aviso la sobre tarefa abandonada de proposito em vez de
// morta a força), o stream para, os buffers somem, o cliente do PPA sai e
// CAM_RST volta a ficar em reset. CHAMAR SO DA TAREFA DE INTERFACE (ver o
// aviso grande acima) - de onde tab5_camera_iniciar() foi chamada
// (ao_fechar()). Seguro chamar mesmo se tab5_camera_iniciar() nunca rodou,
// falhou, ou ja foi parada por outro caminho.
void tab5_camera_parar(const char* quem);

// PAUSA COM A TELA APAGADA - uma regra do SISTEMA, nao do app (06/11/2026,
// docs/SUSPENSAO_2026-11-05.md). Quem chama e o kernel (kernel/core.cpp), no
// repouso, pelo app em primeiro plano: com a tela apagada ninguem ve o
// preview, e a camera ligada a toa segura 180 MHz e o sono leve (o recurso
// CAMERA). Antes cada app fazia isto no proprio ao_repouso (app Camera,
// apps Berry) - e o consumidor que esquecia ficava filmando no escuro.
//
// tab5_camera_pausar(quem): se 'quem' usa a camera, solta o uso dele (como
// tab5_camera_parar) e guarda o destino do preview. O preview para de
// escrever na superficie mesmo se outro consumidor (o navegador) mantiver a
// camera ligada. Devolve true se pausou. Um quadro pedido que morrer com a
// camera volta como pronto com ok = false (quem pediu ja trata a falha).
// tab5_camera_despausar(religar): religar = true liga de novo pelo mesmo
// 'quem' e devolve o destino; false so esquece a pausa (o app fechou).
// Se o dono pausado chamar tab5_camera_parar/iniciar no meio (fechou ou o
// script religou), a pausa e esquecida e a contagem segue certa.
// So da tarefa de interface, como iniciar/parar.
bool tab5_camera_pausar(const char* quem);
void tab5_camera_despausar(bool religar);

// Ha algum interessado vivo agora (contagem de referencia > 0)? Existe para
// sistema_impor_preferencias() (kernel/core.cpp) nao cortar CAM_RST por
// baixo de um consumidor que nao e o app em foco - ver o aviso grande no
// topo deste arquivo. Apps normais nao precisam chamar isto.
bool tab5_camera_ativa(void);

// ONDE O PREVIEW E ESCRITO. A tarefa da camera escreve cada
// frame, ja girado e escalado para largura x altura (os de
// tab5_camera_iniciar), DIRETO num retangulo de uma imagem RGB565 - na pratica
// a tela virtual do sistema (sistema_superficie, kernel/app.h). Sem copia
// intermediaria. 'buf' precisa de alinhamento de cache (a tela virtual tem).
// 'avisar' (opcional) e chamado da tarefa da camera a cada frame escrito -
// sistema_acordar_interface, para a interface levar o frame na hora.
typedef struct {
  void*  buf;          // nulo = nenhum destino (so quadros pedidos)
  size_t bytes;        // tamanho total de buf
  int    pic_l, pic_a; // dimensoes da imagem inteira
  int    x, y;         // canto do retangulo dentro dela
  int    l, a;         // tamanho do retangulo: o preview (escala uniforme, preenchendo)
  void (*avisar)(void);
} tab5_cam_destino_t;

void tab5_camera_definir_destino(const tab5_cam_destino_t* d);   // nullptr = nenhum

// true UMA vez por frame escrito no destino desde a ultima chamada.
bool tab5_camera_quadro_escrito(void);

// QUADRO SOB PEDIDO: o proximo frame inteiro, no tamanho l x a de quem pediu
// (RGB565, linha a linha de cima para baixo), com a mesma escala uniforme do
// preview - so o que nao cabe na proporcao sai das bordas. E a FOTO
// (TAB5_CAM_FOTO_L x _A em pe: o quadro inteiro do sensor, sem escala) e o
// VIDEO do navegador (menor, comprimido em JPEG por quem pediu).
//
// EM PE OU COMO O SENSOR VE. O sensor esta de lado na placa: em pe, o PPA
// gira 270 graus e espelha (como a tela e a foto) - e GIRAR CUSTA: ~117 ms
// por quadro de 480x640 (medido 05/11/2026; o PPA le a PSRAM coluna a
// coluna), contra os ~32 ms entre dois frames. 'em_pe' = false entrega o
// quadro deitado (l x a na proporcao 4:3 do sensor), so reduzido, e quem
// mostra gira - o navegador faz isso de graca. Para mostrar em pe: girar 90
// graus no sentido horario e espelhar na horizontal (= transpor).
//
// O SENSOR RODA NO MODO 1600x1200 (sdkconfig.defaults). O modo 1280x720 do
// driver nao reduz a imagem: ele liga uma JANELA no meio do sensor (colunas
// 160-1447, linhas 240-967), 80% da largura e 60% da altura - a foto saia
// como se tivesse zoom.
//
// A CAMERA SO CAPTURA; QUEM GRAVA OU COMPRIME E QUEM PEDIU. O pedido e
// atendido no proximo frame com UMA operacao de PPA (poucos ms): o preview
// nao para. Ate TAB5_CAM_PEDIDOS (2) pedidos ao mesmo tempo - a foto de um
// app e o video do PC nao disputam a vez.
#define TAB5_CAM_FOTO_L 1200
#define TAB5_CAM_FOTO_A 1600

// Buffer certo para um quadro l x a (PSRAM, alinhado a linha de cache, o
// tamanho que o PPA exige). Liberar com heap_caps_free.
uint16_t* tab5_camera_quadro_alocar(int l, int a);
size_t    tab5_camera_quadro_bytes(int l, int a);

// Pede o proximo frame em 'destino' (de tab5_camera_quadro_alocar, no mesmo
// l x a). false = camera desligada, sem vaga, ou este destino ja tem pedido.
// 'destino' e escrito pela tarefa da camera ate tab5_camera_quadro_pronto()
// devolver true - nao libere antes disso (tab5_camera_parar() tambem espera
// a tarefa terminar).
bool tab5_camera_pedir_quadro(uint16_t* destino, int l, int a, bool em_pe = true);

// true UMA vez quando o quadro pedido para 'destino' chegou (ok = PPA deu
// certo); a vaga fica livre para o proximo pedido.
bool tab5_camera_quadro_pronto(const uint16_t* destino, bool* ok);

// REGISTRADOR DO SENSOR, para diagnostico (comando 'camera' do console).
// valor >= 0 escreve antes de ler; -1 so le. false = camera desligada ou o
// SCCB falhou. O que for escrito vale ate a camera desligar: ao ligar de novo
// o driver reaplica a tabela de inicializacao.
bool tab5_camera_reg(uint16_t reg, int valor, uint8_t* lido);
