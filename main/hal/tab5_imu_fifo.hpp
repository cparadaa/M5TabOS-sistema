// IMU (BMI270) - o FIFO do chip, para quem precisa de TODAS as amostras do
// giroscopio e nao so da ultima (hoje: o app Radar, que integra o rumo).
//
// POR QUE ISTO MORA NO HAL: ler o FIFO e falar com o chip pelo I2C interno
// (registradores, cabecalhos de quadro, relogio do chip). Isso e driver, e
// driver nao fica em app - o app so recebe as amostras prontas.
//
// O M5Unified le so os registradores de dado (a ULTIMA amostra). O FIFO e uma
// fila de ~2 KB dentro do chip: cada amostra do giroscopio entra la, e quem le
// recebe todas, em ordem. Ligar o FIFO nao muda nada para quem le os
// registradores (tab5_imu_medir, tab5_imu_amostra): sao caminhos independentes.
//
// TODAS as funcoes abaixo so da TAREFA DE INTERFACE - a regra do I2C interno
// (ver tab5_imu.hpp e tab5_power.hpp).
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  float gyr_hz;             // taxa nominal do giroscopio
  float gyr_lsb, acc_lsb;   // contagens por grau/s e por g
} tab5_imu_fifo_info_t;

// Liga o FIFO (giro + acelerometro, com cabecalho). 'info' pode ser nullptr.
// false = sem chip ou sem memoria: quem chamou usa tab5_imu_amostra().
bool tab5_imu_fifo_ligar(tab5_imu_fifo_info_t* info);

// Devolve o chip ao estado de antes e libera o buffer.
void tab5_imu_fifo_desligar(void);

bool tab5_imu_fifo_ligado(void);

// Uma amostra de giro (graus/s) e, quando veio junto, o acelerometro (g;
// nullptr se nao veio). 'dt' e o passo (s) e 't_us' o horario (esp_timer) dela.
typedef void (*tab5_imu_fifo_cb)(const float g[3], const float* a, float dt, int64_t t_us, void* ctx);

// Le o que a fila do chip juntou e chama 'cb' para cada amostra de giro, em
// ordem. 'parado' = o app sabe que o aparelho esta parado (calibrando): so
// entao o driver confere a ordem giro/acelerometro dentro do quadro (o
// acelerometro e que marca ~1 g). Descartes do chip e a taxa real do giroscopio
// (o relogio do BMI270 nao e o do P4) sao tratados aqui dentro.
void tab5_imu_fifo_ler(bool parado, tab5_imu_fifo_cb cb, void* ctx);

// Contadores para diagnostico.
typedef struct {
  uint32_t lidas, bytes, cortes, vazios, ctrl[4], desconhecidos, cheios;
  uint32_t pulos;           // vezes que o chip descartou amostras; ACUMULADO, nao zera
  bool     acel_primeiro;   // ordem do quadro "giro + acel" neste chip
  float    dt;              // passo atual do giroscopio (s)
  uint8_t  quadro[21];      // inicio do ultimo quadro de giro lido
  int      quadro_tam;      // tamanho real daquele quadro (o que nao cabe em quadro[] fica de fora)
} tab5_imu_fifo_diag_t;

// Copia os contadores. 'zerar' recomeca os da janela (menos 'pulos').
void tab5_imu_fifo_diag(tab5_imu_fifo_diag_t* d, bool zerar);
