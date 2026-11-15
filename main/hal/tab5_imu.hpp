// IMU (BMI270) - leitura compartilhada, no MESMO espirito de tab5_power.hpp
// (medir() vs ultima()) e tab5_sensores.hpp (cache em vez de queue).
//
// -------------------------------------------------------------------------
// POR QUE ISTO EXISTE - achado ao revisar "pronto para mais de um
// consumidor", o mesmo exercicio que corrigiu tab5_gps.cpp.
// -------------------------------------------------------------------------
// app_imu.cpp sempre leu o BMI270 direto (M5.Imu.update()/getAccel/getGyro),
// e isso era SEGURO - mas nao porque alguem pensou em concorrencia: e
// porque o unico chamador e o proprio desenhar() do app, que so roda na
// tarefa de interface, a UNICA que pode falar no barramento I2C interno
// (regra documentada em tab5_power.hpp: "o I2C_Class do M5Unified nao tem
// mutex e duas tarefas intercalando leem registrador trocado sem dar erro").
// Nenhum outro app tinha como perguntar "qual a inclinacao do aparelho
// agora" sem duplicar aquele codigo E sem correr o risco de acessar o
// barramento de uma tarefa errada.
//
// A SOLUCAO, EXATAMENTE COMO PARA ENERGIA E TEMPERATURA: uma funcao que MEDE
// de verdade (so da tarefa de interface) e outra que LE O CACHE (qualquer
// tarefa, protegido por mutex - ver o aviso sobre tab5_gps.cpp que motivou
// isto).
//
// -------------------------------------------------------------------------
// CADENCIAS DIFERENTES DE PROPOSITO - isto NAO e so "copiar o padrao do INA226"
// -------------------------------------------------------------------------
// tab5_power_medir()/tab5_sensores_medir() rodam 1x/segundo porque energia e
// temperatura mudam devagar - 1Hz serve pra qualquer consumidor, incluindo
// quem "e dono" delas. A IMU e diferente: o proprio app_imu.cpp precisa de
// leitura A CADA FRAME para o indicador de nivel nao parecer travado -
// amostrar so 1x/segundo pioraria a UNICA tela que hoje usa isto de verdade.
//
// Por isso o app_imu.cpp le por tab5_imu_amostra() (abaixo), na mesma tarefa,
// a cada quadro - ele e seguro (mesma tarefa) e rapido. tab5_imu_medir()
// roda em paralelo, 1x/segundo, alimentando SO o cache que os OUTROS apps vao
// ler - "o aparelho esta de pe ou deitado", nao "o nivel bolha precisa ser
// fluido". Um consumidor que precisar de mais frequencia que isso no futuro
// precisa da MESMA discussao que este arquivo documenta, nao so aumentar um
// numero.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  bool  valida;   // false ate a primeira medida (ou se a IMU nunca subiu)
  float ax, ay, az;   // g (aceleracao, com gravidade incluida)
  float gx, gy, gz;   // graus/s (giroscopio)
} tab5_imu_dados_t;

// Le o BMI270 DE VERDADE e atualiza o cache. SO DA TAREFA DE INTERFACE -
// mesma regra do INA226/temperatura/teclado, ver o aviso grande acima.
// No-op silencioso se M5.Imu.isEnabled() for false (perfil.imu=false no
// boot deste app, ou nenhum app que precise de IMU rodou ainda).
void tab5_imu_medir(void);

// Em repouso a medida nao roda, mas o GIROSCOPIO ainda precisa seguir o
// pedido: um pedido do Berry vencido (5 s) ou retirado com a tela apagando
// deixava o giroscopio ligado o repouso inteiro (centenas de uA). So aplica o
// estado pedido - I2C so quando o estado muda. SO DA TAREFA DE INTERFACE.
void tab5_imu_repouso(void);

// A ULTIMA leitura, sem tocar no I2C - o que qualquer tarefa deve usar.
// Devolve false (com 'valida'=false em '*saida') se a IMU nunca mediu.
bool tab5_imu_ultima(tab5_imu_dados_t* saida);

// A IMU subiu e responde? (nao toca no I2C)
bool tab5_imu_disponivel(void);

// A amostra MAIS RECENTE direto do chip (aceleracao em g, giro em graus/s), a
// cada quadro de tela. SO DA TAREFA DE INTERFACE. Devolve false se a IMU nao
// esta disponivel ou nao ha amostra nova desde a ultima chamada.
bool tab5_imu_amostra(float a[3], float g[3]);

// ---------------------------------------------------------------------------
// O GIROSCOPIO SO QUANDO ALGUEM LE
// ---------------------------------------------------------------------------
// O M5Unified liga o BMI270 com giroscopio e sem economia (o pior consumo do
// chip) e assim ele ficava a sessao inteira. O giro da tela (kernel/
// orientacao) so usa o ACELEROMETRO: sem pedido, o sistema deixa so ele, em
// economia. Quem le o giroscopio pede com o proprio bit e solta depois.
//
// De QUALQUER tarefa (um OR atomico): a escrita no chip acontece na tarefa de
// interface, no proximo tab5_imu_medir() - ate 1 s depois. Quem le direto
// do sensor (o app IMU) chama tab5_imu_medir() logo depois de pedir.
#define TAB5_GIRO_APP_IMU (1u << 0)   // app IMU (le direto, a cada quadro)
#define TAB5_GIRO_RADAR   (1u << 1)   // app Radar
#define TAB5_GIRO_BERRY   (1u << 2)   // sistema.giro() - ver tab5_imu_giroscopio_berry
void tab5_imu_giroscopio(uint32_t quem, bool sim);

// O pedido do Berry e por PRAZO: um script nao avisa quando para de ler.
// Cada sistema.giro() renova; 5 s sem chamada, o pedido cai sozinho.
void tab5_imu_giroscopio_berry(void);
