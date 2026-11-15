// APP Mesh - a CONVERSA: a localizacao dentro de uma mensagem (coordenada
// virando distancia e rumo) e a conversa em si (baloes, faixa, envio). Partido de
// app_mesh.cpp (o ciclo do app); o que as partes compartilham esta em
// app_mesh_interno.hpp.

#include "idioma.h"
#include "tab5_tela.hpp"
#include "tab5_ui.hpp"
#include "ajustes.h"
#include "tab5_gps.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "app_mesh_interno.hpp"
#include "tab5_tema.hpp"

static bool s_eh_canal = false;       // conversa aberta e de canal (rotulo de remetente)

// ROTULO DE REMETENTE, DENTRO do balao (era ACIMA, fora dele
// mudou) - so em conversa de CANAL, onde varias pessoas diferentes
// escrevem no mesmo lugar. Numa conversa direta o lado e a cor do balao ja
// dizem quem mandou; um rotulo ali so repetiria informacao. A ALTURA
// reservada para ele (NOME_A) e medida de verdade em desenhar_msgs() - ver o
// aviso la - nao mais um numero fixo aqui.

// Nome do remetente para o rotulo - ver tab5_mesh_nome_exibicao() no servico:
// shortname primeiro, ID em hexa se o shortname nao prestar (vazio, "?" ou
// emoji) ou o no ainda nao se apresentou. So um wrapper fino aqui; a regra em
// si mora no servico porque a conversa direta (tab5_mesh.cpp) precisa da
// MESMA regra, e duas copias dela e exatamente o tipo de duplicacao que
// tab5_ui.hpp existe para evitar (ver o aviso no topo daquele arquivo).
static void nome_do_no(uint32_t num, char* out, size_t out_tam)
{
  tab5_mesh_nome_exibicao(num, out, out_tam);
}

// COR POR REMETENTE, so para o rotulo de nome em conversa de CANAL - pedido
// depois de ver o app oficial do Meshtastic no celular, que
// pinta cada participante numa cor propria em vez de cinza uniforme igual
// para todo mundo. Hash simples (o proprio numero do no, modulo o tamanho da
// paleta): determinismo e tudo que se precisa aqui - a mesma pessoa sempre
// cai na mesma cor NESTA sessao, e isso basta para o olho aprender a
// distinguir quem escreveu o que numa janela cheia de gente. Nao precisa
// sobreviver a reinicio, e so identificacao visual, nao dado gravado.
static uint16_t cor_do_no(uint32_t num)
{
  static const uint16_t PALETA[] = {
    TEMA_APP_AZUL, TEMA_APP_LARANJA, TEMA_APP_ROSA, TEMA_APP_AMARELO,
    TEMA_APP_CIANO, TEMA_APP_ROXO, TEMA_APP_LIMA, TEMA_APP_VERDE,
  };
  return PALETA[num % (sizeof(PALETA) / sizeof(PALETA[0]))];
}

// -----------------------------------------------------------------------
// LOCALIZACAO DENTRO DE UMA MENSAGEM
// -----------------------------------------------------------------------
// Quem compartilha localizacao pelo app do Meshtastic no celular quase
// sempre manda uma mensagem de TEXTO normal com as coordenadas embutidas
// (um link tipo "https://maps.google.com/?q=-12.97,-38.50", ou so o par
// cru) - nao existe um tipo de pacote separado que a conversa precise
// entender. Entao a deteccao e sobre o TEXTO, nao sobre o protocolo.
//
// achar_coord() procura, em QUALQUER lugar da string, um par "numero,
// numero" (com o primeiro cabendo em latitude e o segundo em longitude) -
// serve tanto para o link quanto para o par cru, sem se importar com o
// resto do texto ao redor (URL, legenda, o que vier).
static bool achar_coord(const char* txt, float* lat, float* lon)
{
  for (const char* p = txt; *p; ++p) {
    if (*p != '-' && (*p < '0' || *p > '9')) { continue; }

    char* fim1 = nullptr;
    const double a = strtod(p, &fim1);
    if (fim1 == p) { continue; }

    const char* q = fim1;
    while (*q == ' ') { ++q; }
    if (*q != ',') { continue; }
    ++q;
    while (*q == ' ') { ++q; }

    char* fim2 = nullptr;
    const double b = strtod(q, &fim2);
    if (fim2 == q) { continue; }

    // O PRIMEIRO numero tem que caber em latitude e o SEGUNDO em longitude -
    // e o que distingue um par de coordenadas de dois numeros quaisquer
    // separados por virgula que por acaso apareçam num texto.
    if (a < -90.0 || a > 90.0)   { continue; }
    if (b < -180.0 || b > 180.0) { continue; }

    *lat = (float)a;
    *lon = (float)b;
    return true;
  }
  return false;
}

