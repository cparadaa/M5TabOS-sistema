// Comandos do console - CARTAO: cartao, lista, ler, despejar, apagar, receber, arquivos.
// (Partido de console.cpp; a tabela COMANDOS e a ajuda continuam la.)

#include "mbedtls/base64.h"
#include "tab5_ui.hpp"   // tab5_bytes_txt
#include "sdkconfig.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
  #include "driver/usb_serial_jtag.h"
  #include "driver/usb_serial_jtag_vfs.h"
#else
  #include "driver/uart.h"
  #include "driver/uart_vfs.h"
#endif
#include "tab5_arquivos.hpp"
#include "mbedtls/md5.h"
#include "ff.h"
#include "console/console_interno.hpp"

// Tamanho legivel, num buffer estatico proprio. Estatico e nao do chamador
// porque ele aparece duas vezes na MESMA linha de printf em alguns pontos - e
// dois usos simultaneos do mesmo buffer se atropelariam. Por isso alterna
// entre dois.
static const char* tamanho_curto(uint64_t bytes)
{
  static char buf[2][16];
  static int  alt = 0;
  char* b = buf[alt = (alt + 1) & 1];
  tab5_bytes_txt(b, 16, bytes);
  return b;
}

// Panorama do cartao: espaco e uma linha por pasta de aplicativo.
//
// O `lista` mostra UMA pasta e serve para procurar arquivo. Este responde outra
// pergunta, a que se faz primeiro: "o que existe aqui e quanto esta ocupado?".
void cmd_cartao(void)
{
  {
    // O gravador do sistema (hal/tab5_sd.hpp): o que ainda espera, o que ja foi.
    uint32_t pend = 0, grav = 0, perd = 0;
    tab5_sd_anexos_estado(&pend, &grav, &perd);
    printf(tr("gravador: %lu B esperando, %lu linha(s) gravadas, %lu perdida(s) desde o boot\n"),
           (unsigned long)pend, (unsigned long)grav, (unsigned long)perd);
  }
  if (!tab5_sd_montar()) {
    printf(tr("cartao nao montou (%s)\n"), tab5_sd_descricao());
    return;
  }

  printf(tr("--- cartao ---\n  %s\n"), tab5_sd_descricao());

  // Espaco livre pelo FATFS. O statvfs do VFS nao e implementado para FAT no
  // IDF, entao vamos ao driver: clusters livres x setores por cluster.
  FATFS* fs = nullptr;
  DWORD livres = 0;
  if (f_getfree("0:", &livres, &fs) == FR_OK && fs) {
    // SETOR REAL, nao FF_MAX_SS. O FF_MAX_SS e o setor MAXIMO que o FATFS foi
    // compilado para aceitar (4096 no IDF); o do cartao e 512. Multiplicar pelo
    // maximo dava 8x a mais - um cartao de 14,5GB aparecia com 118GB.
    //
    // Quando FF_MAX_SS == FF_MIN_SS o campo ssize nem existe (o FATFS o remove,
    // porque so ha um tamanho possivel), dai o #if.
#if FF_MAX_SS != FF_MIN_SS
    const uint64_t setor = fs->ssize;
#else
    const uint64_t setor = FF_MAX_SS;
#endif
    const uint64_t tot = (uint64_t)(fs->n_fatent - 2) * fs->csize * setor;
    const uint64_t liv = (uint64_t)livres * fs->csize * setor;
    printf(tr("  %llu MB livres de %llu MB (%llu%% em uso)\n"),
           liv / (1024 * 1024), tot / (1024 * 1024),
           tot ? ((tot - liv) * 100 / tot) : 0);
  }

  DIR* d = opendir(TAB5_SD_APPS);
  if (!d) {
    printf(tr("  (nao ha %s ainda)\n"), TAB5_SD_APPS);
    tab5_sd_desmontar();
    return;
  }

  printf("\n  %-16s %6s  %s\n", tr("pasta"), tr("arqs"), tr("tamanho"));
  struct dirent* e;
  int k = 0;
  while ((e = readdir(d)) != nullptr) {
    ceder(k++);
    if (e->d_name[0] == '.') { continue; }

    char sub[160];
    snprintf(sub, sizeof(sub), "%s/%s", TAB5_SD_APPS, e->d_name);

    // Conta e soma sem recursao: as pastas de app sao planas por convencao
    // (ver tab5_sd.hpp), e descer em arvore aqui so criaria caminho para
    // estourar stack num cartao que alguem encheu de subpastas.
    DIR* sd = opendir(sub);
    if (!sd) { continue; }

    // Guarda os nomes para imprimir DEPOIS do cabecalho da pasta - o total tem
    // que aparecer em cima, e so da para saber o total depois de percorrer.
    #define CARTAO_MOSTRA 10
    char nomes[CARTAO_MOSTRA][40];
    uint64_t tams[CARTAO_MOSTRA];
    int n = 0; uint64_t bytes = 0;

    struct dirent* f2;
    while ((f2 = readdir(sd)) != nullptr) {
      if (f2->d_name[0] == '.') { continue; }
      char cam[320];
      snprintf(cam, sizeof(cam), "%s/%s", sub, f2->d_name);
      struct stat st;
      if (stat(cam, &st) != 0 || !S_ISREG(st.st_mode)) { continue; }
      if (n < CARTAO_MOSTRA) {
        snprintf(nomes[n], sizeof(nomes[0]), "%s", f2->d_name);
        tams[n] = (uint64_t)st.st_size;
      }
      ++n;
      bytes += (uint64_t)st.st_size;
    }
    closedir(sd);

    printf("  %-16s %6d  %s\n", e->d_name, n, tamanho_curto(bytes));

    // Os arquivos, indentados. Faltava exatamente isto: dava para ver que a
    // pasta Sistema tinha 3 arquivos e 1,5MB, e NAO dava para ver se o firmware
    // do C6 era um deles.
    const int mostrar = (n < CARTAO_MOSTRA) ? n : CARTAO_MOSTRA;
    for (int i = 0; i < mostrar; ++i) {
      printf("      %-28.28s %10s\n", nomes[i], tamanho_curto(tams[i]));
    }
    if (n > CARTAO_MOSTRA) {
      printf(tr("      ... e mais %d\n"), n - CARTAO_MOSTRA);
    }
  }
  closedir(d);
  tab5_sd_desmontar();
  printf(tr("\n  'lista %s/<pasta>' mostra os arquivos de uma delas\n"), TAB5_SD_APPS);
}

