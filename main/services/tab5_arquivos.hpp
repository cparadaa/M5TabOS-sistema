// ARQUIVOS - o motor de arquivos do sistema: listar, copiar, mover, apagar.
//
// POR QUE EXISTE. Cada app que precisava de arquivo fazia o proprio opendir
// (Camera, Azimutal, Trilha, Exemplo, Arquivos), e copiar nao existia em
// lugar nenhum - quem espetava um pendrive com musicas nao tinha como passa-
// las para o cartao. Aqui fica o motor, uma vez; o app Arquivos (e o Berry)
// so mostram e pedem. Regra da casa: servico no sistema, app so interface.
//
// TUDO POR CAMINHO ABSOLUTO, em qualquer disco (services/tab5_volumes):
// "/sdcard/Música/Rock", "/usb/Fotos/x.jpg". O servico segura o disco
// (tab5_volume_montar) enquanto mexe nele - quem chama nao monta nada.
//
// O QUE APARECE E O QUE SE MEXE (o mesmo para todo app):
//   - ESCONDIDO: nome que comeca com '.' (a .Lixeira, as pastas que o Mac
//     deixa), "System Volume Information" e "$RECYCLE.BIN" do Windows, e os
//     arquivos com senha do sistema (wifi.txt, wifi_no.txt em apps/).
//     Escondido nao aparece, nao se le e nao se copia.
//   - SO LEITURA: /sdcard/apps/ (os dados dos apps e do sistema) - menos
//     /sdcard/apps/Berry/, onde moram os apps Berry (copiar um .be para la
//     instala). Le-se e copia-se PARA FORA; nao se apaga, renomeia ou cola.
//   - O resto do cartao e o pendrive inteiro: livres.
//
// APAGAR NAO SOME: vai para a lixeira do proprio disco (<raiz>/.Lixeira),
// que e esvaziada como acao separada (tab5_arq_esvaziar_lixeira).
//
// COPIAR E MOVER sao TRABALHO DE FUNDO: uma vaga do pool, prioridade baixa
// (abaixo do tocador - musica tocando do cartao continua), blocos de 64 KB
// com read()/write() POSIX (o stdio e ~10x mais lento no FAT). O trabalho
// segue com o app fechado e com a tela apagada (os 5V do pendrive nao caem:
// ele segura o disco), mostra o progresso na barra e avisa no fim. Pendrive
// arrancado ou cartao emprestado ao computador viram erro limpo; o arquivo
// pela metade e apagado. Um trabalho por vez.
//
// QUEM CHAMA: as funcoes que mexem no disco bloqueiam (montar o cartao,
// FAT) - do worker (sistema_trabalho), nunca da tarefa de interface. As de
// estado (tab5_arq_trabalho, a area de transferencia) sao seguras de
// qualquer tarefa.
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define TAB5_ARQ_NOME_MAX     96    // nome de um item, UTF-8, com o '\0'
#define TAB5_ARQ_CAMINHO_MAX  256   // caminho absoluto, com o '\0'
#define TAB5_ARQ_LIXEIRA      ".Lixeira"


// ---------------------------------------------------------------------------
// LISTAR
// ---------------------------------------------------------------------------
// O tipo, pela extensao - para o icone e o "abrir com".
typedef enum {
  TAB5_ARQ_OUTRO = 0,
  TAB5_ARQ_PASTA,
  TAB5_ARQ_MUSICA,     // .mp3 .wav
  TAB5_ARQ_IMAGEM,     // .bmp .jpg .jpeg .png
  TAB5_ARQ_TEXTO,      // .txt .csv .log .json .md .m3u .gpx .nmea
  TAB5_ARQ_BERRY,      // .be
} tab5_arq_tipo_t;
tab5_arq_tipo_t tab5_arq_tipo(const char* nome);
// O nome termina em 'ext' (com o ponto, ex.: ".csv"; maiuscula ou minuscula)?
bool tab5_arq_extensao_e(const char* nome, const char* ext);

typedef struct {
  char            nome[TAB5_ARQ_NOME_MAX];   // como esta no disco (UTF-8)
  tab5_arq_tipo_t tipo;
  uint64_t        bytes;   // arquivo: tamanho; pasta: 0 (somar e caro)
  int64_t         data;    // ultima mudanca (epoch), 0 = desconhecida
} tab5_arq_item_t;

typedef enum {
  TAB5_ARQ_ORDEM_NOME = 0,      // A-Z
  TAB5_ARQ_ORDEM_NOME_INVERSA,  // Z-A (o log de hoje primeiro: log_2026-10-21.txt)
  TAB5_ARQ_ORDEM_DATA,          // o mais novo primeiro
} tab5_arq_ordem_t;

// As entradas VISIVEIS de 'pasta', pastas primeiro, na ordem pedida.
// 'filtro': extensoes aceitas, ";" entre elas (".mp3;.wav"), ou nullptr =
// todas; pastas passam sempre. Devolve quantas copiou (ate 'max'); 'total'
// (pode ser nullptr) recebe quantas a pasta tem - pode passar de 'max'.
// -1 = o disco nao esta ai ou a pasta nao abriu.
int tab5_arq_listar(const char* pasta, tab5_arq_item_t* out, int max, int* total,
                    tab5_arq_ordem_t ordem, const char* filtro);

