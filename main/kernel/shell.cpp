// O "shell" do M5tabOS: barra de status, tela inicial (launcher), roteamento
// de toque para o app certo, e a calibracao do toque.
//
// -------------------------------------------------------------------------
// ETAPA 1 do docs/ARQUITETURA.md - metade do SHELL (aparencia)
// -------------------------------------------------------------------------
// Ate aqui isto vivia dentro de kernel/sistema.cpp, junto do MOTOR (agenda
// de desenho, ciclo de vida dos apps, politica de energia) - 2457 linhas
// fazendo oito coisas diferentes. Partido em dois, sem mudar uma linha de
// logica (a unica excecao esta documentada em kernel/internal.hpp, junto da
// troca area_util() -> sistema_area()): core.cpp e o motor, este arquivo e
// a aparencia.
//
// "shell" no sentido de sistema operacional (Windows Shell, GNOME Shell): a
// camada visual em volta dos apps. NAO tem relacao com console.cpp (que e um
// shell de LINHA DE COMANDO, outro conceito, outro arquivo).
//
// UMA tarefa so manda no display e no I2C, de proposito - ver o aviso
// identico em core.cpp. Os apps nao desenham das proprias tarefas; a funcao
// desenhar() de cada um e chamada pelo motor, que mora no outro arquivo.
//
// -------------------------------------------------------------------------
// PARTIDO POR ASSUNTO (so mudou de arquivo, nada de logica)
// -------------------------------------------------------------------------
// Este arquivo ficou com a geometria do toque, a correcao vertical, o
// diagnostico de toque, os gestos, o splash de boot e tratar_toque() (o
// roteamento do toque). O resto foi para kernel/shell_*.cpp; o que as partes
// compartilham esta em kernel/shell_interno.hpp:
//   shell_barra.cpp     icone de bateria, barra de status, hora, faixa de baixo da home, botao da navegacao
//   shell_home.cpp      paginas da home, ordem dos icones, mover/remover icone, desenho da home
//   shell_bloqueio.cpp  tela de bloqueio
//   shell_avisos.cpp    central de avisos e miniplayer

#include "app.h"
#include "shell_interno.hpp"
#include "tela_virtual.hpp"
#include "tab5_tela.hpp"
#include "tab5_ui.hpp"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "shell";

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// CORRECAO VERTICAL DO TOQUE
// ---------------------------------------------------------------------------
// O painel capacitivo devolve o CENTROIDE da area de contato, e o dedo nao
// toca a tela com a ponta que voce ve: ele toca com a polpa, alguns milimetros
// abaixo. O olho mira na borda visivel, o sensor mede o meio da mancha - e a
// leitura sai sistematicamente ABAIXO de onde voce achou que tocou.
//
// O sintoma e sempre o mesmo e foi exatamente o observado: "tenho que tocar mais
// para cima para acertar o botao". Nao e imprecisao do painel; e a diferenca
// entre onde voce mira e onde voce encosta.
//
// A 294 DPI deste painel, 10px sao ~0,85mm - dentro da faixa que os sistemas
// operacionais aplicam para a mesma coisa. Nao e chute cego: o comando `toque`
// do console mede o desvio real, e este numero existe para ser ajustado com
// aquele dado, nao com impressao.
//
// Fica no SHELL e nao nos apps de proposito: e propriedade do painel e do
// dedo, igual para todo mundo. Cada app corrigindo por conta seria oito
// correcoes diferentes para uma causa so. O valor (TOQUE_DY) mora em
// kernel/internal.hpp: o toque por nivel, em core.cpp, aplica o mesmo.

// Diagnostico do toque, ligado pelo console. Imprime onde o dedo caiu, cru e
// ja convertido - a unica forma de trocar "parece impreciso" por um numero.
static bool s_diag_toque = false;
bool diag_toque_ligado(void) { return s_diag_toque; }
void sistema_diag_toque(bool ligar)
{
  s_diag_toque = ligar;
  ESP_LOGI(TAG, "diagnostico de toque %s (correcao atual: %d px)",
           ligar ? "LIGADO" : "desligado", TOQUE_DY);
}

