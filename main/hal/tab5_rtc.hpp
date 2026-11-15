// RTC (RX8130) - cristal e bateria propria, guarda a hora entre boots.
// M5.begin() ja chama M5.Rtc.setSystemTimeFromRtc() sozinho (ver M5Unified.cpp)
// e le o RTC so uma vez; depois disso o relogio do sistema anda pelo clock
// interno (no sono leve, um RC de ~5%). O RTC, com cristal, e a referencia
// precisa: services/tab5_hora.cpp o rele para corrigir essa deriva.
//
// Sem alguem AJUSTAR o RX8130 pelo menos uma vez, ele guarda uma hora sem
// sentido (de fabrica) - a bateria preserva o que foi gravado, nao inventa a
// hora certa sozinha. Ver services/tab5_hora.hpp para QUEM decide quando
// ajustar e de qual fonte.
//
// TUDO AQUI SO DA TAREFA DE INTERFACE - o RX8130 e I2C interno (mesmo
// barramento do toque/IMU/PMIC), mesma regra documentada em tab5_power.hpp.
#pragma once

#include <stdbool.h>
#include <time.h>

// Faixa de datas aceita como hora de verdade: 2025-01-01 a 2040-01-01 (UTC).
bool tab5_rtc_plausivel(time_t e);

// Converte data/hora UTC (struct tm) em epoch, sem depender de timegm().
time_t tab5_rtc_utc_para_epoch(const struct tm* t);

// Le o RTC. So devolve true se a hora e confiavel: RTC ligado, leitura ok,
// flag VLF desligado e epoch plausivel. 'epoch_utc' em segundos desde 1970.
bool tab5_rtc_ler(time_t* epoch_utc);

// Byte de flags do RX8130 (registrador 0x1D) para diagnostico, ou -1 se o RTC
// esta desligado ou a leitura falhou. Bit 0x02 = VLF (hora invalida), bit 0x80
// = VBLF (bateria de backup fraca, so aviso).
int tab5_rtc_flags(void);

// Escreve o RTC E o relogio do sistema (settimeofday) NA MESMA CHAMADA - sem
// o segundo passo, so a PROXIMA vez que o aparelho ligar e que a sessao
// passaria a responder certo em time()/gmtime(); com os dois juntos, quem
// chamar time() JA AGORA ve a hora certa.
//
// Recusa epoch fora de tab5_rtc_plausivel() (false, nada e gravado). Depois de
// gravar, le de volta e confere (+-2 s): se nao bater devolve false, mas o
// relogio do sistema ja foi acertado. Conferido, limpa o flag VLF do chip.
// 'epoch_utc' em segundos desde 1970, UTC.
bool tab5_rtc_definir(time_t epoch_utc);
