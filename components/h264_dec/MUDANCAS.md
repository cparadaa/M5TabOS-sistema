# h264_dec: o que o M5tabOS mudou no original

A origem está em `ORIGEM.txt` (Tab5-Media-Player, licença MIT em `LICENSE`).
Toda mudança vem marcada com `M5tabOS:` no código.

**Junção dos componentes.** `h264_dec` e `vdec_common` viraram um componente só. Os núcleos comuns foram renomeados para `src/vdec_kernels_c.c` e `src/vdec_kernels_p4.S`.

**Robustez contra arquivo malformado** (achados do fuzz e da revisão):
- `params.c`: o recorte do SPS com valores enormes dava a volta em 32 bits e passava na conferência (`crop_left` perto de 65280). Agora valores acima de 8192 são recusados.
- `h264_internal.h` e `decoder.c`: `BITS_FOLGA` (4 KB) depois dos buffers de bits. A leitura só confere o fim no fim de cada macrobloco ou SPS, então um arquivo ruim lia alguns KB além do que foi carregado.
- `bits.c`: a recarga apagava a marca de estouro deixada por `bits_ue` (o resto da fatia virava lixo sem ninguém saber). Agora a marca fica.
- `decoder.c`: a falta de memória (arena ou quadros) ao ativar um SPS agora devolve `H264_DEC_NO_MEMORY`, e não `UNSUPPORTED`. Assim quem chama pode tentar com mais memória (campo `sem_memoria`).

**Arena em pedaços.** A configuração ganhou `work_extra[]` e `work_extra_bytes[]`, porque a SRAM do P4 quase nunca tem um bloco do tamanho da arena inteira.
- Cada pedido vai para o primeiro pedaço onde cabe.
- As linhas mínimas são reservadas primeiro, depois a janela de referência, depois mais linhas. É a mesma prioridade do original, só que pedido a pedido.

**Buffer de bits de 4 KB** (`BITS_CHUNK`, eram 16 KB no original): poupa 12 KB de SRAM.

**Teste** (`test/h264_dec_test.c`): o modo `H264_PEDACOS=1` entrega a arena em 4 pedaços.

**Conferência:** `testes/rodar_h264.sh` compara cada quadro com o ffmpeg, bit a bit, em 3 modos (um núcleo, com o trabalhador e com a arena em pedaços). `FUZZ=N` roda N arquivos corrompidos com ASan e UBSan.

**Um bloco por quadro** (`decoder.c`, `frame_mem[]`): os quadros de referência eram um bloco contíguo só. No 1080p são 5 × 3 MB, e a PSRAM fragmentada não tem 16 MB contíguos. Agora cada quadro é pedido separado, com a mesma folga no fim (`FRAME_SLACK`).