// Distancia (formula de haversine, correta em qualquer escala) e direcao
// (rumo inicial, arredondado para os 8 pontos cardeais) da posicao de
// referencia ("casa", ajustes.h) ate a localizacao recebida.
void distancia_direcao(float lat1, float lon1, float lat2, float lon2,
                              float* dist_km, const char** direcao)
{
  const double GRAU = M_PI / 180.0;
  const double R_TERRA = 6371.0;   // km

  const double dlat = (lat2 - lat1) * GRAU;
  const double dlon = (lon2 - lon1) * GRAU;
  const double s = sin(dlat / 2) * sin(dlat / 2)
                 + cos(lat1 * GRAU) * cos(lat2 * GRAU) * sin(dlon / 2) * sin(dlon / 2);
  *dist_km = (float)(2.0 * R_TERRA * atan2(sqrt(s), sqrt(1.0 - s)));

  const double y = sin(dlon) * cos(lat2 * GRAU);
  const double x = cos(lat1 * GRAU) * sin(lat2 * GRAU)
                  - sin(lat1 * GRAU) * cos(lat2 * GRAU) * cos(dlon);
  double rumo = atan2(y, x) * 180.0 / M_PI;
  if (rumo < 0.0) { rumo += 360.0; }

  static const char* NOMES[8] = { "N", "NE", "L", "SE", "S", "SO", "O", "NO" };
  *direcao = NOMES[((int)((rumo + 22.5) / 45.0)) % 8];
}

// ---------------------------------------------------------------------------
// A CONVERSA - o componente do sistema (kernel/conversa),
// ---------------------------------------------------------------------------
// Baloes, barra de escrita, teclado virtual e rolagem eram uma copia inteira
// aqui (~700 linhas). O componente nasceu DELES, para o ESP-NOW; agora o Mesh
// volta a usar o original, e uma correcao no teclado ou no balao vale para os
// dois. O que e so do Mesh mora nas tres respostas abaixo: rotulo do remetente
// no canal, "N saltos"/MQTT, coordenada virando distancia e rumo, e as quatro
// marcas de envio.
// Fundo da area de mensagens (o mesmo do componente - trilho e colunas usam).
uint16_t fundo_msg(lgfx::LovyanGFX&) { return TEMA_FUNDO; }

static float s_ref_lat = 0, s_ref_lon = 0;   // referencia de distancia (GPS ou casa)
static bool  s_tem_ref = false;

void atualizar_eh_canal(void)
{
  s_eh_canal = s_ab.ok && s_ab.canal;
}

int conv_contar(void*) { return (s_hist && s_ab.ok && chave_igual(s_hist_chave, s_ab)) ? s_n_hist : 0; }