// Travado: desenhado apagado na barra de navegacao, para o olho saber que
// nao adianta tocar. Ver sistema_travar_navegacao() logo abaixo.
//
// NAO 'static' - core.cpp tambem escreve false direto em fechar_app() (rede
// de seguranca: um app fechado nao pode deixar a navegacao travada para o
// proximo). Declarada extern em kernel/internal.hpp.
bool s_nav_travada = false;
void sistema_travar_navegacao(bool t)
{
  s_nav_travada = t;
  if (t && s_painel_avisos) { painel_avisos_abrir(false); }   // aberta antes da trava: fecha
  s_sujo_layout = true;
}

// ---------------------------------------------------------------------------
// GESTOS
// ---------------------------------------------------------------------------
static const int GESTO_PAGINA_PX     = 120;   // arraste lateral minimo para trocar de pagina
static const int GESTO_DESBLOQUEIO_PX = 220;  // deslize para cima minimo para desbloquear

void tratar_gesto(int dx, int dy, int y0)
{
  if (s_bloqueado) {
    if (dy < -GESTO_DESBLOQUEIO_PX && abs(dy) > 2 * abs(dx)) { desbloquear_tela(); }
    return;
  }
  if (s_painel_avisos) {
    if (abs(dy) > 2 * abs(dx) && abs(dy) > 40 && painel_avisos_rolar(y0 - TOQUE_DY, dy)) { return; }
    if (dy < -GESTO_PAGINA_PX && abs(dy) > 2 * abs(dx)) { painel_avisos_abrir(false); }
    return;
  }
  if (s_app) { return; }   // dentro de um app o arraste e dele
  if (abs(dx) > GESTO_PAGINA_PX && abs(dx) > 2 * abs(dy)) {
    home_mudar_pagina(dx < 0 ? +1 : -1);   // arrastar para a esquerda avanca
  }
}

