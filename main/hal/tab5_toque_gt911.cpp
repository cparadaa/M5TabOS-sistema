// Ver tab5_toque_gt911.hpp: o defeito do driver e por que esta classe existe.
#include "tab5_toque_gt911.hpp"
#include "tab5_power.hpp"   // tab5_painel_toque_gt911
#include "esp_log.h"
#include <algorithm>
#include "lgfx/v1/touch/Touch_GT911.hpp"   // M5GFX.h nao expoe a classe do GT911

static const char* TAG = "toque_gt911";

// Contador de falhas de I2C (so diagnostico).
static volatile uint32_t s_falhas = 0;

// Comando do GT911: escrever 0 em 0x814E diz ao chip que o relatorio foi lido.
static constexpr uint8_t CMD_LIMPAR[] = { 0x81, 0x4E, 0x00 };

namespace {

class Toque_GT911 : public lgfx::Touch_GT911 {
public:
  // Assume a configuracao do toque que o M5GFX ja montou e iniciou. Nao chama
  // init(): o chip ja foi iniciado pelo M5GFX e repetir (i2c::init e o pulso
  // do INT em wakeup()) so atrapalharia o barramento ja em uso.
  void adotar(const config_t& cfg) {
    _cfg = cfg;
    _inited = true;
  }

  uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override {
    if (!_inited || count == 0) return 0;
    if (count > 5) count = 5;

    // INT em nivel baixo = o chip tem relatorio novo. INT alto: nao ha nada
    // novo, vale o ultimo estado BOM (igual ao original).
    if (_cfg.pin_int < 0 || !lgfx::gpio_in(_cfg.pin_int)) {
      const uint32_t agora = lgfx::millis();
      const uint32_t passou = agora - _ultima;
      _ultima = agora;

      _falhou = false;
      if (passou >= 128) {
        // O GT911 repete o mesmo relatorio ate receber o 0 em 0x814E; depois
        // de muito tempo parado o relatorio e velho. Descarta e rele, ate 24x.
        escrever(CMD_LIMPAR, 3);
        int tentativas = 24;
        do {
          lgfx::delay(1);
        } while (ler() != Leitura::nova && --tentativas);
      } else {
        ler();
      }

      // CONSERTO: leitura com falha => sem dedo, e o buffer velho e jogado fora.
      if (_falhou) {
        _dados[0] = 0;
        s_falhas = s_falhas + 1;
        return 0;
      }
    }

    // Decodifica os pontos: 8 bytes cada (id, x, y, tamanho), igual ao original.
    const uint32_t pontos = std::min<uint_fast8_t>(count, _dados[0] & 0x0F);
    for (uint32_t i = 0; i < pontos; ++i) {
      const auto* d = reinterpret_cast<const uint16_t*>(&_dados[i * 8]);
      tp[i].id   = d[0] >> 8;
      tp[i].x    = d[1];
      tp[i].y    = d[2];
      tp[i].size = d[3];
    }
    return pontos;
  }

private:
  enum class Leitura { nova, sem_novidade, falha };
  static constexpr int MAX_PONTOS = 5;

  uint32_t _ultima = 0;                    // ms da ultima consulta
  bool     _falhou = false;                // a ultima ler() deu falha de I2C
  uint8_t  _dados[MAX_PONTOS * 8 + 2] = {};  // [0]=status, depois os pontos

  bool escrever(const uint8_t* dados, size_t n) {
    return lgfx::i2c::transactionWrite(_cfg.i2c_port, _cfg.i2c_addr, dados, n, _cfg.freq).has_value();
  }

  // Le o status (0x814E) e, se ha pontos, os pontos. So grava em _dados se a
  // leitura inteira deu certo; falha de I2C marca _falhou.
  Leitura ler(void) {
    if (!lgfx::i2c::beginTransaction(_cfg.i2c_port, _cfg.i2c_addr, _cfg.freq, false).has_value()) {
      _falhou = true;
      return Leitura::falha;
    }
    uint8_t status = 0;
    bool i2c_ok = lgfx::i2c::writeBytes(_cfg.i2c_port, CMD_LIMPAR, 2).has_value()
               && lgfx::i2c::restart(_cfg.i2c_port, _cfg.i2c_addr, _cfg.freq, true).has_value()
               && lgfx::i2c::readBytes(_cfg.i2c_port, &status, 1).has_value();
    bool pronto = i2c_ok && (status & 0x80);
    if (pronto) {
      const uint_fast8_t pontos = std::min<uint_fast8_t>(MAX_PONTOS, status & 0x0Fu);
      if (pontos) {
        pronto = lgfx::i2c::readBytes(_cfg.i2c_port, &_dados[1], pontos * 8, true).has_value();
        i2c_ok = pronto;
      }
    }
    lgfx::i2c::endTransaction(_cfg.i2c_port);

    if (!i2c_ok) {
      _falhou = true;
      return Leitura::falha;
    }
    if (!pronto) return Leitura::sem_novidade;   // chip sem relatorio novo: nao e erro

    _dados[0] = status;
    escrever(CMD_LIMPAR, 3);                    // avisa o chip que ja lemos
    _falhou = false;
    return Leitura::nova;
  }
};

// Dono do novo toque. O antigo continua com o M5GFX (shared_ptr _touch_last):
// o painel so guarda ponteiro cru, entao o antigo fica vivo, parado, e nunca
// e liberado por nos. Nunca liberamos este: vive ate o reset.
Toque_GT911 s_toque;

}  // namespace

bool tab5_toque_gt911_instalar(void) {
  if (!tab5_painel_toque_gt911()) return false;   // so o ILI9881C tem GT911

  auto* painel = M5.Display.panel();
  auto* antigo = M5.Display.touch();
  if (painel == nullptr || antigo == nullptr) {
    ESP_LOGW(TAG, "sem painel ou sem toque - chamado antes do M5.begin()?");
    return false;
  }
  if (antigo == &s_toque) return true;            // ja instalado

  s_toque.adotar(antigo->config());
  // setTouch refaz a calibracao a partir da mesma config: sem mudanca de
  // coordenadas. O M5.Touch consulta o display a cada update (nao guarda o
  // ITouch), entao passa a usar o novo na proxima leitura.
  painel->setTouch(&s_toque);
  ESP_LOGI(TAG, "toque GT911 com protecao contra falha de I2C instalado");
  return true;
}

uint32_t tab5_toque_gt911_falhas(void) { return s_falhas; }
