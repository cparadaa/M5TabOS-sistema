// TOCADOR DO SISTEMA - o servico de musica. Le os arquivos da pasta de musica
// do cartao, decodifica (MP3 pelo componente minimp3, WAV PCM direto) e
// alimenta o alto-falante em blocos (sistema_som_fluxo_*), com qualquer app
// na tela - ou nenhum.
//
// QUEM USA E SO INTERFACE. O app Musica (main/app_musica) e o modulo Berry
// 'musica' (services/berry_apps) sao duas caras deste mesmo servico: listam,
// pedem para tocar, mostram o estado e o espectro. Nenhum deles decodifica,
// le o cartao ou mexe no alto-falante. Um tocador so no aparelho: quem pede
// para tocar troca a fila de quem estava tocando.
//
// O QUE O SISTEMA GARANTE SEM NINGUEM PEDIR (o servico roda como parte de
// fundo do kernel, kernel/fundo.h, com o nome "Música"):
//   - toca com a janela de quem pediu fechada;
//   - a barra mostra o que toca, e a central de avisos tem o miniplayer e o X
//     que para (kernel/midia.h) - tocar o titulo abre o app que pediu;
//   - pausado por TAB5_MUSICA_PAUSA_MAX_S (ou sem nada para tocar por
//     TAB5_MUSICA_VAZIO_MAX_S), encerra sozinho;
//   - o cartao indo para o computador (pendrive) para a musica na hora;
//   - a capa de cada musica (etiqueta, pasta ou internet) e procurada sozinha.
// Tambem moram aqui o aleatorio/repetir, as playlists (.m3u) e a duracao de
// cada arquivo - a interface so pede e mostra.
//
// DE ONDE TOCA: a pasta de musica do cartao (caminhos RELATIVOS a ela,
// "Rock/x.mp3") ou qualquer pasta de um disco, por caminho ABSOLUTO
// ("/usb/Rock/x.mp3" - o pendrive no USB-A, services/tab5_volumes). Toda
// funcao que recebe 'rel' ou 'caminho' aceita os dois; a fila, as playlists
// e as Favoritas misturam os dois sem problema (a faixa de um pendrive que
// nao esta plugado e pulada). O pendrive inteiro e "a pasta de musica" dele:
// listar("/usb") mostra a raiz do pendrive.
//
// TAREFAS: tudo daqui pode ser chamado de qualquer tarefa (interface, tarefa
// de um app, Berry, console). Os controles so marcam um pedido; quem atende
// e o laco do servico, entre um bloco de som e outro.
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TAB5_MUSICA_NOME_MAX   128           // nome de arquivo como esta no disco (bytes, UTF-8)
#define TAB5_MUSICA_CAMINHO_MAX 256          // relativo a pasta de musica ("Rock/x.mp3") ou absoluto ("/usb/...")
#define TAB5_MUSICA_FILA_MAX   400           // arquivos numa fila (uma pasta ou uma playlist)
#define TAB5_MUSICA_LISTA_MAX  400           // itens que listar() devolve de uma pasta
#define TAB5_MUSICA_PAUSA_MAX_S (5 * 60)     // pausado (com musica carregada) por mais que isto, o tocador se encerra
#define TAB5_MUSICA_VAZIO_MAX_S 30           // sem nada para tocar (a lista acabou, parou): encerra antes
#define TAB5_MUSICA_FAIXAS     32            // faixas do espectro
// As playlists sao arquivos .m3u nesta subpasta da pasta de musica - o mesmo
// formato que qualquer tocador do computador abre (linhas "../Rock/x.mp3").
// A pasta nao aparece em listar(): a interface mostra as playlists a parte.
#define TAB5_MUSICA_PLAYLISTS  "Playlists"
#define TAB5_MUSICA_FAVORITAS  "Favoritas"   // a playlist do coracao

