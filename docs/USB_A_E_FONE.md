# USB-A (host) e fone P2 - análise para implementar

Levantamento feito em 2026-10-17, antes de qualquer código. Fontes: o
**esquema oficial do Tab5** (Tab5_Schematics_PDF, Altium, 2025-06-09 - lido
página a página), o código do M5Unified 0.2.21 que já usamos, o BSP
`espressif/m5stack_tab5` 1.3.x, o ESP-IDF 5.5.5 local e o registro de
componentes da Espressif. O que é dedução (e não está escrito no esquema) está
marcado **[a confirmar no teste]**.

---

## 1. USB-A

### 1.1 O hardware (do esquema)

| O quê | Como está ligado |
|---|---|
| Dados | J10 (USB-A) ← ESD0524P (D21) ← choque de modo comum FT2 (SDMM0806H, 90 Ω) ← R113/R114 **33 Ω em série** ← `USB_HOST_DP/DM` ← pinos 49/50 do P4: **`USB2_OTG_D-/D+`** |
| Controlador | o **OTG 2.0 High-Speed (480 Mbit/s)**, com PHY UTMI próprio - pinos dedicados, não é GPIO |
| VBUS | `SYS_INT5V` → **MT9700** (U27, chave com limite de corrente) → `SYS_USB5V` → J10. Enable = `USB5V_EN` (expansor 1 / 0x44, bit 3) |
| Limite de corrente | programado por R84 = 5,1 kΩ no SET. O datasheet do MT9700 só traz o gráfico I×R; pela curva fica na casa de ~1 A **[a confirmar medindo]** |
| Sinal de falha | **não existe** - o MT9700 não tem pino de flag. Sobrecorrente só se percebe pelo aparelho sumindo do barramento (ou pelo INA226) |
| USB-C, para comparar | `USB_DEVICE_DP/DM` → GPIO24/25 (`USB1P1_0`), o PHY full speed do console - o que o modo pendrive troca |

**Consequências diretas:**

1. **O USB-A é independente de tudo que já existe.** Outro controlador, outro
   PHY, outros pinos. Não briga com o console (USB-C), nem com o modo pendrive
   (que usa o OTG *full speed* pelo USB-C), nem com o I2S do alto-falante: o
   problema de GPIO26/27 (memória "pendrive") é do PHY1 full speed, não deste.
   Dá para ter pendrive no computador **e** um pendrive no USB-A ao mesmo tempo.
2. **Alta velocidade de verdade**, mas com ressalva: os 33 Ω em série + o choque
   estão no caminho de um sinal de 480 Mbit/s (o PHY já tem a terminação de
   45 Ω interna). Deve funcionar em trilha curta, mas é o primeiro ponto a medir:
   enumerar um pendrive HS e conferir que veio como HS e não caiu para FS.
3. O padrão do IDF já é o certo: `usb_host_config_t.peripheral_map = 0` no P4
   instala no periférico **HS** (BIT0) - igual ao BSP da Espressif, que não
   passa nada.
4. **Revisão do chip.** O `usb_phy.c` do IDF liga os pull-downs de 15 kΩ do
   host por software só no P4 v3 (`!CONFIG_ESP32P4_SELECTS_REV_LESS_V3`); no
   v1 o próprio controlador cuida. Temos as duas variantes de sdkconfig
   (`chip_v1`/`chip_v3`) - testar nas duas.

### 1.2 Os 5 V do conector

Já temos o controle em `tab5_power` (`perfil.usb_a_5v`, `M5.Power.setExtOutput(.., m5::ext_USB)`).
Atenção à divergência: o nosso mapa (tirado do M5Unified) diz que `USB5V_EN`
fica como **entrada com pull** (o enable real é resistor na placa) e o
M5Unified inverte o *pull*; o BSP da Espressif programa o mesmo pino como
**saída push-pull**. Os dois funcionam; seguimos com o M5Unified, que é dono
do expansor aqui.

Regras para o serviço:
- 5 V do USB-A **só ligados enquanto o serviço host estiver ativo** (é consumo
  de bateria mesmo sem nada plugado: a chave e o que estiver no conector).
