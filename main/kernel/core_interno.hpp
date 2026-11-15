// O que os arquivos do core compartilham: o estado usado por mais de um deles
// (declarado aqui, definido em core.cpp ou no arquivo indicado) e as funcoes
// internas que cruzam arquivos. So para eles: nenhum app inclui este header.
// Quem cruza a fronteira core <-> shell continua sendo o kernel/internal.hpp
// (incluido por este); app fala com o sistema so por app.h.
//
// O core foi partido por assunto (so mudou de arquivo, nada de logica):
//   core.cpp          estado do motor, ISR do toque e do teclado, despertar do sono leve, tarefa de interface, sistema_iniciar
//   core_toque.cpp    vigia e lupa do GT911, leitura continua, dedos da amostra, bancada (simular toque/gesto/abrir)
//   core_radios.cpp   Bluetooth, Mesh pela internet, liga/desliga de Wi-Fi, Grove e via do Mesh
//   core_apps.cpp     abrir/fechar app, montagem do SD em tarefa propria, app recusado
//   core_energia.cpp  tela acesa por pedido, contrato de economia, relogio do painel, perfil
//   core_som.cpp      ociosidade do alto-falante, pinos do USB, som em fluxo, bip
//   core_desenho.cpp  area util, passadas medidas, passada de desenho do conteudo
#pragma once

#include "internal.hpp"
#include <atomic>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

// ---------------------------------------------------------------------------
// Estado usado por mais de um arquivo (cada um definido no arquivo indicado)
// ---------------------------------------------------------------------------
// core.cpp:
extern bool s_dedo_na_tela;                 // o NOSSO estado: desceu e ainda nao subiu
extern volatile bool s_toque_pulso;         // a ISR viu um pulso desde a ultima leitura
extern std::atomic<uint32_t> s_pulsos;      // pulsos do GT911 desde o boot (vigia do toque)
extern bool s_dedo_do_despertar;            // o dedo que acordou a tela (ou abriu o app) nao toca nada
extern TaskHandle_t s_tarefa_interface;     // capturado na criacao, ver sistema_iniciar()
extern uint32_t s_ultimo_desenho;

// core_toque.cpp:
extern uint32_t s_vig_falhas, s_vig_presos, s_vig_surdos, s_vig_reinicios;
extern uint8_t  s_vig_status;
extern sistema_toque_t s_toques[SISTEMA_TOQUES_MAX];
extern int      s_n_toques;
extern bool     s_toque_consumido;
extern volatile int s_sim_x, s_sim_y;
extern const app_t* volatile s_sim_abrir;
extern volatile bool s_sim_fechar;
extern volatile int s_simg[4];
extern volatile bool s_simg_pedido;

// core_apps.cpp:
extern volatile bool s_sd_montando;         // true do disparo ate a tarefa terminar

// core_energia.cpp:
extern std::atomic<uint32_t> s_acordado;
extern volatile bool s_pedir_acordar;
extern SemaphoreHandle_t s_relogio_feito;   // criado com a interface (sistema_iniciar)

// core_som.cpp:
extern const uint32_t SPEAKER_OCIOSO_MS;
extern uint32_t s_speaker_ocioso_desde;     // 0 = tocando ou desligado
extern SemaphoreHandle_t s_som_parou;       // criado em sistema_iniciar
extern SemaphoreHandle_t s_fluxo_pronto;    // criado em sistema_iniciar

// core_desenho.cpp:
extern uint32_t s_fase_limpar_us;           // a ultima limpeza da area (+ nav)

// ---------------------------------------------------------------------------
// Funcoes que cruzam arquivos
// ---------------------------------------------------------------------------
// core.cpp:
uint32_t periodo_do_app(void);

// core_toque.cpp:
void ajustar_leitura_do_toque(bool continuo);
void lupa_toque(uint32_t t, bool tocando);
void vigiar_toque(uint32_t t, bool tocando);
void vigiar_toque_repouso(uint32_t t, bool tocando, bool em_repouso);

// core_radios.cpp:
void vigiar_mesh_internet(uint32_t t);
void bt_aplicar(bool ligar);

// core_apps.cpp:
void concluir_abertura_do_sd(void);

// core_energia.cpp:
void atender_pedido_de_relogio(void);
tab5_perfil_t perfil_da_home(void);

// core_som.cpp:
void som_configurar(void);
void som_atender_fluxo(void);
void som_atender_reserva(void);
void atualizar_beep_e_ociosidade_do_som(uint32_t t);

// core_desenho.cpp:
area_t area_util(void);
int limite_do_app(const area_t& a);
void animar_na_rampa(void);
void anotar_passada(const char* nome, uint32_t us, uint32_t barra, uint32_t limpar, uint32_t conteudo);
// recorte_app: so o caminho de deslocamento de rolagem passa (o padrao e a area util inteira)
void desenhar_conteudo(bool limpar_area, bool completo_para_apps, const area_t* recorte_app = nullptr);
