// CORE - o desenho: a area util, a medida das passadas completas, a tela do
// app recusado e a passada de desenho do conteudo (home, bloqueio, avisos ou
// app). Partido de core.cpp (o motor); o que as partes compartilham esta em
// core_interno.hpp.

#include "tab5_tema.hpp"
#include "ajustes.h"
#include "esp_attr.h"
#include "ritmo.hpp"
#include "rolagem.hpp"
#include "tela_virtual.hpp"
#include "compositor.hpp"
#include "tab5_tela.hpp"
#include "tab5_ui.hpp"
#include "tab5_hora.hpp"
#include "idioma.h"
#include <stdio.h>
#include "esp_timer.h"
#include "core_interno.hpp"

area_t area_util(void)
{
  area_t a;
  a.x       = 0;
  a.y       = ALTURA_BARRA;
  a.largura = M5.Display.width();
  a.altura  = M5.Display.height() - ALTURA_BARRA - ALTURA_NAV;
  return a;
}

// Publicada em app.h: e a UNICA fonte correta da geometria para os apps (e
// para kernel/shell.cpp, que a usa pela mesma porta publica em vez da
// area_util() privada - ver o aviso em kernel/internal.hpp). Antes os apps
// copiavam as alturas daqui, e a copia envelhecia sozinha.
area_t sistema_area(void) { return area_util(); }
// limpar_area = true SO quando a tela mudou de conteudo (abriu/fechou app).
// Nos redesenhos de dado, o conteudo e pintado por cima do anterior.
// O app precisa saber quando o sistema limpou a area embaixo dele. Sem isso,
// um app que pinta por diferenca (guardando o que ja desenhou) fica com a tela
// EM BRANCO depois do repouso: ele acha que nada mudou, mas tudo foi apagado.
// Foi o que aconteceu com o app Ajustes.
static bool s_area_limpa = false;
bool sistema_area_foi_limpa(void) { return s_area_limpa; }
float sistema_quadro_dt(void) { return ritmo_dt(); }

// NAO E rolagem_limite() do modulo generico (kernel/rolagem.hpp), de
// proposito - essa aqui consulta s_app->altura_conteudo/altura_agora,
// que sao politica de app/nucleo, nao geometria de rolagem pura. Nome
// diferente para nao convidar a confundir as duas.
// A altura do conteudo do app aberto (altura_agora ganha de altura_conteudo).
static int altura_do_app(const area_t& a)
{
  if (!s_app) { return 0; }
  return s_app->altura_agora ? s_app->altura_agora(a) : s_app->altura_conteudo;
}

int limite_do_app(const area_t& a)
{
  const int h = altura_do_app(a);
  if (h <= a.altura) { return 0; }
  return h - a.altura;
}

// 'limpar_area' manda FISICAMENTE apagar a area (tab5_tela_limpar + barra de
// nav) - custa a rajada grande de PSRAM. 'completo_para_apps' e o que cada
// app VE em pintura_completa(): TRUE diz "redesenhe sua estrutura estatica
// inteira (titulo/ajuda/botao guardados atras de um 'if (s_tudo)'), nao so os
// campos que mudaram de valor".
//
// Os dois eram o MESMO parametro, porque so existiam dois
// casos: repintura completa (os dois true - abrir/fechar app, acordar,
// redesenho pedido) ou incremental pura (os dois false - so um dado mudou,
// nada mexeu de posicao). O deslocamento de rolagem (tab5_tela_rolar,
// ver o loop principal) criou um TERCEIRO caso: a posicao na tela MUDOU (os
// pixels foram deslocados, nao apagados) mas ninguem apagou nada
// fisicamente. Chamando com (false, true): pula a rajada de apagar (os
// pixels que nao mudaram de valor ja estao certos, o deslocamento cuidou
// disso) mas ainda avisa cada app que ele PRECISA redesenhar a propria
// estrutura estatica - porque a faixa recem-exposta pelo deslocamento nunca
// foi pintada NESTA posicao antes, e um app cujo titulo/botao so desenha
// quando s_tudo e verdadeiro (a maioria de Ajustes faz isso) simplesmente
// nunca preencheria aquela faixa. Foi visto ao vivo: rolar para baixo
// parecia bom (a faixa nova exposta e fina, o vazio passava despercebido),
// mas voltar para cima expunha de uma vez o rastro acumulado de faixas nunca
// pintadas - "tudo sumia".
//
// 'recorte_app' - so usado pelo caminho de deslocamento de
// rolagem (ver o loop principal): limita a escrita de pixels do app a UMA
// FAIXA da area util, em vez da area inteira. Correto porque o app e chamado
// com a MESMA 'rolada' de sempre (nao sabe que existe recorte) e o
// LovyanGFX descarta silenciosamente qualquer pixel fora do recorte - os
// pixels fora da faixa ja estao certos (o deslocamento cuidou disso antes de
// chamar aqui), entao nao ha diferenca visual em pula-los, so MUITO menos
// bytes escritos na PSRAM. nullptr (o padrao) usa a area util inteira, igual
// a antes desta data.
// MEDIDA DA PASSADA COMPLETA, por fase (ver sistema_passadas_completas).
uint32_t s_fase_limpar_us = 0;   // a ultima limpeza da area (+ nav)

