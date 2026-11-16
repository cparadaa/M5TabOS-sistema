// O "core" do M5tabOS: agenda de desenho, ciclo de vida dos apps, politica
// de energia.
//
// -------------------------------------------------------------------------
// ETAPA 1 do docs/ARQUITETURA.md - o MOTOR
// -------------------------------------------------------------------------
// Ate aqui isto vivia dentro de kernel/sistema.cpp, junto da APARENCIA
// (home, barra de status, roteamento de toque, calibracao) - 2457 linhas
// fazendo oito coisas diferentes, compartilhando estaticas sem fronteira.
// Partido em dois, sem mudar uma linha de logica (a unica excecao esta
// documentada em kernel/internal.hpp): este arquivo decide O QUE roda e
// QUANDO; kernel/shell.cpp decide como a tela PARECE. Os dois se falam pelo
// pouco que kernel/internal.hpp expoe - nunca por variavel global crua fora
// dali.
//
// UMA tarefa so manda no display e no I2C, de proposito. O LovyanGFX nao e
// seguro para acesso concorrente e o I2C_Class do M5Unified nao tem mutex
// nenhum - duas tarefas no mesmo barramento leem registrador trocado sem dar
// erro. Por isso os apps NAO desenham das tarefas deles: eles produzem dados
// em queues e a funcao desenhar() e chamada daqui.
//
// -------------------------------------------------------------------------
// PARTIDO POR ASSUNTO (so mudou de arquivo, nada de logica)
// -------------------------------------------------------------------------
// Este arquivo ficou com o estado do motor, as interrupcoes do toque e do
// teclado e o despertar do sono leve, a tarefa de interface (vTaskInterface) e
// sistema_iniciar(). O resto foi para kernel/core_*.cpp; o que as partes
// compartilham esta em kernel/core_interno.hpp:
//   core_toque.cpp    vigia e lupa do GT911, leitura continua, dedos, bancada (simular toque/gesto/abrir)
//   core_radios.cpp   Bluetooth, Mesh pela internet, liga/desliga de Wi-Fi, Grove e via do Mesh
//   core_apps.cpp     abrir/fechar app, montagem do SD em tarefa propria, app recusado
//   core_energia.cpp  tela acesa por pedido, contrato de economia, relogio do painel, perfil
//   core_som.cpp      ociosidade do alto-falante, pinos do USB, som em fluxo, bip
//   core_desenho.cpp  area util, passadas medidas, passada de desenho do conteudo

#include "app.h"
#include "esp_attr.h"
#include "tab5_sd.hpp"
#include <atomic>
#include "core_interno.hpp"
#include "bloqueio_anim.hpp"
#include "ritmo.hpp"
#include "rolagem.hpp"
#include "tela_virtual.hpp"
#include "boot_anim.hpp"
#include "orientacao.hpp"
#include "compositor.hpp"
#include "trabalho.hpp"
#include "pool_tarefas.h"
#include "tab5_monitor.hpp"
#include "registro.h"
#include "tab5_log.hpp"
#include "tab5_camera.hpp"
#include "ajustes.h"
#include "tab5_ble.hpp"
#include "tab5_wifi.hpp"
#include "tab5_espnow.hpp"
#include "tab5_teclado.hpp"
#include "tab5_usb.hpp"
#include "tab5_tela.hpp"
#include "tab5_ui.hpp"
#include "grove.hpp"
#include "idioma.h"
#include "tab5_sensores.hpp"
#include "tab5_imu.hpp"
#include "tab5_hora.hpp"
#include "tab5_barramento.hpp"
#include "tab5_bateria.hpp"
#include "tab5_ensaio.hpp"
#include "tab5_wifi_cochilo.hpp"
#include "console.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "hal/gpio_ll.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

// ---------------------------------------------------------------------------
// ONDE A INTERFACE ESTA - para quando ela trava sem deixar nada no log.
// ---------------------------------------------------------------------------
// Cada passo do loop deixa o nome aqui (ETAPA) e cada volta completa conta.
// O monitor (outra tarefa) le isto 1x/s: loop parado ha mais de 3 s vira
// evento com o NOME do passo em que parou; e o comando 'ui' do console mostra
// na hora, mesmo com a tela congelada. 'dedo' diz se o toque ainda e lido.
static volatile const char* s_etapa   = "boot";
static std::atomic<uint32_t>    s_voltas  = 0;
static volatile uint32_t    s_volta_ms = 0;   // quando a ultima volta terminou
static volatile uint32_t    s_dedo_ms  = 0;   // a ultima vez que havia dedo na tela
#define ETAPA(nome) (s_etapa = (nome))

// ---------------------------------------------------------------------------
// O TOQUE DO SISTEMA (toques normais que nao eram reconhecidos)
// ---------------------------------------------------------------------------
// Ate aqui o clique era o wasClicked() do M5Unified, e ele recusa dois casos
// que sao toques normais de um dedo nesta tela:
//   - o dedo andou mais de 8 px entre descer e subir (vira "flick"). A 294 DPI
//     isso e 0,7 mm - o rolar natural da ponta do dedo ja passa disso;
//   - o dedo ficou mais de 500 ms (vira "hold"). Um toque sem pressa some.
// Nos dois casos o toque simplesmente nao acontecia, sem log nenhum.
//
// Agora o reconhecedor e do SISTEMA, no modelo do LVGL (CLICKED): desceu e
// subiu sem arrastar = clique, com qualquer duracao. "Arrastar" = afastou-se
// do ponto de descida mais que TOQUE_TOLERANCIA_PX, ou a rolagem do sistema
// andou (rolagem_foi_arraste). ~2 mm, bem abaixo de qualquer gesto - e o
// MESMO limite das listas (ROLAGEM_ARRASTE_MIN): um limite so no sistema.
#define TOQUE_TOLERANCIA_PX ROLAGEM_ARRASTE_MIN
bool     s_dedo_na_tela = false;   // o NOSSO estado: desceu e ainda nao subiu
static int      s_toque_x0, s_toque_y0;   // onde desceu
static int      s_toque_x, s_toque_y;     // onde esta (fica o da soltura)
static int      s_toque_desvio = 0;       // maior afastamento da descida, px
static uint32_t s_toque_desde = 0;

// A LEITURA DO GT911 NAO PODE PERDER A SOLTURA. O driver do M5GFX so le o chip
// quando o pino de interrupcao (GPIO 23) esta em nivel baixo; com ele alto,
// devolve o ULTIMO dado lido. Se o aviso da soltura passar entre duas voltas
// do loop, o dado velho diz "dedo na tela" para sempre: nenhum clique, a tela
// nunca entra em repouso, e o proximo toque some junto - a tela parece
// travada ate alguns toques depois. Com o dedo na tela, entao, a leitura e
// SEMPRE pelo I2C (pin_int = -1): o GT911 guarda cada relatorio ate ser lido,
// e o da soltura e lido de certeza. Sem dedo, volta a esperar o pino - a tela
// parada nao gasta I2C (e a ISR do mesmo pino acorda o loop no proximo toque).
//
// E O PULSO NAO PODE SE PERDER (sintoma: a tela entrava em repouso com o
// dedo em uso, com o CreazySnake aberto). O GT911 avisa por PULSO
// no pino, e o driver so olha o NIVEL do pino na hora do M5.update(). Com a
// tela ativa (jogo a 20 ms, ou qualquer coisa pendente) a interrupcao ficava
// desligada, e o loop amostrava o pino a cada 20 ms em fase fixa: se o pulso
// caisse sempre entre duas amostras, NENHUM toque novo era visto - nem pelo
// jogo, nem pela contagem do repouso. Agora a interrupcao fica LIGADA sempre e
// so ANOTA o pulso (s_toque_pulso); a leitura seguinte vai ao I2C de certeza.
// Acordar a queue continua so em repouso/parado (s_isr_acorda) - o motivo de ela
// ter nascido desligada (arraste enchendo a queue) segue valendo.
volatile bool s_toque_pulso = false;   // a ISR viu um pulso desde a ultima leitura
static volatile bool s_isr_acorda  = false;   // a ISR tambem acorda o loop (repouso / parado)
std::atomic<uint32_t> s_pulsos  = 0;       // pulsos do GT911 desde o boot (vigia do toque)


static const char *TAG = "core";

// ---- geometria -------------------------------------------------------------
// ALTURA_BARRA/ALTURA_NAV moraram aqui ate a Etapa 1; agora vem de
// kernel/internal.hpp porque kernel/shell.cpp tambem precisa delas.

// GEOMETRIA DO BOTAO "INICIO" (a pilula na faixa de navegacao) - moveu para
// kernel/shell.cpp, junto de desenhar_barra_nav() e tratar_toque(), que sao
// quem realmente usa essas constantes agora.

// (REPINTURA_MIN_MS, o freio de 80ms entre repinturas, saiu:
// era limite de BANDA para desenhar na pagina visivel. Com o compositor, tudo
// e desenhado na tela virtual e vai para o painel no ritmo dos frames dele.)
static uint32_t s_ultimo_teclado  = 0;
// Relogio de pixel do painel, em MHz. Ver tab5_power_relogio_dpi() - e a unica
// coisa que reduz de forma PERMANENTE a banda que o painel consome da PSRAM, e
// portanto a margem que sobra para todo o resto desenhar sem piscar.
//
// NAO E MAIS CONSTANTE: virou preferencia do usuario, gravada em
// ajustes_relogio_painel() - o console ('tela relogio') e o app Ajustes gravam ali,
// e o boot aplica o que estiver guardado, em vez de sempre voltar para 60.

// ---- comportamento ---------------------------------------------------------
// Tempo sem toque ate o repouso. Vem do AJUSTE do usuario, nao de constante:
// e preferencia de quem segura o aparelho, igual ao brilho. Ver ajustes.h para
// o porque dos limites de 15 e 60 s.
static inline uint32_t ocioso_ms(void) { return (uint32_t)ajustes_tela_s() * 1000u; }
static const uint32_t PERIODO_UI  = 20;
// Teclado aberto: a volta mais longa entre duas leituras do toque. Abaixo dos
// 128 ms em que o driver do GT911 passa a descartar o relatorio pendente (ver
// ajustar_leitura_do_toque).
static const uint32_t TOQUE_FRESCO_MS = 100;

// (PERIODO_OCIOSO/OCIOSO_APOS_MS sairam. O loop acordado era
// amostrado a 25ms para nao perder toque rapido - "a cura definitiva e o toque
// por INTERRUPCAO". Ela chegou: parada, a interface dorme com a interrupcao do
// GPIO 23 ligada e acorda no instante do toque. Ver o fim do loop.)

