# API do M5tabOS

A referência do que o sistema oferece a um aplicativo: a função, o que ela faz
e **de qual tarefa ela pode ser chamada**. O app [`Exemplo`](../main/app_exemplo/app_exemplo.cpp)
usa cada uma destas áreas num cartão próprio. Abra-o no aparelho e leia o
código lado a lado.

Para o *como pensar* (as regras de desenho, o porquê de cada uma, os erros que
já cometemos) o guia é o [ESCREVENDO_APPS.md](ESCREVENDO_APPS.md). Este arquivo
é o *o quê*.

---

## Por que passar pela API

Nada impede um app de falar direto com o hardware: `M5.Imu`, `Wire`, `fopen`,
`xTaskCreate`, `M5.Display.fillRect` em tela cheia. Funciona na bancada e
quebra no uso, longe da causa. Cada API abaixo existe porque o caminho direto
já quebrou assim:

| caminho direto | o que acontece | a API |
|---|---|---|
| ler sensor pelo I2C interno de outra tarefa | o `I2C_Class` do M5Unified não tem trava: duas tarefas intercalam e leem o **registrador errado, sem erro** | `tab5_imu_ultima`, `tab5_power_ultima` (cópia, sem I2C) |
| desenhar de uma tarefa do app | o LovyanGFX não é seguro para concorrência: ponteiro corrompido, *exception* | mude o dado, chame `sistema_pedir_redesenho()` |
| `fopen(..., "a")` para gravar | com o cartão emprestado ao computador (modo pendrive), corrompe o FAT ou perde o dado | `tab5_sd_anexar` / `_acrescentar` / `_substituir` |
| `xTaskCreate` + `vTaskDelete` | a tarefa morta segurando um semáforo deixa o recurso preso para sempre | `tarefa_app_criar` / `tarefa_app_encerrar` |
| `M5.Speaker.tone()` | o codec é desligado ocioso; o primeiro bip sai **mudo** | `sistema_beep` |
| `fillRect` grande | rajada na PSRAM, o painel fica sem dado e a tela **pisca azul** | `tab5_tela_limpar` |
| ligar hardware no `ao_abrir` com `tab5_power_*` | desliga de baixo de outro app ou fica ligado depois de fechar | `app_t.perfil` (o sistema soma e desfaz) |

**A regra das três tarefas**, que decide quase todas as colunas "de onde" abaixo:

- **interface**: a única que desenha e a única que fala no I2C interno. É nela
  que rodam `ao_abrir`, `ao_fechar`, `desenhar`, `ao_tocar*`, `ao_teclar`,
  `ao_aviso`. Nada nela pode bloquear (cartão, rede, espera longa).
- **tarefas do app** (`tarefa_app`), **worker** (`sistema_trabalho`), **fundo**
  (`fundo.h`) e **console**: fazem o trabalho lento, mudam dado (atômico, fila
  ou com trava) e pedem redesenho.
- **serviços do sistema** (Mesh, ESP-NOW, WiFi, GPS...): têm tarefa própria e
  entregam por cópia (`tab5_*_msg`, `tab5_*_posicao`) e por aviso (`AVISO_*`).

Na coluna **de onde**: **UI** = só da tarefa de interface; **qualquer** =
qualquer tarefa; **fora da UI** = qualquer uma, menos a interface (bloqueia).

---

## 1. O app: `kernel/app.h`

Um app é uma pasta `main/app_<nome>/` que define `extern const app_t APP_<NOME>`.
O build gera a lista sozinho (`main/CMakeLists.txt`). Não há tabela para editar.

| campo | para quê |
|---|---|
| `nome`, `cor`, `icone` | identidade na tela inicial (`icone` opcional: desenha o seu) |
| `perfil` | o hardware que **este** app precisa ligado (`imu`, `alto_falante`, `microfone`, `camera`, `grove_5v`, `usb_a_5v`). O resto fica desligado com ele na tela |
| `usa_sd` / `exige_sd` | o sistema monta o cartão antes do `ao_abrir` / não abre o app sem o cartão |
| `orientacao` | `ORI_EM_PE`, `ORI_DEITADO`, `ORI_SEGUE_IMU` |
| `ao_abrir` / `ao_fechar` | criar / destruir **tudo**. App fechado custa zero |
| `ao_repouso` / `ao_retomar` | o que é do app antes de a tela apagar (gravar a nota aberta). Hardware e suspensão são do sistema: ver `kernel/recursos.h` |
| `desenhar(tela, area)` | a única função que desenha |
| `ao_tocar(x, y)` | clique (na soltura), coordenadas relativas à área |
| `ao_tocar_nivel(x, y, pressionado)` | a cada ciclo, com o dedo na tela (jogo, arraste, lista) |
| `ao_teclar(texto, codigo, mod)` | teclado físico já decodificado (UTF-8) |
| `periodo_ms` / `periodo_agora` | redesenho periódico (prefira 0 e avisos) |
| `avisos` / `ao_aviso` | máscara de `AVISO_*`: o serviço avisa, o app redesenha na hora |
| `altura_conteudo` | maior que a área: o sistema rola a área inteira |
| `ao_girar` | a tela girou (`ORI_SEGUE_IMU`) |
| `comandos` | comandos de console do app (seção 12) |

