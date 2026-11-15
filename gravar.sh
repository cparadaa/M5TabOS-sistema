#!/bin/bash
# Compila e grava o M5tabOS no Tab5, e abre o monitor serial.
# Uso:  ./gravar.sh            pergunta qual variante (Enter = a ultima)
#       ./gravar.sh berry      grava a berry direto  (ver docs/VARIANTES.md)
# Ctrl+] sai do monitor.
cd "$(dirname "$0")" || exit 1

# SEM A VARIANTE, UM MENU (o botao "Gravar" do VSCode e o ferramentas/menu.sh
# caem aqui). Cada variante se descreve na linha "# menu:" do seu arquivo -
# uma variante nova aparece sozinha. Enter repete a ultima gravada (guardada
# dentro do .git, fora do repositorio).
ULTIMA_ARQ=".git/m5_ultima_variante"
VARIANTE="$1"
if [ -z "$VARIANTE" ]; then
    ULTIMA="$(cat "$ULTIMA_ARQ" 2>/dev/null)"
    [ -f "variantes/$ULTIMA.cmake" ] || ULTIMA=completa
    if [ -t 0 ]; then
        echo "Qual variante gravar?"
        NOMES=()
        for f in variantes/*.cmake; do
            n="$(basename "$f" .cmake)"
            NOMES+=("$n")
            printf "  %d) %-9s %s\n" "${#NOMES[@]}" "$n" "$(sed -n 's/^# menu: //p' "$f")"
        done
        printf "Numero (Enter = %s): " "$ULTIMA"
        read -r ESC
        if [ -z "$ESC" ]; then
            VARIANTE="$ULTIMA"
        elif [[ "$ESC" =~ ^[0-9]+$ ]] && [ "$ESC" -ge 1 ] && [ "$ESC" -le "${#NOMES[@]}" ]; then
            VARIANTE="${NOMES[$((ESC-1))]}"
        else
            VARIANTE="$ESC"   # tambem aceita o nome
        fi
    else
        VARIANTE="$ULTIMA"
    fi
fi
if [ ! -f "variantes/$VARIANTE.cmake" ]; then
    echo "Variante '$VARIANTE' nao existe. As que ha: $(ls variantes | sed 's/\.cmake//' | tr '\n' ' ')"
    exit 1
fi
source ./ambiente.sh

# A porta do Tab5 (USB-Serial-JTAG nativo do P4): /dev/cu.usbmodem* no macOS,
# /dev/ttyACM* no Linux. PORTA=... ./gravar.sh escolhe outra.
if [ -z "$PORTA" ]; then
    PORTA=$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)
fi
if [ -z "$PORTA" ]; then
    echo "Nenhuma porta USB do Tab5 encontrada. O Tab5 esta ligado e conectado?"
    exit 1
fi

# A REVISAO DO P4 decide o binario (ver sdkconfig.defaults.chip_v1/_v3): um
# binario de v1.x nao roda num v3.x, nem o contrario. Lida do proprio
# aparelho; CHIP=v1 ou CHIP=v3 ./gravar.sh pula a leitura.
# --after no-reset: depois de ler, o chip FICA no modo de gravacao. Sem isso
# ele reiniciava, rodava o programa antigo por um instante, e so entao o
# flash entrava - um reinicio a toa a cada gravacao.
if [ -z "$CHIP" ]; then
    REV=$(esptool.py --chip esp32p4 -p "$PORTA" --after no-reset chip-id 2>/dev/null | grep -o "revision v[0-9]*" | head -1)
    case "$REV" in
        "revision v3"*) CHIP=v3 ;;
        "revision v"*)  CHIP=v1 ;;
        *) echo "Nao consegui ler a revisao do chip - usando v1 (CHIP=v3 ./gravar.sh forca a outra)"; CHIP=v1 ;;
    esac
fi
echo ">>> P4 $CHIP, variante $VARIANTE, gravando em $PORTA"
echo "$VARIANTE" > "$ULTIMA_ARQ" 2>/dev/null || true

# Cada combinacao compila na SUA pasta (o sdkconfig mora dentro dela):
#   build/ (v1 completa)  build_v3/  build_berry/  build_berry_v3/
PASTA=build
[ "$VARIANTE" != "completa" ] && PASTA="${PASTA}_$VARIANTE"
[ "$CHIP" = "v3" ] && PASTA="${PASTA}_v3"

# SEM FONTE NOVO, NAO COMPILA: 'idf.py flash' sempre roda o build antes, e o
# carimbo de "Compilado em" (main/carimbo.cmake) muda a cada minuto - todo
# flash recompilava e religava o firmware inteiro sem nada ter mudado. Se o
# binario da pasta e mais novo que tudo que entra nele, grava o que ja esta
# la. COMPILAR=1 ./gravar.sh forca o build.
BIN="$PASTA/flash_args"
if [ -z "$COMPILAR" ] && [ -f "$BIN" ] && [ -f "$PASTA/M5tabOS.bin" ] && \
   [ -z "$(find main components compartilhado berry_exemplos variantes \
                CMakeLists.txt partitions.csv sdkconfig.defaults* \
                -newer "$PASTA/M5tabOS.bin" -type f -print -quit 2>/dev/null)" ]; then
    echo ">>> nada mudou desde o ultimo build de $PASTA/ - gravando sem compilar"
    ( cd "$PASTA" && esptool.py --chip esp32p4 -p "$PORTA" -b 460800 \
          --before default_reset --after hard_reset write_flash "@flash_args" ) || exit 1
    idf.py -B "$PASTA" -p "$PORTA" monitor
    exit $?
fi

if [ "$PASTA" = "build" ]; then
    idf.py -p "$PORTA" flash monitor
else
    idf.py -B "$PASTA" -DM5_CHIP="$CHIP" -DM5_VARIANTE="$VARIANTE" -p "$PORTA" flash monitor
fi
