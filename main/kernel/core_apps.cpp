// CORE - os apps: abrir e abrir pelo nome, fechar, a montagem do cartao SD em
// tarefa propria, o app recusado (cartao emprestado ao computador) e a
// conclusao da abertura. Partido de core.cpp (o motor); o que as partes
// compartilham esta em core_interno.hpp.

#include "tab5_sd.hpp"
#include "rolagem.hpp"
#include "tela_virtual.hpp"
#include "orientacao.hpp"
#include "trabalho.hpp"
#include "pool_tarefas.h"
#include "registro.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "core_interno.hpp"

static const char *TAG = "core";

bool sistema_abrir_app(const char* nome)
{
  if (!nome) { return false; }
  for (int i = 0; i < SISTEMA_NUM_APPS; ++i) {
    if (!strcmp(SISTEMA_APPS[i]->nome, nome)) { s_sim_abrir = SISTEMA_APPS[i]; return true; }
  }
  return false;
}


bool sistema_app_exige_sd(void)
{
  const app_t* a = s_app;
  return a && a != s_app_recusado && a->exige_sd;
}

// Se o cartao foi REALMENTE montado para este app. Sem isto, um app que pediu
// SD mas cuja montagem falhou ainda assim chamava desmontar() ao fechar -
// decrementando uma contagem que nunca foi incrementada. Se o registro de
// energia estivesse com o cartao montado naquele instante, o contador caia a
// zero e o cartao era DESMONTADO POR BAIXO DELE.
static bool s_app_montou_sd = false;

// App cuja montagem de cartao SD esta rodando em tarefa de fundo (ver
// vTaskMontarSDApp mais abaixo). NULL quando nao ha abertura pendente. A
// abertura de verdade - chamar ao_abrir() e liberar a tela para o app - so
// acontece quando esta bandeira volta a NULL.
const app_t* s_app_carregando = nullptr;   // extern em internal.hpp - shell.cpp le em tratar_toque
const app_t* s_app_recusado   = nullptr;   // idem - ver recusar_app()
volatile bool s_sd_montando  = false;  // true do disparo ate a tarefa terminar
static bool          s_sd_montou_ok = false;  // resultado da ultima montagem de fundo

// O app em primeiro plano segura uma referencia do cartao (app_t.usa_sd), ou
// esta abrindo e montando. Quem e dono da referencia e o kernel: o pendrive
// pergunta aqui para recusar na hora, com o motivo certo. A leitura vem de
// outra tarefa e nao e atomica - so serve para escolher a mensagem; quem
// decide de verdade e a contagem do tab5_sd_emprestar.
bool sistema_app_segura_sd(void)
{
  if (s_app_carregando || s_sd_montando) { return true; }
  const app_t* a = s_app;
  return a && a != s_app_recusado && s_app_montou_sd;
}
// ---------------------------------------------------------------------------
// Ciclo de vida dos apps
// ---------------------------------------------------------------------------
// Declarada aqui e definida mais abaixo (ver o bloco "MONTAGEM (E DESMONTAGEM)
// DE SD EM TAREFA PROPRIA"): fechar_app() precisa dela antes de abrir_app()
// ser lido, e a definicao mora perto da irma vTaskMontarSDApp por clareza.
static void vTaskDesmontarSDApp(void*);