---

## 2. Redesenho e geometria: `kernel/app.h`

| função | de onde | o que faz |
|---|---|---|
| `sistema_pedir_redesenho()` | qualquer | um **valor** mudou: redesenha no próximo ciclo |
| `sistema_pedir_redesenho_completo()` | UI | a **tela** mudou (página, layout): limpa e repinta sem piscar |
| `sistema_area_foi_limpa()` | UI, no `desenhar` | `true` = a área chegou limpa (abertura, volta do repouso): repinte tudo |
| `sistema_area()` | UI | a área do app; **nunca** recalcule com constantes de barra |
| `sistema_quadro_dt()` | UI, no `desenhar` | segundos que este quadro representa (animação por tempo) |
| `sistema_avisar(AVISO_*)` | qualquer | publica um aviso (serviço → apps que o declararam) |
| `sistema_acordar_interface()` | qualquer | tira a interface da espera do repouso agora |
| `sistema_superficie(r, &s)` / `_mudou(r)` | UI | buffer RGB565 para quem escreve imagem por DMA (câmera) |
| `sistema_app_atual()`, `sistema_bloqueado()` | qualquer | quem está na tela; tela de bloqueio na frente |

Avisos existentes: `AVISO_MESH_MSG`, `AVISO_MESH_ENVIO`, `AVISO_MESH_NOS`,
`AVISO_GPS`, `AVISO_TRILHA`, `AVISO_TRABALHO`, `AVISO_ESPNOW`.

> **Exemplo:** todo cartão chama `sistema_pedir_redesenho()` depois de mudar
> dado; `.avisos` declara `AVISO_TRABALHO | AVISO_ESPNOW | ...`.

---

## 3. Desenho

### `tab5_ui.hpp`: componentes prontos

| função | o que faz |
|---|---|
| `tab5_fonte_corpo(d)`, `tab5_fonte_corpo_negrito(d)`, `tab5_titulo(d, px)` | fontes suaves (Roboto com acento) |
| `tab5_texto(d, n)` | tamanho N (altura N×8 px) na altura nativa mais próxima |
| `tab5_botao(d, x, y, l, a, txt, cor)` | o botão do sistema; salva e devolve o estilo de texto |
| `tab5_celula(d, cx, y, rotulo, valor, cor, sub)` | rótulo pequeno + valor grande |
| `tab5_dentro(x, y, rx, ry, rl, ra)` | teste de toque num retângulo |
| `tab5_cortar_ao_caber` / `_com_reticencias` | corta texto UTF-8 até caber em N px |
| `tab5_linha_grossa(d, ...)` | linha suave que respeita o recorte (use no lugar de `drawWideLine`) |
| `tab5_sprite_novo` / `tab5_sprite_soltar` | sprite sempre na PSRAM (ou `nullptr`) |
| `SIS_FUNDO`, `SIS_LINHA`, `SIS_BORDA` | a paleta do sistema (`app.h`) |

### Pintura por diferença: `kernel/pintura.hpp`

`pintura_iniciar(&p)` no começo do `desenhar`; `pintura_completa(&p)` para o
que nunca muda; `pintura_mudou_i/_s(&p, &memoria, agora)` para cada valor. Não
encadeie comparadores com `||` (veja o guia, 2.3).

### Limpar: `hal/tab5_tela.hpp`

`tab5_tela_limpar(x, y, l, a, cor)` (UI): limpa em faixas, sem piscar azul. Use
para qualquer área que **pode** passar de ~40 linhas da tela.

### Listas: `kernel/lista.hpp`

A lista rolável do sistema: mede cada item uma vez, rola deslocando pixels e
repinta só o item que mudou (`lista_item_mudou`). O Exemplo inteiro é uma lista.

