// Servico Mesh - a POSICAO do GPS para a malha. Partido de tab5_mesh.cpp (o
// servico); o que as partes compartilham esta em mesh/tab5_mesh_interno.hpp.

#include "esp_log.h"
#include "esp_random.h"
#include "ajustes.h"
#include "tab5_mesh_mqtt.hpp"
#include "tab5_mesh_admin.hpp"
#include "mesh_pb.hpp"
#include "tab5_gps.hpp"
#include "tab5_hora.hpp"
#include "mesh/tab5_mesh_interno.hpp"

static const char *TAG = "mesh";

// ---------------------------------------------------------------------------
// POSICAO DO GPS PARA A MALHA
// ---------------------------------------------------------------------------
// Com o C6L no Bluetooth o Grove fica livre para o GPS. A posicao vai num
// MeshPacket, porta POSITION_APP (3), com um Position dentro:
//   1: latitude_i  (sfixed32, graus x 1e7)
//   2: longitude_i (sfixed32, graus x 1e7)
//   3: altitude    (int32, metros)
//   4: time        (fixed32, epoch UTC)
//
// PARA QUEM, e a diferenca e de tempo de ar.
//   - COM O NO do outro lado: SO PARA ELE (to = o numero dele), como faz o
//     app do celular. Ele passa a usa-la como a propria posicao e a difunde
//     no ritmo dele (Localizacao > "enviar a cada", "ao se mover"), com a
//     precisao do canal. Antes ia em DIFUSAO, 3 saltos: a malha recebia a
//     mesma posicao duas vezes - a do Tab5 e, logo depois, a do no repetindo
//     o que ja sabia, cada uma no ritmo de um.
//   - O no JA SABE onde esta (posicao fixa, ou o GPS dele ligado): nada a
//     mandar. A fixa ignoraria a nossa; o GPS dele a atropelaria.
//   - PELA INTERNET (sem no, ou o Bluetooth caiu e o MQTT cobre): o Tab5 e o
//     no, entao difunde ele mesmo - com a precisao do canal primario que o
//     no deixou configurada (0 = nao manda; menos de 32 = so a regiao).
// Sem fix, nao manda nada (devolve false e a tarefa tenta de novo em 30 s);
// true = resolvido ate a proxima volta (mandou, ou nao ha o que mandar).
bool enviar_posicao(void)
{
  if (!s_servico_ativo || !tab5_gps_servico_ativo()) { return false; }
  const bool com_no = radio_aceita_admin();
  const bool pela_net = !com_no && (s_via == AJUSTES_MESH_INTERNET || s_via == AJUSTES_MESH_BLUETOOTH)
                     && mesh_mqtt_conectado() && !mesh_mqtt_em_proxy();
  if (!com_no && !pela_net) { return false; }
  if (com_no && mesh_admin_no_tem_posicao_propria()) { return true; }
  const int32_t prec = mesh_admin_precisao_primario();
  if (pela_net && prec == 0) { return true; }

  const nmea_posicao_t g = tab5_gps_posicao();
  if (!g.valida || g.qualidade == 0) { return false; }
  int32_t lat = (int32_t)(g.lat * 1e7), lon = (int32_t)(g.lon * 1e7);
  if (pela_net && prec > 0 && prec < 32) {
    // O mesmo corte do firmware (PositionModule): os bits que sobram vao
    // para o meio da celula, nao para o canto.
    const uint32_t mascara = 0xFFFFFFFFu << (32 - prec);
    lat = (int32_t)(((uint32_t)lat & mascara) + (1u << (31 - prec)));
    lon = (int32_t)(((uint32_t)lon & mascara) + (1u << (31 - prec)));
  }

  uint8_t pos[32];
  pb_escritor_t w = pb_escritor(pos, sizeof(pos));
  mesh_pb_posicao(&w, lat, lon, (int32_t)g.altitude_m, tab5_hora_epoch());

  // Para o no: sem saltos (nao sai dele). Difusao: 3 saltos (ver tab5_mesh_enviar).
  const mesh_pb_envelope_t env = { com_no ? s_meu_num : MESH_PB_DIFUSAO, -1, PORT_POSICAO, pos,
                                   pb_tamanho(&w), esp_random() | 1u, (uint8_t)(com_no ? 0 : 3), false };
  uint8_t quadro[96];
  const size_t n = pb_tamanho(&w) ? mesh_pb_to_radio(&env, quadro, sizeof(quadro)) : 0;
  const bool ok = n && enlace_enviar(quadro, n);
  if (ok) {
    ESP_LOGI(TAG, "posicao do GPS %s (%.5f, %.5f)", com_no ? "passada ao no" : "difundida pela internet",
             g.lat, g.lon);
  }
  return ok;
}