#define PASSADAS_MAX 24
EXT_RAM_BSS_ATTR static sistema_passada_t s_passadas[PASSADAS_MAX];   // so a interface e o console leem
static int s_n_passadas = 0;

void anotar_passada(const char* nome, uint32_t us, uint32_t barra, uint32_t limpar, uint32_t conteudo)
{
  sistema_passada_t* p = nullptr;
  for (int i = 0; i < s_n_passadas; ++i) { if (s_passadas[i].nome == nome) { p = &s_passadas[i]; break; } }
  if (!p) {
    if (s_n_passadas >= PASSADAS_MAX) { return; }
    p = &s_passadas[s_n_passadas++];
    *p = {};
    p->nome = nome;
  }
  ++p->n;
  p->us += us;
  p->us_barra += barra;
  p->us_limpar += limpar;
  p->us_conteudo += conteudo;
  if (us > p->max_us) { p->max_us = us; }
}

// Leitura do console (outra tarefa) sem trava: copia de numeros de
// diagnostico; um valor torto num instante raro nao engana ninguem.
int sistema_passadas_completas(sistema_passada_t* out, int max)
{
  const int n = s_n_passadas < max ? s_n_passadas : max;
  for (int i = 0; i < n; ++i) { out[i] = s_passadas[i]; }
  return n;
}

// A tela do sistema no lugar de um app recusado (ver recusar_app).
static void desenhar_recusa(lgfx::LovyanGFX& d, const area_t& a)
{
  const int cx = a.x + a.largura / 2;
  int y = a.y + a.altura / 2 - 90;
  d.setTextDatum(middle_center);
  tab5_texto(d, 3);
  d.setTextColor(TFT_ORANGE, TFT_BLACK);
  char l[64];
  snprintf(l, sizeof(l), tr("%s precisa do cartão SD"), tr(s_app->nome));
  d.drawString(l, cx, y);
  tab5_texto(d, 2);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  y += 70;
  d.drawString(tr("O cartão está no computador (modo pendrive)."), cx, y);
  y += 44;
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.drawString(tr("Ele volta ao ejetar no computador,"), cx, y);
  y += 40;
  d.drawString(tr("ou em Ajustes > USB. O app abre sozinho."), cx, y);
  d.setTextDatum(top_left);
  tab5_fonte_corpo(d);
}

