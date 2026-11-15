// USB-A - a porta host do Tab5 (o pendrive que alguem espeta).
//
// HARDWARE (esquema oficial, ver docs/USB_A_E_FONE.md): o conector USB-A vai
// ao controlador OTG 2.0 de ALTA VELOCIDADE do P4 (pinos dedicados, PHY
// proprio) - outro controlador e outros pinos que os do USB-C. Nada daqui
// briga com o console, com o modo pendrive (o Tab5 virando disco do
// computador, services/tab5_pendrive) nem com o I2S do alto-falante. Os 5V
// do conector saem por uma chave com limite de corrente (MT9700) ligada pelo
// USB5V_EN do expansor - no perfil de energia, campo usb_a_5v, imposto pela
// preferencia ajustes_usb_a() (kernel/core.cpp).
//
// O QUE ESTE SERVICO FAZ: espetou um pendrive, ele aparece montado em
// /usb (FAT32 ou exFAT - components/fatfs liga o exFAT; NTFS e os do Mac
// nao, e a tabela de particoes tem que ser MBR, nao GPT), avisa
// o sistema (tab5_volumes_mudou -> AVISO_VOLUMES) e poe o aviso "Pendrive"
// na barra, que abre o app Musica. Tirou: quem estava lendo e avisado pelo
// tab5_volume_disponivel() e tem ate 3 s para soltar; depois o disco some.
//
// QUEM USA nao chama nada daqui direto: usa services/tab5_volumes
// (montar/desmontar por caminho). As funcoes abaixo sao do sistema, do
// console ('usb') e do proprio tab5_volumes.
//
// TAREFAS (pilhas na PSRAM): "usb_lib" (a USB Host Library), "usb_msc" (o
// driver da classe - as leituras do pendrive terminam nela, por isso a
// prioridade acima de quem le) e "usb" (montar, desmontar, ler o aparelho).
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TAB5_USB_RAIZ  "/usb"

// No boot, depois de sistema_iniciar(). Com o ajuste desligado nao sobe nada.
void tab5_usb_iniciar(void);

// O ajuste mudou (Ajustes, console). Liga/desliga os 5V do conector (pelo
// perfil de energia) e, ligando a primeira vez, sobe a pilha USB. Desligar
// corta a energia: o pendrive sai como se tivesse sido arrancado.
void tab5_usb_ligar(bool ligar);

// Segura / solta o pendrive (ver tab5_volumes.hpp). false = nao ha pendrive
// montado (ou ele esta saindo).
bool tab5_usb_montar(void);
void tab5_usb_desmontar(void);

// Ha um pendrive montado em /usb, e ele nao esta saindo.
bool tab5_usb_presente(void);

// EJETAR - antes de tirar um pendrive em que o sistema GRAVOU (o Arquivos
// copiando para ele). Arrancar no meio de uma gravacao deixa o FAT pela
// metade; ejetar espera quem segura o pendrive soltar (ate 'espera_ms'),
// desmonta - o FatFs grava o que tinha em cache - e avisa "pode tirar".
// O aparelho continua plugado e com energia, mas sem /usb: so volta a montar
// quando for tirado e espetado de novo (ou quando os 5V caem no repouso e
// voltam). Bloqueia: nunca da tarefa de interface.
typedef enum {
  USB_EJETADO = 0,
  USB_EJETAR_SEM_PENDRIVE,   // nao havia pendrive montado
  USB_EJETAR_EM_USO,         // alguem segurou alem do prazo: continua montado
} tab5_usb_ejetar_t;
tab5_usb_ejetar_t tab5_usb_ejetar(uint32_t espera_ms);

// O nome do pendrive (rotulo do volume; sem rotulo, o produto que ele diz
// ser); "" sem pendrive.
void tab5_usb_nome(char* out, size_t tam);

// Espaco do pendrive, em bytes. Pode demorar (o FAT conta os livres na
// primeira vez): fora da interface.
bool tab5_usb_espaco(uint64_t* total, uint64_t* livre);

// O USB-A NO REPOUSO (chamado pelo kernel, da interface: ao apagar a tela, a
// cada volta do repouso e ao acender). O conector NAO tem pino que avise "algo
// foi espetado": quem ve um aparelho chegar e o proprio controlador USB, e so
// com os 5V ligados. Entao, com a tela apagada e ninguem lendo do pendrive
// (nenhum tab5_usb_montar de pe - a musica tocando dele segura) e nada que nao
// seja disco plugado (um fone carregando pelo conector fica), os 5V caem:
// o pendrive desmonta, o aparelho some e o sono leve, que ele segurava, volta.
// Ao acender a tela os 5V voltam e o pendrive monta de novo sozinho (~1 s).
// Espetar algo com a tela apagada so e visto quando ela acender.
void tab5_usb_repouso(bool repouso);
// Os 5V estao cortados por tab5_usb_repouso (o perfil nao os religa).
bool tab5_usb_cortado(void);

// Para o console e a tela de diagnostico.
typedef struct {
  bool     ligado;          // pilha USB de pe e 5V pedidos
  bool     aparelho;        // ha algo plugado (qualquer aparelho)
  uint8_t  velocidade;      // 0 baixa (1,5 Mb/s), 1 cheia (12), 2 alta (480)
  uint16_t vid, pid;
  uint8_t  classe;          // bDeviceClass (0 = cada interface diz a sua)
  char     produto[40];     // o que o aparelho diz ser (UTF-8)
  bool     disco;           // o aparelho e um disco (classe MSC) e foi instalado
  bool     montado;         // ... e o FAT dele montou em /usb
  bool     ejetado;         // ... e foi ejetado (tab5_usb_ejetar): pode tirar
  uint32_t setores, setor_bytes;
  int      refs;            // quem segura o pendrive agora
  uint32_t conexoes;        // aparelhos vistos desde o boot
  char     erro[64];        // o ultimo problema ("" = nenhum)
} tab5_usb_estado_t;
void tab5_usb_estado(tab5_usb_estado_t* out);