// NAO 'static' - declarada em kernel/internal.hpp, chamada por
// kernel/shell.cpp (tratar_toque, ao fechar o app pelo botao INICIO).
void fechar_app(void)
{
  if (!s_app) { return; }
  ESP_LOGI(TAG, "fechando '%s'", s_app->nome);

  // RECUSADO: o app nunca abriu (nem ao_abrir, nem perfil, nem cartao) - so
  // a tela de motivo do sistema sai.
  if (s_app == s_app_recusado) {
    s_app_recusado = nullptr;
    s_app = nullptr;
    rolagem_zerar(&s_rol);
    s_sujo_layout = true;
    return;
  }

  const bool devolver_sd = s_app_montou_sd;

  // O TRABALHO DO APP TERMINA ANTES DO ao_fechar(): a queue e descartada e o
  // que estiver executando e esperado - so entao o app pode liberar os
  // buffers em que o worker escreve (kernel/trabalho.hpp).
  // FECHAR E TELA PARADA: tudo daqui ate o fim roda na tarefa de interface.
  // Passou de 150 ms, o log diz quem segurou - o trabalho ou o ao_fechar().
  const int64_t t0 = esp_timer_get_time();
  trabalho_encerrar_do_app();
  const int64_t t1 = esp_timer_get_time();
  if (s_app->ao_fechar) { s_app->ao_fechar(); }
  tela_virtual_soltar_superficie();   // a camera ja parou no ao_fechar()
  const int64_t t2 = esp_timer_get_time();
  if (t2 - t0 > 150000) {
    ESP_LOGW(TAG, "fechar '%s' segurou a interface %lld ms (trabalho %lld, ao_fechar %lld)",
             s_app->nome, (t2 - t0) / 1000, (t1 - t0) / 1000, (t2 - t1) / 1000);
  }
  s_nav_travada     = false;  // rede de seguranca: app esquecido nao prende ninguem
  sistema_manter_acordado(SISTEMA_ACORDADO_APP, false);   // idem: o pedido era do app
  rolagem_zerar(&s_rol);
  s_app = nullptr;

  // A home e sempre em pe. Restaurar aqui, e nao no proximo abrir, para que a
  // tela inicial nunca apareca deitada por um frame. Vale tambem para o app
  // que seguia o IMU e abriu em pe mas fechou deitado.
  orientacao_ao_fechar();

  // Desmonta DEPOIS do ao_fechar(): o app pode querer fechar arquivos ali.
  // So devolve se ESTE app tiver de fato tomado.
  //
  // EM TAREFA DE FUNDO, fire-and-forget: nada daqui em diante espera a
  // desmontagem terminar, e o mutex + contagem de referencia dentro de
  // tab5_sd.cpp (ver trava()/destrava()/s_refs) ja protegem contra a corrida
  // obvia - abrir outro app com SD antes desta tarefa rodar so incrementa a
  // referencia sobre o cartao que ainda esta montado, em vez de montar de novo.
  if (devolver_sd) {
    pool_criar(nullptr, vTaskDesmontarSDApp, nullptr, "sd_desmonta", 4096, 1);
    s_app_montou_sd = false;
  }
  registro_app_atual(nullptr);

  tab5_power_aplicar(perfil_da_home());
  s_sujo_layout = true;
}

// ---------------------------------------------------------------------------
// MONTAGEM (E DESMONTAGEM) DE SD EM TAREFA PROPRIA
// ---------------------------------------------------------------------------
// Tab5_sd_montar()/desmontar() rodavam DIRETO na tarefa de
// interface, dentro de abrir_app()/fechar_app() - a MESMA tarefa que chama
// M5.update() e processa toque. montar_de_verdade() (tab5_sd.cpp) liga o LDO
// da IO do cartao, negocia o host SDMMC e monta o FAT: nada catastrofico como
// os ~10s do Grove, mas a mesma classe de erro - hardware de verdade demorando
// dezenas a centenas de ms dentro da tarefa que precisa continuar lendo o
// dedo. Um cartao lento para responder bastava para o mesmo sintoma do Grove
// aparecer em escala menor.
//
// A DESMONTAGEM e simples: fecha o app, some da tela, ninguem mais espera por
// ela. Vai para uma tarefa solta (fire-and-forget) sem coordenacao nenhuma - o
// mutex e a contagem de referencia dentro de tab5_sd.cpp (ver trava()/
// destrava()/s_refs) ja garantem que montar() e desmontar() concorrentes de
// tarefas diferentes nao se atropelam, entao nao ha corrida a resolver aqui.
//
// A MONTAGEM e mais delicada: ao_abrir() de apps como Arquivos e Mesh presume
// que o cartao ja esta pronto (listam pasta, leem historico) na primeira
// chamada. Nao da para simplesmente disparar e seguir chamando ao_abrir()
// como se nada tivesse mudado - a abertura do app precisa esperar o resultado
// da montagem antes de chamar ao_abrir(). Por isso abrir_app() trava a tela no
// app (s_app = app, tela mostra "abrindo...") mas SO libera o app de verdade -
// chamando ao_abrir() - quando a tarefa de fundo termina. Ver
// concluir_abertura_do_sd() e a checagem no loop da interface.
static void vTaskMontarSDApp(void*)
{
  s_sd_montou_ok = tab5_sd_montar();
  s_sd_montando  = false;   // POR ULTIMO - so agora o loop pode concluir a abertura
}