// De quanto em quanto tempo perguntar ao teclado se ha tecla.
//
// Era a cada ciclo (20ms) = 50 transacoes I2C por segundo, sempre. Digitacao
// humana nao passa de ~10 teclas por segundo; 60ms e imperceptivel ao dedo e
// corta a conversa com o teclado a um terco.
static const uint32_t TECLADO_MS = 60;
// Com uma tecla que repete segurada (apagar, seta), le no ritmo da interface:
// a repeticao e de 70 ms, e a 60 ms ela sairia de duas em duas leituras.
static uint32_t teclado_ms(void) { return tab5_teclado_segurando() ? 20 : TECLADO_MS; }
// Periodo do loop em repouso. Foi 200ms e era CEDO DEMAIS para o toque: o
// M5.update() so ve o dedo se ele estiver na tela no instante da amostragem, e
// um toque de 100ms cabe inteiro entre duas amostras de 200ms. Detectar por
// nivel nao resolve se nao ha amostra durante o toque.
//
// A 80ms, um toque humano tipico (150ms ou mais) e visto sempre. HOJE so vale
// quando a ISR do toque nao subiu: com ela, o repouso dorme ate o proximo
// prazo real (~1 s) e o toque acorda o loop pela interrupcao. (O repouso
// baixa a CPU para o piso - 40 MHz, ver tab5_power_repouso.)
static const uint32_t PERIODO_ZZZ = 80;

// Teto do clock no repouso: FIXO, decidido uma vez na entrada do repouso.
// Reconfigurar o PM (esp_pm_configure) com o sono leve ativo travou o Tab5
// (os dois nucleos presos em secao critica, um vindo de esp_pm_lock_release,
// pego pelo JTAG em 06/11/2026) quando um download comecou com a tela ja
// apagada. 180 e o maior abaixo dos 200 MHz da PSRAM (ver faixa_segura).
static const int REPOUSO_TETO_MHZ = 180;
static const uint32_t BARRA_MS    = 2000;

// ---- estado ----------------------------------------------------------------
const app_t* s_app = nullptr;      // NULL = tela inicial - extern em internal.hpp

// ---------------------------------------------------------------------------
// ROLAGEM POR TOQUE
// ---------------------------------------------------------------------------
// Mora no CORE, e nao em cada app, pela mesma razao que a area util mora:
// e geometria, e geometria repetida em oito lugares vira oito jeitos de errar.
//
// O app declara altura_conteudo e nao pensa mais nisso. Quem desloca o desenho,
// recorta na area, converte as coordenadas do toque e distingue arraste de
// clique e o codigo abaixo - agora delegado a kernel/rolagem.hpp, o mecanismo
// COMPARTILHADO com qualquer app que precisar de rolagem propria (o Mesh,
// consolidado - ver o aviso grande em rolagem.hpp sobre POR QUE
// isto deixou de ser so deste arquivo).
rolagem_t s_rol;   // zero-init ja e VERTICAL/INICIO - extern em internal.hpp

// DOIS niveis de "sujo", e a distincao entre eles e o que mata o flickering:
//
//   s_sujo_layout - a TELA MUDOU (abriu app, fechou app, acordou do repouso).
//                   Aqui sim vale apagar a area e redesenhar tudo.
//   s_sujo_dados  - so os NUMEROS mudaram. Redesenha por cima, SEM apagar.
//
// Antes havia um flag so, e todo redesenho comecava com um fillRect preto na
// area inteira - 720x1120 pixels apagados e repintados a cada segundo. Isso e
// literalmente uma piscada preta na taxa do redesenho.
// Como os apps escrevem com cor de fundo (setTextColor(frente, fundo)), o texto
// pinta o proprio retangulo e nao precisa de limpeza previa.
bool s_sujo_layout = true;   // extern em internal.hpp - shell.cpp tambem escreve
bool s_sujo_dados  = true;   // extern em internal.hpp

// TELA DE BLOQUEIO. Fica NA FRENTE do que estiver aberto (home ou app): o app
// continua vivo por baixo, so nao desenha nem recebe toque enquanto isto for
// verdadeiro. Ligada ao acordar do repouso (se o ajuste estiver ligado) e
// desligada pelo deslize para cima (tratar_gesto, shell.cpp).
bool s_bloqueado = false;

static uint32_t s_ultimo_toque   = 0;
uint32_t s_ultimo_desenho = 0;
static uint32_t s_ultima_barra   = 0;
static uint32_t s_acordou_em     = 0;   // instante em que TERMINOU de acordar

// O DEDO QUE ACORDOU A TELA nao pode apertar nada.
//
// Havia so uma carencia de 500ms contada a partir do INICIO do despertar. Mas o
// despertar em si consome esse tempo: remontar o DSI, repintar a tela inteira,
// esperar o frame sair e acender em rampa. Quando isso passa de meio segundo -
// e passa, o proprio log mede -, a carencia vence ANTES de o dedo sair da tela,
// a soltura vira clique, e o aparelho abre o app que estava embaixo do dedo.
//
// Tempo era a medida errada. O que define o problema nao e "quanto demorou", e
// "e o MESMO dedo". Esta bandeira segue a pressao: nasce ao acordar e so morre
// quando o dedo sai da tela. Enquanto ela estiver de pe, nenhum clique passa -
// nao importa se o despertar levou 50ms ou 5s.
bool s_dedo_do_despertar = false;
static uint32_t s_repouso_falhou = 0;   // instante da ultima tentativa frustrada
static uint32_t s_ultima_medida  = 0;
// CONTRATO DE ECONOMIA (app.h): com a tela apagada o P4 dorme entre as voltas,
// e cada volta e uma acordada. Os mesmos trabalhos, prazos mais longos -
// evento de verdade (toque, tecla, aviso) segue acordando na hora.
#define MEDIDA_MS()  sistema_ritmo_ms(1000, 5000)       // INA226, sensores, hora (a guarda conta tempo real)
#define BARRA_RMS()  sistema_ritmo_ms(BARRA_MS, 5000)   // em repouso: o beep de aviso
// A maior espera da interface por volta: o prazo do watchdog menos 2 s (ver
// esp_task_wdt_add, no inicio da tarefa).
static const uint32_t UI_ESPERA_MAX_MS = CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000u - 2000u;

TaskHandle_t s_tarefa_interface = nullptr;   // capturado na criacao, ver sistema_iniciar()



bool sistema_bloqueado(void) { return s_bloqueado; }

// QUEUE DE EVENTOS - ETAPA 2 do docs/ARQUITETURA.md, primeira fatia.
//
// Ate aqui o loop principal so sabia "vTaskDelay(periodo) e confira tudo nao
// vezes". Isso significa acordar 12,5x/s (PERIODO_ZZZ=80ms) MESMO COM A TELA
// APAGADA, so para perguntar "aconteceu algo?" - e quase sempre a resposta
// era nao. Medido (comando "energia travas 40"): a trava de PM
// "rtos0" do proprio esp_pm fica presa exatamente porque o core 0 nunca
// fica ocioso por tempo suficiente para ela soltar - ver docs/ARQUITETURA.md,
// secao 2.2.1.
//
// A mudanca aqui e so em COMO o loop espera, nunca em O QUE ele decide: toda
// bandeira suja, todo pintura_mudou_i/f/s, todo barra_mudou() continua
// EXATAMENTE igual - eles so passam a ser conferidos num ritmo mais esperto
// (o proximo prazo real, ou uma interrupcao de toque de verdade) em vez de
// um numero fixo.
//
// SO O REPOUSO usa o prazo adaptativo (ver o calculo no fim do loop). Acordado,
// o timeout passado a xQueueReceive() e o MESMO PERIODO_UI/PERIODO_OCIOSO de
// sempre - o unico ganho ali e trocar vTaskDelay() por uma espera que TAMBEM
// acordaria antes se algo chegasse na queue, sem mudar a cadencia observavel.
//
// O DESPERTADOR E UMA NOTIFICACAO DE TAREFA, nao mais uma fila. A fila
// carregava um 'evento_t' (toque, servico, tecla) que ninguem lia - o loop
// sempre conferia tudo de novo. Para "acorde a tarefa X" o FreeRTOS tem a
// notificacao direta: mais rapida que a fila, sem RAM de fila, e sem o "fila
// cheia, aviso jogado fora" (ela ACUMULA, e um ulTaskNotifyTake leva todos).
// Quem avisa: a ISR do toque (GPIO 23), a ISR do teclado (G50) e
// sistema_acordar_interface() (notificacoes, servicos, pedidos de outras
// tarefas).
//
// NUM INDICE PROPRIO (1), nao no 0: o indice 0 e o padrao de tudo o que usa
// notificacao por baixo (stream buffers do FreeRTOS, alguns drivers), e um
// aviso nosso nele poderia acordar uma espera que nao e nossa. Com o
// sdkconfig antigo (1 entrada so) cai no 0 - funciona igual ao que era, sem
// a protecao. CONFIG_FREERTOS_TASK_NOTIFICATION_ARRAY_ENTRIES=2 no
// sdkconfig.defaults.
#define UI_AVISO_IDX ((configTASK_NOTIFICATION_ARRAY_ENTRIES) > 1 ? 1 : 0)

static inline void ui_avisar_da_isr(void)
{
  if (!s_tarefa_interface) { return; }
  BaseType_t acordou = pdFALSE;
  vTaskNotifyGiveIndexedFromISR(s_tarefa_interface, UI_AVISO_IDX, &acordou);
  portYIELD_FROM_ISR(acordou);
}

// false = a ISR do toque nao subiu (por algum motivo raro - ver
// sistema_iniciar()); o repouso volta ao PERIODO_ZZZ fixo de sempre, o MESMO
// comportamento que o sistema tinha antes desta fatia. Nunca piora nada, so
// deixa de ganhar o teto adaptativo.
static bool s_toque_isr_ok = false;

// TELA TDDI (ST7123/ST7121): a ISR e instalada, mas so passa a valer depois
// de PROVAR que o pino pulsa - o driver desses toques nem olha o GPIO 23, e
// ninguem testou o pino neles. Ate a prova, s_toque_isr_ok fica false e a
// interface roda no caminho sem interrupcao (20 ms acordada, PERIODO_ZZZ em
// repouso), que funciona com qualquer toque. Prova: 3 bordas durante UM
// toque lido pelo I2C. Ver validar_isr_tddi().
static bool s_toque_isr_instalada = false;
static bool s_tecla_isr_ok = false;          // a ISR do teclado (G50) subiu - ver isr_teclado

// A INTERRUPCAO NASCE DESLIGADA E SO E LIGADA DENTRO DO REPOUSO - achado na
// revisao deste desenho, antes de escrever a primeira linha: enquanto um
// dedo arrasta na tela, o GT911 pulsa este mesmo pino a cada relatorio novo
// (5-20ms, taxa de atualizacao do proprio controlador). Se a interrupcao
// estivesse ligada o tempo todo alimentando esta queue, e essa MESMA queue
// tambem regesse PERIODO_UI/PERIODO_OCIOSO, a queue nunca ficaria vazia
// durante um arraste - xQueueReceive() retornaria quase instantaneo a cada
// chamada, e o loop deixaria de respeitar os 20/25ms escolhidos a dedo,
// rodando tao rapido quanto o proprio corpo do loop permitisse. Por isso:
// s_isr_acorda liga so no instante em que tab5_power_repouso() tem sucesso (ou
// com a tela parada) e desliga quando a tela volta a ter motivo para rodar a
// 20 ms. a interrupcao em si fica ligada o tempo todo: ela
// ANOTA cada pulso (s_toque_pulso, ver ajustar_leitura_do_toque) - com ela
// desligada, um toque cujo pulso caia entre duas amostras do loop se perdia.
//
// AVISO PARA O FUTURO: isto assume que ninguem reconfigura o GPIO 23 depois
// do boot. E verdade hoje - conferido, Touch_GT911::sleep()/
// wakeup() (que REUTILIZAM este mesmo pino como saida) nunca sao chamados em
// lugar nenhum deste projeto, so M5.Display.sleep()/wakeup() (que so mexe no
// painel). Se um dia alguem adicionar M5.Touch.sleep()/wakeup() para
// economizar energia do controlador de toque, isso vai reconfigurar o pino
// por baixo desta ISR e quebra-la em silencio - o sintoma seria "o repouso
// para de ganhar o teto adaptativo e volta ao PERIODO_ZZZ fixo sem log
// nenhum explicando por que". Se isso acontecer, comece a busca aqui.
// A PROVA DO PINO NUMA TELA TDDI (ver s_toque_isr_instalada). Chamada a cada
// volta acordada; depois de provado, nao faz mais nada.
static void validar_isr_tddi(bool tocando)
{
  static uint32_t base = 0;
  static bool dedo = false;
  if (s_toque_isr_ok || !s_toque_isr_instalada) { return; }
  if (tocando && !dedo) { base = s_pulsos; }
  if (tocando && (s_pulsos - base) >= 3) {
    s_toque_isr_ok = true;
    ESP_LOGI(TAG, "toque da tela %s: o pino INT pulsa - repouso e tela parada por interrupcao",
             tab5_painel_nome());
  }
  dedo = tocando;
}