// ---- a pasta de musica ------------------------------------------------------
// Qualquer pasta na RAIZ do cartao cujo nome, sem acento e sem diferenca de
// maiuscula, comece por "music" (Music, Música, Musicas, MUSICA~1); com mais de
// uma, a que tem mais coisa. Nenhuma: cria "Music". A "Music" vazia que o
// sistema criou antes e apagada quando ha outra com musica.
// out recebe o caminho absoluto ("/sdcard/Música"). false = sem cartao.
bool tab5_musica_pasta(char* out, size_t tam);

typedef struct {
  char nome[TAB5_MUSICA_NOME_MAX];   // como esta no disco (UTF-8)
  bool pasta;
} tab5_musica_item_t;

// O conteudo de uma subpasta ('rel' relativo a pasta de musica, "" = a raiz;
// ou absoluto, "/usb" = a raiz do pendrive):
// pastas primeiro, depois .mp3/.wav, em ordem alfabetica (a pasta das
// playlists e as ocultas ficam de fora). Devolve quantos
// (ate 'max'); -1 = sem cartao ou a pasta nao existe. 'cheia' (opcional) =
// havia mais que 'max'.
int tab5_musica_listar(const char* rel, tab5_musica_item_t* out, int max, bool* cheia);

// Um nome do disco, para mostrar. O FAT ja entrega UTF-8 (e isto so copia);
// texto que nao e UTF-8 valido veio de antes disso - uma playlist antiga,
// gravada em CP437 - e e convertido.
void tab5_musica_nome_utf8(const char* nome, char* out, size_t out_tam);

// ---- tocar --------------------------------------------------------------------
// TOCA a pasta 'rel' a partir do item 'idx' de tab5_musica_listar(rel) (tem
// que ser um arquivo): a fila sao as musicas da pasta, em ordem. 'app' = o
// nome do app que pediu (tocar o titulo no miniplayer abre ele; nullptr = o
// app Musica). false = nao ligou (sem cartao, item nao e musica, sem memoria).
bool tab5_musica_tocar(const char* rel, int idx, const char* app);

// TOCA UM arquivo so ('caminho' absoluto, ex. "/sdcard/Videos/filme.mp3"): a
// fila tem ele sozinho, entao no fim o tocador para (ou repete, se o repetir
// estiver ligado) em vez de seguir para o proximo da pasta. E o som de um
// video (services/tab5_video). 'app' como em tab5_musica_tocar.
bool tab5_musica_tocar_um(const char* caminho, const char* app);

// TOCA uma playlist (o nome como esta no cartao, sem .m3u) a partir da faixa
// 'idx' de tab5_musica_playlist_itens(); idx < 0 = do comeco (com o
// aleatorio ligado, uma qualquer). As faixas que sumiram do cartao sao
// puladas. false = playlist vazia ou nao abriu.
bool tab5_musica_tocar_playlist(const char* nome, int idx, const char* app);

// ---- como a fila anda ---------------------------------------------------------
// ALEATORIO: a fila e embaralhada (a musica atual fica tocando e vira a
// primeira); desligar volta a ordem da pasta/playlist sem cortar o som.
// REPETIR: NAO = toca ate o fim e para; TUDO = no fim volta ao comeco (com o
// aleatorio, embaralha de novo); UMA = a mesma musica de novo ao acabar (os
// botoes de proxima/anterior continuam andando na fila). Guardados nos
// ajustes: valem para a proxima fila e ao ligar de novo.
typedef enum {
  TAB5_MUSICA_REPETIR_NAO = 0,
  TAB5_MUSICA_REPETIR_TUDO,
  TAB5_MUSICA_REPETIR_UMA,
} tab5_musica_repetir_t;
void tab5_musica_definir_aleatorio(bool sim);
bool tab5_musica_aleatorio(void);
void tab5_musica_definir_repetir(tab5_musica_repetir_t r);
tab5_musica_repetir_t tab5_musica_repetir(void);