// ---------------------------------------------------------------------------
// RECEBER ARQUIVO PELA SERIAL
// ---------------------------------------------------------------------------
//   receber <app> <arquivo> <bytes>
//
// Nasceu de uma necessidade concreta: mandar 1,4MB de firmware do C6 para o
// cartao sem tirar o cartao do aparelho.
//
// TRES DECISOES QUE NAO SAO OBVIAS:
//
// 1. LE DIRETO DO DRIVER, nao do stdin. O console roda com
//    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR), que traduz
//    fim de linha na entrada - inofensivo para texto, DESTRUTIVO para binario:
//    todo 0x0D viraria outra coisa. O usb_serial_jtag_read_bytes() passa ao
//    largo do VFS e entrega o byte como veio.
//
// 2. GUARDA TUDO NA PSRAM ANTES DE ESCREVER NO CARTAO. A serial nao tem
//    controle de fluxo: se a gente parar para escrever no SD - e o FATFS para,
//    as vezes por centenas de milissegundos, quando fecha um cluster - o buffer
//    de recepcao enche e os bytes se perdem em silencio. Com 32MB de PSRAM,
//    receber inteiro e depois gravar desacopla as duas coisas por completo.
//
// 3. NAO IMPRIME NADA DURANTE A RECEPCAO. O jeito natural de mandar o arquivo e
//    "cat arquivo > /dev/cu.usbmodem*", e nesse arranjo NINGUEM esta lendo a
//    saida. Se a gente escrever, o buffer de TX enche e a escrita bloqueia -
//    parando justamente o loop que precisa consumir a entrada. Silencio ate o
//    fim e o que mantem o fluxo vivo.
#define RECEBER_MAX (6u * 1024u * 1024u)