// SONO LEVE: QUEM ACORDA O P4. Em light sleep a interrupcao de borda nao
// chega; so o "gpio wakeup" por NIVEL tira o chip do sono. Ao entrar no sono
// os pinos do toque e do teclado viram despertar por nivel (o contrario do
// nivel de repouso de cada um); ao sair voltam a borda. Por nivel a
// interrupcao repetiria enquanto o pino segue ativo (o INT do teclado fica em
// baixo ate a fila ser lida) - entao a propria ISR se desliga no primeiro
// disparo armado, com o gpio_ll (inline, seguro em ISR).
static volatile bool s_despertar_armado = false;
static bool s_toque_nivel_ocioso = true;
// O G50 so acorda o chip por nivel enquanto esta EM CIMA ao armar. Em baixo
// sem ninguem ler a fila (teclado que a procura nao achou, outro acessorio no
// ExtPort1), o despertar por nivel tiraria o chip de todo sono leve na hora -
// repouso com consumo de acordado, sem log. Era o que acontecia com o teclado
// encaixado e o antigo ajuste "desativado".
static bool s_tecla_despertar = false;

static void tecla_despertar(bool sim)
{
  if (sim) {
    if (!s_tecla_despertar) { gpio_wakeup_enable((gpio_num_t)TAB5_TECLADO_INT, GPIO_INTR_LOW_LEVEL); }
    gpio_intr_enable((gpio_num_t)TAB5_TECLADO_INT);   // a ISR se desliga no disparo por nivel
  } else if (s_tecla_despertar) {
    gpio_wakeup_disable((gpio_num_t)TAB5_TECLADO_INT);
    gpio_set_intr_type((gpio_num_t)TAB5_TECLADO_INT, GPIO_INTR_NEGEDGE);
    gpio_intr_enable((gpio_num_t)TAB5_TECLADO_INT);
  }
  s_tecla_despertar = sim;
}