bool conv_ler(int i, conversa_msg_t* o, void*)
{
  if (!s_hist || i < 0 || i >= s_n_hist) { return false; }
  const mesh_msg_t* m = &s_hist[i];

  // O componente le em ordem, de 0 em diante: a referencia de distancia vale
  // para a passada inteira (uma leitura do GPS, nao duzentas).
  if (i == 0) {
    const nmea_posicao_t fix = tab5_gps_posicao();
    s_tem_ref = false;
    if (fix.valida && fix.qualidade > 0) { s_ref_lat = (float)fix.lat; s_ref_lon = (float)fix.lon; s_tem_ref = true; }
    else if (ajustes_casa(&s_ref_lat, &s_ref_lon)) { s_tem_ref = true; }
  }

  o->minha  = (m->de == tab5_mesh_meu_numero());
  o->quando = m->quando;

  // Coordenada no texto vira "alfinete, lat, lon, distancia e rumo".
  float lat, lon;
  if (achar_coord(m->texto, &lat, &lon)) {
    if (s_tem_ref) {
      float km; const char* dir;
      distancia_direcao(s_ref_lat, s_ref_lon, lat, lon, &km, &dir);
      snprintf(o->texto, sizeof(o->texto), "\xF0\x9F\x93\x8D %.4f, %.4f  %.1fkm %s",
               (double)lat, (double)lon, (double)km, dir);
    } else {
      snprintf(o->texto, sizeof(o->texto), "\xF0\x9F\x93\x8D %.4f, %.4f", (double)lat, (double)lon);
    }
  } else {
    snprintf(o->texto, sizeof(o->texto), "%s", m->texto);
  }

  // Rotulo so no CANAL (varias pessoas no mesmo lugar), e so na primeira de
  // uma sequencia do mesmo remetente.
  if (s_eh_canal && !o->minha && (i == 0 || s_hist[i - 1].de != m->de)) {
    nome_do_no(m->de, o->nome, sizeof(o->nome));
    o->cor_nome = cor_do_no(m->de);
  }

  if (!o->minha) {
    // Por onde chegou: MQTT (internet) ou quantos saltos de radio. Sem o dado
    // no pacote, o que a tabela de nos sabe do remetente.
    bool via_mqtt = m->via_mqtt;
    uint8_t saltos = m->saltos;
    if (!via_mqtt && saltos == MESH_SALTOS_DESCONHECIDO) {
      bool nm = false; uint8_t ns = MESH_SALTOS_DESCONHECIDO;
      if (tab5_mesh_no_enlace(m->de, &nm, &ns)) { via_mqtt = nm; saltos = ns; }
    }
    if (via_mqtt)                                { snprintf(o->meta, sizeof(o->meta), "MQTT"); }
    else if (saltos != MESH_SALTOS_DESCONHECIDO) { snprintf(o->meta, sizeof(o->meta), tr("%u salto%s"), (unsigned)saltos, saltos == 1 ? "" : "s"); }
    else                                         { snprintf(o->meta, sizeof(o->meta), "--"); }
    o->cor_meta = via_mqtt ? (uint16_t)TEMA_DESTAQUE
                : (saltos == MESH_SALTOS_DESCONHECIDO) ? (uint16_t)TEMA_TEXTO_3
                : (saltos <= 1) ? (uint16_t)TEMA_SUCESSO
                : (saltos <= 3) ? (uint16_t)TEMA_APP_LIMA : (uint16_t)TEMA_ALERTA;
  } else if (m->id != 0) {
    // O estado vivo do envio (ACK chega depois), ou o que ficou gravado.
    uint8_t erro = 0;
    uint8_t est = tab5_mesh_estado_envio(m->id, &erro);
    if (est == MESH_ENVIO_NA) { est = m->envio; }
    o->envio = (est == MESH_ENVIANDO)     ? CONVERSA_ENVIO_PENDENTE
             : (est == MESH_ENTREGUE)     ? CONVERSA_ENVIO_OK
             : (est == MESH_FALHOU)       ? CONVERSA_ENVIO_FALHOU
             : (est == MESH_SEM_RESPOSTA) ? CONVERSA_ENVIO_SEM_RESPOSTA
             : (est == MESH_OUVIDO)       ? CONVERSA_ENVIO_OUVIDO
                                          : CONVERSA_ENVIO_NADA;
  }
  return true;
}

bool conv_enviar(const char* texto, void*)
{
  // PELA CHAVE: e para quem esta na tela, mesmo que a coluna tenha se
  // reordenado entre o desenho e o toque em ENVIAR.
  if (mesh_s_conv < 0 || !s_ab.ok || !tab5_mesh_servico_ativo()) { return false; }
  const bool ok = tab5_mesh_enviar(s_ab.canal ? 0xFFFFFFFFu : s_ab.no, s_ab.canal ? s_ab.idx : 0, texto);
  s_hist_pronto = false;          // rele incluindo a que acabou de sair
  return ok;
}