void cmd_receber(int argc, char** argv)
{
  if (argc < 4) {
    printf("%s", tr("uso: receber <app> <arquivo> <bytes>\n"
           "  ex: receber Sistema tab5_c6.bin 1397904\n"
           "\n"
           "Depois de dar o comando, mande o arquivo CRU pela mesma porta.\n"
           "Do Mac, com o monitor FECHADO:\n"
           "  cat arquivo.bin > /dev/cu.usbmodem*\n"
           "\n"
           "Ou tudo de uma vez, sem precisar do monitor:\n"
           "  { printf 'receber Sistema tab5_c6.bin 1397904\\r'; sleep 2; \\\n"
           "    cat arquivo.bin; } > /dev/cu.usbmodem*\n"));
    return;
  }

  const long tam = atol(argv[3]);
  if (tam <= 0 || (unsigned long)tam > RECEBER_MAX) {
    printf(tr("tamanho invalido (1 a %u bytes)\n"), (unsigned)RECEBER_MAX);
    return;
  }

  uint8_t* buf = (uint8_t*)heap_caps_malloc((size_t)tam, MALLOC_CAP_SPIRAM);
  if (!buf) { printf(tr("sem PSRAM para %ld bytes\n"), tam); return; }

  printf(tr("mande %ld bytes agora (10 s de paciencia entre blocos)\n"), tam);
  fflush(stdout);

  size_t lidos = 0;
  int vazios = 0;
  while (lidos < (size_t)tam) {
    const int n = usb_serial_jtag_read_bytes(buf + lidos, (size_t)tam - lidos,
                                             pdMS_TO_TICKS(1000));
    if (n > 0) { lidos += (size_t)n; vazios = 0; }
    else if (++vazios >= 10) { break; }   // desistiu de esperar
  }

  // GRAVA O QUE CHEGOU, mesmo incompleto. Jogar 1,4MB fora por causa dos
  // ultimos 2KB nao ajuda ninguem: com o arquivo no cartao e o md5 na tela,
  // da para ver SE faltou e QUANTO faltou. Quem decide se serve e voce - e o
  // `c6 gravar` confere o cabecalho antes de usar, de qualquer forma.
  const bool completo = (lidos == (size_t)tam);
  if (lidos == 0) {
    printf("%s", tr("\nnao chegou byte nenhum.\n"));
    heap_caps_free(buf);
    return;
  }
  if (!completo) {
    printf(tr("\nINCOMPLETO: %u de %ld bytes (faltaram %ld). Gravando assim mesmo\n"
           "para voce conferir - compare o md5 abaixo.\n"),
           (unsigned)lidos, tam, tam - (long)lidos);
  }

  // MD5 para conferir contra o `md5 -q` do Mac. Um tamanho igual nao prova
  // nada: byte trocado no meio da serial da o mesmo tamanho.
  unsigned char md5[16];
  mbedtls_md5(buf, lidos, md5);

  bool ok = false;
  if (tab5_sd_montar()) {
    FILE* f = tab5_sd_abrir(argv[1], argv[2], "wb");
    if (f) {
      ok = (fwrite(buf, 1, lidos, f) == lidos);
      fclose(f);
    }
    tab5_sd_desmontar();
  }
  heap_caps_free(buf);

  if (!ok) { printf("%s", tr("\nrecebi tudo, mas a gravacao no cartao falhou\n")); return; }

  printf(tr("\ngravado: /sdcard/apps/%s/%s  (%u bytes)%s\nmd5: "),
         argv[1], argv[2], (unsigned)lidos, completo ? "" : tr("  <- INCOMPLETO"));
  for (int i = 0; i < 16; ++i) { printf("%02x", md5[i]); }
  printf("\n");
}