- Com o Tab5 na bateria, o orçamento é o do conversor de 5 V interno;
  aparelhos gulosos (HD externo de 2,5", hub com muita coisa) vão bater no
  limite do MT9700. Hub com fonte própria é o caminho para isso.

### 1.3 Bibliotecas

| Pilha | Situação no P4 | Veredito |
|---|---|---|
| **USB Host Library do IDF** (`components/usb`, já no IDF 5.5.5) | HS no P4, hubs opcionais (`CONFIG_USB_HOST_HUBS_SUPPORTED`, hoje desligado), multi-hub | **usar esta** |
| `espressif/usb` 1.6.0 (a mesma biblioteca, como componente) | exige IDF ≥ 5.5.3 (temos 5.5.5). Traz o que a embutida não tem: **suspende a porta antes do sono leve** (1.5.0), evento `DEV_REMOVED`, `fsls_only` (forçar FS se o HS der problema de sinal, 1.6.0), dual host no P4 | trocar para ela **se** o sono leve ou o sinal HS pedirem |
| TinyUSB (`espressif/tinyusb` 0.21) | "Only Device part ... is supported" - host não | não serve (fica só no pendrive) |
| CherryUSB 1.6.1 | host HS, hubs, muitas classes | segunda pilha USB no firmware, sem ganho que a da Espressif não dê; descartada |

Drivers de classe (todos da Espressif, todos com P4; no IDF 5.x usam o `usb` embutido):

| Componente | Versão | Para quê no M5tabOS |
|---|---|---|
| `usb_host_msc` | 1.3.0 | **pendrive no USB-A** → montado em VFS/FAT (`/usb`), Arquivos e Música leem dele. Só BOT/SCSI transparente (todo pendrive comum). Bloco 512-4096 |
| `usb_host_hid` | 1.2.1 | **teclado e mouse**. Entrega o relatório cru (+ atalhos para Boot Protocol). O layout (ABNT2) é nosso - encaixa no `hal/tab5_teclado` |
| `usb_host_cdc_acm` + `usb_host_vcp` (CH34x 2.2.1, CP210x 2.2.0, FTDI 2.1.1) | 2.4.1 | serial USB: nó Meshtastic/rádio no cabo, GPS USB, Arduino |
| `usb_host_uac` | 1.5.0 | headset/DAC USB. **Só UAC 1.0** (a maioria dos headsets e adaptadores baratos é UAC1; DACs "hi-fi" UAC2 não funcionam) |
| `usb_host_uvc` | 2.6.0 (temos 2.4.2 por tabela, via esp_video) | webcam. O `esp_video` já tem o caminho pronto: `CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE` vira um `/dev/videoN` para o app Câmera |

Versões: travar com `==` no `main/idf_component.yml`, como o resto (ver o
aviso no topo dele: build limpo ao trocar componente).

### 1.4 Como montar para o melhor desempenho

Arquitetura (regra da casa: motor em `services/`, apps só interface, módulo Berry por serviço):

```
services/tab5_usb_host.{hpp,cpp}   motor: instala/desinstala, tarefa usb_lib,
                                   eventos de conexão, 5 V, energia
  ├─ classe MSC  → monta /usb, avisa "pendrive conectado" (notificacao)
  ├─ classe HID  → injeta no tab5_teclado / ponteiro
  └─ classe CDC  → fluxo de bytes para quem pedir (Mesh, GPS)
apps (Arquivos, Música, Config) e Berry 'usb' → só pedem e mostram
```

Pontos de desempenho e robustez:

1. **Tarefas.** Uma tarefa `usb_lib` (`usb_host_lib_handle_events`) + a do
   driver de classe. Prioridade acima da UI, abaixo do áudio. Fixar núcleo
   conforme `reserva_nucleo` (não disputar com o núcleo da tela).
2. **Memória.** Os buffers DMA do DWC ficam na RAM interna por padrão - e RAM
   interna é o recurso mais apertado (ver auditoria de RAM). Existe
   `CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM` (só P4) com "pequena perda de
   desempenho". Medir os dois; o mais provável é ir para a PSRAM, como o
   esp_hosted foi.
3. **Leitura de arquivos (MSC).** A lição do SD vale aqui:
   `read()` POSIX com blocos grandes, não `fread` de 4 KB. O próprio
   componente diz que o cache por arquivo (`setvbuf`) para de ajudar acima de
   16 KB. Medir MB/s HS com o mesmo `ensaio` do SD.
4. **FIFO.** `CONFIG_USB_HOST_HW_BUFFER_BIAS_*`: *Balanced* (atual) para MSC;
   *Bias IN* se o uso principal for webcam/áudio de entrada.
5. **Hot-plug.** Tirar o pendrive com arquivo aberto: o serviço tem que
   desmontar o VFS e avisar os apps (igual ao "cartão foi para o computador"
   do pendrive/Música).
6. **Energia/sono leve.** Com host ativo, o sono leve derruba o barramento
   (mesmo tipo de problema do UART do Mesh). Ou o serviço segura uma trava de
   PM enquanto há aparelho conectado, ou usamos o `espressif/usb` ≥ 1.5 que
   suspende a porta sozinho. Sem nada conectado: desinstala o host e corta os
   5 V.
7. **Hub.** Ligar `CONFIG_USB_HOST_HUBS_SUPPORTED` (custa um pouco de RAM)
   permite teclado + pendrive juntos. O MSC suporta até 4 drives no P4.

Ordem sugerida: **MSC → HID → CDC/VCP → UVC → UAC**.

---

## 2. Fone P2 (3,5 mm)

### 2.1 O hardware (do esquema)

```
ES8388 LOUT1 ─ 22µF ─ HPOUT_L ─ 22Ω ─ J5 pino 6 (L)   ──┐
ES8388 ROUT1 ─ 22µF ─ HPOUT_R ─ 22Ω ─ J5 pino 3 (R)     │ PJ-342, 4 polos (TRRS)
AUDIO_VDD ─ 3,3k ─ HPMIC ─────────── J5 pino 5 (mic)   │
                     └─ 1µF ─ ES7210 MIC4P             │
AUDIO_VDD ─ 100k ─┬─ J5 pino 7 (chave do L)            │
                  └─ 10k ─ HP_DET → expansor 0, bit 7 ──┘
HPOUT_L ─ 0Ω ─ AEC_P ─ 1µF ─ ES7210 MIC3P   (referência para cancelar eco)
HPOUT_L ─ 100nF/68k ─ NS4150B (U15) ─ alto-falante;  SPK_EN = CTRL (expansor 0, bit 1)
ES8388 LOUT2/ROUT2: não ligados
```

O que isto significa:

1. **Fone e alto-falante saem do mesmo par de pinos (LOUT1/ROUT1).** Não há
   chaveamento por hardware: plugar o fone **não** cala o alto-falante. Quem
   cala é o software, desligando `SPK_EN` quando `HP_DET` acusar o plugue.
2. **O alto-falante é mono e só recebe o canal ESQUERDO.** Hoje o
   M5Unified está em `stereo = false` (o mesmo sinal nos dois canais), então
   não se perde nada. Ao ligar estéreo para o fone, **com o alto-falante ativo
   é preciso misturar (L+R)/2 no esquerdo** - senão o alto-falante perde tudo
   que só existe no direito.
3. **`HP_DET`** - sem plugue a chave do pino 7 encosta no L (5,1k para o
   terra) e o sinal fica baixo (~0,16 V); com plugue a chave abre e o 100k puxa
   para 3,3 V. Logo **alto = fone plugado [a confirmar no teste]**. O `nINT`
   do expansor não vai ao P4: é por leitura (I2C, na tarefa de interface, que
   é a dona do barramento - ex.: a cada 300-500 ms, só com som ativo ou tela
   acesa).
4. **O P2 tem microfone de headset** (CTIA, polarizado por 3,3k) no **MIC4**
   do ES7210. MIC1/MIC2 são os dois microfones da placa, **MIC3 é a cópia da
   saída esquerda** (referência de AEC). Só o SDOUT1 do ES7210 vai ao P4 - para
   ler MIC3/MIC4 o ES7210 tem que ir para TDM de 4 canais. O M5Unified hoje
   lê só MIC1/2 e deixa MIC3/4 com ganho 0.
5. **Graves.** O capacitor de acoplamento de 22 µF + 22 Ω forma um passa-altas
   com o fone: ~134 Hz para fone de 32 Ω, ~190 Hz para 16 Ω. Fone de ouvido
   comum vai soar **sem grave**. Não é defeito do nosso código; dá para
   compensar com um *low-shelf* (biquad) no software, só quando for fone (o
   alto-falante não reproduz grave e iria distorcer). Em entrada de linha
   (amplificador, caixa ativa ~10 kΩ) o corte cai para ~1 Hz - som cheio.
6. **LOUT2/ROUT2 não estão ligados**, mas o M5Unified liga os quatro
   (`reg 4 = 0x3C`). `0x30` liga só LOUT1/ROUT1 - pouca economia, mas
   correto.

### 2.2 Bibliotecas

| Opção | Prós | Contras |
|---|---|---|
| **M5Unified `Speaker`** (hoje) | já integrado (bips, fluxo da Música, desligar I2S ocioso, ceder pinos ao pendrive); mixer de canais | `sample_rate` fixo em 48 kHz com reamostragem linear própria (MP3 é 44,1 kHz → reamostra tudo); `magnification = 4` (ganho ×4 digital → corte/clip em música alta); volume por software (perde resolução); não sabe de fone |
| **`esp_codec_dev` 1.6.2** + driver I2S `i2s_std` do IDF | volume **no codec** (0,5 dB por passo, sem perder bits), taxa de amostragem nativa por arquivo, o mesmo caminho do BSP oficial | é trocar o dono do I2S0 e do ES8388 - o microfone (ES7210) divide o mesmo barramento I2S (clock comum), então teria que migrar junto; refazer o que o `sistema_som_*` já resolve. Seleção LOUT1/2 e detecção de fone **não** constam no driver |
| ESP-ADF | pipeline completo | pesado, traz muita coisa que já temos (decoder, SD) |

**Recomendação: ficar no M5Unified e acertar por cima, em fases.**
O `sistema_som_*` em `kernel/core.cpp` já isola todo mundo do M5Unified - a
troca de motor, se um dia vier, fica escondida ali.

### 2.3 Como montar para o melhor desempenho

1. **Detecção (`HP_DET`) no sistema**, não em app: o kernel lê o bit 7 do
   expansor 0, gera o evento "fone plugado/tirado" e:
   - plugou → rampa de volume para baixo, `SPK_EN` desliga, aplica o volume do
     fone, estéreo;
   - tirou → **pausa a Música** (o comportamento que todo celular tem), volta
     ao volume do alto-falante, mono, `SPK_EN` liga.
2. **Dois volumes guardados nos ajustes** (alto-falante e fone), aplicados
   **no ES8388** (registros 46/47, `LOUT1VOL/ROUT1VOL`) e não no PCM - o PCM
   fica em ganho 1 e preserva os 16 bits. Volume inicial do fone baixo, com
   teto (segurança auditiva; hoje o M5Unified deixa a saída em +4,5 dB, `0x21`).
3. **Estéreo e taxa nativa para o fone.** Com fone: `spk_cfg.stereo = true` e
   `sample_rate` = a do arquivo (44 100) quando a Música abre o fluxo -
   elimina a reamostragem. Os bips de sistema continuam funcionando (o mixer do
   M5Unified converte o resto). Reabrir o I2S nessa troca só entre faixas, nunca
   no meio.
4. **Ganho digital.** Rever `magnification = 4` para o fluxo de música (é
   ganho pensado para o alto-falante pequeno); no fone, 1.
5. **Graves no fone**: biquad *low-shelf* (~120 Hz, +6 a +9 dB) no laço da
   Música, só com fone, com *headroom* (baixar 3-6 dB antes para não clipar).
   Custo desprezível no P4.
6. **Microfone do headset**: fase posterior - ES7210 em TDM, escolher MIC4
   quando `HP_DET` acusar (e o MIC3 de referência abre caminho para
   cancelamento de eco com o esp-sr, se um dia houver chamada/voz).
7. **Estalos**: o ES8388 já tem o "click free" ligado (`reg 28 = 0x08`); a
   troca de `SPK_EN` tem que acontecer com o DAC mudo (o próprio M5Unified faz
   assim no `end()`).
8. **Sem conflito com o USB-A.** Os pinos do I2S (GPIO26/27) só conflitam com
   o PHY full speed (modo pendrive) - já tratado por `sistema_som_ceder_pinos`.

---

## 3. O que já foi feito (fases 0 e 1 - 2026-10-17)

- **FAT em UTF-8** (`CONFIG_FATFS_API_ENCODING_UTF_8`): os nomes chegam e
  abrem como o computador gravou ("Canção.mp3", não `CANCAO~1.MP3`). A Música
  entende os `.m3u` antigos em CP437.
- **`services/tab5_volumes`**: os discos por caminho absoluto (`/sdcard`,
  `/usb`) - montar/desmontar, disponível, versão, `AVISO_VOLUMES`.
- **`services/tab5_usb`**: host HS (`peripheral_map = 0`), `usb_host_msc`
  1.3.0 travado, pendrive em `/usb`, retirada com até 3 s para quem lê
  soltar, aviso "USB-A" na barra (abre a Música), sono leve segurado
  (`ECONOMIA_SEGURA_USB`) com aparelho plugado, buffers de DMA na PSRAM.
  Ajuste `usb_a` (nasce ligado) → `usb_a_5v` no perfil (kernel/core.cpp).
- **Música**: o pendrive no topo das Pastas; caminhos `/usb/...` em listar,
  tocar, duração, capa (cache no cartão), playlists e Favoritas. O laço segura
  só o disco que a fila usa.
- **Berry**: `musica.pendrive()`, `musica.lista("/usb")`, `# avisos: volumes`.
- **Console**: `usb`, `usb ler <arquivo>`, `usb espaco`, `usb ligar|desligar`.

Fica para a próxima fase: o serviço de arquivos (copiar/mover/apagar com
lixeira, área de transferência, "abrir com" por extensão) e o app Arquivos
navegando nos dois discos - e daí o "copiar músicas do pendrive para o
cartão" sai da mesma operação.

## 4. Plano de teste (quando o Tab5 voltar ao cabo)

**Pendrive tocando (fase 1)**
- [ ] Boot com o pendrive já espetado: `usb` mostra o aparelho, a velocidade
      e o disco montado; a barra mostra "Pendrive <nome>".
- [ ] Espetar com o Tab5 ligado (tela acesa e em repouso).
- [ ] Música > Pastas: a linha do pendrive no topo; entrar, subpastas, voltar.
- [ ] Tocar do pendrive; a capa (etiqueta e cover.jpg da pasta); duração.
- [ ] Arrancar tocando: a música para em até ~3 s, a lista volta à raiz, nada
      trava (`usb` com 0 segurando).
- [ ] Nome com acento ("Canção.mp3") no cartão e no pendrive.
- [ ] Favoritar uma faixa do pendrive; tirar o pendrive; a playlist pula a
      faixa; pôr de volta, toca.
- [ ] Pendrive exFAT (64 GB de fabrica): monta e toca. Pendrive NTFS: aviso "use FAT32/exFAT", nada trava.
- [ ] Cartao SD exFAT (SDXC de 64 GB): monta no boot; o modo pendrive continua funcionando.
- [ ] `usb ler` num MP3 grande: KB/s com DMA na PSRAM.
- [ ] Modo pendrive (cartão no computador) tocando do USB-A: o som continua.
- [ ] Consumo em repouso com e sem pendrive (`energia ensaio`).

**Fone e USB-A em geral**

Medições antes de escrever o serviço - cada uma responde uma dúvida acima.

**Fone**
- [ ] Ler o bit 7 do expansor 0 com e sem plugue → confirma a polaridade do `HP_DET`.
- [ ] Plugue de 3 polos (TRS) e de 4 polos (TRRS): o `HP_DET` acusa os dois?
- [ ] Tom só no L, só no R → confirma estéreo no fone e que o alto-falante só toca o L.
- [ ] `SPK_EN` desligado com fone: alto-falante mudo, fone tocando.
- [ ] Varredura 40 Hz-2 kHz no fone: conferir a perda de grave estimada.
- [ ] Volume no ES8388 (regs 46/47): faixa útil e ruído de fundo em volume baixo.

**USB-A**
- [ ] 5 V no conector com `usb_a_5v` ligado/desligado (e corrente em repouso pelo INA226).
- [ ] Pendrive HS: enumera como **high speed**? (se cair para FS, olhar sinal/`fsls_only`).
- [ ] MB/s de leitura e escrita, buffers DMA na RAM interna × PSRAM.
- [ ] Teclado e mouse USB (boot protocol); hub simples.
- [ ] Pendrive no USB-A **junto** com o modo pendrive no USB-C.
- [ ] Sobrecorrente: aparelho guloso → o que o MT9700 faz e como o host percebe.
- [ ] Repetir o básico com `sdkconfig` chip_v1 e chip_v3.

## Referências

- Esquema: https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1132/Tab5_Schematics_PDF.pdf (pág. 1: P4 e expansores; pág. 3: áudio; pág. 4: conectores USB; pág. 5: MT9700)
- Página do Tab5: https://docs.m5stack.com/en/core/Tab5
- BSP Espressif: https://components.espressif.com/components/espressif/m5stack_tab5
- Classes USB host: https://github.com/espressif/esp-usb/tree/master/host/class
- `espressif/usb`: https://components.espressif.com/components/espressif/usb
- `esp_codec_dev`: https://components.espressif.com/components/espressif/esp_codec_dev
