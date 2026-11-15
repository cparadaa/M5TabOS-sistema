// GNSS - o registro de satelites do Azimutal: gravar a constelacao no cartao
// e carregar de volta o dia que se quer ver. O app Azimutal so desenha.
//
// FORMATO DO ARQUIVO (um por dia, Azimutal/satelites-AAAA-MM-DD.csv, dia UTC
// do GPS) - arquivos antigos continuam abrindo:
//   SAT,<hh:mm:ss>,<constelacao>,<prn>,<elev>,<azim>,<snr>
//   POS,<hh:mm:ss>,<lat>,<lon>,<alt_m>,<hdop>,<qualidade>,<sats_usados>   onde EU estava
// Arquivos antigos podem nao ter o <snr>.
//
// O cartao so e tocado pelas portas de hal/tab5_sd.hpp (gravar) e pelo leitor
// de services/tab5_volumes.hpp (ler).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "nmea.h"

// Um satelite do catalogo do dia: quem aparece no arquivo.
#define TAB5_GNSS_SATS_MAX 40
typedef struct { char cons[12]; int prn; } tab5_gnss_sat_t;

// Uma posicao registrada. Seis bytes por ponto, na PSRAM.
typedef struct {
  uint8_t  sat;      // indice no catalogo (tab5_gnss_sats)
  uint8_t  elev;     // 0..90
  uint16_t azim;     // 0..359
  uint16_t minuto;   // hh*60+mm, para os rotulos de inicio e fim
  uint8_t  snr;      // dB (0 = sem trava)
} tab5_gnss_ponto_t;

// GRAVAR a constelacao no arquivo do dia (pelo gravador do sistema: entrega
// a linha e segue, com o app aberto ou nao). So grava o que tem posicao de
// verdade, com hora e fix do GPS. 'idade_max_ms': satelite cuja posicao nao
// foi reobservada ha mais que isso nao e observacao e fica de fora. Devolve
// quantas linhas SAT entregou.
uint32_t tab5_gnss_gravar(const SatelliteSnapshot_t* snap, uint32_t idade_max_ms);

// OS DIAS no cartao (nomes "satelites-AAAA-MM-DD.csv", do mais novo ao mais
// velho). Nao monta o cartao: lista se ele ja estiver montado. Devolve quantos.
#define TAB5_GNSS_DIA_NOME_TAM 32
int tab5_gnss_listar_dias(char (*dias)[TAB5_GNSS_DIA_NOME_TAM], int max);

// CARREGAR UM DIA para a memoria (PSRAM), uma vez: trocar o filtro de
// satelite depois e so um loop em RAM, sem tocar o cartao. Ja ser o dia
// carregado e nao fazer nada. false = sem memoria ou sem arquivo.
// Pode demorar (arquivo de MB): cede a CPU de vez em quando.
bool tab5_gnss_carregar_dia(const char* arquivo);
// O dia carregado. Os ponteiros valem ate descarregar/carregar outro dia.
const tab5_gnss_ponto_t* tab5_gnss_pontos(int* n);
const tab5_gnss_sat_t*   tab5_gnss_sats(int* n);
// Solta a memoria do dia (app fechado tem que custar zero).
void tab5_gnss_descarregar(void);