```c
lista_definir(&l, ROLAGEM_INICIO, n, altura_de, desenhar_item, ctx);   // ao_abrir
lista_desenhar(&l, d, area, TFT_BLACK, sistema_area_foi_limpa());       // desenhar
if (lista_arrastar(&l, x, y, pressionado)) sistema_pedir_redesenho();   // ao_tocar_nivel
int i = lista_item_tocado(&l, x, y, &dy);   // ao_tocar; <0 = nada ou fim de arraste
```

### Conversa e teclado virtual: `kernel/conversa.hpp`, `kernel/teclado_virtual.hpp`

`conversa_t` é a tela de mensagens pronta (balões, histórico, barra de escrita,
teclado); o app só diz como contar, ler e enviar. `tecvirt_*` é o teclado na tela
para quem precisa só dele. Os apps Mesh e ESP-NOW usam os dois. Segurar um
balão copia a mensagem; segurar a barra de escrita (ou Ctrl+V) cola.

### Editor de texto: `kernel/editor.hpp`

`editor_t` é o texto de várias linhas com cursor (o app Notas usa): quebra por
palavra e por linha, rola com o dedo, teclado virtual e físico, segurar ou
Ctrl+V cola. O buffer é do app (PSRAM); o editor avisa em `mudou` quando há o
que gravar.

### Área de transferência: `services/transferencia.h`

Um texto só, como nos celulares, de até 4 KB (`TRANSF_MAX`). O buffer na PSRAM
só existe depois da primeira cópia. Qualquer tarefa pode chamar.

| função | o que faz |
|---|---|
| `transf_copiar(texto)` | guarda (substitui o anterior) |
| `transf_colar(out, cap)` | o texto copiado; devolve os bytes (0 = vazia) |
| `transf_vazia()` | nada copiado ainda |

A área de transferência não tem tarefa nem timer: é um buffer com trava,
chamado por quem copia ou cola.
| `sistema_recado("Copiado")` | (`kernel/app.h`, UI) a confirmação na barra de status por ~2 s |

---

### Idioma: `services/idioma.h`

Todo texto que aparece na tela passa por `tr("texto em português")`; texto
guardado numa tabela leva `N_("...")` onde nasce e `tr()` onde é desenhado.
O console e os logs ficam em português.

Cada idioma é um arquivo `.po` (o formato do gettext, que o Poedit abre):
`idiomas/en.po` vai embutido no firmware; qualquer outro `.po` copiado para
`/sdcard/apps/Sistema/idiomas/` aparece em Ajustes > Idioma no próximo boot,
sem compilar nada (um `en.po` ali substitui o embutido).

```bash
ferramentas/idiomas.py                       # junta os tr() do código e atualiza idiomas/*.po
ferramentas/idiomas.py novo fr "Français"    # cria idiomas/fr.po com tudo para traduzir
```

---

## 4. Toque e teclado: `kernel/app.h`, `hal/tab5_teclado.hpp`

| função | de onde | o que faz |
|---|---|---|
| `sistema_toques(out, max)` | UI, no `ao_tocar_nivel` | todos os dedos da amostra (multitoque) |
| `sistema_consumir_toque()` | UI | o toque já foi tratado por nível: a soltura não vira clique |
| `toque_longo(&t, x, y, pressionado)` | UI, no `ao_tocar_nivel` | `kernel/toque_longo.hpp`: true uma vez quando o dedo fica parado 550 ms |
| `sistema_travar_navegacao(b)` | UI | a faixa de navegação vira do app (teclado virtual) |
| `tab5_teclado_presente()` | qualquer | teclado físico encaixado |
| `tab5_teclado_ascii(txt, out, n)` | qualquer | texto sem acento, para protocolos ASCII |

As teclas chegam por `ao_teclar`. Códigos especiais: `TAB5_TECLA_ENTER`,
`_APAGAR`, `_DEL`, `_ESC`, `_TAB`, `_ESQUERDA`, `_DIREITA`, `_CIMA`, `_BAIXO`;
modificadores `TAB5_TECLA_CTRL/SHIFT/ALT`.

---

## 5. Trabalho fora da interface

| API | quando | de onde |
|---|---|---|
| `tarefa_app_criar(&t, corpo, nome, pilha, prio)` / `tarefa_app_encerrar(&t, ms)` (`kernel/tarefa_app.h`) | laço contínuo enquanto o app está aberto | UI (`ao_abrir`/`ao_fechar`) |
| `sistema_trabalho(fn, arg)` (`kernel/trabalho.hpp`) | uma operação lenta (ler arquivo, montar lista); fim chega por `AVISO_TRABALHO` | qualquer |
| `sistema_trabalho_cancelado()` | dentro do trabalho: o app fechou, pare | worker |
| `sistema_trabalho_solto(fn, arg)` | segundos de trabalho que não podem segurar o fechamento (gravar foto); `arg` é dele | qualquer |
| `sistema_fundo_iniciar/parar/ativo(&def)` (`kernel/fundo.h`) | trabalho que **continua com o app fechado** (gravar trilha, cronômetro) | UI |
| `fundo_esperar(f, ms)`, `fundo_ceder(f)`, `fundo_estado(f, n, txt)` | dentro do laço de fundo | fundo |