static void vTaskDesmontarSDApp(void*)
{
  tab5_sd_desmontar();
}

// ---------------------------------------------------------------------------
// APP RECUSADO PELO SISTEMA
// ---------------------------------------------------------------------------
// Um app que EXIGE o cartao (app_t.exige_sd: Arquivos, Camera) nao abre com
// ele emprestado ao computador (modo pendrive) - e a regra e do sistema, nao
// de cada app. No lugar do app aparece uma tela do sistema dizendo por que;
// INICIO volta normalmente. Se o cartao voltar com ela na tela, o app abre
// sozinho (loop da interface).
//
// s_app aponta para o app (a barra mostra o nome, INICIO funciona), mas
// nenhuma funcao dele e chamada - app_vivo() e falso.
static void recusar_app(const app_t* app)
{
  ESP_LOGW(TAG, "'%s' nao abre: o cartao esta emprestado ao computador", app->nome);
  s_app = app;
  s_app_recusado = app;
  rolagem_zerar(&s_rol);
  orientacao_ao_fechar();                 // a tela de motivo e em pe
  tab5_power_aplicar(perfil_da_home());   // nada do perfil do app fica ligado
  s_sujo_layout = true;
}

// Segunda metade de abrir_app() quando o app usa SD: chamada pelo loop da
// interface assim que a montagem de fundo termina (ok ou nao). Espelha
// exatamente o que abrir_app() fazia embutido, so que depois da espera.
void concluir_abertura_do_sd(void)
{
  const app_t* app = s_app_carregando;
  s_app_carregando = nullptr;

  s_app_montou_sd = s_sd_montou_ok;
  // CORRIDA COM O PENDRIVE: o cartao foi emprestado entre a checagem do
  // abrir_app() e a montagem. Mesma regra: quem exige o cartao nao abre.
  if (!s_app_montou_sd && app->exige_sd && tab5_sd_emprestado()) {
    recusar_app(app);
    return;
  }
  if (!s_app_montou_sd) {
    ESP_LOGW(TAG, "'%s' pediu cartao SD e ele NAO montou - abrindo assim mesmo, "
                  "mas o app nao vai conseguir gravar", app->nome);
  }

  if (app->ao_abrir && !app->ao_abrir()) {
    ESP_LOGE(TAG, "'%s' recusou abrir", app->nome);
    if (s_app_montou_sd) {
      pool_criar(nullptr, vTaskDesmontarSDApp, nullptr, "sd_desmonta", 4096, 1);
      s_app_montou_sd = false;
    }
    s_app = nullptr;      // volta para a home: a abertura falhou de verdade
    tab5_power_aplicar(perfil_da_home());
    s_sujo_layout = true;
    return;
  }

  registro_app_atual(app->nome);
  s_sujo_layout = true;   // troca a tela de "abrindo..." pelo conteudo de verdade
}