// A FAIXA DE CIMA DA CONVERSA EM PE: VOLTAR, o tipo (canal ou direta) e o
// nome. Deitado ela nao existe - a coluna ao lado ja diz qual conversa e.
// Repintada so quando o nome muda (ou na passada completa).
static void desenhar_faixa_frente(lgfx::LovyanGFX& d, const area_t& a, const mesh_conversa_t& c)
{
  static char s_ant[MESH_NOME_CONVERSA + MESH_NOME_LONGO + 4];
  char sig[sizeof(s_ant)];
  snprintf(sig, sizeof(sig), "%s|%s", c.nome, c.sub);
  if (!pintura_mudou_s(&s_p, s_ant, sizeof(s_ant), sig)) { return; }

  const uint16_t FUNDO = fundo_msg(d);
  tab5_tela_limpar(a.x, a.y, a.largura, FRENTE_TOPO_A, FUNDO);
  tab5_botao(d, a.x + 16, a.y + FRENTE_VOLTAR_Y, FRENTE_VOLTAR_L, FRENTE_VOLTAR_A,
             tr("VOLTAR"), TEMA_SUPERFICIE_ALTA, TEMA_TEXTO);
  const int gl = FRENTE_TOPO_A - 24;
  const int gx = a.x + 16 + FRENTE_VOLTAR_L + 20;
  caixa_glifo(d, gx, a.y + 12, gl, gl, c.canal ? GL_DIFUSAO : GL_DIRETO,
              c.canal ? TEMA_DESTAQUE_SUAVE : TEMA_SUPERFICIE_ALTA, TEMA_TEXTO);

  // Nome e, embaixo, o longo (ou "canal N") - cortados por caractere ate
  // caber, como na coluna.
  const int tx = gx + gl + 16;
  const int max_l = a.x + a.largura - 16 - tx;
  char nome[MESH_NOME_CONVERSA], sub[MESH_NOME_LONGO];
  snprintf(nome, sizeof(nome), "%s", c.nome);
  snprintf(sub, sizeof(sub), "%s", c.sub);
  d.setTextDatum(top_left);
  tab5_texto(d, 3.84f);
  cortar_ao_caber(d, nome, max_l);
  d.setTextColor(TEMA_TEXTO, FUNDO);
  d.drawString(nome, tx, a.y + (sub[0] ? 10 : 24));
  if (sub[0]) {
    tab5_texto(d, 2.4f);
    cortar_ao_caber(d, sub, max_l);
    d.setTextColor(TEMA_TEXTO_3, FUNDO);
    d.drawString(sub, tx, a.y + 50);
  }
  d.drawFastHLine(a.x, a.y + FRENTE_TOPO_A - 1, a.largura, SIS_BORDA);
}

void desenhar_msgs(lgfx::LovyanGFX& d, const area_t& a, int x0)
{
  const int larg = a.x + a.largura - x0;
  if (mesh_s_conv < 0) {
    conversa_fechar_teclado(&s_c);
    lista_esconder(&s_c.lista);   // sem conversa, nada de baloes para rolar
    if (TUDO) {
      const uint16_t FUNDO = fundo_msg(d);
      tab5_tela_limpar(x0, a.y, larg, a.altura, FUNDO);
      tab5_texto(d, 3.4f);
      d.setTextDatum(middle_center);
      d.setTextColor(TEMA_TEXTO_2, FUNDO);
      d.drawString(tr("escolha uma conversa"), x0 + larg / 2, a.y + a.altura / 2);
      d.setTextDatum(top_left);
    }
    return;
  }
  mesh_conversa_t c = {};
  if (tab5_mesh_conversa(mesh_s_conv, &c) && c.nao_lidas > 0) { tab5_mesh_marcar_conversa_lida(mesh_s_conv); }
  if (!s_hist_pronto) { carregar_hist(); }

  // Em pe a conversa esta POR CIMA da coluna: a faixa do VOLTAR ocupa o topo.
  int topo = 0;
  if (em_pe(a)) {
    topo = FRENTE_TOPO_A;
    desenhar_faixa_frente(d, a, c);
  }

  // A conversa e a coluna da direita (deitado) ou a area inteira abaixo da
  // faixa (em pe); o teclado, a largura inteira da area nos dois casos.
  const area_t r = { x0, a.y + topo, larg, a.altura - topo };
  conversa_area_teclado(&s_c, a);
  conversa_desenhar(&s_c, d, r, TUDO, tab5_mesh_servico_ativo(),
                    tr("sem nó Meshtastic - só consulta"));
}