Regras do laço (as três): nunca espere com `portMAX_DELAY` (o pedido de parada
precisa chegar); não desenhe; memória sua, nunca da janela.

> **Exemplo:** cartões TAREFA (tarefa + fila de 1), TRABALHO (conta arquivos e
> recebe `AVISO_TRABALHO`) e FUNDO (cronômetro que aparece na barra com o app fechado).

---

## 6. Cartão SD: `hal/tab5_sd.hpp`

Pasta do app: `/sdcard/apps/<Nome>/`, criada sozinha.

| função | de onde | o que faz |
|---|---|---|
| `tab5_sd_anexar(app, arq, linha, cabecalho)` | qualquer | acrescenta uma linha **sem esperar** (gravação em lote) |
| `tab5_sd_acrescentar(app, arq, dados, n)` | fora da UI | acrescenta um bloco agora |
| `tab5_sd_substituir(app, arq, dados, n, flags)` | fora da UI | troca o arquivo inteiro (nunca deixa meio arquivo); `TAB5_SD_SIGILOSO` zera o antigo |
| `tab5_sd_apagar(app, arq)` | fora da UI | apaga o arquivo da pasta do app (mesma trava; recusa com o cartão emprestado) |
| `tab5_sd_abrir(app, arq, "r")` | fora da UI | **ler** (e arquivo novo grande, com `exige_sd`) |
| `tab5_sd_pasta(app, buf, n)` | fora da UI | caminho da pasta |
| `tab5_sd_montado()`, `tab5_sd_espaco(&t, &l)` | qualquer | estado |

As três portas de gravação guardam no diário da flash quando o cartão está no
computador e entregam na volta, na ordem. **Só reescreva o que leu.**

> **Exemplo:** cartão CARTÃO (`tab5_sd_anexar` com cabeçalho de CSV).

### Discos e arquivos de qualquer disco: `services/tab5_volumes.hpp`, `services/tab5_arquivos.hpp`

Para o que não é a pasta do próprio app - a música, o pendrive, copiar entre
discos - tudo por **caminho absoluto** (`/sdcard/...`, `/usb/...`). O serviço
segura o disco enquanto mexe: quem chama não monta nada.

| função | de onde | o que faz |
|---|---|---|
| `tab5_volumes_listar(v, max)` | qualquer | os discos de agora (cartão sempre; pendrive quando plugado) |
| `tab5_volume_ejetar(raiz, espera_ms)` | fora da UI | desmonta o pendrive para tirar com segurança |
| `tab5_arq_listar(pasta, itens, max, &total, ordem, filtro)` | fora da UI | o que a pasta mostra (pastas primeiro, escondidos fora) |
| `tab5_arq_percorrer(pasta, visita, ctx)` | fora da UI | cada entrada, sem tabela nem limite (sem tamanho/data) - **não use `opendir`** |
| `tab5_arq_maior_numero(pasta, "foto_%04lu.bmp")` | fora da UI | o maior número já gravado: o próximo arquivo numerado |
| `tab5_arq_visivel/gravavel/mexivel(caminho)` | qualquer | as regras: escondido, só leitura (`/sdcard/apps`, menos `apps/Berry`), livre |
| `tab5_arq_criar_pasta`, `tab5_arq_renomear`, `tab5_arq_apagar` | fora da UI | rápidas; apagar vai para `<raiz>/.Lixeira` |
| `tab5_arq_copiar(origens, n, destino, mover)` | fora da UI | **começa** um trabalho de fundo; progresso em `tab5_arq_trabalho()` e `AVISO_ARQUIVOS` |
| `tab5_arq_area_por / tab5_arq_colar` | qualquer / fora da UI | a área de transferência do sistema (Copiar, Recortar, Colar) |
| `tab5_arq_esvaziar_lixeira(raiz)`, `tab5_arq_cancelar()` | fora da UI / qualquer | |

Copiar continua com o app fechado e a tela apagada, mostra `Copiando 45%` na
barra, e nunca deixa arquivo pela metade (pendrive arrancado, cancelado, disco
cheio: o arquivo da vez é apagado). Console: `arquivos`, `usb ejetar`.

