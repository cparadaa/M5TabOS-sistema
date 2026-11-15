// VOLUMES - os discos do aparelho, vistos de um lugar so.
//
// O sistema tem mais de um lugar onde moram arquivos: o cartao SD (/sdcard,
// hal/tab5_sd) e o pendrive no USB-A (/usb, services/tab5_usb). Quem trabalha
// com ARQUIVOS - tocar uma musica, listar uma pasta, copiar - nao deveria
// saber de qual dos dois o caminho veio, nem como cada um monta. Este modulo
// e essa porta: tudo por CAMINHO ABSOLUTO ("/usb/Rock/x.mp3").
//
//   if (tab5_volume_montar(caminho)) {      // segura o disco onde ele mora
//     int fd = open(caminho, O_RDONLY);
//     ...
//     tab5_volume_desmontar(caminho);       // solta
//   }
//
// MONTAR = SEGURAR. O cartao monta de verdade na primeira referencia (ver
// tab5_sd.hpp); o pendrive ja esta montado enquanto esta plugado, e segurar
// so faz quem tira o pendrive ESPERAR quem esta lendo terminar (ate 3 s).
// Quem segura por muito tempo (o tocador) pergunta tab5_volume_disponivel() a
// cada bloco e solta quando ele vira false: o cartao foi emprestado ao
// computador, ou o pendrive foi arrancado.
//
// MUDOU: quando um disco entra ou sai, o sistema publica AVISO_VOLUMES (app.h)
// - quem lista volumes recarrega.
//
// Tudo aqui e seguro de qualquer tarefa, mas montar o cartao bloqueia
// (~100 ms): fora da tarefa de interface.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>

#define TAB5_VOL_CARTAO_RAIZ    "/sdcard"   // = TAB5_SD_RAIZ
#define TAB5_VOL_PENDRIVE_RAIZ  "/usb"      // = TAB5_USB_RAIZ

typedef enum {
  TAB5_VOL_NENHUM = -1,     // o caminho nao e de disco nenhum
  TAB5_VOL_CARTAO = 0,
  TAB5_VOL_PENDRIVE,
  TAB5_VOL_N,
} tab5_vol_id_t;

typedef struct {
  tab5_vol_id_t id;
  const char*   raiz;        // "/sdcard", "/usb"
  const char*   tipo;        // "Cartão SD", "Pendrive" (para mostrar)
  char          nome[40];    // o nome do disco no computador (rotulo; UTF-8), ou ""
  bool          disponivel;  // da para usar AGORA (montar deve funcionar)
} tab5_vol_t;

// Os discos que existem agora (o cartao sempre aparece - "sem cartao" e
// descoberto ao montar; o pendrive so quando plugado e lido). Devolve quantos.
int tab5_volumes_listar(tab5_vol_t* out, int max);

// De qual disco e este caminho absoluto (pelo comeco: "/usb", "/usb/...").
tab5_vol_id_t tab5_volume_de(const char* caminho);

// O disco deste caminho, para mostrar ("Pendrive", "Cartão SD"); nunca NULL.
const char* tab5_volume_tipo(const char* caminho);

// Segura / solta o disco onde mora 'caminho'. false = nao deu (sem cartao,
// cartao emprestado, pendrive ausente ou saindo) - e ai NAO conta referencia.
bool tab5_volume_montar(const char* caminho);
void tab5_volume_desmontar(const char* caminho);

// LER um arquivo de qualquer disco (cartao ou pendrive). Segura o disco onde ele
// mora e, no cartao, grava antes o que estava pendente (a leitura ve a ultima
// escrita). NULL = falhou (o disco ja foi solto). Fechar SEMPRE com
// tab5_volume_fechar (com o mesmo caminho) - so se abriu.
FILE* tab5_volume_abrir_leitura(const char* caminho);
void  tab5_volume_fechar(FILE* f, const char* caminho);   // fecha e solta o disco

// O arquivo INTEIRO na PSRAM (ate 'max' bytes; maior que isso = falha). Devolve o
// buffer (liberar com heap_caps_free) e o tamanho em *n; NULL = falhou.
uint8_t* tab5_volume_ler_tudo(const char* caminho, size_t max, size_t* n);

// LEITOR RAPIDO - para arquivo GRANDE lido aos poucos (imagem, GIF).
//
// POR QUE NAO FREAD. fread no cartao rende 52-572 KB/s: cada chamada pede um
// pedaco pequeno ao FAT, que le setor por setor. read() POSIX em blocos de
// 64 KB, num buffer alinhado a linha de cache (128 B) na PSRAM, chega a 4,6 MB/s
// (ver o leitor de BMP16 que o app Arquivos tinha). O leitor guarda esse
// buffer e entrega o que o chamador pedir, do tamanho que for.
//
//   tab5_leitor_t* r = tab5_leitor_abrir(caminho);   // segura o disco
//   if (r) {
//     tab5_leitor_ler(r, cab, 16);
//     tab5_leitor_ir(r, 4096);                       // salta para um ponto
//     ...
//     tab5_leitor_fechar(r);                         // solta o disco
//   }
//
// Como abrir_leitura: monta o disco (e no cartao grava antes o que estava
// pendente). Bloqueia: fora da tarefa de interface. Um leitor e de UMA tarefa.
typedef struct tab5_leitor tab5_leitor_t;
tab5_leitor_t* tab5_leitor_abrir(const char* caminho);   // nullptr = falhou (o disco ja foi solto)
// Le ate 'n' bytes para 'dst' (nullptr = so descarta). Devolve quantos leu
// (menos que 'n' so no fim do arquivo) ou -1 em erro.
int            tab5_leitor_ler(tab5_leitor_t* r, void* dst, size_t n);
// Vai para a posicao 'pos' do arquivo (false = alem do fim ou erro). Dentro do
// que ja esta no buffer e de graca; fora, o disco e lido a partir do setor.
bool           tab5_leitor_ir(tab5_leitor_t* r, uint64_t pos);
uint64_t       tab5_leitor_pos(const tab5_leitor_t* r);       // onde a proxima leitura comeca
uint64_t       tab5_leitor_tamanho(const tab5_leitor_t* r);
void           tab5_leitor_fechar(tab5_leitor_t* r);          // fecha e solta o disco

// Tamanho e espaco livre do disco de 'caminho', em bytes. Pode demorar (o
// FAT conta os livres na primeira vez): fora da interface.
bool tab5_volume_espaco(const char* caminho, uint64_t* total, uint64_t* livre);

// O disco continua usavel? (Para quem segura por muito tempo.)
bool tab5_volume_disponivel(const char* caminho);

// EJETAR - so o pendrive (o cartao nao sai com o aparelho ligado). Espera
// quem segura o disco soltar (ate 'espera_ms'), desmonta e avisa "pode
// tirar" (ver tab5_usb_ejetar). Bloqueia: fora da tarefa de interface.
typedef enum {
  TAB5_VOL_EJETADO = 0,
  TAB5_VOL_EJETAR_NAO_DA,    // nao e um disco que se ejeta, ou ja nao esta la
  TAB5_VOL_EJETAR_EM_USO,    // alguem (uma copia, a musica) segurou alem do prazo
} tab5_vol_ejetar_t;
bool              tab5_volume_ejetavel(const char* caminho);
tab5_vol_ejetar_t tab5_volume_ejetar(const char* caminho, uint32_t espera_ms);

// Para os DONOS dos discos (tab5_usb, tab5_sd): um disco entrou ou saiu.
// Aumenta a versao, publica AVISO_VOLUMES e acorda a interface.
void tab5_volumes_mudou(void);