// ---------------------------------------------------------------------------
// SPLASH DE BOOT - "M5Tab OS" grande no meio da tela.
// ---------------------------------------------------------------------------
// #ifdef DE PROPOSITO, nao uma preferencia em runtime:
// durante o desenvolvimento normal a gente reinicia o aparelho o tempo todo
// para testar uma mudanca, e um splash de 1,4s a cada boot vira atrito puro
// sem agregar nada - o usuario ja SABE que e o M5tabOS. A chave mora em
// kernel/internal.hpp (TAB5_MOSTRAR_SPLASH) porque core.cpp precisa ver a
// MESMA definicao para decidir se chama esta funcao.
//
// MORA NO SHELL, e a CHAMADA mora em core.cpp, depois de
// tab5_power_relogio_dpi() - defeito observado: o splash piscava e sumia
// antes dos apps. Causa: desenhar antes daquela chamada
// significa desenhar antes do painel assumir o relogio final, e a troca de
// relogio DESMONTA E REMONTA o DSI, apagando o que estiver na tela.
#ifdef TAB5_MOSTRAR_SPLASH
void desenhar_splash(void)
{
  auto& d = M5.Display;
  d.fillScreen(TFT_BLACK);

  // FONTE PROPRIA DO SPLASH, DE PROPOSITO DIFERENTE da tab5_fonte_titulo()
  // atual.
  // TODO titulo do sistema (barra, rotulo da home, secao
  // dentro de um app) era a Orbitron_Light_32; naquele dia ela foi trocada
  // por FreeMonoBold12pt7b em tab5_fonte_titulo() (ver o comentario grande
  // em tab5_ui.hpp) e passou a valer em todo lugar - MENOS aqui: o splash
  // continua com a Orbitron de proposito, e por isso NAO chama
  // tab5_fonte_titulo() como o resto do sistema.
  //
  // TAMANHO 2 (dobro do nativo), NAO um valor fracionario tipo 1.5 - pedido
  // foi "uns 150% maior" (ou seja, 2,5x), mas fonte bitmap-por-
  // glifo como esta so pode escalar por MULTIPLO INTEIRO sem descartar/
  // duplicar linha de pixel de um jeito irregular (ver o aviso grande sobre
  // isso em tab5_ui.hpp - foi exatamente esse defeito que fez "CONFIG" na
  // barra parecer outra fonte). Entre o 1x de antes (pequeno demais, motivo
  // deste pedido) e o 3x (passaria de 500px so no "M5Tab OS", too largo para
  // o pedido de "uns 150%"), 2x e o degrau limpo mais proximo.
  d.setFont(&fonts::Orbitron_Light_32);
  d.setTextSize(2);
  d.setTextDatum(middle_center);

  // NEGRITO FALSO, - a Orbitron_Light e
  // "light" por DESENHO (ver o aviso ali em cima), e a biblioteca so tem essa
  // variante dela (nenhuma bold, em nenhum tamanho) - nao ha fonte pronta
  // mais grossa para trocar.
  //
  // SEM COR DE FUNDO aqui (so um argumento em setTextColor = so estampa
  // pixel branco, nunca apaga o que ja esta atras) - a tela inteira ja
  // nasceu preta no fillScreen() ali em cima, entao nao falta nada para
  // apagar. Desenhando a MESMA string 5x, deslocada 1px para cada lado
  // (cruz: centro + cima/baixo/esquerda/direita), cada copia so ACRESCENTA
  // pixel branco - o traco engorda em vez de borrar.
  //
  // NO TAMANHO 1x (nativo) isto borraria em vez de engordar - foi o achado
  // documentado na Orbitron da barra de status (tab5_ui.hpp): la, 1px de
  // tela E um pixel de fonte INTEIRO, entao deslocar por 1 e pular para o
  // pixel vizinho, nao dividir o mesmo. Aqui a fonte ja esta em 2x: 1px de
  // tela e so METADE de um pixel de fonte, fino o bastante para engrossar a
  // borda sem deslocar o desenho inteiro para um vizinho visivel.
  const int cx = d.width() / 2, cy = d.height() / 2;
  d.setTextColor(TFT_WHITE);
  d.drawString("M5Tab OS", cx,     cy);
  d.drawString("M5Tab OS", cx - 1, cy);
  d.drawString("M5Tab OS", cx + 1, cy);
  d.drawString("M5Tab OS", cx,     cy - 1);
  d.drawString("M5Tab OS", cx,     cy + 1);

  // Devolve o estado global do texto - MESMA pegadinha do datum/fonte vazando
  // documentada em tab5_botao()/tab5_ui.hpp. O loop principal de core.cpp,
  // logo depois, conta com top_left e a fonte de corpo, como sempre.
  d.setTextDatum(top_left);
  tab5_fonte_corpo(d);

  vTaskDelay(pdMS_TO_TICKS(1400));
}
#endif

