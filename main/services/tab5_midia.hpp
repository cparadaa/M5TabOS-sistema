// MIDIA - a ponte entre as bibliotecas de midia (components/medf: leitores de
// MP4 e outros conteineres; components/audf: decodificadores de som como o
// AAC) e o sistema.
//
// O medf le o arquivo por funcoes que o sistema registra: aqui elas usam o
// leitor de tab5_volumes (cartao ou pendrive, o disco fica seguro enquanto o
// arquivo esta aberto). Quem usa o medf chama tab5_midia_preparar() antes.
#pragma once
#include <stddef.h>
#include "medf_buffer.h"

// Registra as funcoes de arquivo no medf (uma vez; chamar de novo nao faz nada).
void tab5_midia_preparar(void);

// A memoria de leitura de um arquivo (o "arena" do medf), na PSRAM e com o
// alinhamento que ele pede. 'bytes' em multiplos de 64 KB; o maior pacote que
// pode ser lido de uma vez e 1/4 disso. false = sem memoria.
bool tab5_midia_arena(medf_arena_t* a, size_t bytes);
void tab5_midia_arena_soltar(medf_arena_t* a);