void tab5_musica_tocar_pausar(void);
void tab5_musica_proxima(void);
void tab5_musica_anterior(void);   // depois de 3 s de musica, volta ao comeco dela
void tab5_musica_ir_para(uint32_t ms);
void tab5_musica_parar(void);      // pede e segue: o servico sai logo depois

typedef struct {
  bool     ativo;          // ha fila (o servico esta rodando)
  bool     tocando;        // false = pausado (ou carregando)
  int      idx, n;         // posicao na ordem em que a fila toca (0 = a primeira)
  char     pasta[160];     // a pasta da fila, caminho absoluto ("" = fila de playlist)
  char     rel[160];       // a mesma, relativa a pasta de musica ("" = a raiz)
  char     playlist[TAB5_MUSICA_NOME_MAX];   // a playlist que toca (como no cartao); "" = uma pasta
  char     caminho[TAB5_MUSICA_CAMINHO_MAX]; // o que toca, relativo a pasta de musica
  char     arquivo[TAB5_MUSICA_NOME_MAX];    // so o nome do arquivo
  char     titulo[64];     // da etiqueta ID3, ou do nome do arquivo ("Artista - Titulo (Video)" -> "Titulo")
  char     artista[48];    // da etiqueta ID3, ou da parte antes do " - " no nome ("" = nao tem)
  char     album[48];      // da etiqueta ID3 ("" = nao tem)
  bool     aleatorio;
  uint8_t  repetir;        // tab5_musica_repetir_t
  uint32_t pos_ms, dur_ms; // a posicao do que SAI no alto-falante; dur 0 = desconhecida
  uint32_t hz;
  uint8_t  canais;
  uint16_t kbps;           // 0 = WAV
  char     erro[48];       // o ultimo problema ("" = nenhum)
  // DIAGNOSTICO (console: musica estado): o som engasga quando a fila do
  // alto-falante seca. 'secas' conta os blocos enfileirados com a fila JA
  // vazia (fora o primeiro depois de abrir, buscar ou retomar).
  uint32_t blocos, secas;
  uint32_t dec_max_us, dec_media_us;   // decodificar um bloco
  uint32_t bloco_ms;                   // quanto som cabe num bloco
  uint32_t ler_no_pior_us;             // do pior bloco, quanto foi lendo o cartao
  uint32_t ler_soma_ms;                // tempo total lendo o cartao
  uint32_t esp_max_us;                 // pior tempo do espectro num bloco
} tab5_musica_estado_t;

void tab5_musica_estado(tab5_musica_estado_t* out);
void tab5_musica_zerar_diag(void);

// ---- o espectro ---------------------------------------------------------------
// As faixas (0..255, graves primeiro) do som que esta saindo AGORA. Cada
// quadro (1024 amostras, ~23 ms) leva a hora em que sai no alto-falante: a
// barra bate com o que se ouve. O servico so calcula enquanto ALGUEM LE: a
// primeira leitura liga, e um segundo sem leitura desliga - com a janela
// fechada ou a tela apagada, nao gasta nada. false = nada saindo (pausa,
// fim, ou a primeira leitura, que so liga o calculo).
bool tab5_musica_espectro(uint8_t* faixas);

// ---- a cor --------------------------------------------------------------------
// A cor de destaque do tocador (RGB565; 0 = a do sistema): a pastilha da barra
// e o miniplayer usam esta. Guardada nos ajustes: vale ao ligar de novo.
void tab5_musica_definir_cor(uint16_t cor565);

// ---- playlists ------------------------------------------------------------------
// Tudo aqui le ou grava o cartao: chamar FORA da tarefa de interface
// (sistema_trabalho), menos o que diz o contrario. Nomes de playlist como
// estao no cartao (sem .m3u, UTF-8).
typedef struct {
  char caminho[TAB5_MUSICA_CAMINHO_MAX];   // relativo a pasta de musica
} tab5_musica_faixa_t;