---

## 7. Notificações: `services/notificacao.h`

Estado, não fila: cada fonte tem **um** estado atual, e publicar de novo substitui.

```c
int id = notif_registrar("MeuApp");                 // idempotente pelo nome
notif_associar_app(id, "MeuApp");                   // tocar no aviso abre o app
notif_publicar(id, 3, NOTIF_ATENCAO, "3 alertas");  // qualquer tarefa
notif_limpar(id);                                   // = publicar 0
```

Níveis: `NOTIF_ESTADO` (atividade), `NOTIF_INFO`, `NOTIF_ATENCAO` (endereçado a
você), `NOTIF_URGENTE`. A barra pinta pela pior fonte ativa.

---

## 8. Energia, som e tela

| função | de onde | o que faz |
|---|---|---|
| `app_t.perfil` | (declaração) | o que o app precisa ligado; o sistema soma com a preferência do usuário |
| `sistema_reaplicar_perfil()` | UI | uma preferência mudou: vale na hora |
| `sistema_manter_acordado(SISTEMA_ACORDADO_APP, b)` | qualquer | a tela não apaga (o sistema solta ao fechar o app) |
| `tab5_power_ultima()` | qualquer | bateria: volts, mA, mW, % (cópia, sem I2C) |
| `sistema_beep(hz, ms)` | UI | bip (precisa de `perfil.alto_falante`) |
| `sistema_som_fluxo_abrir/livre/tocar/calar/fechar` | qualquer, menos UI | som em blocos (música): a interface liga o codec, a tarefa alimenta; ver abaixo |
| `tab5_painel()`, `tab5_painel_nome()` | qualquer | qual das três telas do Tab5 (ILI9881C, ST7123, ST7121) |
| `tab5_power_cpu_mhz()` | qualquer | clock agora (360 MHz no P4 v1.x, 400 no v3.x) |

> **Exemplo:** cartões ENERGIA, SOM e ACORDADO.

**O tocador do sistema: `services/tab5_musica.hpp`.** Música é serviço, não
app: a pasta de música do cartão (`tab5_musica_pasta`), listar
(`tab5_musica_listar`), tocar uma pasta a partir de um item
(`tab5_musica_tocar(rel, idx, app)`), os controles, o estado e o espectro
(`tab5_musica_espectro`, calculado só enquanto alguém lê). Roda como parte de
fundo do kernel ("Música"): barra, miniplayer e o X que para vêm sozinhos. O
app Música e o módulo Berry `musica` são duas interfaces dele - nenhum dos
dois decodifica nem lê o cartão. De qualquer tarefa; os controles só marcam
um pedido. Também do serviço: **aleatório e repetir**
(`tab5_musica_definir_aleatorio`, `tab5_musica_definir_repetir` - não, a
fila toda, a mesma música; guardados nos ajustes), **playlists** em `.m3u`
na subpasta `Playlists` (`tab5_musica_playlists`, `_playlist_criar`,
`_playlist_itens`, `_playlist_por`, `_playlist_tirar`, `_playlist_apagar`,
`tab5_musica_tocar_playlist`; a `Favoritas` é a do coração), a **duração**
de um arquivo sem tocar (`tab5_musica_duracao`, só o cabeçalho; lembrada) e
a **capa** do que toca (`tab5_musica_capa`: a da etiqueta ID3, a
cover/folder.jpg da pasta ou, com Wi-Fi, a busca do iTunes por artista e
título - guardada em `Music/.capas/`). Todo `rel`/`caminho` aceita também
caminho ABSOLUTO de um disco (`"/usb/Rock/x.mp3"`): o pendrive do USB-A toca
direto, e fila e playlists misturam os dois. Formatos: MP3, WAV e AAC em
`.m4a`/`.mp4` (o som de um vídeo).

**O tocador de vídeo: `services/tab5_video.hpp`.** `.mp4/.m4v/.mov` com H.264
(som AAC de dentro, tocado pela Música) e `.avi` MJPEG (som do `.mp3` de mesmo
nome): `tab5_video_abrir`, os controles, o estado, `tab5_video_pintar` (escala
pelo PPA para a superfície do app) e `TAB5_VIDEO_EXTENSOES` (o filtro para
listar). Por baixo: `services/tab5_h264` (o decodificador H.264, com a
bancada `video h264`) e `services/tab5_midia` (a ponte das bibliotecas
`components/medf` - leitores de contêiner - e `components/audf` -
decodificadores de som - com o leitor de arquivos do sistema).

