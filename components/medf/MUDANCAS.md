# medf: o que o M5tabOS mudou no original

A origem está em `ORIGEM.txt` (Tab5-Media-Player, licença MIT em `LICENSE`).
Toda mudança vem marcada com `M5tabOS:` no código.

- **Leitura do arquivo por funções de fora** (`medf_set_io` em `medf_buffer.h`): o sistema só lê o cartão pelas portas dele (`tab5_leitor`), então registra as suas funções. Sem registro, vale o POSIX de antes.
- **`medf_mp4_demux_drop_video`**: lê só a faixa de áudio de um MP4. O tocador de música toca o som do vídeo enquanto o vídeo lê o mesmo arquivo por conta própria.
- **Leitor de MP4 na PSRAM primeiro**: a SRAM interna é disputada pelo Wi-Fi.
- **Prioridade e núcleo da leitura antecipada** (`readahead_prio`, `readahead_core` em `medf_io_t`): o sistema põe 5, em qualquer núcleo. Com 3 no núcleo 0, um decodificador de vídeo ocupando o núcleo deixava a leitura do som sem vez.
