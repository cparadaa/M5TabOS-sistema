// TRAVA - um mutex que ja existe antes de qualquer tarefa.
//
//   static Trava s_trava;
//   s_trava.travar(); ... s_trava.destravar();
//   { Trava::Guarda g(s_trava); ... }   // destrava sozinho no fim do bloco
//
// O mutex nasce no construtor, que roda com os construtores globais - antes
// do app_main e de existir concorrencia. Nao ha "criar na primeira vez", nem
// corrida nessa criacao, nem _iniciar() para lembrar de chamar. O mutex vem
// do heap (RAM interna), como antes: so o objeto Trava pode morar na PSRAM.
//
// tab5_sd.cpp continua com o mutex criado no tab5_sd_iniciar() dele - a
// escolha de la e deliberada (ver o comentario la).
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class Trava {
public:
  Trava() : m_(xSemaphoreCreateMutex()) {}
  Trava(const Trava&) = delete;
  Trava& operator=(const Trava&) = delete;

  void travar(void)    { xSemaphoreTake(m_, portMAX_DELAY); }
  bool travar(TickType_t espera) { return xSemaphoreTake(m_, espera) == pdTRUE; }
  void destravar(void) { xSemaphoreGive(m_); }

  // Para quem ainda passa o handle direto a xSemaphoreTake/Give.
  operator SemaphoreHandle_t() const { return m_; }

  class Guarda {
  public:
    explicit Guarda(Trava& t) : t_(t) { t_.travar(); }
    ~Guarda() { t_.destravar(); }
    Guarda(const Guarda&) = delete;
    Guarda& operator=(const Guarda&) = delete;
  private:
    Trava& t_;
  };

private:
  SemaphoreHandle_t m_;
};
