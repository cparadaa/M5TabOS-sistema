# Suspensão: quem decide hoje, o que quebra e o modelo proposto (05/11/2026)

## Resumo

O Tab5 caiu ao acordar, com a câmera do navegador ligada. A causa é um
defeito do ESP-IDF no DMA que a tela e a câmera dividem, e foi corrigida no
lugar certo: a camada do DMA (`hal/tab5_dma.cpp`). O mesmo crash **já tinha
aparecido** com o app Câmera e foi "resolvido" com um gancho no app, depois
copiado para a câmera dos apps Berry. Quando um terceiro consumidor (o
navegador) usou a câmera sem o gancho, o crash voltou. É o retrato do
problema: **hoje a suspensão depende de cada app e cada serviço lembrar das
regras**.

A proposta é um **registro de recursos no kernel**. Quem é dono do hardware (o
serviço) declara o que está usando, uma tabela única diz o que a suspensão
precisa preservar para cada recurso, e o sistema decide sozinho. Apps em C e
scripts Berry deixam de fazer qualquer coisa: o script que liga a câmera é
contado porque a câmera (o serviço) se declarou.

## 1. O que a suspensão faz hoje

`kernel/core.cpp` decide quando (30 s sem toque, ajustável), e
`hal/tab5_power.cpp` (`tab5_power_repouso`/`tab5_power_retomar`) executa:

| Passo | Repouso | Ao acordar |
|---|---|---|
| Tela | escurece, desmonta painel DPI e barramento DSI | remonta (~225 ms), repinta, acende |
| CPU | 360 → 40 MHz (ou o maior piso pedido) | 360 MHz |
| Sono leve | ligado, se ninguém segura | desligado |
| USB-A 5 V | cortado, se ninguém usa | religado |
| Wi-Fi | modem sleep máximo; depois de 3 min, o cochilo desliga o rádio | volta |
| Apps | `ao_repouso()` do app aberto, se tiver | `ao_retomar()` |

## 2. Os mecanismos que existem (sete, cada um num lugar)

| Mecanismo | Onde | O que segura | Quem usa hoje |
|---|---|---|---|
| `sistema_economia_segurar(ECONOMIA_SEGURA_*)` | `kernel/app.h` | o sono leve | Wi-Fi (cochilo), cabo, Mesh, USB-A, Arquivos, servidor web, teste |
| `tab5_wifi_manter_acordado(TAB5_ACORDADO_*)` | `hal/tab5_wifi.hpp` | o receptor do Wi-Fi | ESP-NOW, farejo, CSI, teste |
| `tab5_wifi_precisar(TAB5_PRECISA_*)` | `hal/tab5_wifi.hpp` | o rádio contra o cochilo | servidor web |
| `sistema_manter_acordado(SISTEMA_ACORDADO_*)` | `kernel/app.h` | a tela acesa | app (Berry `sistema.acordado`), pendrive |
| `app_t.piso_repouso_mhz` | `kernel/app.h` | o clock mínimo | Azimutal (40) |
| `fundo_def_t.piso_repouso_mhz` | `kernel/fundo.h` | o clock mínimo | Música (180), Terminal (200), Azimutal (40) |
| `app_t.ao_repouso` / `ao_retomar` | `kernel/app.h` | o que o app quiser | Câmera, Notas, apps Berry com câmera |

Cada mecanismo tem uma lista de bits própria, definida em um header diferente.
Não há um lugar que responda "por que o aparelho não está dormindo agora" ou
"o que este app está usando".

## 3. O caso do crash: o mesmo defeito, três consertos

- **O defeito** (ESP-IDF 5.5): `esp_lcd_panel_del()` do painel DPI apaga o
  canal do DW-GDMA **sem pará-lo**. Sozinha, a tela não sofre: o grupo do DMA
  zera e o driver o reseta. Com a câmera usando o mesmo DMA, o grupo continua
  de pé, o painel que acorda ganha o mesmo canal ainda habilitado e a escrita
  na configuração dele dá falha de barramento (`Store access fault`, tarefa
  `ui`, `dw_gdma_channel_use_link_list`).
