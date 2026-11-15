// Modulo Berry 'gnss' (os satelites que o receptor ve). Ver berry_apps.cpp
// (o motor) e berry_interno.hpp.

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/idf_additions.h"   // xQueueCreateWithCaps - a fila do gnss na PSRAM
#include "esp_heap_caps.h"
#include "tab5_gps.hpp"
#include "nmea.h"
#include "berry_interno.hpp"

// ---------------------------------------------------------------------------
// MODULO 'gnss' - a constelacao: os satelites que o receptor ve (GSV)
// ---------------------------------------------------------------------------
// sistema.gps() da a POSICAO (o servico ja interpreta RMC/GGA). Os satelites
// vem das sentencas GSV, que so chegam a quem ASSINA o servico de GPS - e ele
// aceita um assinante so (hoje: o Azimutal em C, que nao existe na variante
// berry). gnss.ligar() assina; o resto le.
//
// QUEM ESVAZIA A FILA E O PROPRIO SCRIPT: cada gnss.satelites() (ou
// gnss.tempo()) le o que chegou desde a ultima vez e passa pelo mesmo parser
// do Azimutal. Sem tarefa nova - mas chame pelo menos a cada segundo ou dois
// (o desenhar com '# periodo: 1000', ou um fundo(1000)): a fila guarda ~2 s
// de sentencas e, cheia, o que chega depois se perde e a foto envelhece.
//
// UM DONO: o app que ligou. A maquina dele apagada solta a assinatura sozinha.
static QueueHandle_t       s_gnss_fila = nullptr;   // criada no primeiro ligar e mantida (ver gnss_soltar)
static const berry_app_t*  s_gnss_dono = nullptr;
static SatelliteSnapshot_t s_gnss_foto;            // a ultima rodada completa
static SatelliteSnapshot_t s_gnss_rodada;          // a que o parser esta montando
static bool                s_gnss_tem_foto = false;
static uint32_t            s_gnss_versao = 0;      // muda a cada rodada nova
static NMEARawSentence_t   s_gnss_crua;

// A fila NAO e apagada ao soltar: a tarefa do GPS pode estar no meio de um
// xQueueSend nela (ela le o assinante e envia sem trava). Fica alocada (~3 KB
// de PSRAM) e e reaproveitada no proximo ligar.
void gnss_soltar(const berry_app_t* a)
{
  if (!s_gnss_dono || (a && s_gnss_dono != a)) { return; }
  tab5_gps_dessinscrever();
  s_gnss_dono = nullptr;
  s_gnss_tem_foto = false;
}

static void gnss_esvaziar(void)
{
  if (!s_gnss_dono || !s_gnss_fila) { return; }
  while (xQueueReceive(s_gnss_fila, &s_gnss_crua, 0) == pdPASS) {
    if (strncmp(s_gnss_crua.sentence + 3, "GSV", 3) != 0) { continue; }   // GP/GL/GA/GB + GSV
    if (nmea_parse_gsv(s_gnss_crua.sentence, &s_gnss_rodada)) {
      s_gnss_foto = s_gnss_rodada;
      s_gnss_tem_foto = true;
      ++s_gnss_versao;
    }
  }
}

static bool gnss_meu(void) { return C->app && s_gnss_dono == C->app; }

// ligar() - true = assinando. false = sem app (console) ou o GPS ja tem outro
// assinante. Sem GPS no Grove ainda assim liga: as sentencas chegam quando
// ele subir.
static int g_ligar(bvm* vm)
{
  bool ok = gnss_meu();
  if (!ok && C->app && !s_gnss_dono) {
    if (!s_gnss_fila) { s_gnss_fila = xQueueCreateWithCaps(24, sizeof(NMEARawSentence_t), MALLOC_CAP_SPIRAM); }
    if (s_gnss_fila) {
      xQueueReset(s_gnss_fila);
      if (tab5_gps_assinar(s_gnss_fila)) {
        nmea_reset();
        memset(&s_gnss_rodada, 0, sizeof(s_gnss_rodada));
        s_gnss_tem_foto = false;
        s_gnss_dono = C->app;
        ok = true;
      }
    }
  }
  be_pushbool(vm, ok);
  be_return(vm);
}

static int g_desligar(bvm* vm) { if (gnss_meu()) { gnss_soltar(C->app); } be_return_nil(vm); }
static int g_ligado(bvm* vm)   { be_pushbool(vm, gnss_meu()); be_return(vm); }

static const char* gnss_sistema(GnssType t)
{
  switch (t) {
    case GPS: return "GPS"; case GLONASS: return "GLONASS"; case GALILEO: return "GALILEO";
    case BEIDOU: return "BEIDOU"; case QZSS: return "QZSS"; case SBAS: return "SBAS";
    default: return "?";
  }
}

// satelites() - lista de {sis, prn, elev, azim, snr, pos, idade}: 'pos' false =
// o receptor sabe do satelite mas ainda nao tem onde ele esta (nao desenhe);
// 'idade' = segundos desde a ultima posicao observada (para GRAVAR, so as
// frescas: o parser segura a ultima quando a GSV chega sem posicao). nil =
// desligado ou nenhuma rodada completa ainda.
static int g_satelites(bvm* vm)
{
  if (!gnss_meu()) { be_return_nil(vm); }
  gnss_esvaziar();
  if (!s_gnss_tem_foto) { be_return_nil(vm); }
  const uint32_t agora = pdTICKS_TO_MS(xTaskGetTickCount());
  lista_nova(vm);
  for (uint8_t i = 0; i < s_gnss_foto.count && i < MAX_SATS_TRACKED; ++i) {
    const SatelliteInfo_t& s = s_gnss_foto.satellites[i];
    mapa_novo(vm);
    m_txt(vm, "sis", gnss_sistema(s.type));
    m_int(vm, "prn", s.prn);
    m_int(vm, "elev", s.elevation);
    m_int(vm, "azim", s.azimuth);
    m_int(vm, "snr", s.snr);
    m_bool(vm, "pos", s.tem_posicao);
    m_int(vm, "idade", s.tem_posicao ? (int)((agora - s.pos_ms) / 1000) : -1);
    fim(vm);                 // sobra o mapa: lista, dado da lista, mapa
    be_data_push(vm, -2);    // o mapa vai para o dado da lista
    be_pop(vm, 1);
  }
  fim(vm);
  be_return(vm);
}

// versao() - muda a cada rodada completa nova (para so redesenhar/gravar o novo).
static int g_versao(bvm* vm)
{
  if (gnss_meu()) { gnss_esvaziar(); }
  be_pushint(vm, (bint)s_gnss_versao);
  be_return(vm);
}

// tempo() - a data e a hora do PROPRIO GPS, em UTC (RMC): {valida, fix, ano,
// mes, dia, hora, min, seg}. Nao depende do relogio do aparelho estar certo.
static int g_tempo(bvm* vm)
{
  const nmea_tempo_t t = tab5_gps_tempo();
  mapa_novo(vm);
  m_bool(vm, "valida", t.valida);
  m_bool(vm, "fix", t.fix);
  m_int(vm, "ano", t.ano); m_int(vm, "mes", t.mes); m_int(vm, "dia", t.dia);
  m_int(vm, "hora", t.hora); m_int(vm, "min", t.min); m_int(vm, "seg", t.seg);
  fim(vm);
  be_return(vm);
}

const membro_t BERRY_MOD_GNSS[] = {
  { "ligar", g_ligar }, { "desligar", g_desligar }, { "ligado", g_ligado },
  { "satelites", g_satelites }, { "versao", g_versao }, { "tempo", g_tempo },
  { nullptr },
};
