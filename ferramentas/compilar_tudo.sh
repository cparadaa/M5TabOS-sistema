#!/bin/bash
# COMPILA TODAS AS VARIANTES - a regra de ouro do docs/VARIANTES.md: um commit so
# esta pronto quando todas compilam. O nucleo e um so; uma mudanca nele que
# quebra uma variante quebra o sistema.
#   ferramentas/compilar_tudo.sh          as variantes, P4 v1
#   ferramentas/compilar_tudo.sh v3       tambem as de P4 v3
# Confere tambem os apps Berry de exemplo (testes/checar_be.sh).
cd "$(dirname "$0")/.." || exit 1
source ./ambiente.sh > /dev/null
CHIPS="v1"
[ "$1" = "v3" ] && CHIPS="v1 v3"
FALHAS=""
for chip in $CHIPS; do
  for v in $(ls variantes | sed 's/\.cmake$//'); do
    PASTA=build
    [ "$v" != "completa" ] && PASTA="${PASTA}_$v"
    [ "$chip" = "v3" ] && PASTA="${PASTA}_v3"
    printf ">>> %-9s P4 %s  (%s/) ... " "$v" "$chip" "$PASTA"
    if [ "$PASTA" = "build" ]; then
      SAIDA=$(idf.py build 2>&1)
    else
      SAIDA=$(idf.py -B "$PASTA" -DM5_CHIP="$chip" -DM5_VARIANTE="$v" build 2>&1)
    fi
    if [ $? -eq 0 ]; then
      echo "ok  $(echo "$SAIDA" | grep -o '[0-9]*%) free' | tail -1)"
    else
      echo "FALHOU"
      echo "$SAIDA" | grep -E "error:|Error" | head -10
      FALHAS="$FALHAS $v/$chip"
    fi
  done
done
if [ -x testes/checar_be.sh ] && [ -d berry_exemplos ]; then
  echo ">>> apps Berry de exemplo"
  testes/checar_be.sh || FALHAS="$FALHAS berry_exemplos"
fi
# O teclado virtual (kernel/teclado_virtual + o editor) tem banco de prova no
# Mac: os tipos de campo, o toque longo, o espaco duplo e o Enter das Notas.
if [ -x testes/rodar_teclado.sh ]; then
  printf ">>> teclado virtual (banco de prova) ... "
  if SAIDA=$(testes/rodar_teclado.sh 2>&1); then echo "ok"; else echo "FALHOU"; echo "$SAIDA" | grep -E "FALHA|error" | head -10; FALHAS="$FALHAS teclado"; fi
fi
# O decodificador de GIF (services/tab5_gif_nucleo) tambem: GIFs reais e feitos
# a mao conferidos pixel a pixel, e arquivos estragados sob ASan/UBSan.
if [ -x testes/rodar_gif.sh ]; then
  printf ">>> decodificador de GIF (banco de prova) ... "
  if SAIDA=$(testes/rodar_gif.sh 2>&1); then echo "ok"; else echo "FALHOU"; echo "$SAIDA" | grep -E "FALHA|error" | head -10; FALHAS="$FALHAS gif"; fi
fi
if [ -n "$FALHAS" ]; then echo "FALHOU:$FALHAS"; exit 1; fi
echo "tudo compilou"