// PERCORRER - cada entrada visivel de 'pasta', sem tabela e sem limite (uma
// pasta com milhares de fotos), na ordem do disco. Rapido: nao le tamanho nem
// data (no item vem 0) - quem precisa disso usa tab5_arq_listar. 'visita'
// devolve false para parar. Devolve quantas visitou; -1 = a pasta nao abriu.
typedef bool (*tab5_arq_visita_t)(const tab5_arq_item_t* item, void* ctx);
int tab5_arq_percorrer(const char* pasta, tab5_arq_visita_t visita, void* ctx);

// O MAIOR NUMERO entre os nomes que seguem 'formato' (um sscanf com UM %lu:
// "foto_%04lu.bmp"). 0 = nenhum. E o "proximo arquivo" de quem grava
// numerado (Camera, Trilha): a numeracao vem do disco, nao de uma variavel
// que nasce zerada a cada boot.
uint32_t tab5_arq_maior_numero(const char* pasta, const char* formato);

// ---------------------------------------------------------------------------
// PERMISSOES (as regras do topo; nao tocam no disco)
// ---------------------------------------------------------------------------
bool tab5_arq_visivel(const char* caminho);      // aparece, le e copia
bool tab5_arq_gravavel(const char* pasta);       // pode criar e colar DENTRO dela
bool tab5_arq_mexivel(const char* caminho);      // pode apagar, renomear, recortar
bool tab5_arq_nome_valido(const char* nome);     // nome novo aceitavel no FAT

// ---------------------------------------------------------------------------
// OPERACOES RAPIDAS (bloqueiam pouco - mas tocam no disco: fora da interface)
// ---------------------------------------------------------------------------
typedef enum {
  TAB5_ARQ_OK = 0,
  TAB5_ARQ_ERRO_PROTEGIDO,   // area so leitura ou escondida
  TAB5_ARQ_ERRO_NOME,        // nome invalido
  TAB5_ARQ_ERRO_EXISTE,      // ja ha um item com esse nome (ou ja esta ali)
  TAB5_ARQ_ERRO_DISCO,       // o disco nao esta ai (sem cartao, emprestado, pendrive saiu)
  TAB5_ARQ_ERRO_OCUPADO,     // ja ha um trabalho de copia rodando
  TAB5_ARQ_ERRO_ESPACO,      // nao cabe no destino
  TAB5_ARQ_ERRO_DENTRO,      // pasta para dentro dela mesma
  TAB5_ARQ_ERRO_FALHOU,      // o disco recusou (ver o log)
} tab5_arq_erro_t;
const char* tab5_arq_erro_texto(tab5_arq_erro_t e);   // para mostrar; nunca NULL

tab5_arq_erro_t tab5_arq_criar_pasta(const char* pasta, const char* nome);

// ARQUIVO NOVO AOS PEDACOS - o que vem de fora (baixar da internet, receber):
// cria '<pasta>/<nome>' (nome repetido vira "x (2).ext"), segura o disco ate
// fechar e, se nao terminou bem (manter = false, erro ao gravar, disco saiu),
// APAGA o pedaco: nunca fica arquivo pela metade. read/write POSIX (rapido).
typedef struct { int fd; char caminho[TAB5_ARQ_CAMINHO_MAX + TAB5_ARQ_NOME_MAX]; char pasta[TAB5_ARQ_CAMINHO_MAX]; } tab5_arq_novo_t;
tab5_arq_erro_t tab5_arq_novo_abrir(tab5_arq_novo_t* a, const char* pasta, const char* nome);
bool            tab5_arq_novo_escrever(tab5_arq_novo_t* a, const void* dados, size_t n);
tab5_arq_erro_t tab5_arq_novo_fechar(tab5_arq_novo_t* a, bool manter);
tab5_arq_erro_t tab5_arq_renomear(const char* caminho, const char* novo_nome);
// Para a lixeira do disco (rapido: so muda de pasta).
tab5_arq_erro_t tab5_arq_apagar(const char* caminho);

// ---------------------------------------------------------------------------
// COPIAR, MOVER, ESVAZIAR - trabalho de fundo
// ---------------------------------------------------------------------------
// Confere tudo na hora (permissoes, discos, pasta dentro dela mesma, um
// trabalho por vez) e, se aceitou, comeca em segundo plano: TAB5_ARQ_OK =
// comecou, nao "terminou". O fim e o progresso vem em tab5_arq_trabalho() e
// pelo AVISO_ARQUIVOS (app.h). Os caminhos sao copiados: quem chama pode
// liberar os seus. Nome repetido no destino vira "Nome (2).mp3".
#define TAB5_ARQ_ITENS_MAX  64
tab5_arq_erro_t tab5_arq_copiar(const char* const* origens, int n, const char* destino, bool mover);

// Quantos itens (soltos, sem entrar em pasta) a lixeira do disco de 'raiz'
// tem. 0 = vazia ou sem disco. Toca no disco: fora da interface.
int tab5_arq_lixeira_itens(const char* raiz);

