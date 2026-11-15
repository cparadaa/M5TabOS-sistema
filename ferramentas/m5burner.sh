#!/bin/bash
# GERA O FIRMWARE PARA O M5BURNER - um .bin unico, gravado a partir de 0x0.
#
#   ferramentas/m5burner.sh              a variante m5burner, P4 v1.x
#   ferramentas/m5burner.sh v3           a variante m5burner, P4 v3.x
#   ferramentas/m5burner.sh berry [v3]   outra variante (ver docs/VARIANTES.md)
#
# O QUE ENTRA e decidido pela variante (variantes/m5burner.cmake: uma copia
# da berry - apps do sistema + Berry, com o Ola.be de exemplo). Junta bootloader,
# tabela de particoes e app num arquivo so, na raiz do projeto (fora do git):
#   M5tabOS-<versao>-p4<chip>.bin          (variante m5burner)
#   M5tabOS-<versao>-<variante>-p4<chip>.bin
#
# Gravar este arquivo APAGA os ajustes salvos (o espaco entre as partes e
# preenchido, inclusive a NVS). O firmware do C6 (ESP-NOW, Bluetooth) nao vai
# junto - o M5Burner nao grava o C6.
set -euo pipefail
cd "$(dirname "$0")/.." || exit 1

VARIANTE=m5burner
[ -f variantes/m5burner.cmake ] || VARIANTE=completa   # a copia publica nao tem a m5burner
CHIP=v1
for a in "$@"; do
    case "$a" in
        v1|v3) CHIP="$a" ;;
        *) VARIANTE="$a" ;;
    esac
done
if [ ! -f "variantes/$VARIANTE.cmake" ]; then
    echo "uso: ferramentas/m5burner.sh [variante] [v1|v3] - variantes: $(ls variantes | sed 's/\.cmake//' | tr '\n' ' ')"
    exit 1
fi
source ./ambiente.sh

# A pasta de build da variante (a mesma do compilar_tudo.sh e do gravar.sh).
PASTA=build_$VARIANTE
[ "$VARIANTE" = "completa" ] && PASTA=build
[ "$CHIP" = "v3" ] && PASTA="${PASTA}_v3"
idf.py -B "$PASTA" -DM5_CHIP="$CHIP" -DM5_VARIANTE="$VARIANTE" build

VERSAO="$(head -1 version.txt | tr -d '[:space:]')"
NOME="M5tabOS-$VERSAO"
[ "$VARIANTE" != "m5burner" ] && NOME="$NOME-$VARIANTE"
SAIDA="$PWD/$NOME-p4$CHIP.bin"
( cd "$PASTA" && esptool.py --chip esp32p4 merge-bin -o "$SAIDA" @flash_args > /dev/null )
echo ">>> $SAIDA ($(du -h "$SAIDA" | cut -f1)) - no M5Burner, gravar a partir de 0x0"