// ---------------------------------------------------------------------------
// ler <caminho> [linhas]
// ---------------------------------------------------------------------------
int cmd_ler(int argc, char** argv)
{
  if (argc < 2) { printf("%s", tr("uso: ler <caminho> [linhas]\n")); return 1; }
  char buf[160];
  const char* caminho = caminho_sd(argv[1], buf, sizeof(buf));
  // Teto de linhas: um satelites-*.csv de um dia inteiro tem milhares delas,
  // e despejar tudo a 115200 baud prenderia o console por minutos. Quem quer
  // mais pede o numero; quem quer o arquivo inteiro usa 'despejar'.
  int teto = (argc >= 3) ? atoi(argv[2]) : 200;
  if (teto < 1) { teto = 1; }

  return com_cartao([&]() -> int {
    FILE* f = fopen(caminho, "r");
    if (!f) { printf(tr("erro: %s nao abriu\n"), caminho); return 1; }

    printf("--- %s ---\n", caminho);
    char linha[160];
    int n = 0;
    while (n < teto && fgets(linha, sizeof(linha), f)) {
      fputs(linha, stdout);
      if ((++n % 64) == 0) { fflush(stdout); vTaskDelay(1); }
    }
    if (n >= teto && !feof(f)) {
      printf(tr("... (cortado em %d linhas - 'ler %s <N>' mostra mais, 'despejar' mostra tudo)\n"),
             teto, argv[1]);
    }
    printf("%s", tr("--- fim ---\n"));
    fclose(f);
    return 0;
  });
}

// ---------------------------------------------------------------------------
// despejar <caminho>  - o arquivo INTEIRO, para ser capturado do outro lado
// ---------------------------------------------------------------------------
// Irmao do "ler", com tres diferencas que sao a razao de ele existir separado:
//
//   1. SEM TETO DE LINHAS. O "ler" corta em 200 porque ele e para OLHAR - e
//      olhar mais que isso na tela do terminal nao serve para nada. Este e para
//      COPIAR: um energia.csv de uma descarga inteira tem umas 500 linhas, e um
//      arquivo cortado no meio nao serve para ajustar curva nenhuma.
//
//   2. CALA O LOG ENQUANTO DESPEJA. Este console divide a serial com o
//      ESP_LOG de todas as tarefas. Sem calar, uma linha de "power" ou
//      "mesh" cai NO MEIO do CSV, e quem esta capturando do outro lado recebe
//      um arquivo contaminado. Restaurado no fim, sempre - inclusive quando o
//      fopen falha.
//
//   3. MARCADORES QUE O SCRIPT RECONHECE. O ">>>INICIO" e o ">>>FIM" com a
//      contagem de linhas dizem ao capturador onde comeca, onde termina e se
//      chegou tudo. Sem eles, o script do outro lado teria que adivinhar
//      quando parar de escutar - e adivinhar sempre erra na hora errada.
int cmd_despejar(int argc, char** argv)
{
  if (argc < 2) {
    printf("%s", tr("uso: despejar <caminho>\n"
           "  ex.: despejar /sdcard/apps/Sistema/energia/energia-AAAA-MM-DD.csv\n"
           "  no Mac, para salvar:  ./ferramentas/baixar.py <caminho>\n"));
    return 1;
  }
  // -b: BINARIO (foto, firmware), em base64 - fgets/fputs quebram no
  // primeiro byte nulo. O baixar.py decodifica.
  const bool binario = strcmp(argv[1], "-b") == 0;
  if (binario && argc < 3) { printf("%s", tr("uso: despejar -b <caminho>\n")); return 1; }
  char buf[160];
  const char* caminho = caminho_sd(argv[binario ? 2 : 1], buf, sizeof(buf));

  if (binario) {
    return com_cartao([&]() -> int {
      FILE* f = fopen(caminho, "rb");
      if (!f) { printf(tr("erro: %s nao abriu\n"), caminho); return 1; }
      long tam = 0;
      tam = tab5_sd_tamanho(f); if (tam < 0) { tam = 0; }
      esp_log_level_set("*", ESP_LOG_NONE);
      printf(tr(">>>INICIO %s %ld base64\n"), caminho, tam);
      unsigned char cru[57 * 16];
      unsigned char b64[80];
      long n = 0;
      size_t lidos;
      while ((lidos = fread(cru, 1, sizeof(cru), f)) > 0) {
        for (size_t o = 0; o < lidos; o += 57) {
          size_t olen = 0;
          const size_t pedaco = lidos - o < 57 ? lidos - o : 57;
          mbedtls_base64_encode(b64, sizeof(b64), &olen, cru + o, pedaco);
          b64[olen] = 0;
          puts((const char*)b64);
          // Mais vezes que no texto: com o buffer da serial USB cheio, linhas
          // inteiras eram descartadas (medido: 6% de uma foto se perdia).
          if ((++n % 2) == 0) { fflush(stdout); vTaskDelay(1); }
        }
      }
      fflush(stdout);
      printf(tr(">>>FIM %ld linhas\n"), n);
      esp_log_level_set("*", ESP_LOG_INFO);
      fclose(f);
      return 0;
    });
  }

  return com_cartao([&]() -> int {
    FILE* f = fopen(caminho, "r");
    if (!f) { printf(tr("erro: %s nao abriu\n"), caminho); return 1; }

    // Tamanho ANTES, para o outro lado poder conferir o que recebeu.
    long tam = 0;
    tam = tab5_sd_tamanho(f); if (tam < 0) { tam = 0; }

    // A partir daqui, silencio - ver o motivo 2 no cabecalho.
    esp_log_level_set("*", ESP_LOG_NONE);

    printf(tr(">>>INICIO %s %ld\n"), caminho, tam);
    char linha[256];
    long n = 0;
    while (fgets(linha, sizeof(linha), f)) {
      fputs(linha, stdout);
      // Uma pausa a cada 64 linhas. Nao e educacao: o despejo de um arquivo
      // grande e um loop fechado nesta tarefa, e sem ceder a CPU o watchdog de
      // tarefa ociosa reclama e a interface engasga.
      if ((++n % 64) == 0) { fflush(stdout); vTaskDelay(1); }
    }
    fflush(stdout);
    printf(tr(">>>FIM %ld linhas\n"), n);

    esp_log_level_set("*", ESP_LOG_INFO);
    fclose(f);
    return 0;
  });
}