**Os discos: `services/tab5_volumes.hpp`.** Cartão (`/sdcard`) e pendrive no
USB-A (`/usb`, `services/tab5_usb`) por caminho absoluto: `tab5_volumes_listar`,
`tab5_volume_montar(caminho)`/`_desmontar` (segurar o disco onde o caminho
mora), `tab5_volume_disponivel` (para quem segura muito tempo: virou `false`,
solte - cartão emprestado ou pendrive arrancado). Um disco
entrou ou saiu: `AVISO_VOLUMES` (Berry: `# avisos: volumes`).

**Som em fluxo** (quem usa é o tocador do sistema, `services/tab5_musica.cpp`):
`sistema_som_fluxo_abrir(ms)` pede à interface para ligar o alto-falante e
espera o codec aquecer; depois a tarefa (a parte de fundo) enfileira blocos
PCM de 16 bits com `sistema_som_fluxo_tocar(pcm, quadros, hz, estereo)`
sempre que `sistema_som_fluxo_livre()`. O alto-falante guarda dois blocos (o
que toca e o próximo): use **três** buffers em rodízio, na PSRAM. `tocar`
devolve `false` quando o fluxo foi fechado por baixo (o modo pendrive tomou
os pinos) - pare. `calar` descarta a fila (pausa, troca de faixa); `fechar`
solta o alto-falante. O bip do sistema sai por cima da música, noutro canal.

**Lendo arquivo grande do cartão, use `open`/`read`, não `fread`.** Medido
no Tab5, lendo 256 KB: `fread` com `setvbuf(_IONBF)` 52 KB/s (a newlib lê
um byte por chamada ao FAT), `fread` comum 572 KB/s, `read()` 4,6 MB/s - o
pedido inteiro vai ao FatFs, que faz DMA de vários setores de uma vez.

**Tocando agora: `kernel/midia.h`.** Quem toca mídia assume a vaga
(`midia_assumir(fonte, app, &controles)`: a parte de fundo que toca e o app
que o miniplayer abre) e publica título, tocando/pausado,
posição e duração (`midia_publicar`, de qualquer tarefa). O sistema mostra
uma pastilha lilás na barra (convive com a de avisos) e o miniplayer no
rodapé da central de avisos; os controles (`tocar_pausar`, `proxima`,
`anterior`, `ir_para`) rodam na interface e só devem marcar um pedido.
`midia_definir_cor` troca a cor da pastilha e do miniplayer.

**Gravar nos ajustes (NVS) desliga o cache, e sem cache a PSRAM some**: de
uma tarefa com a pilha na PSRAM (a do Berry) isso derruba o aparelho. Os
`ajustes_definir_*` chamáveis de qualquer tarefa gravam pelo caminho adiado
(na tarefa do `esp_timer`); o `ajustes` recusa, com log de erro, uma gravação
direta vinda de pilha na PSRAM.

---

## 9. Sensores e hora

| função | de onde | o que faz |
|---|---|---|
| `tab5_imu_ultima(&d)` (`hal/tab5_imu.hpp`) | qualquer | aceleração (g) e giro (°/s); precisa de `perfil.imu` |
| `tab5_gps_no_ar()`, `tab5_gps_posicao()`, `tab5_gps_tempo()` (`hal/tab5_gps.hpp`) | qualquer | GPS no Grove; lat/lon zerados sem fix |
| `tab5_gps_assinar(fila)` | qualquer | recebe cada sentença nova numa fila sua |
| `tab5_sensores_temp(&c)` (`hal/tab5_sensores.hpp`) | qualquer | temperatura interna |
| `tab5_hora_epoch()` (`services/tab5_hora.hpp`) | qualquer | agora em UTC, ou 0 se o relógio não foi acertado |
| `tab5_hora_local(epoch, &tm)` | qualquer | para **mostrar** (o fuso que a pessoa escolheu, `ajustes_fuso_min()`); guarde sempre UTC |
| `tab5_hora_fonte()` | qualquer | de onde veio o acerto (GPS, rede, malha, RTC) |
| `tab5_agora_ms()` | qualquer | relógio monótono para medir intervalo (compare por diferença) |

> **Exemplo:** cartões HORA, IMU e GPS.

---

## 10. Rede

### WiFi: `hal/tab5_wifi.hpp`