void desenhar_conteudo(bool limpar_area, bool completo_para_apps,
                       const area_t* recorte_app)
{
  // A TELA VIRTUAL, nao o M5.Display (docs/COMPOSITOR.md, etapa 3a): nada aqui
  // escreve mais na pagina que o painel varre.
  auto& d = tela_virtual();
  area_t a = area_util();

  // TODO quadro passa pelo ritmo (kernel/ritmo): agenda o proximo periodico e
  // da o dt de quem anima. O dono e quem esta desenhando AGORA - trocou, o
  // relogio recomeca.
  static char DONO_BLOQUEIO, DONO_AVISOS, DONO_HOME;   // so os enderecos importam
  const void* dono = s_bloqueado ? (const void*)&DONO_BLOQUEIO
                   : s_painel_avisos ? (const void*)&DONO_AVISOS
                   : s_app ? (const void*)s_app : (const void*)&DONO_HOME;
  ritmo_quadro(dono, periodo_do_app());

  d.startWrite();
  if (limpar_area) {
    // EM FAIXAS, nao num fillRect so. A area util e ~1,6MB de escrita na PSRAM
    // e o painel DSI le da mesma memoria sem parar; as duas rajadas juntas
    // secam o FIFO dele e o frame sai AZUL. Ver tab5_tela.hpp - o driver do
    // IDF descreve o sintoma com essas palavras.
    //
    // Era o que acontecia ao SAIR de um app para a home: a home e quase toda
    // preta, e area preta grande e o preenchimento mais rapido e contiguo que
    // existe. Abrir um app escreve o mesmo tanto, mas espalhado em dezenas de
    // operacoes pequenas - e por isso nunca piscava na entrada.
    const int64_t limpar_t0 = esp_timer_get_time();
    // A COR DA LIMPEZA (ver sistema_cor_fundo, mais abaixo). Limpar ja na cor
    // de fundo de quem vai desenhar faz o pedido igual dele (a home, por
    // exemplo) cair no "ja esta limpo" abaixo - sem isso seriam DUAS rajadas
    // grandes seguidas, exatamente o que fazia o frame sair azul.
    const uint16_t cor_limpa = sistema_cor_fundo();
    tab5_tela_limpar(a.x, a.y, a.largura, a.altura, cor_limpa);
    // AVISA A CAMADA DE TELA. Daqui ate o fim desta passada, todo pedido de
    // limpar nesta mesma cor dentro desta area e dispensado - e a rajada dobrada que
    // produzia a piscada azul deixa de acontecer, para TODO app, sem que
    // nenhum deles precise saber da regra.
    tab5_tela_marcar_limpo(a.x, a.y, a.largura, a.altura, cor_limpa);
    if (s_bloqueado) {
      // Bloqueado nao ha navegacao: a faixa de baixo e apagada aqui e vira
      // parte da tela de bloqueio (ver desenhar_bloqueio, mais abaixo).
      tab5_tela_limpar(0, d.height() - ALTURA_NAV, d.width(), ALTURA_NAV, TFT_BLACK);
    } else {
      desenhar_barra_nav(d);
    }
    s_fase_limpar_us = (uint32_t)(esp_timer_get_time() - limpar_t0);
  }
  s_area_limpa = completo_para_apps;
  // s_app E s_app_carregando (nao so um deles): os dois comecam NULL, e
  // "nullptr == nullptr" e verdadeiro - sem o s_app aqui, a home (sem app
  // nenhum aberto) caia nesta condicao e tentava ler s_app->nome com s_app
  // nulo. Foi o crash do boot: Load access fault bem nesta linha.
  if (s_bloqueado) {
    // A TELA INTEIRA ABAIXO DA BARRA, inclusive a faixa da navegacao (que
    // nao existe bloqueado): antes ela ficava preta, sobrando na base.
    area_t bq = a;
    bq.altura += ALTURA_NAV;
    desenhar_bloqueio(d, bq, limpar_area);
  } else if (s_painel_avisos) {
    desenhar_painel_avisos(d, a, limpar_area);
  } else if (s_app && s_app == s_app_recusado) {
    desenhar_recusa(d, a);
  } else if (s_app && s_app == s_app_carregando) {
    // MONTANDO O SD AINDA. s_app ja aponta para o app (ver abrir_app()), mas
    // ao_abrir() dele NAO foi chamado - o app pode nao ter estado nenhum para
    // desenhar, e chamar s_app->desenhar() aqui seria confiar em memoria que o
    // proprio app ainda nao teve chance de preparar. Uma tela de espera simples
    // ate concluir_abertura_do_sd() liberar o app de verdade.
    d.setTextDatum(middle_center);
    tab5_texto(d, 2);
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    char msg[48];
    snprintf(msg, sizeof(msg), tr("abrindo %s..."), tr(s_app->nome));
    d.drawString(msg, a.x + a.largura / 2, a.y + a.altura / 2);
  } else if (s_app) {
    const int max = limite_do_app(a);
    if (s_app->desenhar && max > 0) {
      // RECORTE PRIMEIRO. Com o conteudo deslocado, o app desenharia por cima
      // da barra de status e da navegacao sem perceber - ele nao sabe que
      // existe rolagem. O recorte e o que torna essa ignorancia segura.
      //
      // recorte_app, quando vem preenchido, ESTREITA esse recorte para so a
      // faixa recem-exposta (ver o aviso grande no topo da funcao) - o app
      // continua recebendo a MESMA 'rolada' de sempre, sem saber da
      // diferenca.
      const area_t& clip = recorte_app ? *recorte_app : a;
      d.setClipRect(clip.x, clip.y, clip.largura, clip.altura);
      area_t rolada = a;
      rolada.y     -= s_rol.atual;
      rolada.altura = altura_do_app(a);
      s_app->desenhar(d, rolada);
      d.clearClipRect();
    } else if (s_app->desenhar) {
      // RECORTE SEMPRE, tambem sem rolagem: o app (em C ou Berry) so pinta a
      // propria area. Um script com a conta errada - uma figura que passa da
      // borda, um texto comprido - pintava a barra de status e a navegacao
      // (visto no Estresse.be, 14/11/2026). Cortado aqui, para todo app, sem
      // que nenhum precise conferir as proprias coordenadas.
      d.setClipRect(a.x, a.y, a.largura, a.altura);
      s_app->desenhar(d, a);
      d.clearClipRect();
    }
  } else if (limpar_area) {
    // ICONE NAO MUDA. A home e estatica: oito icones e oito rotulos, sempre os
    // mesmos. O que se mexe nela - bateria, temperatura, a linha de servicos -
    // e desenhado por fora, com caminho proprio e comparacao antes de pintar.
    //
    // Redesenhar tudo numa passada de DADOS era repintar 578KB para chegar ao
    // mesmo desenho. Numa transicao isso e a diferenca entre caber e nao caber
    // na banda que sobra do painel; e a passada de dados acontece na pagina
    // VISIVEL, sem a protecao da troca de pagina.
    //
    // A condicao e exatamente a certa: so ha o que redesenhar aqui quando a
    // area foi APAGADA - e ai o desenho realmente sumiu.
    desenhar_home(d, a);
  }
  d.endWrite();

  s_area_limpa = false;
  // SO LIMPA s_sujo_layout QUANDO FOI ESTA PASSADA QUE O ATENDEU
  // (limpar_area==true, ou seja: o loop principal chamou aqui exatamente
  // porque s_sujo_layout estava true). Esta linha zerava
  // INCONDICIONALMENTE - e s_app->desenhar() (chamada acima, dentro da
  // passada INCREMENTAL tambem, limpar_area==false) pode legitimamente
  // pedir um repaint completo NO MEIO da propria passada de dados (ex.:
  // app Camera chamando sistema_travar_navegacao(false) de dentro do
  // desenhar() dela, ao notar que uma captura terminou). Zerar aqui sem
  // condicao apagava esse pedido antes que o loop principal chegasse a ve-lo
  // - a barra de navegacao nunca era redesenhada destravada, e o INICIO
  // ficava escuro para sempre (defeito observado, "captura concluida" aparecia
  // no log e o botao continuava escuro do mesmo jeito).
  //
  // NAO E SO DA CAMERA: qualquer app que chame sistema_pedir_redesenho_completo()/
  // sistema_travar_navegacao() de dentro do proprio
  // desenhar() cairia na mesma armadilha - o conserto e aqui, nao em cada app.
  if (limpar_area) { s_sujo_layout = false; }
  s_sujo_dados  = false;
  s_ultimo_desenho = tab5_agora_ms();

  // A MARCA MORRE COM A PASSADA. Ela afirma "este retangulo esta desta cor
  // AGORA"; depois que o app desenhou por cima, deixa de ser verdade. Deixa-la
  // viva ate a proxima passada faria a limpeza seguinte ser dispensada sobre
  // conteudo real - e o app ficaria com rastro por uma otimizacao do sistema.
  tab5_tela_esquecer_limpo();
}

// Um quadro da tela de bloqueio durante a rampa de brilho do despertar.
void animar_na_rampa(void)
{
  if (!s_bloqueado) { return; }
  desenhar_conteudo(false, false);
  comp_apresentar();
}

// A cor de fundo da area agora (declarada em app.h). A home sem papel de
// parede e os apps ja migrados (app_t.fundo_tema) usam o fundo do tema; o
// resto - bloqueio, home com papel de parede, apps ainda nao migrados - segue
// no preto, porque desenha texto com fundo TFT_BLACK.
uint16_t sistema_cor_fundo(void)
{
  if (s_bloqueado) { return TFT_BLACK; }
  if (s_app)       { return s_app->fundo_tema ? TEMA_FUNDO : TFT_BLACK; }
  return ajustes_parede() == AJUSTES_PAREDE_NENHUM ? TEMA_FUNDO : TFT_BLACK;
}