// ---------------------------------------------------------------------------
// lista [caminho]
// ---------------------------------------------------------------------------
int cmd_lista(int argc, char** argv)
{
  char buf[160];
  const char* caminho = caminho_sd(argc >= 2 ? argv[1] : ".", buf, sizeof(buf));

  return com_cartao([&]() -> int {
    DIR* d = opendir(caminho);
    if (!d) { printf(tr("erro: %s nao abriu\n"), caminho); return 1; }

    printf("--- %s ---\n", caminho);
    struct dirent* e;
    int i = 0;
    while ((e = readdir(d)) != nullptr)
    {
      ceder(i++);
      char completo[300];
      snprintf(completo, sizeof(completo), "%s/%s", caminho, e->d_name);
      struct stat st;
      if (e->d_type == DT_DIR)            { printf(tr("  [dir]  %s\n"), e->d_name); }
      else if (stat(completo, &st) == 0)  { printf("  %6ld  %s\n", (long)st.st_size, e->d_name); }
      else                                { printf("     ?   %s\n", e->d_name); }
    }
    closedir(d);
    printf("%s", tr("--- fim ---\n"));
    return 0;
  });
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// apagar <caminho>  - remove UM arquivo do cartao
// ---------------------------------------------------------------------------
//
// Existe para poder comecar uma gravacao do zero sem tirar o cartao. Apagar e
// irreversivel, entao:
//   - so aceita caminho dentro de /sdcard/, para um dedo errado nao alcancar
//     outra coisa;
//   - um arquivo por vez, sem curinga: nada de "apagar *".
int cmd_apagar(int argc, char** argv)
{
  if (argc < 2) { printf("%s", tr("uso: apagar apps/<App>/<arquivo>\n")); return 1; }
  char buf[160];
  const char* caminho = caminho_sd(argv[1], buf, sizeof(buf));

  if (strncmp(caminho, "/sdcard/", 8) != 0 || strstr(caminho, "..") != nullptr)
  {
    printf("%s", tr("recusado: so apago dentro de /sdcard/ (e sem '..')\n"));
    return 1;
  }

  return com_cartao([&]() -> int {
    struct stat st;
    if (stat(caminho, &st) != 0) { printf(tr("nao existe: %s\n"), caminho); return 1; }

    if (remove(caminho) != 0) { printf(tr("nao consegui apagar %s\n"), caminho); return 1; }
    printf(tr("apagado: %s (%ld bytes)\n"), caminho, (long)st.st_size);
    return 0;
  });
}

void listar_pasta(const char* pasta, const char* filtro)
{
  const int MAX = 200;
  tab5_arq_item_t* it = (tab5_arq_item_t*)heap_caps_malloc(sizeof(tab5_arq_item_t) * MAX, MALLOC_CAP_SPIRAM);
  if (!it) { printf("%s", tr("sem memoria\n")); return; }
  int total = 0;
  const int n = tab5_arq_listar(pasta, it, MAX, &total, TAB5_ARQ_ORDEM_NOME, filtro);
  if (n < 0) { printf(tr("nao abriu %s (disco ausente, escondida ou nao existe)\n"), pasta); }
  static const char* const TIPO[] = { "", "/", " (musica)", " (imagem)", " (texto)", " (berry)" };
  for (int i = 0; i < n && !console_interrompido(); ++i) {
    ceder(i);
    if (it[i].tipo == TAB5_ARQ_PASTA) { printf("  %s/\n", it[i].nome); }
    else { printf("  %-40s %10llu%s\n", it[i].nome, (unsigned long long)it[i].bytes, TIPO[it[i].tipo]); }
  }
  if (n >= 0) {
    printf(tr("%d de %d item(s); %s\n"), n, total, tab5_arq_gravavel(pasta) ? tr("gravavel") : tr("SO LEITURA"));
  }
  heap_caps_free(it);
}

// 'arquivos lixeira <raiz> [restaurar <nome> | excluir <nome> | esvaziar | antigos <dias>]'
// Sem acao, lista. O nome pode ter espacos ("x (2).txt"): junta o resto da linha.
static void lixeira_arquivos(int argc, char** argv, const char* raiz, void (*erro)(tab5_arq_erro_t))
{
  const char* acao = argc >= 4 ? argv[3] : "";
  char nome[TAB5_ARQ_NOME_MAX] = "";
  for (int i = 4; i < argc; ++i) {
    snprintf(nome + strlen(nome), sizeof(nome) - strlen(nome), "%s%s", i > 4 ? " " : "", argv[i]);
  }
  if (eh(acao, "esvaziar")) { erro(tab5_arq_esvaziar_lixeira(raiz)); return; }
  if (eh(acao, "restaurar") || eh(acao, "excluir")) {
    if (!nome[0]) { printf("%s", tr("uso: arquivos lixeira <raiz> restaurar|excluir <nome>\n")); return; }
    erro(eh(acao, "restaurar") ? tab5_arq_lixeira_restaurar(raiz, nome) : tab5_arq_lixeira_excluir(raiz, nome));
    return;
  }
  if (eh(acao, "antigos")) {
    if (argc < 5) { printf("%s", tr("uso: arquivos lixeira <raiz> antigos <dias>\n")); return; }
    printf(tr("%d item(s) excluido(s)\n"), tab5_arq_lixeira_limpar_antigos(raiz, atoi(argv[4])));
    return;
  }
  if (acao[0]) { printf("%s", tr("acao desconhecida: use restaurar, excluir, esvaziar ou antigos\n")); return; }

  // Listar
  const int MAX = 200;
  tab5_arq_lixo_t* it = (tab5_arq_lixo_t*)heap_caps_malloc(sizeof(tab5_arq_lixo_t) * MAX, MALLOC_CAP_SPIRAM);
  if (!it) { printf("%s", tr("sem memoria\n")); return; }
  uint64_t total = 0;
  const int n = tab5_arq_lixeira_listar(raiz, it, MAX, &total);
  for (int i = 0; i < n && !console_interrompido(); ++i) {
    ceder(i);
    char data[16] = "?";
    if (it[i].quando) {
      const time_t t = (time_t)it[i].quando;
      struct tm tm;
      localtime_r(&t, &tm);
      strftime(data, sizeof(data), "%Y-%m-%d", &tm);
    }
    printf("%3d  %s%s  %s  %s  <- %s\n", i + 1, it[i].nome, it[i].pasta ? "/" : "", data,
           tamanho_curto(it[i].bytes), it[i].origem[0] ? it[i].origem : "?");
  }
  printf(tr("%d item(s), %s no total\n"), n, tamanho_curto(total));
  heap_caps_free(it);
}

// O motor de arquivos (services/tab5_arquivos): o mesmo que o app Arquivos
// usa, para testar sem tela. Caminhos absolutos (/sdcard/..., /usb/...) ou
// relativos a pasta atual da sessao ('cd').
void c_arquivos(int argc, char** argv)
{
  static void (*const erro)(tab5_arq_erro_t) = [](tab5_arq_erro_t r) {
    printf(r == TAB5_ARQ_OK ? tr("ok\n") : tr("recusado: %s\n"), tab5_arq_erro_texto(r));
  };
  char c2[TAB5_ARQ_CAMINHO_MAX] = "", c3[TAB5_ARQ_CAMINHO_MAX] = "";
  if (argc >= 3) { resolver(argv[2], c2, sizeof(c2)); }
  if (argc >= 4) { resolver(argv[3], c3, sizeof(c3)); }
  if (argc >= 2 && eh(argv[1], "ls")) {
    listar_pasta(argc >= 3 ? c2 : sessao()->cwd, argc >= 4 ? argv[3] : nullptr);
    return;
  }
  if (argc >= 4 && (eh(argv[1], "cp") || eh(argv[1], "mv"))) {
    const char* o = c2;
    erro(tab5_arq_copiar(&o, 1, c3, eh(argv[1], "mv")));
    printf("%s", tr("acompanhe com 'arquivos estado'\n"));
    return;
  }
  if (argc >= 3 && eh(argv[1], "rm"))     { erro(tab5_arq_apagar(c2)); return; }
  if (argc >= 4 && eh(argv[1], "mkdir"))  { erro(tab5_arq_criar_pasta(c2, argv[3])); return; }
  if (argc == 3 && eh(argv[1], "mkdir")) {
    // Um caminho so (o mkdir de sempre): a pasta pai e o nome novo.
    char* b = strrchr(c2, '/');
    if (!b || !b[1]) { printf("%s", tr("uso: mkdir <pasta nova>\n")); return; }
    *b = 0;
    erro(tab5_arq_criar_pasta(c2[0] ? c2 : "/", b + 1));
    return;
  }
  if (argc >= 4 && eh(argv[1], "ren"))    { erro(tab5_arq_renomear(c2, argv[3])); return; }
  if (argc >= 3 && eh(argv[1], "lixeira")) { lixeira_arquivos(argc, argv, c2, erro); return; }
  if (argc >= 2 && eh(argv[1], "cancelar")) { tab5_arq_cancelar(); printf("%s", tr("pedido\n")); return; }
  if (argc >= 3 && (eh(argv[1], "copiar") || eh(argv[1], "recortar"))) {
    const char* c = c2;
    printf(tab5_arq_area_por(&c, 1, eh(argv[1], "recortar")) ? tr("na area\n") : tr("recusado\n"));
    return;
  }
  if (argc >= 3 && eh(argv[1], "colar")) { erro(tab5_arq_colar(c2)); return; }
  // estado
  tab5_arq_trabalho_t t;
  tab5_arq_trabalho(&t);
  static const char* const FASE[] = { "parado", "contando", "copiando", "apagando", "feito", "FALHOU", "cancelado" };
  printf(tr("trabalho: %s%s, %d/%d arquivo(s), %.1f/%.1f MB, %lu KB/s\n"),
         FASE[t.fase <= TAB5_ARQ_CANCELADO ? t.fase : 0], t.mover ? tr(" (mover)") : "",
         t.itens_feitos, t.itens, t.bytes_feitos / 1048576.0, t.bytes / 1048576.0, (unsigned long)t.kbps);
  if (t.atual[0])   { printf(tr("arquivo: %s\n"), t.atual); }
  if (t.destino[0]) { printf(tr("destino: %s\n"), t.destino); }
  if (t.erro[0])    { printf(tr("erro: %s\n"), t.erro); }
  bool rec = false;
  const int na = tab5_arq_area(&rec);
  printf(tr("area de transferencia: %d item(s)%s\n"), na, na && rec ? tr(" (recortados)") : "");
  for (int i = 0; i < na && i < 8; ++i) {
    char c[TAB5_ARQ_CAMINHO_MAX];
    if (tab5_arq_area_item(i, c, sizeof(c))) { printf("  %s\n", c); }
  }
}