// Apaga DE VEZ o que esta na lixeira do disco de 'raiz' ("/sdcard", "/usb"),
// com o indice.
tab5_arq_erro_t tab5_arq_esvaziar_lixeira(const char* raiz);

// LIXEIRA POR ITEM - cada apagar guarda a origem e a hora em <raiz>/.Lixeira/.indice.
typedef struct {
  char     nome[TAB5_ARQ_NOME_MAX];       // como esta na lixeira (pode ser "x (2)")
  char     origem[TAB5_ARQ_CAMINHO_MAX];  // caminho de onde saiu; "" = desconhecida
  uint32_t quando;                        // epoch de quando foi apagado; 0 = desconhecido
  uint64_t bytes;                         // arquivo: tamanho; pasta: soma de tudo dentro
  bool     pasta;
} tab5_arq_lixo_t;

// Lista a lixeira, do mais recente para o mais antigo (sem data no fim).
// Devolve quantos copiou (ate 'max'); 'total_bytes' (pode ser nullptr) recebe
// o tamanho de TUDO. Soma pastas inteiras: pode ser lento, chamar de trabalho
// de fundo (nunca da interface).
int tab5_arq_lixeira_listar(const char* raiz, tab5_arq_lixo_t* out, int max, uint64_t* total_bytes);

// Devolve um item para a origem (recria as pastas que faltam; nome ocupado vira
// "x (2)"; origem desconhecida = raiz do disco). Rapido (so renomeia), mas toca
// no disco: fora da interface.
tab5_arq_erro_t tab5_arq_lixeira_restaurar(const char* raiz, const char* nome);

// Apaga de vez UM item (pasta inteira). Pasta grande demora: chamar de trabalho
// de fundo. ERRO_OCUPADO = ha copia/esvaziar rodando.
tab5_arq_erro_t tab5_arq_lixeira_excluir(const char* raiz, const char* nome);

// Exclui de vez os itens apagados ha mais de 'dias' dias (so os que tem data).
// Devolve quantos. Pode demorar: trabalho de fundo. 0 se o relogio esta sem hora.
int tab5_arq_lixeira_limpar_antigos(const char* raiz, int dias);

// Quanto a lixeira do disco de 'raiz' pode pesar antes do aviso: 8% da capacidade
// do disco, no minimo 256 MB e no maximo 10 GB. Toca no disco: fora da interface.
uint64_t tab5_arq_lixeira_limite(const char* raiz);

// Manutencao da lixeira de 'raiz' (trabalho de fundo): se o ajuste "limpar sozinho"
// esta ligado, exclui o que passou de N dias; depois confere o tamanho e publica
// (ou tira) o aviso "Lixeira" na central de avisos. Pode demorar.
void tab5_arq_lixeira_manter(const char* raiz);

// Para o trabalho em curso (o arquivo pela metade e apagado; o que ja foi
// copiado fica).
void tab5_arq_cancelar(void);

typedef enum {
  TAB5_ARQ_PARADO = 0,   // nunca houve trabalho (ou o fim ja foi visto)
  TAB5_ARQ_CONTANDO,     // medindo o que vai copiar
  TAB5_ARQ_COPIANDO,
  TAB5_ARQ_APAGANDO,     // a origem de um mover entre discos, ou a lixeira
  TAB5_ARQ_FEITO,
  TAB5_ARQ_FALHOU,
  TAB5_ARQ_CANCELADO,
} tab5_arq_fase_t;

typedef struct {
  tab5_arq_fase_t fase;
  bool     mover;
  int      itens, itens_feitos;      // arquivos
  uint64_t bytes, bytes_feitos;
  uint32_t kbps;                     // ritmo medio
  char     atual[TAB5_ARQ_NOME_MAX]; // o arquivo da vez
  char     destino[TAB5_ARQ_CAMINHO_MAX];
  char     erro[96];                 // FALHOU: o que houve
  uint32_t versao;                   // muda a cada passo (para redesenhar)
} tab5_arq_trabalho_t;
void tab5_arq_trabalho(tab5_arq_trabalho_t* out);
bool tab5_arq_ocupado(void);
// Quem mostrou o fim ao usuario (o Arquivos ao abrir): o aviso da barra sai
// e a fase volta a PARADO.
void tab5_arq_trabalho_visto(void);

// ---------------------------------------------------------------------------
// AREA DE TRANSFERENCIA - do sistema, nao do app: sobrevive a fechar o app
// ---------------------------------------------------------------------------
// Copiar / Recortar guardam os caminhos; colar e o tab5_arq_copiar deles.
// Recortado e colado com sucesso, a area esvazia; copiado, continua (cola de
// novo noutra pasta).
bool tab5_arq_area_por(const char* const* caminhos, int n, bool recortar);
int  tab5_arq_area(bool* recortar);                    // quantos itens (0 = vazia)
bool tab5_arq_area_item(int i, char* out, size_t tam);
void tab5_arq_area_limpar(void);
tab5_arq_erro_t tab5_arq_colar(const char* destino);