// ---------------------------------------------------------------------------
// Toque
// ---------------------------------------------------------------------------
void tratar_toque(int tx, int ty)
{
  // AINDA MONTANDO O SD deste app: engole o toque. Nem a navegacao ("INICIO")
  // nem o conteudo existem de verdade ainda - ao_abrir() so roda quando
  // concluir_abertura_do_sd() (core_apps.cpp) liberar. Dura poucos frames na pratica.
  if (s_app_carregando) { return; }
  // Bloqueado: so o deslize (tratar_gesto) desbloqueia; toque simples nao faz nada.
  if (s_bloqueado) { return; }

  // sistema_area(), e nao a funcao privada area_util() de core.cpp - a mesma
  // geometria, so pela porta publica (ver o aviso em kernel/internal.hpp).
  area_t a = sistema_area();

  // Correcao aplicada UMA vez, na entrada. Daqui para baixo - barra, navegacao,
  // icones da home, apps - todo mundo ja recebe o ponto corrigido, e nenhum
  // deles precisa saber que a correcao existe.
  const int ty_cru = ty;
  ty -= TOQUE_DY;

  // CENTRAL DE AVISOS: aberta, o toque e todo dela (inclusive na barra e na
  // navegacao - qualquer toque fora de uma linha so fecha). Fechada, tocar a
  // barra de status a abre.
  if (s_painel_avisos) { painel_avisos_tocar(a, tx, ty); return; }
  // NAVEGACAO TRAVADA vale para a barra tambem: uma linha da central abre
  // outro app, e isso fecharia este por um caminho que a trava existe para
  // impedir (o Pendrive, com o disco montado no computador).
  if (ty < ALTURA_BARRA) { if (!s_nav_travada) { painel_avisos_abrir(true); } return; }

  if (s_diag_toque) {
    // (a barra de status ja voltou acima: aqui so sobra navegacao ou area)
    ESP_LOGI(TAG, "toque: cru (%d,%d) -> corrigido (%d,%d) -> app (%d,%d)%s",
             tx, ty_cru, tx, ty, tx - a.x, ty - a.y + s_rol.atual,
             (ty >= M5.Display.height() - ALTURA_NAV) ? "  [navegacao]" : "");
  }

  if (ty >= M5.Display.height() - ALTURA_NAV) {
    // SO A REGIAO DO BOTAO fecha o app agora.
    //
    // ATE AQUI, qualquer toque na faixa de navegacao inteira (96px de
    // altura, a LARGURA TODA da tela) fechava o app - so o Y era checado
    // acima, nunca o X. Na pratica isso nunca incomodou porque nao havia
    // mais nada alem do botao naquela faixa para tocar por engano - mas era
    // uma area de toque bem maior que a pilula desenhada, e abre espaco para
    // colocar informacao NAO tocavel do lado dela sem essa informacao
    // roubar o toque.
    //
    // O retangulo abaixo e EXATAMENTE o que desenhar_barra_nav() pinta -
    // NAV_BTN_L/NAV_BTN_A/NAV_BTN_Y0, ver o aviso grande junto delas.
    const int nav_y0 = M5.Display.height() - ALTURA_NAV;
    const int L = M5.Display.width();
    //
    // AREA DE TOQUE MAIOR QUE O DESENHO. A pilula encolheu com a
    // faixa (72px) e ficou justa para o dedo - errar por 10px acima ou abaixo
    // dela nao fazia nada. O toque agora vale a ALTURA INTEIRA da faixa e
    // NAV_BTN_FOLGA a mais de cada lado; o desenho continua o mesmo. Nao sobe
    // para dentro da area do app: o que esta acima da faixa e do app.
    const bool dentro_do_botao =
        tx >= L / 2 - NAV_BTN_L / 2 - NAV_BTN_FOLGA && tx < L / 2 + NAV_BTN_L / 2 + NAV_BTN_FOLGA &&
        ty >= nav_y0;

    // NAVEGACAO TRAVADA: a faixa passa a ser do app (teclado na tela). O
    // teclado encosta na barra, e um dedo que passa da fileira de baixo cai
    // aqui - entregue na ultima linha da area, vale a tecla de baixo em vez de
    // sumir. Mesma regra do toque por nivel (core.cpp).
    if (s_nav_travada) {
      if (app_vivo() && s_app->ao_tocar) { s_app->ao_tocar(tx - a.x, a.altura - 1 + s_rol.atual); }
      return;
    }
    if (dentro_do_botao && s_app) { fechar_app(); }
    return;
  }

  if (s_app) {
    // ROLAGEM SOMADA AQUI, e so aqui. O app compara contra as MESMAS constantes
    // que usou para desenhar, sem saber de rolagem nenhuma.
    if (app_vivo() && s_app->ao_tocar) { s_app->ao_tocar(tx - a.x, ty - a.y + s_rol.atual); }
    return;
  }

  if (s_remover_app) { home_remover_tocar(tx, ty); return; }
  const int pos = home_pos_em(a, tx, ty, false);
  if (pos >= 0) { abrir_app(sistema_home_app(pos)); }
}