// NAO 'static' - declarada em kernel/internal.hpp, chamada por
// kernel/shell.cpp (tratar_toque, ao tocar um icone da home).
bool abrir_app(const app_t* app)
{
  if (s_app) { fechar_app(); }
  ESP_LOGI(TAG, "abrindo '%s'", app->nome);
  // O DEDO QUE ABRIU O APP NAO E TOQUE NELE. O app abre na soltura, mas o
  // GT911 ainda diz "pressionado" por duas leituras, no lugar do icone: o
  // Ajustes recebia isso como arraste na coluna da esquerda e mexia no volume.
  // A mesma regra do dedo que acende a tela - vale ate nenhum dedo na tela.
  s_dedo_do_despertar = true;

  // O CARTAO ESTA NO COMPUTADOR e este app nao funciona sem ele: o sistema
  // nao abre (ver recusar_app). Antes de qualquer outra coisa - nem perfil,
  // nem orientacao, nem montagem.
  if (app->exige_sd && tab5_sd_emprestado()) {
    recusar_app(app);
    return true;
  }

  // A ordem importa: liga o que o app precisa ANTES de ele iniciar.
  //
  // E o BRILHO NAO VEM DO APP. O perfil do app diz que hardware ele precisa;
  // quao clara a tela fica e preferencia de quem segura o aparelho. Copiamos o
  // perfil e sobrescrevemos - assim o app_t continua declarando o campo (o
  // tab5_power e autocontido e usa ele), mas quem manda e o ajuste.
  // O MESMO VALE PARA O RADIO, e esta linha nasceu de um bug caro.
  //
  // Todo app declara radio=false (o padrao), entao abrir QUALQUER app cortava o
  // WLAN_PWR_EN e matava o ESP32-C6 com o WiFi rodando. O log nao
  // deixa duvida:
  //
  //   sistema: abrindo 'Ajustes'
  //   tab5pwr: trilho do C6 (WLAN_PWR_EN) = 0
  //   sdmmc_send_cmd returned 0x108        <- 1ms depois
  //   eh_sdio: Unrecoverable host sdio state  -> reinicio da placa
  //
  // A regra geral: o perfil do app diz o que ELE precisa ligado, nunca o que os
  // outros podem desligar. Recurso de SERVICO do sistema - brilho, radio - e
  // decidido aqui, e o app so pode SOMAR, nunca subtrair.
  // Os DOIS caminhos precisam da regra, e por isso ela mora tambem em
  // perfil_da_home(): fechar um app tambem reaplica perfil, e um WiFi que
  // sobrevive a abrir o app Ajustes mas morre ao fechar seria pior que o bug antigo -
  // falharia so as vezes.
  // Orientacao ANTES do perfil e de qualquer desenho: girar muda largura e
  // altura, e tudo que for calculado a partir delas precisa ver o valor novo.
  // Quais rotacoes existem e por que: kernel/orientacao.cpp.
  orientacao_ao_abrir(app);

  tab5_perfil_t perfil = app->perfil;
  sistema_impor_preferencias(perfil);
  tab5_power_aplicar(perfil);

  // Cartao montado antes do ao_abrir(), pelo mesmo motivo de sempre: o app
  // pode querer ler configuracao ou abrir arquivo de log logo na abertura.
  //
  // A MONTAGEM EM SI NAO RODA MAIS AQUI (ver o bloco grande acima,
  // "MONTAGEM (E DESMONTAGEM) DE SD EM TAREFA PROPRIA"). Travamos a tela
  // neste app agora - por isso s_app ja e atribuido nesta funcao, antes de
  // ao_abrir() ter sido chamado - e devolvemos true: a home some, a barra de
  // navegacao mostra "INICIO" (a checagem de app aberto so olha s_app) e
  // desenhar_conteudo() mostra "abrindo..." enquanto s_app_carregando apontar
  // para este app. O toque fica engolido nesse meio-tempo (ver tratar_toque,
  // kernel/shell.cpp), por poucos frames na pratica - a mesma folga que o
  // cartao levava para montar, so que agora fora da tarefa que le o dedo.
  //
  // Falhar em montar NAO impede o app de abrir - ver concluir_abertura_do_sd().
  // Sem cartao, o Azimutal ainda desenha o mapa; ele so nao grava.
  s_app_montou_sd = false;
  if (app->usa_sd) {
    s_app_carregando = app;
    s_sd_montando    = true;
    pool_criar(nullptr, vTaskMontarSDApp, nullptr, "sd_monta", 4096, 1);

    s_app = app;
    rolagem_zerar(&s_rol);
    s_sujo_layout = true;
    return true;   // a abertura conclui em concluir_abertura_do_sd(), no loop
  }

  if (app->ao_abrir && !app->ao_abrir()) {
    ESP_LOGE(TAG, "'%s' recusou abrir", app->nome);
    tab5_power_aplicar(perfil_da_home());
    return false;
  }

  s_app = app;
  rolagem_zerar(&s_rol);   // todo app comeca no topo
  registro_app_atual(app->nome);
  s_sujo_layout = true;
  return true;
}
