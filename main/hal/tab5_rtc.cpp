#include "tab5_rtc.hpp"
#include <M5Unified.h>
#include <sys/time.h>
#include "esp_log.h"

static const char* TAG = "rtc";

// RX8130: endereco I2C e registrador de flags (datasheet RX8130CE).
#define RX8130_ENDERECO   0x32
#define RX8130_REG_FLAGS  0x1D
#define RX8130_FLAG_VLF   0x02   // oscilador parou / faltou energia: hora invalida
#define I2C_FREQ_HZ       400000

// Os flags do 0x1D sao "escreva 0 para limpar" (escrever 1 e ignorado). Esta
// mascara tem 0 so no VLF: limpa ele e nao encosta em TF/AF (alarme/timer),
// que um ler-e-escrever poderia apagar sem querer.
#define RX8130_LIMPA_VLF  0xFD

// Tolerancia da conferencia da gravacao: o RTC tem resolucao de 1 s e a
// escrita/leitura pode cruzar a virada do segundo.
#define TOLERANCIA_S      2

// Faixa de datas aceita: 2025-01-01 a 2040-01-01 (UTC).
#define EPOCH_PISO        1735689600
#define EPOCH_TETO        2208988800

bool tab5_rtc_plausivel(time_t e)
{
  // O teto existe porque o RX8130 aceita qualquer ano ate 2099 e uma fonte
  // errada (ex. Mesh) ja gravou o ano 2049. Rever o teto em ~2035.
  return e >= (time_t)EPOCH_PISO && e < (time_t)EPOCH_TETO;
}

// Formula de Howard Hinnant (days_from_civil, dominio publico): dias UTC desde
// 1970-01-01, correta em qualquer ano. Substitui timegm() - a libc deste IDF
// nao a declara em <ctime> sem uma macro de extensao.
time_t tab5_rtc_utc_para_epoch(const struct tm* t)
{
  int y = t->tm_year + 1900;
  const int m = t->tm_mon + 1;
  const int d = t->tm_mday;
  y -= (m <= 2);
  const long era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long dias = era * 146097 + (long)doe - 719468;
  return (time_t)dias * 86400 + t->tm_hour * 3600 + t->tm_min * 60 + t->tm_sec;
}

int tab5_rtc_flags(void)
{
  if (!M5.Rtc.isEnabled()) { return -1; }
  uint8_t flags = 0;
  // readRegister (bool) em vez de readRegister8, que devolve 0 na falha e
  // nao deixa distinguir "sem flags" de "leitura falhou".
  if (!M5.In_I2C.readRegister(RX8130_ENDERECO, RX8130_REG_FLAGS, &flags, 1, I2C_FREQ_HZ)) { return -1; }
  return flags;
}

// Le o calendario do chip e converte para epoch UTC, sem olhar flags nem faixa.
static bool ler_calendario(time_t* epoch_utc)
{
  if (!M5.Rtc.isEnabled()) { return false; }
  m5::rtc_datetime_t dt;
  if (!M5.Rtc.getDateTime(&dt)) { return false; }

  struct tm t = { };
  t.tm_year = dt.date.year - 1900;
  t.tm_mon  = dt.date.month - 1;
  t.tm_mday = dt.date.date;
  t.tm_hour = dt.time.hours;
  t.tm_min  = dt.time.minutes;
  t.tm_sec  = dt.time.seconds;
  *epoch_utc = tab5_rtc_utc_para_epoch(&t);
  return true;
}

bool tab5_rtc_ler(time_t* epoch_utc)
{
  const int flags = tab5_rtc_flags();
  if (flags < 0 || (flags & RX8130_FLAG_VLF)) { return false; }

  time_t e = 0;
  if (!ler_calendario(&e) || !tab5_rtc_plausivel(e)) { return false; }
  *epoch_utc = e;
  return true;
}

bool tab5_rtc_definir(time_t epoch_utc)
{
  if (!M5.Rtc.isEnabled()) { return false; }
  if (!tab5_rtc_plausivel(epoch_utc)) {
    ESP_LOGW(TAG, "hora %lld fora da faixa plausivel - nao gravada", (long long)epoch_utc);
    return false;
  }

  struct tm t = { };
  gmtime_r(&epoch_utc, &t);

  // RTC_Class::setDateTime(const tm*) devolve void: a falha so aparece lendo
  // de volta, mais abaixo.
  M5.Rtc.setDateTime(&t);

  // O RELOGIO DO SISTEMA TAMBEM, na mesma chamada - ver o aviso no .hpp. Vale
  // mesmo que a gravacao do RTC falhe.
  struct timeval tv = { };
  tv.tv_sec  = epoch_utc;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  // Confere o que ficou no chip.
  time_t lido = 0;
  if (!ler_calendario(&lido) || lido < epoch_utc - TOLERANCIA_S || lido > epoch_utc + TOLERANCIA_S) {
    ESP_LOGW(TAG, "RTC nao guardou a hora gravada (lido %lld, esperado %lld)",
             (long long)lido, (long long)epoch_utc);
    return false;
  }

  // Hora gravada e conferida: o aviso de "hora invalida" do chip deixa de valer.
  if (!M5.In_I2C.writeRegister8(RX8130_ENDERECO, RX8130_REG_FLAGS, RX8130_LIMPA_VLF, I2C_FREQ_HZ)) {
    ESP_LOGW(TAG, "nao consegui limpar o VLF do RTC");
  }
  return true;
}