| função | de onde | o que faz |
|---|---|---|
| `tab5_wifi_estado()`, `tab5_wifi_descricao()` | qualquer | estado e texto pronto para a tela |
| `tab5_wifi_ssid()`, `tab5_wifi_ip()`, `tab5_wifi_rssi()` | qualquer | a conexão atual |
| `tab5_wifi_ligar()` / `_desligar()` | UI | normalmente é o usuário em Ajustes, não o app |
| `tab5_wifi_procurar()`, `tab5_wifi_vista(i, &v)` | qualquer | varrer redes em volta |
| `tab5_wifi_gravar_credenciais(ssid, senha)` | fora da UI | guarda rede (senha cifrada) |

Com o WiFi de pé, o resto é o ESP-IDF normal (`esp_http_client`, sockets, MQTT).

### ESP-NOW: `services/tab5_espnow.hpp`

Precisa do firmware nosso no C6 (`c6/`, ver [RADIO_C6.md](RADIO_C6.md)).

| função | de onde | o que faz |
|---|---|---|
| `tab5_espnow_pronto()`, `tab5_espnow_canal()` | qualquer | no ar, e em que canal |
| `tab5_espnow_enviar(texto)` | qualquer | difusão; não bloqueia |
| `tab5_espnow_enviar_para(mac, texto)` | qualquer | direto (unicast); não bloqueia |
| `tab5_espnow_enviar_todos_canais(mac_ou_null, texto)` | qualquer | canais 1-13; o Wi-Fi cai por alguns segundos |
| `tab5_espnow_n()`, `tab5_espnow_msg(i, &m)` | qualquer | histórico em RAM (cópia) |
| `tab5_espnow_janela(b)` | UI | a janela abriu/fechou: receptor ligado com o app na tela |

Aviso: `AVISO_ESPNOW`.

### Mesh (Meshtastic): `services/tab5_mesh.hpp`

Ver [MESHTASTIC.md](MESHTASTIC.md). Serviço do sistema: o rádio, os nós e o
histórico vivem nele, e o app é só janela.

| função | de onde | o que faz |
|---|---|---|
| `tab5_mesh_servico_ativo()`, `tab5_mesh_no_ar()` | qualquer | estado |
| `tab5_mesh_num_nos()`, `tab5_mesh_no(i, &no)` | qualquer | nós conhecidos (cópia) |
| `tab5_mesh_num_nos_busca(txt)`, `tab5_mesh_no_busca(txt, i, &no)` | qualquer | os mesmos, só os que batem com o nome/curto/`!id` (sem acento nem maiúscula) |
| `tab5_mesh_num_msgs()`, `tab5_mesh_msg(i, &m)` | qualquer | mensagens (0 = mais recente) |
| `tab5_mesh_nao_lidas()` | qualquer | não lidas |
| `tab5_mesh_enviar(para, canal, texto)` | qualquer | `para = 0xFFFFFFFF` difunde no canal; outro = direta |
| `tab5_mesh_estado_envio(id, &erro)` | qualquer | ACK, falha |

Avisos: `AVISO_MESH_MSG`, `AVISO_MESH_ENVIO`, `AVISO_MESH_NOS`.

Servidores do modo internet (`services/tab5_mesh_mqtt.hpp`): a lista é da
pessoa. `mesh_mqtt_n_servidores()` / `mesh_mqtt_servidor(i, &s)` (0 =
automático), `mesh_mqtt_salvar_servidor(i, &s)` (i = 0: novo),
`mesh_mqtt_apagar_servidor(i)`, `mesh_mqtt_modelo(i)` para preencher um novo.
A escolha é `ajustes_mqtt_servidor()`; trocar pede reiniciar o serviço
(`sistema_mesh_reiniciar()`). Trocar a via: `sistema_mesh_via(via)` (a via
Bluetooth liga o Bluetooth junto).

### Bluetooth: `hal/tab5_ble.hpp`

`tab5_ble_pronto()`, `tab5_ble_varrer(ms)`, `tab5_ble_visto(i, &v)`. Quem liga e
desliga é o usuário (`sistema_bluetooth`).

**Ligar um serviço** é sempre uma chamada do sistema (`kernel/app.h`), que grava
a escolha e aplica: `sistema_bluetooth`, `sistema_wifi`, `sistema_grove`,
`tab5_usb_ligar`. O app não grava o ajuste e mexe no
hardware por conta própria.

> **Exemplo:** cartões WIFI, ESP-NOW (com botão de difundir) e MESH (só leitura:
> um app de exemplo não deve falar num canal público).

---

## 11. Câmera: `hal/tab5_camera.hpp`