// As playlists, em ordem alfabetica (a Favoritas primeiro). 'out[i].pasta' =
// false; -1 = sem cartao.
int  tab5_musica_playlists(tab5_musica_item_t* out, int max);
// Cria uma vazia. O nome vem da pessoa (UTF-8); letras que o cartao nao
// guarda viram a mais parecida. 'nome_cartao' (opcional) recebe o nome final.
// false = ja existe, nome vazio ou sem cartao.
bool tab5_musica_playlist_criar(const char* nome_utf8, char* nome_cartao, size_t tam);
bool tab5_musica_playlist_apagar(const char* nome);
// As faixas, na ordem do arquivo. -1 = nao existe.
int  tab5_musica_playlist_itens(const char* nome, tab5_musica_faixa_t* out, int max);
// Poe no fim (a playlist nasce se nao existir) / tira. false = ja estava (por)
// ou nao estava (tirar).
bool tab5_musica_playlist_por(const char* nome, const char* caminho);
bool tab5_musica_playlist_tirar(const char* nome, const char* caminho);
bool tab5_musica_playlist_tem(const char* nome, const char* caminho);

// ---- duracao ----------------------------------------------------------------------
// Quanto dura um arquivo (caminho relativo a pasta de musica), em ms, sem
// decodificar: le o cabecalho (MP3: Xing/VBRI, ou o bitrate e o tamanho; WAV:
// o cabecalho). Alguns ms de cartao - fora da interface. 0 = nao deu.
// Lembra o que ja mediu (o mesmo arquivo nao e lido duas vezes).
uint32_t tab5_musica_duracao(const char* caminho);
// So o que ja foi medido, sem tocar no cartao (pode ser da interface).
// false = ainda nao medido.
bool tab5_musica_duracao_lembrada(const char* caminho, uint32_t* ms);

// ---- capa -------------------------------------------------------------------------
// A CAPA DO QUE TOCA. Ao abrir uma musica o servico procura, numa tarefa
// propria e nesta ordem: a imagem dentro da etiqueta ID3 (APIC), uma
// cover/folder/capa.jpg na pasta da musica, e - com o Wi-Fi conectado - a
// busca do iTunes por artista e titulo. O que achar vai para o cache
// <pasta de musica>/.capas/ (a proxima vez nao busca de novo; a internet so e
// consultada uma vez por musica).
typedef enum {
  TAB5_MUSICA_CAPA_NENHUMA = 0,   // nao tem (ou nada tocando)
  TAB5_MUSICA_CAPA_BUSCANDO,      // procurando (ainda pode aparecer)
  TAB5_MUSICA_CAPA_PRONTA,        // 'arquivo' = JPEG ou PNG no cartao, caminho absoluto
} tab5_musica_capa_t;
// Pode ser da interface (so copia o estado). 'versao' muda a cada capa nova
// (troca de musica, a busca terminou): quem desenha recarrega quando mudar.
tab5_musica_capa_t tab5_musica_capa(char* arquivo, size_t tam, uint32_t* versao);
// Le o arquivo da capa ('arquivo' de tab5_musica_capa) inteiro para a PSRAM
// (JPEG ou PNG; ate 3 MB). Le o cartao: chamar FORA da interface. Quem
// chamou libera com heap_caps_free. nullptr = nao abriu ou grande demais.
uint8_t* tab5_musica_capa_ler(const char* arquivo, size_t* n);

// ---- nome para mostrar ----------------------------------------------------------
// O titulo (e o artista, se o nome trouxer) a partir do nome de um arquivo:
// "Artista - Titulo (Video).mp3" -> "Titulo" e "Artista". Sem " - ", o nome
// todo e o titulo; '_' vira espaco; sem extensao e sem trechos entre
// parenteses/colchetes. 'artista' pode ser nullptr (so o titulo).
void tab5_musica_titulo_do_arquivo(const char* nome, char* titulo, size_t tt, char* artista, size_t ta);