// ARMADO ANTES DE VIRAR NIVEL. Era o contrario: o gpio_wakeup_enable troca o
// pino para interrupcao por NIVEL na hora, e se o nivel ja estivesse ativo
// (o dedo na tela enquanto ela apagava, um pulso do GT911 caindo bem na
// leitura de s_toque_nivel_ocioso) a ISR disparava com s_despertar_armado
// ainda falso - nao se desligava, e o nivel a disparava de novo, sem fim, no
// mesmo nucleo desta tarefa. Armado primeiro, o pior caso e um disparo a
// toa (rearmar_despertar() religa).
static void armar_despertar(void)
{
  s_despertar_armado = true;
  if (s_toque_isr_instalada) {
    s_toque_nivel_ocioso = gpio_get_level(GPIO_NUM_23) != 0;
    gpio_wakeup_enable(GPIO_NUM_23, s_toque_nivel_ocioso ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  }
  if (s_tecla_isr_ok) { tecla_despertar(gpio_get_level((gpio_num_t)TAB5_TECLADO_INT) != 0); }
  esp_sleep_enable_gpio_wakeup();
}

// Um disparo que nao acordou ninguem (o nivel lido ao armar ja era velho)
// deixou a interrupcao do toque desligada pela propria ISR: o proximo toque
// so seria visto no prazo do laco (segundos). Com o pino de volta ao nivel de
// repouso, religa - e o toque volta a acordar na hora.
static void rearmar_despertar(void)
{
  if (!s_despertar_armado) { return; }
  if (s_toque_isr_instalada) {
    if ((gpio_get_level(GPIO_NUM_23) != 0) == s_toque_nivel_ocioso) {
      gpio_intr_enable(GPIO_NUM_23);
    } else {
      // PINO PRESO NO ATIVO: o GT911 so solta o INT quando alguem le o
      // relatorio, e em repouso so se le com pulso - que e justo o que esta
      // desligado. Visto em 26/10 (log): tela bloqueada que o toque nao
      // acendia e a tecla sim. Forca a leitura na proxima volta; ela solta o
      // pino e a volta seguinte religa a interrupcao.
      s_toque_pulso = true;
    }
  }
  // O G50 segue o nivel de agora: a fila ja foi consultada nesta volta
  // (tab5_teclado_pendente), entao em baixo aqui e pino que ninguem solta.
  if (s_tecla_isr_ok) { tecla_despertar(gpio_get_level((gpio_num_t)TAB5_TECLADO_INT) != 0); }
}

static void desarmar_despertar(void)
{
  if (!s_despertar_armado) { return; }
  s_despertar_armado = false;
  if (s_toque_isr_instalada) {
    gpio_wakeup_disable(GPIO_NUM_23);
    gpio_set_intr_type(GPIO_NUM_23, GPIO_INTR_ANYEDGE);
    gpio_intr_enable(GPIO_NUM_23);
  }
  if (s_tecla_isr_ok) { tecla_despertar(false); }
}

static void IRAM_ATTR isr_toque(void*)
{
  if (s_despertar_armado) { gpio_ll_intr_disable(&GPIO, GPIO_NUM_23); }   // nivel: uma vez so
  s_toque_pulso = true;          // sempre: a proxima leitura vai ao I2C
  ++s_pulsos;
  if (!s_isr_acorda) { return; } // tela ativa: o loop ja roda a 20 ms, nao acordar a toa
  ui_avisar_da_isr();
}

// TECLADO FISICO ACORDA A TELA. O INT do teclado (G50 do ExtPort1, ativo em
// baixo) desce a cada evento e so sobe quando a fila e lida (o driver limpa o
// INT_STAT a cada leitura) - uma borda por tecla, nunca uma rajada, entao ela
// pode ficar ligada de vez, ao contrario da do toque. So acorda a queue no
// mesmo caso da do toque (s_isr_acorda: repouso ou tela parada); acordada, a
// leitura periodica ja pega a tecla. Sem teclado encaixado o pull-up segura
// o pino em cima e ela nunca dispara.

static void IRAM_ATTR isr_teclado(void*)
{
  if (s_despertar_armado) { gpio_ll_intr_disable(&GPIO, TAB5_TECLADO_INT); }   // nivel: uma vez so
  if (!s_isr_acorda) { return; }
  ui_avisar_da_isr();
}

// Acorda a interface AGORA, sem esperar o proximo prazo periodico - chamado
// por quem publicou algo que vale a pena perceber na hora (ver
// notificacao.cpp e a ponte da camera mais abaixo). Seguro chamar de
// QUALQUER tarefa: so poe um evento na queue (nunca bloqueia, 0 de espera) -
// se a queue estiver cheia (o que exigiria 4 avisos sem a interface rodar
// nem uma volta do loop, praticamente impossivel), o evento e descartado sem
// erro, porque o proprio loop ja vai acordar por outro motivo em breve.
// Os avisos pendentes. Atomico porque qualquer tarefa publica e so a
// interface consome - exchange(0) pega tudo de uma vez sem perder o que chegar
// no meio.
static std::atomic<uint32_t> s_avisos{0};

void sistema_avisar(uint32_t avisos)
{
  s_avisos.fetch_or(avisos, std::memory_order_relaxed);
  sistema_acordar_interface();
}

// O periodo do app aberto AGORA (0 = so acorda por aviso ou toque).
uint32_t periodo_do_app(void)
{
  // Bloqueado: o unico "app" que desenha e a animacao de fundo (~25 Hz) com o
  // relogio por cima (ver desenhar_bloqueio).
  if (s_bloqueado) { return BQ_ANIM_PERIODO_MS; }
  if (!app_vivo()) { return 0; }
  return s_app->periodo_agora ? s_app->periodo_agora() : s_app->periodo_ms;
}


void sistema_acordar_interface(void)
{
  if (!s_tarefa_interface) { return; }
  xTaskNotifyGiveIndexed(s_tarefa_interface, UI_AVISO_IDX);
}


// DE FORA DA INTERFACE, ACORDA ELA. Parada, a interface dorme
// ate o proximo prazo (ver o fim do loop) - sem isto, o pedido de uma tarefa
// de app esperaria ate 1s. De DENTRO da interface so marca: acordar a si mesma
// a cada desenho faria o loop girar sem nunca dormir.
static inline void acordar_se_de_fora(void)
{
  if (xTaskGetCurrentTaskHandle() != s_tarefa_interface) { sistema_acordar_interface(); }
}

void sistema_pedir_redesenho(void) { s_sujo_dados = true; acordar_se_de_fora(); }

// Redesenho COMPLETO: o sistema limpa a area e o app repinta tudo (a passada
// chega a ele como completa - pintura_completa). O pedir_redesenho() comum e
// incremental: o app redesenha so o que mudou. Os dois passam pela tela
// virtual e pelo compositor; a diferenca e so quanto o app repinta.
//
// Regra: mudou um valor, use pedir_redesenho(). Mudou a TELA, use este.
void sistema_pedir_redesenho_completo(void) { s_sujo_layout = true; acordar_se_de_fora(); }


const char* sistema_app_atual(void) { return s_app ? s_app->nome : nullptr; }
bool sistema_na_interface(void) { return s_tarefa_interface && xTaskGetCurrentTaskHandle() == s_tarefa_interface; }
// ---------------------------------------------------------------------------
// Tarefa de interface
// ---------------------------------------------------------------------------
static void vTaskInterface(void *pvParameters)
{
  (void)pvParameters;
  s_ultimo_toque = tab5_agora_ms();

  // NO WATCHDOG DE VERDADE, e nao so o da tarefa ociosa.
  //
  // O watchdog do sistema (ver sdkconfig, ESP_TASK_WDT) por padrao so
  // acompanha as duas tarefas IDLE - ele pega uma tarefa que trava o CORE
  // inteiro (nunca devolve CPU, nem para o ocioso rodar), mas NAO pega um
  // DEADLOCK de verdade: esta tarefa bloqueada para sempre num mutex ou
  // semaphore que ninguem mais vai devolver, enquanto o resto do sistema
  // continua rodando normalmente por fora. Sem se inscrever, um travamento
  // assim deixaria a tela congelada para sempre, sem NENHUM sinal disso no
  // log - o aparelho pareceria "vivo" (CPU ociosa disponivel) e estaria
  // surdo e mudo por dentro.
  //
  // O maior periodo que este loop espera e o ritmo do repouso (5 s, ver
  // MEDIDA_MS), e a espera e cortada em UI_ESPERA_MAX_MS - 2 s abaixo do prazo
  // do watchdog (CONFIG_ESP_TASK_WDT_TIMEOUT_S, 10 s). Com o prazo antigo de
  // 5 s, a espera de 5 s do repouso vencia o watchdog a cada volta. Se um dia
  // o loop nao voltar aqui dentro desse prazo, o
  // watchdog reinicia o aparelho - pior que congelado, melhor que travado
  // para sempre e invisivel.
  if (esp_task_wdt_add(NULL) != ESP_OK) {
    ESP_LOGW(TAG, "nao consegui inscrever a interface no watchdog");
  }

  // O WiFi so sobe aqui, e nao no app_main, por uma razao concreta: ligar o
  // trilho do C6 e uma escrita no expansor I2C, e esta tarefa e a dona do I2C.
  // Alem disso, subindo depois da interface, a tela ja esta desenhada quando o
  // radio comeca a negociar - o usuario ve "conectando" em vez de esperar.
  if (ajustes_wifi()) { tab5_wifi_ligar(); }

  // BLUETOOTH. Mesma razao do WiFi para morar aqui: ligar pede o
  // trilho do C6 (I2C). So o botao do usuario liga (ver sistema_bluetooth);
  // com o Mesh no Bluetooth o servico dele sobe junto - ele nao depende do
  // Grove, entao nao espera sondagem nenhuma.
  if (ajustes_bluetooth()) { bt_aplicar(true); }

  // Teclado do ExtPort1. Aqui, e nao no app_main, porque e I2C - e esta tarefa
  // e a dona do barramento. Sem teclado encaixado ele so avisa e segue; sem
  // ajuste de ligar - o HAL acha o teclado sozinho (tab5_teclado.cpp).
  tab5_teclado_iniciar();

  // Sensor de temperatura do P4. Nao e I2C (e um ADC interno), mas nasce aqui
  // junto dos outros para o boot ter um lugar so onde os sensores sobem.
  tab5_sensores_iniciar();

  // PRIORIDADE DA TELA NO BARRAMENTO. Antes de qualquer app desenhar: e a
  // arbitragem que decide se a piscada azul pode ou nao acontecer, e ela nao
  // depende de nada que os apps facam.
  tab5_barramento_priorizar_tela(true);

  // A TELA VIRTUAL (docs/COMPOSITOR.md, etapa 3a). Antes do primeiro desenho: a
  // partir daqui, home, barras e apps desenham nela, e so o compositor
  // escreve nas paginas do painel.
  tela_virtual_iniciar();

  // O WORKER DO SISTEMA (kernel/trabalho.hpp): o que for lento sai
  // desta tarefa.
  trabalho_iniciar();

  // RELOGIO DO PAINEL. A outra metade da mesma briga: a prioridade decide quem
  // ganha a disputa, isto REDUZ a disputa. O M5GFX programa 80MHz para este
  // painel - 64 frames por segundo e 118,5 MB/s lidos da PSRAM sem parar. A
  // 60MHz sao 48 frames e 88,9 MB/s: um quarto a menos de demanda permanente,
  // e nenhum app precisa saber que isso existe.
  //
  // Aqui, antes do primeiro desenho: a troca desmonta e remonta o painel, e
  // fazer isso com a tela ja em uso apagaria o conteudo por um instante.
  if (tab5_painel_toque_gt911()) { tab5_power_relogio_dpi((int)ajustes_relogio_painel()); }   // TDDI: fica o de fabrica

#ifdef TAB5_MOSTRAR_ANIM_BOOT
  // SO AGORA: o painel ja tem o relogio final e nao vai mais ser desmontado
  // neste boot (a troca de relogio apagaria a tela no meio da animacao), e a
  // tela virtual ja existe para desenhar por ela.
  boot_anim_mostrar();
#endif

  // Quem esta no Grove? A vigia (kernel/grove.cpp) sonda um instante depois,
  // com a home ja desenhada - a sondagem leva segundos e nenhum app depende
  // dela para abrir.
  grove_iniciar();

  for (;;) {
    // MEDIDA (kernel/compositor.hpp, comp_estat): a volta comeca aqui, no
    // acordar. Se ela terminar em quadro, o custo inteiro vai ao histograma.
    const int64_t volta_t0 = esp_timer_get_time();
    ETAPA("toque (M5.update, I2C)");
    // Navegacao travada = teclado (ou captura) lendo o toque por nivel: o
    // I2C em toda volta, para o driver nunca descartar o dedo descendo.
    ajustar_leitura_do_toque(s_nav_travada);
    M5.update();
    const uint32_t t = tab5_agora_ms();

    atender_pedido_de_relogio();

    // PAINEL NOVO (boot, troca de relogio): no modo direto as paginas sao a
    // UNICA copia da tela - painel novo nasce sem nada nosso, e o unico jeito
    // de ter a tela de volta e redesenha-la inteira. (O acordar do repouso
    // trata o seu caso la embaixo, antes de acender a luz.)
    if (!tab5_power_em_repouso() && comp_painel_novo()) { s_sujo_layout = true; }

    // As partes de fundo (kernel/fundo.h): quem terminou sozinho e limpo, a
    // parada pedida pelo console acontece, e o perfil efetivo e reaplicado
    // quando o conjunto mudou. Acordado ou em repouso - o fundo nao dorme.
    ETAPA("partes de fundo");
    fundo_manter();
    ETAPA("vigia do Grove / abertura com SD");

    // ABERTURA DE APP COM SD PENDENTE: a tarefa de fundo (vTaskMontarSDApp)
    // terminou desde o ultimo ciclo? Conclui agora, com a tela ja mostrando
    // "abrindo..." ha alguns frames - ver abrir_app() e concluir_abertura_do_sd().
    if (s_app_carregando && !s_sd_montando) {
      concluir_abertura_do_sd();
    }
    // O CARTAO VOLTOU com a tela de recusa aberta: abre o app de verdade.
    if (s_app_recusado && !tab5_sd_emprestado() && !s_bloqueado) {
      const app_t* ap = s_app_recusado;
      fechar_app();
      abrir_app(ap);
    }

    // Uma leitura do INA226 por segundo, ACORDADO OU EM REPOUSO. Esta tarefa e
    // a unica que pode ir ao I2C, entao e ela quem alimenta o cache que o
    // registro de energia consome. Sem isto, o registro so teria dado enquanto
    // a barra de status estivesse sendo desenhada - ou seja, nada em repouso.
    if ((t - s_ultima_medida) >= MEDIDA_MS()) {
      s_ultima_medida = t;
      ETAPA("medidas (I2C: energia, sensores, IMU, hora)");
      tab5_power_medir();
      // Bateria vazia: avisa e desliga antes do brownout (services/tab5_bateria).
      tab5_bateria_vigiar();
      tab5_ensaio_vigiar();   // so faz algo com 'energia ensaio' pedido
      tab5_wifi_cochilo_vigiar();   // Wi-Fi fora com a tela apagada (services/tab5_wifi_cochilo)
      // Mesmo tique: os dois alimentam a mesma barra, e uma cadencia so evita
      // que ela mude por metades em instantes diferentes.
      tab5_sensores_medir();
      // IMU, 1x/s - so para o CACHE que outros apps (nao o app_imu, que le
      // direto e mais rapido - ver o aviso grande em tab5_imu.hpp sobre por
      // que as cadencias sao diferentes de proposito) podem consultar sem
      // tocar no barramento I2C interno.
      // EM REPOUSO, NAO: ninguem le este cache com a tela apagada (quem
      // segue o IMU - kernel/orientacao - mede sozinho a cada 200 ms, e so com
      // app aberto). Era uma transacao I2C por segundo, todo segundo, para
      // nada; acordado, ela continua servindo o console.
      // Em repouso so o giroscopio segue o pedido (tab5_imu_repouso).
      if (!tab5_power_em_repouso()) { tab5_imu_medir(); } else { tab5_imu_repouso(); }
      // Hora: GPS/Mesh como fonte, RTC (I2C interno) como destino - mesma
      // regra de so a interface tocar o barramento. So escreve de fato a
      // cada ~10min (ver RESSINCRONIZAR_MS), a chamada 1x/s e so para
      // perceber uma fonte nova assim que ela aparecer.
      tab5_hora_sincronizar();
      vigiar_mesh_internet(t);
    }

    // Teclado fisico: a cada TECLADO_MS. Quando nao ha tecla e UMA transacao
    // I2C (so o contador de eventos), barato o bastante para o laco acordado.
    //
    // A TRADUCAO E DO DRIVER (hal/tab5_teclado): ele conhece o teclado - o
    // modo HID, a tabela das 70 teclas, as teclas mortas dos acentos - e
    // entrega o evento pronto. Aqui so se entrega ao app. Ate 4 teclas por
    // volta: digitando rapido, mais de uma cabe no intervalo, e uma por volta
    // atrasaria o texto atras dos dedos.
    if (!tab5_power_em_repouso() && (t - s_ultimo_teclado) >= teclado_ms()) {
      s_ultimo_teclado = t;
      ETAPA("teclado (I2C)");
      tab5_tecla_t ev;
      for (int k = 0; k < 4 && tab5_teclado_ler(&ev); ++k) {
        s_ultimo_toque = t;          // digitar conta como uso: nao apague a tela
        if (s_bloqueado) {
          // TELA BLOQUEADA: o teclado nao fala com o app que esta atras - so
          // o Enter, que desbloqueia (o par do deslize para cima). Antes desta
          // guarda o que se digitava no bloqueio ia parar na conversa.
          if (ev.codigo == TAB5_TECLA_ENTER) {
            ESP_LOGI(TAG, "desbloqueada pelo teclado (Enter)");
            desbloquear_tela();
            tab5_teclado_descartar();   // o resto da rajada nao vai para o app
            break;
          }
          continue;
        }
        if (s_painel_avisos) {
          // CENTRAL DE AVISOS ABERTA: o app esta escondido e nao ouve. Esc
          // fecha a central; o resto e engolido (nao vai digitar as cegas).
          if (ev.codigo == TAB5_TECLA_ESC) { painel_avisos_abrir(false); }
          continue;
        }
        if (app_vivo() && s_app->ao_teclar) {
          s_app->ao_teclar(ev.texto, ev.codigo, ev.mod);
          sistema_pedir_redesenho();
        } else {
          ESP_LOGI(TAG, "tecla: \"%s\" cod=%u%s%s%s (nenhum app escutando)",
                   ev.texto, (unsigned)ev.codigo,
                   (ev.mod & TAB5_TECLA_CTRL)  ? " +CTRL"  : "",
                   (ev.mod & TAB5_TECLA_SHIFT) ? " +SHIFT" : "",
                   (ev.mod & TAB5_TECLA_ALT)   ? " +ALT"   : "");
        }
      }
    }

    // Em repouso, detecta o toque por NIVEL (ha dedo na tela?) e nao por borda.
    // O wasClicked() dispara na SOLTURA, e o loop em repouso roda a cada 200ms:
    // um toque rapido comeca e termina dentro do mesmo intervalo, o M5.update()
    // nunca ve a transicao, e o toque se perde. Era por isso que as vezes
    // precisava tocar duas ou tres vezes para acordar.
    ETAPA("toque: decidir");
    const bool tocando = (M5.Touch.getCount() > 0);
    if (tocando) { s_dedo_ms = t; }
    const auto toque = M5.Touch.getDetail();
    if (!tab5_power_em_repouso()) { ETAPA("toque: vigia"); vigiar_toque(t, tocando); lupa_toque(t, tocando); validar_isr_tddi(tocando); }
    vigiar_toque_repouso(t, tocando, tab5_power_em_repouso());

    // TECLA EM REPOUSO: a fila do teclado e consultada a cada volta do repouso
    // (o pulso do G50 acorda a volta na hora; sem ele, o prazo de ~1 s). Uma
    // transacao I2C por volta com teclado encaixado; sem ele, so o nivel do
    // G50 - em baixo, um teclado recem-encaixado e procurado e a tecla acorda.
    bool tecla_acorda = false;
    if (tab5_power_em_repouso()) {
      tecla_acorda = tab5_teclado_pendente();
    }

    // O pedido de acordar so vale para o repouso em que chegou: acordado, ele
    // e descartado - sobrando, acordaria o PROXIMO repouso logo ao entrar.
    const bool pedido_acordar = s_pedir_acordar;
    s_pedir_acordar = false;
    if (tab5_power_em_repouso()) {
      const bool sim_acorda = s_sim_x >= 0 || s_sim_abrir || s_sim_fechar || pedido_acordar;
      if (tocando || tecla_acorda || sim_acorda) {
        s_sim_x = -1;   // o pedido que acordou nao vira clique, como o dedo
        s_ultimo_toque = t;
        // O que acordou nao conta como uso: o dedo nao vira clique
        // (s_dedo_do_despertar), e a tecla nao vira letra (descartada).
        if (tocando) { s_dedo_do_despertar = true; }
        if (tecla_acorda) { tab5_teclado_descartar(); }
        ESP_LOGI(TAG, "acordando (%s): app=%s", tocando ? "toque" : "teclado", s_app ? s_app->nome : "home");
        // DESLIGA A ISR AQUI, antes de mais nada - ver o aviso grande junto
        // de s_toque_isr_ok. Acordado, um arraste pulsaria este pino a cada
        // 5-20ms e a queue nunca ficaria vazia se a interrupcao seguisse
        // ligada.
        s_isr_acorda = false;
        // Os pinos voltam a borda ANTES do retomar (que tambem desliga o sono
        // leve, pelo teto de clock): acordado, o toque pulsa a cada 5-20 ms.
        desarmar_despertar();
        ETAPA("repouso: acordando");
        if (tab5_power_retomar()) {
          tab5_usb_repouso(false);   // os 5V do USB-A voltam: o pendrive monta de novo
          // RELIGA O QUE O APP SOLTOU EM ao_repouso() (ex.: a camera) - so
          // depois do painel ja estar de pe de novo, para nao reabrir um
          // consumidor de DMA antes do canal do painel estar reconstruido.
          // Ver o aviso grande em kernel/app.h junto de ao_repouso/ao_retomar.
          // A camera que o repouso pausou volta antes (o app ja a encontra).
          tab5_camera_despausar(app_vivo());
          if (app_vivo() && s_app->ao_retomar) {
            s_app->ao_retomar();
          }
          // O retomar() devolve o painel ao ar com a LUZ AINDA APAGADA e as
          // paginas pretas - e no modo direto elas eram a unica copia da tela.
          // Redesenho COMPLETO direto na pagina de tras, e so depois da troca
          // CONFIRMADA a luz sobe: a primeira coisa que o olho ve ja e o
          // conteudo certo. comp_painel_novo() e consumido aqui para o topo
          // do loop nao pedir um segundo redesenho completo.
          const uint32_t t0 = tab5_agora_ms();
          bloqueio_ao_ligar_tela();
          comp_painel_novo();
          desenhar_barra_status(tela_virtual(), true);
          desenhar_conteudo(true, true);
          comp_apresentar();
          comp_esperar_troca();
          ESP_LOGI(TAG, "acordando: redesenho levou %lu ms",
                   (unsigned long)(tab5_agora_ms() - t0));

          // A rampa de brilho bloqueia esta tarefa; a animacao da tela de
          // bloqueio avanca um quadro a cada degrau para nao ficar congelada.
          if (s_bloqueado) { tab5_power_gancho_rampa(animar_na_rampa); }
          tab5_power_acender();
          tab5_power_gancho_rampa(nullptr);

          // Marcado AQUI, depois de tudo pronto. A carencia de tempo virou
          // rede secundaria (a bandeira acima e a principal), e agora ela conta
          // do momento em que a tela ficou utilizavel - nao de quando o
          // despertar comecou.
          s_acordou_em = tab5_agora_ms();

          // REDE DE SEGURANCA, AGORA SO QUANDO ELA FAZ FALTA.
          //
          // Ela nasceu para a tela esbranquicada: naquele desenho o painel era
          // RESSINCRONIZADO e o primeiro frame depois disso podia sair ruim,
          // entao um segundo redesenho 20ms depois consertava.
          //
          // Hoje o repouso DESMONTA o painel e o retomar cria um novo. Com
          // double buffer, o que foi para a tela e um frame inteiro pintado do
          // zero e publicado de uma vez - nao ha "primeiro frame duvidoso"
          // para corrigir. O que sobrou foi o CUSTO: um segundo redesenho
          // completo (na home sao os oito icones, 578KB) disparado na pagina
          // VISIVEL exatamente enquanto a luz sobe em rampa. A rajada mais mal
          // colocada que existia, bem no instante em que o olho esta olhando.
          //
          // Sem double buffer a historia e outra: ai o desenho foi feito na
          // propria tela, aos pedacos, e uma segunda passada continua valendo.
          // (A "rede de seguranca" do segundo redesenho morreu com o
          // compositor: o frame que acende e sempre inteiro.)
        } else {
          // retomar() ja loga o motivo. Aqui so registramos que a tela seguiu
          // apagada de proposito, para o log contar a historia inteira.
          ESP_LOGE(TAG, "acordando: retomar() falhou - tela segue apagada");
        }
      }

      if (tab5_power_em_repouso()) { rearmar_despertar(); }

      // Beep de notificacao + desligar o I2S ocioso, TAMBEM em repouso -
      // achado, ver o aviso grande em
      // atualizar_beep_e_ociosidade_do_som() mais acima no arquivo. Mesmo
      // passo de 2s que a barra usa acordada (s_ultima_barra e
      // compartilhada entre os dois ramos: sao mutuamente exclusivos, nunca
      // correm no mesmo ciclo, entao nao ha disputa por ela).
      if ((t - s_ultima_barra) >= BARRA_RMS()) {
        s_ultima_barra = t;
        atualizar_beep_e_ociosidade_do_som(t);
        // A musica do pendrive acabou com a tela apagada: agora os 5V caem.
        if (tab5_power_em_repouso()) { tab5_usb_repouso(true); }
      }
    } else {
      if (tocando) { s_ultimo_toque = t; }

      // Toque por NIVEL, para quem pediu: entregue a cada ciclo (20 ms), com o
      // dedo ainda na tela. E o que o wasClicked() nao da - ele so fala na
      // soltura, e um jogo nao pode esperar o dedo sair para virar a cobra.
      // A borda de subida fica a cargo do app; aqui so contamos o que ha agora.
      //
      // A carencia de 500 ms depois de acordar vale igual: o dedo que acendeu a
      // tela nao pode virar jogada.
      if (app_vivo() && s_app->ao_tocar_nivel
       && !sistema_app_encoberto() && !s_dedo_do_despertar && (t - s_acordou_em) > 500) {
        const area_t a = area_util();
        // MESMA CORRECAO E MESMA FAIXA DO CLIQUE (shell.cpp, tratar_toque):
        // TOQUE_DY aplicado aqui tambem, e com a navegacao travada a faixa de
        // baixo e do app - o ponto nela e entregue na ultima linha da area.
        const int nav_y0 = M5.Display.height() - ALTURA_NAV;
        auto y_do_app = [&](int y_cru, bool* vale) {
          int y = y_cru - TOQUE_DY;
          *vale = y >= ALTURA_BARRA && (y < nav_y0 || s_nav_travada);
          if (y >= nav_y0) { y = nav_y0 - 1; }
          return y;
        };
        bool dentro = false;
        const int ty = y_do_app(toque.y, &dentro);
        dentro = dentro && tocando;
        // SO OS DEDOS NA TELA AGORA. O getCount() do M5Unified conta o dedo
        // que ja subiu por mais DUAS amostras (estados touch_end e depois
        // none), e o getDetail(i) indexa o relatorio cru da ultima leitura -
        // alem do numero de pontos dele, as entradas sao velhas. Lido pelo
        // getCount, o teclado via a tecla "ainda apertada" ~40 ms depois de o
        // dedo sair: toda letra atrasava duas voltas, e o dedo seguinte que
        // descia nessa janela com o mesmo id (o GT911 reaproveita o menor
        // livre) parecia o mesmo dedo escorregando - a tecla anterior sumia.
        // Aqui: as 5 vagas, so as pressionadas, cada id uma vez.
        s_n_toques = 0;
        for (int i = 0; i < (int)m5::Touch_Class::TOUCH_MAX_POINTS && s_n_toques < SISTEMA_TOQUES_MAX; ++i) {
          const auto& dd = M5.Touch.getDetail(i);
          if (!dd.isPressed()) { continue; }
          bool repetido = false;
          for (int k = 0; k < s_n_toques; ++k) { if (s_toques[k].id == dd.id) { repetido = true; break; } }
          if (repetido) { continue; }
          bool vale = false;
          const int y = y_do_app(dd.y, &vale);
          if (!vale) { continue; }
          s_toques[s_n_toques].id = dd.id;
          s_toques[s_n_toques].x  = (int16_t)(dd.x - a.x);
          s_toques[s_n_toques].y  = (int16_t)(y - a.y + s_rol.atual);
          ++s_n_toques;
        }
        // ROLAGEM SOMADA AQUI TAMBEM. O ao_tocar() ja fazia isso e este nao
        // fazia - a assimetria nao aparecia porque nenhum app que rola usava
        // toque por nivel. Deixar assim seria plantar um bug para o primeiro
        // que usasse: os dois retornos de toque TEM que falar o mesmo idioma
        // de coordenadas, senao o app acerta num e erra no outro.
        s_app->ao_tocar_nivel(dentro ? toque.x - a.x : -1,
                              dentro ? ty - a.y + s_rol.atual : -1,
                              dentro);
      }

      // ARRASTE: acumula enquanto o dedo esta na area util. Toda a
      // matematica de arraste/afastamento/sinal mora agora em
      // kernel/rolagem.hpp - ver o aviso grande la sobre por que isto
      // deixou de ser so deste arquivo.
      {
        const area_t ar = area_util();
        s_rol.max = limite_do_app(ar);
        const bool na_area = tocando
                          && toque.y >= ALTURA_BARRA
                          && toque.y <  (M5.Display.height() - ALTURA_NAV);
        // NAO marca s_sujo_layout aqui. Rolagem pura tem um caminho proprio,
        // mais barato (ver o bloco de repintura no loop principal): desloca
        // o pixel ja desenhado em vez de apagar a area inteira e redesenhar
        // tudo de novo a cada amostra de toque.
        rolagem_arrastar(&s_rol, toque.y, na_area && !sistema_app_encoberto() && s_rol.max > 0);
      }

      // O CLIQUE DO SISTEMA - ver TOQUE_TOLERANCIA_PX no topo do arquivo.
      if (toque.isPressed()) {
        if (!s_dedo_na_tela) {
          s_dedo_na_tela = true;
          s_toque_x0 = toque.x; s_toque_y0 = toque.y;
          s_toque_desvio = 0;
          s_toque_desde = t;
        }
        const int dx = abs(toque.x - s_toque_x0), dy = abs(toque.y - s_toque_y0);
        if (dx > s_toque_desvio) { s_toque_desvio = dx; }
        if (dy > s_toque_desvio) { s_toque_desvio = dy; }
        s_toque_x = toque.x; s_toque_y = toque.y;
        // SEGURAR UM ICONE DA HOME levanta ele para mover (shell.cpp). Com um
        // icone levantado o toque e dele: a soltura nao vira clique nem gesto.
        if (!s_app && !s_app_carregando && !sistema_app_encoberto() && !s_dedo_do_despertar && (t - s_acordou_em) > 500
         && home_segurar(s_toque_x, s_toque_y, t, t - s_toque_desde, s_toque_desvio)) {
          s_toque_consumido = true;
        }
      } else if (s_dedo_na_tela) {
        s_dedo_na_tela = false;
        s_ultimo_toque = t;
        home_soltar();   // grava a ordem, se um icone foi movido
        // ARRASTE NAO E CLIQUE. Sem esta guarda, terminar uma rolagem apertaria
        // o que estivesse embaixo do dedo - e o dedo termina em cima de algo,
        // sempre. rolagem_foi_arraste() e chamada em TODA soltura: ela tambem
        // zera a medida para o proximo toque.
        const bool rolou   = rolagem_foi_arraste(&s_rol);
        const bool arrasto = rolou || s_toque_desvio > TOQUE_TOLERANCIA_PX;
        // Duas guardas, e a primeira e a que resolve: e o mesmo dedo que
        // acordou a tela? A segunda pega o toque acidental logo depois.
        const bool valido = !arrasto && !s_dedo_do_despertar && (t - s_acordou_em) > 500
                         && !s_toque_consumido;
        if (diag_toque_ligado()) {
          ESP_LOGI(TAG, "toque: %lu ms, desvio %d px -> %s", (unsigned long)(t - s_toque_desde),
                   s_toque_desvio, valido ? "CLIQUE" : rolou ? "rolagem" : arrasto ? "arrasto"
                                         : s_toque_consumido ? "consumido (nivel)" : "despertar/carencia");
        }
        // GESTO (arraste lateral na home, deslize para cima no bloqueio). Um
        // arraste vertical de rolagem de app nao chega aqui porque o app so
        // ve gesto com a home aberta; o dedo que acordou a tela nunca conta.
        if (arrasto && !s_dedo_do_despertar && (t - s_acordou_em) > 500 && !s_toque_consumido) {
          tratar_gesto(s_toque_x - s_toque_x0, s_toque_y - s_toque_y0, s_toque_y0);
        }
        if (valido) {
          ETAPA("tratando toque (abrir/fechar app, ao_tocar)");
          tratar_toque(s_toque_x, s_toque_y);
        }
      }

      // BANCADA: o pedido do console, na tarefa certa.
      // Com a navegacao travada, trocar de app por fora tambem e recusado:
      // a trava e do app aberto, nao so do botao INICIO.
      // COM O APP AINDA "ABRINDO..." (montando o cartao) os pedidos ESPERAM a
      // proxima volta - a mesma regra do toque (tratar_toque engole). Atender
      // agora chamaria o ao_fechar() de um app cujo ao_abrir() nunca rodou, e
      // depois concluir_abertura_do_sd() abriria um app que ja saiu da tela.
      if (s_sim_fechar && !s_app_carregando) {
        s_sim_fechar = false; s_ultimo_toque = t;
        if (s_app && s_nav_travada) { ESP_LOGW(TAG, "fechar recusado: '%s' travou a navegacao", s_app->nome); }
        else if (s_app)             { fechar_app(); }
      }
      if (const app_t* ap = (s_app_carregando ? nullptr : s_sim_abrir)) {
        s_sim_abrir = nullptr; s_ultimo_toque = t;
        if (s_app && s_nav_travada) {
          ESP_LOGW(TAG, "abrir '%s' recusado: '%s' travou a navegacao", ap->nome, s_app->nome);
        } else {
          if (s_app) { fechar_app(); }
          abrir_app(ap);
        }
      }
      // APPS DE FORA RELIDOS (o cartao voltou do pendrive): troca os icones
      // so na tela inicial e sem dedo nela - nenhum icone levantado no meio.
      if (!s_app && !s_app_carregando && !s_dedo_na_tela && sistema_apps_de_fora_atender()) {
        home_ordem_carregar();   // a ordem salva; os novos entram no fim
        sistema_pedir_redesenho_completo();
      }
      if (s_simg_pedido && !s_dedo_na_tela) {
        s_simg_pedido = false;
        s_ultimo_toque = t;
        ESP_LOGI(TAG, "arraste simulado %d,%d -> %d,%d", s_simg[0], s_simg[1], s_simg[2], s_simg[3]);
        tratar_gesto(s_simg[2] - s_simg[0], s_simg[3] - s_simg[1], s_simg[1]);
      }
      if (s_sim_x >= 0 && !s_dedo_na_tela) {
        const int sx = s_sim_x, sy = s_sim_y;
        s_sim_x = -1;
        s_ultimo_toque = t;
        ESP_LOGI(TAG, "toque simulado em %d,%d", sx, sy);
        tratar_toque(sx, sy);
      }

      // Fim do toque: a marca de consumido nao passa para o proximo. Tambem
      // limpa a marcada sem toque do reconhecedor em curso (o nivel viu um
      // dedo que o clique nao acompanhava).
      if (!s_dedo_na_tela) { s_toque_consumido = false; }

      // O dedo do despertar so deixa de contar quando SAI da tela. Depois do
      // bloco do clique, de proposito: na passada da soltura o reconhecedor
      // ainda precisa ver a bandeira de pe.
      if (!tocando) { s_dedo_do_despertar = false; }
    }

    if (!tab5_power_em_repouso()) {
      // A tentativa frustrada tem carencia de 5s. Sem isso, um repouso que
      // falha no meio desfaz tudo, devolve false, e 20ms depois a condicao de
      // ociosidade continua verdadeira e ele tenta OUTRA VEZ - um loop de
      // apagar e acender, que e o que faz as barras piscarem.
      const bool pode_tentar = (s_repouso_falhou == 0) || ((t - s_repouso_falhou) > 5000);

      // Pedido de tela acesa acabou de sair: o prazo recomeca agora.
      static uint32_t acordado_antes = 0;
      const uint32_t acordado = s_acordado.load();
      if (acordado_antes && !acordado) { s_ultimo_toque = t; }
      acordado_antes = acordado;
      if ((t - s_ultimo_toque) > ocioso_ms() && pode_tentar && !acordado) {
        // Diz o tempo CONFIGURADO junto: e a unica prova, no log, de que o
        // ajuste de Ajustes esta mesmo chegando aqui.
        ESP_LOGI(TAG, "repouso apos %u s sem toque (ajuste de Ajustes)",
                 (unsigned)ajustes_tela_s());
        // TESTADO: 20MHz quebrou a UART do Mesh (o servico voltou
        // a mostrar "sem modulo" mesmo com o C6L plugado e respondendo antes).
        // De volta a 40. A ideia seguinte - piso baixo SO quando tab5_grove_dono()
        // for GROVE_DONO_NINGUEM, alto quando Mesh/GPS estiver de pe - ainda
        // precisa de teste isolado (ver conversa) antes de entrar
        // aqui: falta saber ate onde o I2C interno (touch/INA226, que rodam
        // TODO repouso, com Grove vazio ou nao) tolera descer.
        // AVISA O APP ANTES DE DESMONTAR A TELA - ver o aviso grande em
        // kernel/app.h junto de ao_repouso/ao_retomar (o crash
        // da camera+repouso). Chamado aqui, ainda com o painel de pe, para o
        // app soltar hardware (DMA/etc) que nao pode conviver com o painel
        // sendo destruido e recriado por tab5_power_repouso() logo abaixo.
        ETAPA("repouso: entrando");
        // O fundo da tela de bloqueio (~1,6 MB na PSRAM) nao precisa dormir
        // junto: o despertar sorteia e pinta outro.
        if (s_bloqueado) { bq_anim_soltar(); }
        // A CAMERA DO APP ABERTO PARA COM A TELA APAGADA - regra do sistema
        // (hal/tab5_camera, tab5_camera_pausar): ninguem ve o preview, e
        // ligada ela seguraria 180 MHz. Vale para app em C e Berry sem gancho
        // nenhum; o navegador (outro consumidor) segue filmando.
        if (app_vivo()) { tab5_camera_pausar(s_app->nome); }
        if (app_vivo() && s_app->ao_repouso) {
          s_app->ao_repouso();
        }
        // O TETO DO CLOCK e fixo (REPOUSO_TETO_MHZ), decidido so aqui, na
        // entrada do repouso; ocioso, o DFS desce a 40 MHz do mesmo jeito.
        // Quem proibe o sono leve sao os recursos em uso (kernel/recursos).
        {
          char r[160];
          recursos_resumo(r, sizeof(r));
          ESP_LOGI(TAG, "repouso: recursos em uso: %s (teto do clock %d MHz)", r, REPOUSO_TETO_MHZ);
        }
        if (!tab5_power_repouso(REPOUSO_TETO_MHZ)) {
          s_repouso_falhou = t;
          s_sujo_layout = true;      // a tela voltou: redesenha o que havia
          // tab5_power_repouso() FALHOU - o painel NAO foi desmontado, entao
          // o app fica de pe com a tela acesa e o hardware que ele acabou de
          // soltar em ao_repouso() (linhas acima) tem que voltar AGORA, senao
          // fica desligado ate o proximo repouso ter sucesso.
          tab5_camera_despausar(app_vivo());
          if (app_vivo() && s_app->ao_retomar) {
            s_app->ao_retomar();
          }
        } else {
          s_repouso_falhou = 0;
          // O USB-A sem ninguem usando nao fica alimentado no escuro (e um
          // aparelho plugado seguraria o sono leve) - services/tab5_usb.
          tab5_usb_repouso(true);
          // LIGA A ISR AQUI - so agora, com a tela de fato apagada, e seguro
          // deixar o toque acordar a queue sem atrapalhar PERIODO_UI/OCIOSO
          // (ver o aviso grande junto de s_toque_isr_ok). zerar a notificacao evita
          // que uma sobra de evento de ANTES de dormir cause um acordar
          // espurio um ciclo depois de entrar aqui.
          if (s_toque_isr_ok) {
            ulTaskNotifyTakeIndexed(UI_AVISO_IDX, pdTRUE, 0);   // zera o que sobrou
            s_isr_acorda = true;
          }
          // SONO LEVE (contrato de economia, app.h): so com o toque provado por
          // interrupcao - sem ela, ninguem tiraria o chip do sono no toque.
          if (s_toque_isr_ok && ajustes_sono_leve()) {
            armar_despertar();
            if (!tab5_power_sono_leve(true)) { desarmar_despertar(); }
          }
        }
      } else {
        // O RELOGIO DO APP e o ritmo de quadro (kernel/ritmo): o periodo
        // pedido, em varreduras inteiras do painel.
        const uint32_t per = periodo_do_app();
        if (per && ritmo_falta_ms() == 0) { s_sujo_dados = true; }

        // AVISOS: o que os servicos disseram que mudou desde a ultima volta.
        // So o que o app declarou querer chega a ele; o resto e descartado
        // (a home atualiza a linha de servicos na hora, por conta propria).
        //
        // ENCOBERTO (bloqueio ou central de avisos), NADA E ENTREGUE: os
        // avisos ficam guardados ate o app voltar a ficar visivel. O app atras da tela de bloqueio NAO esta visivel, e
        // o ao_aviso de quem mostra conversa (ESP-NOW) marca tudo como lido
        // - era o aviso que sumia da barra sem ninguem abrir o app: chegava
        // com a tela apagada, o despertar bloqueava, e o app "lia" por tras.
        // A central aberta envelheceu (um aviso chegou ou foi lido): de novo.
        if (painel_avisos_mudou()) { s_sujo_layout = true; }
        else if (painel_avisos_dados_mudou()) { s_sujo_dados = true; }
        const uint32_t av = sistema_app_encoberto() ? 0 : s_avisos.exchange(0, std::memory_order_relaxed);
        if (av) {
          if (app_vivo()) {
            const uint32_t dele = av & s_app->avisos;
            if (dele) {
              if (s_app->ao_aviso) { s_app->ao_aviso(dele); }
              else                 { s_sujo_dados = true; }
            }
          } else if (!s_app) {
            atualizar_linha_servicos();
          }
        }

        // GIRO DA TELA (kernel/orientacao): o IMU para app que o segue, e o
        // teclado fisico encaixado/retirado para todos (home incluida). Gira
        // ENTRE quadros, aqui, antes do desenho: a passada completa que vem
        // logo abaixo ja sai na orientacao nova. Nunca com dedo na tela (nem
        // logo depois dele): um arraste viraria toque em outro lugar, e a
        // soltura cairia num botao que nao estava ali quando o dedo desceu.
        ETAPA("orientacao (IMU, I2C)");
        // Na home tambem: o teclado fisico deita a home (orientacao.hpp).
        const bool pode_girar = (app_vivo() || !s_app) && !s_bloqueado && !s_painel_avisos
                             && !tocando && !s_dedo_na_tela && (t - s_dedo_ms) > 300;
        if (orientacao_vigiar(t, pode_girar)) {
          rolagem_zerar(&s_rol);            // a rolagem era medida na outra altura
          tela_virtual_invalidar_tudo();
          if (app_vivo() && s_app->ao_girar) { s_app->ao_girar(); }
          s_sujo_layout = true;             // barra, navegacao e app: tudo de novo
        }

        ETAPA("desenho (app / home)");
        const int64_t desenho_t0 = esp_timer_get_time();
        const bool desenho_completo = s_sujo_layout;
        comp_marcar_desenho();
        uint32_t fase_barra = 0, fase_conteudo = 0;
        if (s_sujo_layout) {
          const int64_t f0 = esp_timer_get_time();
          desenhar_barra_status(tela_virtual(), true);
          const int64_t f1 = esp_timer_get_time();
          desenhar_conteudo(true, true);
          fase_barra    = (uint32_t)(f1 - f0);
          fase_conteudo = (uint32_t)(esp_timer_get_time() - f1);
          // A pagina visivel agora reflete exatamente s_rol.atual - qualquer
          // rolagem pendente de antes deste redesenho completo ja saiu junto.
          rolagem_sincronizar(&s_rol);
        } else if (rolagem_pendente(&s_rol)) {
          // ROLAGEM: a posicao mudou, o VALOR de nada mudou. Em vez do
          // caminho de cima (limpar ~1,65MB + trocar de pagina), deslocamos
          // os pixels JA CORRETOS que estao na tela (tab5_tela_rolar - memcpy
          // em faixas, sem apagar nada) e so entao chamamos o app para
          // desenhar por cima - pulando a rajada de apagar e a troca de
          // pagina, mas NAO o "app redesenha tudo".
          //
          // completo_para_apps=true (mesmo sem limpar_area) e proposital:
          // a faixa que o deslocamento acabou de expor NUNCA foi pintada
          // NESTA posicao de rolagem antes - se cada app so olhasse o
          // dirty-tracking por campo (pintura_mudou_i/f/s), estrutura
          // estatica que so desenha quando "e passada completa" (titulo,
          // ajuda, botao - ver app_config) nunca preencheria aquela faixa.
          // Foi exatamente o bug visto ao vivo: rolar para baixo parecia bom
          // (a faixa nova e fina, passava despercebida), voltar para cima
          // expunha de uma vez o rastro de faixas nunca pintadas.
          const area_t a = area_util();
          const rolagem_passada_t p = rolagem_deslocar(&s_rol, a, TFT_BLACK);
          if (p.deslocou) {
            desenhar_conteudo(false, true, &p.exposta);
          } else {
            // Arraste rapido demais para o intervalo entre chamadas (delta
            // maior que a propria area util): nao ha o que reaproveitar, cai
            // para o redesenho completo normal no proximo ciclo.
            s_sujo_layout = true;
          }
        } else if (s_sujo_dados && !s_sujo_layout) {
          desenhar_conteudo(false, false);
        }

        // A barra so e repintada quando algo NELA mudou - a porcentagem, o
        // estado de carga ou o app. Repintar de dois em dois segundos a toa era
        // outra fonte de piscada, so que numa faixa menor.
        if ((t - s_ultima_barra) >= BARRA_MS || recado_pendente()) {
          s_ultima_barra = t;
          if (barra_mudou()) {
            tela_virtual().startWrite();
            desenhar_barra_status(tela_virtual(), false);
            tela_virtual().endWrite();
          }

          // Beep de notificacao + desligar o I2S ocioso - ver o aviso grande
          // em atualizar_beep_e_ociosidade_do_som(), mais acima no arquivo.
          atualizar_beep_e_ociosidade_do_som(t);

          // A linha de servicos da home, no mesmo tique e pela mesma regra -
          // ver atualizar_linha_servicos() (kernel/shell.cpp) para o "so
          // redesenha se o texto mudou".
          atualizar_linha_servicos();
        }
        if (comp_tem_sujeira()) {
          const uint32_t us = (uint32_t)(esp_timer_get_time() - desenho_t0);
          const uint32_t cpu = comp_contar_desenho(us, desenho_completo);
          // QUEM PESA. A passada completa media 90-130 ms - a media nao diz
          // de quem. A tabela por app (e por fase) sai no 'tela' do console.
          if (desenho_completo && fase_conteudo) {
            const uint32_t limpar = s_fase_limpar_us;
            anotar_passada(s_bloqueado ? "bloqueio" : (s_app ? s_app->nome : "home"), cpu,
                           fase_barra, limpar, fase_conteudo > limpar ? fase_conteudo - limpar : 0);
          }
        }
      }
    }

    // Periodo adaptativo: rapido durante o uso, mais espacado quando parado.
    //
    // O teto e "quanto falta para o proximo prazo REAL deste loop" - nao
    // inventa prazo nenhum, so para de acordar entre eles a toa. Os prazos de
    // sempre (sensores e barra) entram aqui, uma vez, para os dois estados.
    uint32_t prazo = UI_ESPERA_MAX_MS;
    auto menor = [&](uint32_t intervalo, uint32_t desde) {
      const uint32_t dec = t - desde;
      const uint32_t falta = (dec < intervalo) ? (intervalo - dec) : 0;
      if (falta < prazo) { prazo = falta; }
    };
    menor(MEDIDA_MS(), s_ultima_medida);   // sensores
    menor(BARRA_RMS(), s_ultima_barra);    // barra de status (em repouso, o beep de aviso)
    uint32_t periodo;
    if (tab5_power_em_repouso()) {
      periodo = s_toque_isr_ok ? prazo : PERIODO_ZZZ;   // sem a ISR do toque, o ritmo fixo de sempre
    }
    else {
      // ACORDADA: RAPIDA SO QUANDO HA MOTIVO. Ate aqui o loop
      // rodava a cada 20-25ms sempre que a tela estava acesa - 40 a 50
      // voltas por segundo, cada uma lendo o toque por I2C, com o aparelho
      // parado na mesa. Agora:
      //   - dedo na tela (ou ha menos de 500ms) ou trabalho pendente: 20ms,
      //     com a interrupcao do toque DESLIGADA (num arraste ela pulsaria a
      //     cada 5-20ms e so atrapalharia);
      //   - parada: a interrupcao LIGA e o loop dorme ate o proximo prazo
      //     real. Um toque, um aviso de servico ou um pedido de redesenho de
      //     outra tarefa acordam na hora (a espera na notificacao, la embaixo).
      const bool dedo = (t - s_ultimo_toque) < 500;
      const bool pendente = s_sujo_layout || s_sujo_dados || rolagem_pendente(&s_rol)
                         || comp_tem_sujeira() || s_app_carregando || s_sd_montando;
      if (dedo || pendente || !s_toque_isr_ok) {
        s_isr_acorda = false;
        periodo = PERIODO_UI;
      } else {
        if (prazo > 1000) { prazo = 1000; }
        menor(ocioso_ms(), s_ultimo_toque);              // hora de apagar a tela
        // (o relogio do app entra abaixo, depois do piso de PERIODO_UI)
        if (s_speaker_ocioso_desde) { menor(SPEAKER_OCIOSO_MS, s_speaker_ocioso_desde); }
        if (tab5_teclado_presente()) { menor(teclado_ms(), s_ultimo_teclado); }   // teclado fisico e consultado
        { const uint32_t falta = orientacao_prazo_ms(t); if (falta < prazo) { prazo = falta; } }   // vigia do giro (so com app que segue o IMU)
        if (s_nav_travada && prazo > TOQUE_FRESCO_MS) { prazo = TOQUE_FRESCO_MS; }   // teclado: o toque sempre fresco
        if (prazo < PERIODO_UI) { prazo = PERIODO_UI; }
        s_isr_acorda = s_toque_isr_ok;
        periodo = prazo;
      }
      // O RELOGIO DO APP fura o piso de PERIODO_UI: o proximo quadro cai
      // logo depois de uma varredura, e esse momento pode estar a menos de
      // 20 ms (e com o dedo na tela tambem - arrastar nao pode travar uma
      // animacao).
      if (periodo_do_app()) {
        uint32_t falta = ritmo_falta_ms();
        if (falta < 1) { falta = 1; }
        if (falta < periodo) { periodo = falta; }
      }
    }

    // NUNCA ALEM DO WATCHDOG: a espera mais a volta cabem no prazo dele, com
    // folga para o sono leve acordar atrasado. Um ritmo novo (sistema_ritmo_ms)
    // que passe disso e cortado aqui, e nao vira reinicio.
    if (periodo > UI_ESPERA_MAX_MS) { periodo = UI_ESPERA_MAX_MS; }

    // UM FRAME POR VOLTA (docs/COMPOSITOR.md, regra 4). Tudo que esta volta
    // desenhou ja esta na pagina de tras (modo direto), e o rastreador anotou
    // onde. Aqui a troca e pedida - sem esperar: quem espera e o proximo
    // quadro, ao abrir. Volta sem desenho = nada a fazer.
    bool apresentou = false;
    if (!tab5_power_em_repouso() && comp_tem_sujeira()) {
      ETAPA("compositor (troca de pagina)");
      apresentou = comp_apresentar();
      if (apresentou) { comp_contar_volta((uint32_t)(esp_timer_get_time() - volta_t0)); }
    }

    // PROVA DE VIDA. Uma volta completa do loop terminou - a tela foi tratada,
    // os apps tiveram a vez deles, nada travou. E o sinal que o watchdog
    // inscrito la em cima espera; sem ele a cada volta, os 10 s do prazo vencem
    // e o aparelho reinicia mesmo funcionando bem.
    esp_task_wdt_reset();
    ++s_voltas;   // escritor unico: esta tarefa
    s_volta_ms = tab5_agora_ms();
    ETAPA("esperando evento");

    // A espera na notificacao (ulTaskNotifyTake) no lugar do vTaskDelay() de sempre - acorda no MESMO
    // prazo calculado acima OU antes, se algo chegar na queue (toque de
    // verdade em repouso, ou sistema_acordar_interface() de um servico). O
    // CONTEUDO e o motivo do retorno sao ignorados de proposito: as
    // proximas linhas do PROXIMO ciclo ja conferem M5.Touch e todas as
    // bandeiras sozinhas, exatamente como sempre fizeram.
    if (apresentou && periodo == PERIODO_UI) {
      // NO RITMO DO PAINEL. Com o dedo na tela (ou trabalho pendente) a
      // volta dormia 20 ms DEPOIS de terminar: periodo real = 20 ms + o
      // desenho, ~30 q/s contra um painel de ~63 Hz. Os dois ritmos nao
      // batem, e de tempos em tempos um quadro fica na tela dois periodos -
      // a rolagem trepida mesmo sem rasgo nenhum.
      //
      // Aqui a volta que apresentou espera a TROCA (o painel comecou a varrer
      // o quadro novo) e recomeca na hora: toque lido logo depois da troca,
      // quadro novo pronto para a seguinte. A espera nao e a mais - o proximo
      // quadro teria de espera-la de qualquer jeito (comp_quadro_imagem,
      // passo 1). Vale SO no modo rapido: parada, a interface continua
      // dormindo ate o proximo prazo, como sempre.
      comp_esperar_troca();
      ulTaskNotifyTakeIndexed(UI_AVISO_IDX, pdTRUE, 0);   // consome um aviso que ja tenha chegado
    } else {
      ulTaskNotifyTakeIndexed(UI_AVISO_IDX, pdTRUE, pdMS_TO_TICKS(periodo));
    }
    // A cada volta, nao no passo de 2 s: o pendrive espera esta resposta
    // para ligar o USB (ver sistema_som_ceder_pinos).
    som_atender_reserva();
    som_atender_fluxo();
  }
}

void sistema_iniciar(void)
{
  //                      corpo           nome  stack param prio handle kernel
  // ajustes_iniciar() ja foi chamado no app_main, ANTES do M5.begin(), para a
  // tela nascer no brilho certo em vez de piscar no padrao e corrigir depois.
  pool_iniciar();         // tarefas fixas dos servicos que ligam/desligam e dos apps - ANTES de todos
  tab5_mem_marco("pool de vagas");
  tab5_sd_iniciar();      // cria o mutex do cartao ANTES de haver concorrencia
  tab5_mem_marco("cartao (tarefa)");
  tab5_log_iniciar();     // DEPOIS do SD (usa tab5_sd_montar/abrir) - guarda os logs no cartao
  tab5_mem_marco("log (tarefa+buffer)");
  idioma_iniciar();       // os .po (embutido e do cartao) - antes do primeiro texto da interface
  tab5_mem_marco("idiomas");
  tab5_monitor_iniciar(); // mede CPU/memoria o tempo todo e registra eventos (services/tab5_monitor)
  tab5_mem_marco("monitor (tarefa)");
  tab5_wifi_iniciar();    // cria a queue e a tarefa; NAO liga o radio
  tab5_mem_marco("wifi (tarefa, sem radio)");
  tab5_espnow_iniciar();  // espera o firmware nosso no C6 (peer_data) e liga o ESP-NOW
  tab5_mem_marco("espnow (tarefa)");
  tab5_ble_iniciar();     // cria a tarefa; NAO liga (ver o loop de interface)
  tab5_mem_marco("ble (tarefa, sem radio)");
  console_iniciar();      // comandos pelo monitor serial (help)
  tab5_mem_marco("console (tarefa)");
  sistema_apps_de_fora_iniciar();   // os apps do cartao (kernel/app.h) - ANTES da home montar a ordem
  tab5_mem_marco("apps de fora");

  // ISR DO TOQUE - ver o aviso grande junto de UI_AVISO_IDX,
  // mais acima no arquivo. Precisa existir ANTES da tarefa de interface
  // subir (ela consome a queue desde a primeira volta do loop) e DEPOIS do
  // M5.begin() (main.cpp) - e ele quem configura o GPIO 23 como entrada para
  // o driver do GT911; esta ISR so ACRESCENTA um tipo de interrupcao aquele
  // pino, nunca muda direcao/pull.
  {
    esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
      // INVALID_STATE = outro modulo ja instalou o servico global de ISR
      // antes de chegarmos aqui (o candidato mais provavel e o bring-up do
      // WiFi/C6 pelo esp_hosted, que faz o mesmo em eh_host_port_gpio.c) -
      // normal e seguro, so precisamos que ALGUEM tenha instalado, nao
      // importa quem.
      // No GT911 o proprio driver ja deixou o pino como entrada. Nos TDDI
      // ninguem configurou: entrada com pull-up (linha de interrupcao em
      // dreno aberto, ativa em baixo - sem dedo ela fica em repouso alto).
      if (!tab5_painel_toque_gt911()) {
        gpio_set_direction(GPIO_NUM_23, GPIO_MODE_INPUT);
        gpio_set_pull_mode(GPIO_NUM_23, GPIO_PULLUP_ONLY);
      }
      if (gpio_set_intr_type(GPIO_NUM_23, GPIO_INTR_ANYEDGE) == ESP_OK &&
          gpio_isr_handler_add(GPIO_NUM_23, isr_toque, nullptr) == ESP_OK) {
        // LIGADA DE VEZ: ela anota todo pulso (s_toque_pulso). Quem decide se
        // ela tambem acorda o loop e s_isr_acorda (repouso / tela parada).
        s_toque_isr_instalada = true;
        s_toque_isr_ok = tab5_painel_toque_gt911();
      }
    }
  }
  if (!s_toque_isr_instalada) {
    ESP_LOGW(TAG, "ISR de toque nao subiu - repouso usa o PERIODO_ZZZ fixo de sempre");
  } else if (!s_toque_isr_ok) {
    ESP_LOGI(TAG, "toque da tela %s: interrupcao so depois de provar o pino (validar_isr_tddi)",
             tab5_painel_nome());
  }
  // ISR DO TECLADO (ver isr_teclado). O G50 nao e de mais ninguem: entrada com
  // pull-up, borda de descida. Se nao subir, o teclado ainda acorda a tela -
  // so que no proximo prazo do repouso, pela consulta da fila (ate 5 s com a
  // ISR do toque ok - sistema_ritmo_ms -, ou 80 ms sem ela).
  {
    gpio_config_t c = {};
    c.pin_bit_mask = 1ULL << TAB5_TECLADO_INT;
    c.mode = GPIO_MODE_INPUT;
    c.pull_up_en = GPIO_PULLUP_ENABLE;
    c.intr_type = GPIO_INTR_NEGEDGE;
    s_tecla_isr_ok = gpio_config(&c) == ESP_OK
                  && gpio_isr_handler_add((gpio_num_t)TAB5_TECLADO_INT, isr_teclado, nullptr) == ESP_OK;
    if (!s_tecla_isr_ok) { ESP_LOGW(TAG, "ISR do teclado (G%d) nao subiu - ele acorda a tela pela consulta de 1 s", TAB5_TECLADO_INT); }
  }

  // Handle CAPTURADO, nao NULL - sistema_camera_iniciar()/parar() (ver o
  // aviso grande em app.h) precisam saber se quem as chamou JA E esta
  // tarefa, para nao fazer ponte com elas mesmas (deadlock).
  home_ordem_carregar();   // a ordem dos icones do usuario (NVS), antes do primeiro desenho
  // PRIMEIRO USO: o assistente de boas-vindas abre sozinho (kernel/boas_vindas).
  // So num aparelho que nunca rodou o sistema - ver ajustes_config_inicial().
  if (!ajustes_config_inicial()) {
    extern const app_t APP_BOAS_VINDAS;
    sistema_simular_abrir(&APP_BOAS_VINDAS);
  }
  // O boot tambem e "a tela ligou" - a mesma regra do despertar. Aqui, antes
  // da tarefa de interface existir: o primeiro desenho ja sai bloqueado.
  bloqueio_ao_ligar_tela();
  s_relogio_feito = xSemaphoreCreateBinary();   // antes da interface: ela da, o console espera
  s_som_parou     = xSemaphoreCreateBinary();   // idem: ela da, o pendrive espera
  s_fluxo_pronto  = xSemaphoreCreateBinary();   // idem: ela da, o fundo da musica espera
  som_configurar();   // antes de qualquer M5.Speaker.begin()
  xTaskCreatePinnedToCore(vTaskInterface, "ui", 10240, NULL,   4,  &s_tarefa_interface, 1);
  registro_iniciar();
  tab5_mem_marco("ui + registro (tarefas)");
  ESP_LOGI(TAG, "interface no ar, %d aplicativo(s) registrado(s)", SISTEMA_NUM_APPS);
}


// Ver ETAPA no topo do arquivo. Leitura de outra tarefa, sem trava: palavras
// soltas, e um valor um passo atrasado nao muda o diagnostico.
void sistema_diag_interface(sistema_diag_t* d)
{
  const uint32_t agora = tab5_agora_ms();
  d->etapa          = (const char*)s_etapa;
  d->voltas         = s_voltas;
  d->ms_desde_volta = s_volta_ms ? agora - s_volta_ms : 0;
  d->ms_desde_dedo  = s_dedo_ms ? agora - s_dedo_ms : UINT32_MAX;
  d->toque_pulsos   = s_pulsos;
  d->toque_falhas   = s_vig_falhas;
  d->toque_presos   = s_vig_presos;
  d->toque_surdos   = s_vig_surdos;
  d->toque_reinicios = s_vig_reinicios;
  d->toque_status   = s_vig_status;
  d->repouso        = tab5_power_em_repouso();
  d->app            = s_app ? s_app->nome : nullptr;
}