- **Conserto 1 (antes)**: o app Câmera solta a câmera no `ao_repouso`.
- **Conserto 2 (antes)**: a câmera dos apps Berry, mesmo gancho.
- **Furo**: a câmera do navegador é um serviço, não um app - sem gancho, o
  crash voltou (reproduzido: acordar com a câmera transmitindo, 100%).
- **Conserto 3 (agora, na camada certa)**: `hal/tab5_dma.cpp` desvia
  `dw_gdma_del_channel` (`-Wl,--wrap`) para parar o canal antes de apagá-lo.
  Vale para tela, câmera e qualquer usuário futuro do DMA. Reproduzido de novo
  depois: acordar transmitindo e acordar depois de a câmera desligar em
  repouso - os dois voltam normalmente.

Os ganchos de câmera dos apps não são mais necessários para o DMA. Ficam até a
câmera virar recurso (seção 5): eles ainda param o preview, que escreve na
superfície da tela.

## 4. Furos encontrados no levantamento

| # | Furo | Efeito |
|---|---|---|
| 1 | **A câmera (serviço) não declara nada** | ligada pelo navegador ou pelo console, a tela apaga e o clock cai para 40 MHz com a câmera rodando; nada impede o sono leve se ninguém mais segurar |
| 2 | **Consertos de sistema morando em app** (ganchos de câmera) | todo consumidor novo precisa saber do defeito - foi o crash de hoje |
| 3 | **Sete listas de bits sem visão única** | diagnosticar "por que não dorme" ou "por que gastou bateria" exige ler sete arquivos |
| 4 | **O script Berry é invisível** | o sistema só sabe o que o script pediu de forma explícita (`sistema.acordado`, `sistema.fundo`); o que ele usa (câmera, GPS, ESP-NOW, som) depende de cada serviço lembrar de se declarar, e não há atribuição ao app |
| 5 | **Piso de clock em dois lugares** (app e fundo) e escolhido "de cabeça" | 40, 180, 200 MHz sem tabela que diga o que cada hardware precisa |

## 5. O modelo proposto: recursos declarados pelo dono do hardware

**Uma API no kernel**, de qualquer tarefa, com contagem por recurso:

```c
sistema_recurso_usar(RECURSO_CAMERA, "navegador");   // no iniciar do serviço
sistema_recurso_soltar(RECURSO_CAMERA, "navegador"); // no parar
int sistema_recursos(recurso_uso_t* out, int max);   // quem usa o quê, agora
```

**Uma tabela de política**, em um lugar só (o kernel), dizendo o que a
suspensão preserva para cada recurso:

| Recurso | Sono leve | Clock mínimo | Rádio Wi-Fi | Tela | Dono que declara |
|---|---|---|---|---|---|
| CAMERA | não | 200 MHz | - | - | `hal/tab5_camera` (`iniciar`/`parar`) |
| SOM | não | 180 MHz | - | - | `services/tab5_musica`, bip |
| REDE (servidor, MQTT) | não | - | acordado | - | `tab5_web`, `tab5_mesh_mqtt` |
| RADIO_ESCUTA (ESP-NOW, CSI) | não | - | receptor ligado | - | `tab5_espnow`, `tab5_csi` |
| UART_GROVE (Mesh, GPS) | não | - | - | - | `tab5_mesh`, `tab5_gps` |
| USB_HOST | não | - | - | - | `tab5_usb` (5 V do USB-A fica) |
| CARTAO_ESCRITA | não | - | - | - | `tab5_arquivos`, pendrive |
| TELA_ACESA | - | - | - | acesa | pedido explícito (Berry `sistema.acordado`, pendrive) |

**As regras:**

1. **Só quem é dono do hardware declara**, dentro do próprio `iniciar`/`parar`
   e por contagem - nunca o app. App em C e script Berry não sabem que a
   suspensão existe; usar o serviço já basta.
