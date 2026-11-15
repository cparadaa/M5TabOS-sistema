// LINHAS - ler um arquivo de texto GRANDE, linha por linha, pelo leitor rapido.
//
// POR QUE EXISTE. fgets no cartao engasga (fread rende 52-572 KB/s, ver
// tab5_volumes.hpp); o tab5_leitor chega a 4,6 MB/s, mas entrega bytes, nao
// linhas. Este modulo e a ponte: quem le CSV (a trilha, o registro de
// satelites) pede a proxima linha e nao se preocupa com blocos.
//
//   tab5_linhas_t* l = tab5_linhas_abrir("/sdcard/apps/Trilha/trilha_0001.csv");
//   char linha[112];
//   while (l && tab5_linhas_proxima(l, linha, sizeof(linha))) { ... }
//   tab5_linhas_fechar(l);
//
// Bloqueia (monta o disco): fora da tarefa de interface quando o arquivo e
// grande. Um leitor de linhas e de UMA tarefa.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct tab5_linhas tab5_linhas_t;

// nullptr = falhou (sem disco ou arquivo inexistente; o disco ja foi solto).
tab5_linhas_t* tab5_linhas_abrir(const char* caminho);
// A proxima linha, SEM o '\n' (nem o '\r'). false no fim do arquivo. Linha
// maior que 'tam' - 1 e cortada: o resto dela e descartado, e '*cortada' (se
// informado) fica true.
bool tab5_linhas_proxima(tab5_linhas_t* l, char* out, size_t tam, bool* cortada = nullptr);
// Salta para o byte 'pos' (inicio de uma linha, vindo de tab5_linhas_pos).
// false = nao conseguiu (a leitura segue de onde estava).
bool tab5_linhas_ir(tab5_linhas_t* l, uint64_t pos);
// Posicao, em bytes, do proximo byte a entregar.
uint64_t tab5_linhas_pos(const tab5_linhas_t* l);
void tab5_linhas_fechar(tab5_linhas_t* l);   // fecha e solta o disco; aceita nullptr
