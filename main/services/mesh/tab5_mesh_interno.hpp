// O que os arquivos do servico Mesh compartilham: as constantes e os tipos
// usados por mais de um deles, o estado (declarado aqui, definido em
// tab5_mesh.cpp ou no arquivo indicado) e as funcoes internas que cruzam
// arquivos. So para eles: nenhum app inclui este header - app fala com o
// servico por tab5_mesh.hpp.
//
// O servico foi partido por assunto (so mudou de arquivo, nada de logica):
//   tab5_mesh.cpp             estado, envios em voo e desfecho, tarefa, envio, fila de envio, API do servico
//   mesh/tab5_mesh_hora.cpp       carimbo corrigido, que horas sao, hora ao vivo
//   mesh/tab5_mesh_nos.cpp        tabela de nos, favoritos, busca, cache de nomes, NodeDB no cartao
//   mesh/tab5_mesh_conversas.cpp  conversas e historico no cartao, apagadas, gravacao, ordem da coluna
//   mesh/tab5_mesh_canais.cpp     canais do no
//   mesh/tab5_mesh_receber.cpp    interpretacao das mensagens do radio
//   mesh/tab5_mesh_gps.cpp        posicao do GPS para a malha
#pragma once

#include "tab5_mesh.hpp"

// ---------------------------------------------------------------------------
// Constantes e tipos usados por mais de um arquivo
// ---------------------------------------------------------------------------
// NVS do proprio servico: o que o radio contou (nosso numero, canais,
// favoritos), separado das preferencias da pessoa (ajustes).
#define MESH_NVS "mesh"
#define MESH_PASTA "Meshtastic"
#define MESH_ARQ_ENVIOS "envios.txt"
// O piso de 2020 e para nao aceitar carimbo de radio que ainda nao sincronizou
// (ele manda 0, ou um numero pequeno de uptime) e ancorar a referencia no ano
// de 1970 - o que faria todo no aparecer visto ha 56 anos.
#define EPOCH_MINIMO 1577836800u          // 2020-01-01
#define MESH_MAX_FAVS  32
// Data (dentro do MeshPacket): 1=portnum 2=payload 6=request_id 9=bitfield
#define PORT_TEXTO      1
#define PORT_POSICAO    3
#define PORT_NODEINFO   4
#define PORT_ROTEAMENTO 5
#define PORT_TELEMETRIA 67

// ---------------------------------------------------------------------------
// Estado usado por mais de um arquivo (cada um definido no arquivo indicado)
// ---------------------------------------------------------------------------
// tab5_mesh.cpp:
extern bool s_servico_ativo;
extern uint8_t s_via;
extern mesh_no_t s_nos[MESH_MAX_NOS];
extern uint32_t s_despejos;
extern mesh_msg_t s_msgs[MESH_MAX_MSGS];
extern int s_n_msgs;
extern uint32_t s_total_msgs;
extern int s_nao_lidas;
extern int s_nao_lidas_dir;
extern mesh_saude_t s_saude;
extern uint32_t s_meu_num;
extern char s_firmware[24];
extern uint32_t s_ultimo_quadro_ms;
extern int s_notif;
extern char s_canal_nome[MESH_MAX_CANAIS][MESH_NOME_CONVERSA];
extern bool s_canal_ativo[MESH_MAX_CANAIS];
extern bool s_canais_conhecidos;
extern bool s_canais_sujos;
extern volatile bool s_radio_reiniciou;
extern mesh_conversa_t s_conv[MESH_MAX_CONVERSAS];
extern uint32_t s_conv_seq[MESH_MAX_CONVERSAS];
extern uint32_t s_seq;
extern uint32_t s_conv_ver;
extern uint32_t s_c_quadros, s_c_pacotes, s_c_telemetria;
extern uint32_t s_c_localstats, s_c_texto, s_c_nodeinfo;
extern int s_ultima_variante;

// tab5_mesh_hora.cpp:
extern uint32_t s_config_ok_ms;

// tab5_mesh_nos.cpp:
extern uint32_t s_nos_ver;
extern uint32_t s_favs[MESH_MAX_FAVS];
extern int s_n_favs;
extern bool s_nomes_lidos;
extern uint32_t s_nodedb_ver_salva;

// ---------------------------------------------------------------------------
// Funcoes internas que cruzam arquivos (cada uma definida no arquivo indicado)
// ---------------------------------------------------------------------------
// tab5_mesh.cpp:
void anotar_envio(uint32_t id, uint32_t para);
void marcar_envio(uint32_t id, int erro, uint32_t de, bool so_mqtt = false);
void travar(void);
void destravar(void);
void lembrar_meu_numero(uint32_t num);
uint32_t recordar_meu_numero(void);
bool enlace_enviar(const uint8_t* corpo, size_t n);

// tab5_mesh_hora.cpp:
void lembrar_carimbo(uint32_t id, uint32_t quando);
void ao_saltar_relogio(int32_t delta);
void marcar_epoch(uint32_t carimbo);
bool trafego_ao_vivo(void);
void marcar_hora_viva(uint32_t carimbo);

// tab5_mesh_nos.cpp:
void fixar_se_pelo_ar(mesh_no_t* no);
void recordar_favs(void);
bool radio_aceita_admin(void);
void enviar_favs_pendentes(void);
void fav_do_radio(uint32_t num, bool fav);
const char* longo_conhecido(uint32_t num);
void nome_legivel(const char* in, char* out, size_t tam);
bool curto_utilizavel(const char* s);
void nome_exibicao(uint32_t num, char* out, size_t out_tam);
void lembrar_nome(uint32_t num, const char* curto, const char* longo);
void descarregar_nomes(void);
void carregar_nomes(void);
mesh_no_t* achar_no(uint32_t num);
void salvar_nodedb(void);
void carregar_nodedb(void);

// tab5_mesh_conversas.cpp:
void guardar_nao_lidas(void);
bool ha_nao_lidas_guardadas(void);
bool esta_apagada(bool canal, uint32_t chave);
void desmarcar_apagada(bool canal, uint32_t chave);
int achar_ou_criar_conversa(bool canal, uint8_t idx, uint32_t no);
void gravar_msg(int ci, const mesh_msg_t* m);
void publicar_notif(void);

// tab5_mesh_canais.cpp:
void recordar_canais(void);
void salvar_canais(void);
void aplicar_canal(int idx, int papel, const char* nome_radio, const uint8_t* psk, size_t n_psk);
void criar_conversas_dos_canais(void);

// tab5_mesh_receber.cpp:
void guardar_msg(uint32_t de, uint32_t para, uint8_t canal, const char* txt, uint32_t quando,
                 float snr, int32_t rssi, uint32_t id, bool via_mqtt, uint8_t saltos);
void ler_from_radio(const uint8_t* buf, size_t n);

// tab5_mesh_gps.cpp:
bool enviar_posicao(void);