| função | de onde | o que faz |
|---|---|---|
| `tab5_camera_iniciar(quem)` / `tab5_camera_parar(quem)` | UI | liga/desliga (contagem de referência: vários usuários convivem); `quem` aparece no recurso CAMERA |
| `tab5_camera_definir_destino(&d)` | UI | onde cada quadro do preview é escrito (use `sistema_superficie`) |
| `tab5_camera_pedir_quadro(buf, l, a)`, `tab5_camera_quadro_pronto(buf, &ok)` | qualquer | um quadro no tamanho pedido (a foto: `TAB5_CAM_FOTO_L` x `_A`) |

O app não cuida do repouso: com a tela apagada o sistema pausa a câmera do app
aberto e a religa ao acordar, com o mesmo destino (`tab5_camera_pausar`, chamado
pelo kernel). Uma foto em captura nessa hora volta com `ok = false`. O app
Câmera é a referência completa.

---

## 12. Console do app: `app_t.comandos`

O console serial (`idf.py monitor`, digite `help`) é do sistema, mas um app pode
oferecer comandos próprios. Eles aparecem num grupo **APPS** do `help sistema` (e em `help app`) e são
chamados sempre com o nome do app na frente, para nunca colidir:

```
app exemplo contar 5
```

```c
static void cmd_contar(int argc, char** argv) { ... }   // argv[0] = "contar"

static const app_comando_t COMANDOS[] = {
  { "contar", "[n]\tsoma n no contador", cmd_contar, false },
  { "zerar",  "\tzera (so com o app aberto)", cmd_zerar, true },
  { nullptr },
};
// no app_t:  .comandos = COMANDOS,
```

O comando roda na **tarefa de quem digitou**: a do console serial, ou a parte
de fundo do Terminal. Valem as regras de qualquer tarefa que não é a interface
(não desenhe, mude dado atômico ou com trava, peça redesenho). Escreva com
`printf`: o stdout é da tarefa, e o Terminal guarda o que sai. Laço longo
pergunta `console_interrompido()` (Ctrl+C no Terminal). Caminho relativo vale
a partir da pasta atual da sessão (`cd`).
`so_aberto = true` faz o sistema recusar o comando com o app fechado, e é o
certo para todo comando que toca estado criado no `ao_abrir`.

### Ferramentas de rede: `services/tab5_rede.hpp`

O endereço do aparelho (`tab5_rede_ip`), DNS, a tabela ARP e a varredura da
rede local, traceroute, teste de porta TCP e HTTP/HTTPS (`tab5_rede_http`
entrega o corpo aos pedaços; `tab5_rede_http_texto` junta numa string na
PSRAM). Tudo bloqueia: fora da interface. Os laços longos recebem um
`parar()` (o Terminal passa `console_interrompido`).

### SSH: `services/tab5_ssh.hpp`

`tab5_ssh_rodar()` conecta, autentica por senha, roda um comando (ou o shell
linha a linha) e desconecta, entregando a saída por callback. A senha e as
linhas do shell vêm por `ler_linha` (o console passa `console_ler_linha`).

### Terminal: `services/tab5_terminal.hpp`

A sessão de comandos da tela (o app Terminal). Os comandos **são** os do
console (`console.cpp`, uma tabela só, com a ajuda junto); o console tem
sessões (`console_sessao_t`: a pasta atual e o Ctrl+C de quem digita), e o
Terminal é uma delas. `tab5_terminal_enviar(linha)` roda numa parte de fundo
(continua com o app fechado; a pastilha mostra o comando, o X interrompe);
`tab5_terminal_linha(i, ...)` lê o que saiu. As linhas (~51 KB) ficam na PSRAM
e só existem depois da primeira abertura. Comando novo entra no `COMANDOS` do
console e aparece nos dois lugares.

---

## 13. Hardware: o que muda entre aparelhos

O mesmo binário roda nas **três telas** do Tab5 (o M5GFX detecta). O sistema
ajusta sozinho o que difere: vigia do toque, repouso e relógio do painel (veja
`tab5_painel()` em `hal/tab5_power.hpp`). Um app que desenha pelo `M5.Display`
e lê o toque pelo `ao_tocar` não precisa saber qual é.

O **chip** é outra história: o ESP32-P4 v1.x e o v3.x pedem binários
diferentes (`sdkconfig.defaults.chip_v1` / `_v3`). O `./gravar.sh` lê a
revisão do aparelho e escolhe. Para compilar à mão:

```bash
idf.py -B build_v3 -DM5_CHIP=v3 build
```

Código de app não deve depender da revisão. Se precisar mesmo, use
`CONFIG_ESP32P4_SELECTS_REV_LESS_V3` (v1.x) do `sdkconfig.h`.