2. **O repouso é uma função só** no kernel, que olha a união das políticas
   ativas e aplica (clock, sono leve, rádio, tela). Os sete mecanismos viram
   recursos; `piso_repouso_mhz` sai do app e do fundo.
3. **Defeito de hardware compartilhado é consertado na camada do hardware**
   (como o DMA hoje), nunca com gancho de app.
4. **Atribuição ao app**: o kernel sabe qual app está chamando (o app aberto, a
   parte de fundo, o app Berry). A lista de recursos mostra "Câmera: app
   EspNow.be", "Rádio: ESP-NOW" - no Tarefas, na Energia e no console
   (`energia quem`). Um script Berry que esquece a câmera ligada aparece ali,
   e o sistema pode avisar.
5. **O app que precisa de algo que não é hardware** (gravar a nota antes de
   apagar) continua com `ao_repouso` - é dado do app, não política do sistema.

## 6. O que foi feito (05/11/2026)

1. **Kernel** - `kernel/recursos.{h,cpp}`: `sistema_recurso(r, quem, usando)`
   (por par recurso/quem, idempotente), `sistema_recursos()` para listar e a
   tabela de política. O kernel aplica a união aos mecanismos de baixo
   (`ECONOMIA_SEGURA_RECURSOS`, `TAB5_ACORDADO_RECURSOS`,
   `TAB5_PRECISA_RECURSOS`, `SISTEMA_ACORDADO_RECURSOS`) e o repouso tira o
   clock mínimo dela, registrando no log o que estava em uso.
2. **Câmera** - `tab5_camera_iniciar/parar(quem)` declara CAMERA com o nome de
   quem usa (app Camera, o app Berry pelo nome, "navegador", "console"). A
   ponte `sistema_camera_iniciar/parar` saiu.
3. **Serviços** migrados: servidor web (REDE), Mesh (SERIAL_GROVE), ESP-NOW,
   farejo e CSI (RADIO_ESCUTA), USB-A (USB_HOST), Arquivos (CARTAO), bateria
   (ENERGIA_EXTERNA), pendrive (TELA_ACESA), Música (SOM), Terminal
   (PROCESSAMENTO). `piso_repouso_mhz` saiu de `app_t` e de `fundo_def_t`.
4. **Visibilidade** - `energia quem` no console, `sistema.recursos()` no
   Berry, e a linha "repouso: recursos em uso" no log.
5. **Achado no caminho**: o piso de 200 MHz (Terminal) nunca funcionou - o P4
   só aceita 40, 90, 180 e 360 e o `esp_pm_configure` recusava calado. A
   política usa 180, e `tab5_power` arredonda qualquer pedido para cima.

Provado no aparelho: com a câmera do navegador ligada, o repouso registra
`camera(navegador) (clock minimo 180 MHz)`, fica em 180 MHz sem sono leve e
acorda normalmente; soltando a câmera, o piso volta a 40 MHz.

**Feito depois (06/11/2026)**:

- A lista aparece no app Tarefas (linha "EM USO") e na Energia (linha "Segura
  o repouso"), além do console e do Berry.
- Os ganchos de câmera saíram dos apps: o kernel chama
  `tab5_camera_pausar(nome do app aberto)` no repouso e
  `tab5_camera_despausar()` ao acordar (ou quando o repouso falha). A pausa
  guarda o destino do preview, solta o uso do app e, se a câmera desligar de
  verdade, devolve o quadro pedido como falha. Se o dono parar ou religar a
  câmera no meio, a pausa é esquecida e a contagem segue certa. Só o
  `ao_repouso` das Notas (gravar a nota) continua - é dado do app.

**Falta testar no Tab5**: app Câmera aberto → tela apaga → log
`pausada pelo repouso: Camera (desligou)` e clock 40 MHz → acordar →
`religada depois do repouso: Camera` com o preview de volta; o mesmo com a
câmera do navegador ligada junto (o navegador segue, o log não diz
"desligou"); e um app Berry com câmera.
