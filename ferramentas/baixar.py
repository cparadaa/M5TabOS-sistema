#!/usr/bin/env python3
# Copia um arquivo do cartao do Tab5 para o computador, pela mesma serial do monitor.
#
#   ./baixar.py /sdcard/apps/Sistema/energia.csv
#   ./baixar.py                  <- sem argumento, baixa o energia.csv
#
# Do outro lado quem responde e o comando "despejar" do console (console.cpp).
# Ele cala o ESP_LOG enquanto despeja e cerca o conteudo com >>>INICIO e >>>FIM,
# que e o que permite saber onde comeca, onde termina e se chegou tudo.
#
# FECHE O MONITOR ANTES. A porta e de um processo so: com o idf.py monitor
# aberto, este script nao consegue abrir a porta - ou pior, os dois leem
# metade cada um.

import sys, glob, time, os

# O pyserial nao esta no python do sistema - ele mora no venv do IDF. Em vez de
# exigir "source ambiente.sh" antes (passo facil de esquecer), o proprio arquivo se reexecuta com o interpretador
# certo na primeira falha de import. Roda direto, clicando ou pelo terminal.
try:
    import serial
except ImportError:
    # O python do ambiente do ESP-IDF (IDF_PYTHON_ENV_PATH, definido pelo
    # export.sh / ambiente.sh).
    IDF_PY = os.path.join(os.environ.get("IDF_PYTHON_ENV_PATH", ""), "bin", "python")
    if os.path.exists(IDF_PY) and os.environ.get("_BAIXAR_REEXEC") != "1":
        os.environ["_BAIXAR_REEXEC"] = "1"
        os.execv(IDF_PY, [IDF_PY, os.path.abspath(__file__)] + sys.argv[1:])
    sys.exit("Falta o pyserial. Rode:  source ambiente.sh && ./baixar.py")

ORIGEM  = sys.argv[1] if len(sys.argv) > 1 else "/sdcard/apps/Sistema/energia.csv"
# Binario (foto, firmware) vem em base64 ('despejar -b'): texto puro quebra
# no primeiro byte nulo.
BINARIO = ORIGEM.lower().endswith((".bmp", ".png", ".jpg", ".bin", ".mp3", ".wav"))
import re
B64 = re.compile(r"^[A-Za-z0-9+/]+={0,2}$")
DESTINO = ORIGEM.split("/")[-1]

# DOIS PRAZOS, e a diferenca importa.
#
# Antes de o despejo comecar, o unico motivo de silencio e alguma coisa errada -
# porta ocupada pelo monitor, firmware sem o comando, cartao fora. Esperar dois
# minutos por isso e so demorar a dar a ma noticia.
#
# Depois que ele comecou, um silencio longo e normal: o Tab5 pausa a cada 64
# linhas para nao segurar a CPU, e o cartao as vezes trava um instante.
ESPERA_INICIO = 8       # ate a primeira resposta
ESPERA_FLUXO  = 60      # entre uma linha e a proxima, ja despejando

portas = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
if not portas:
    sys.exit("Nenhuma porta USB do Tab5. Ele esta ligado e conectado?")
porta = portas[0]

print(f">>> {porta}: pedindo {ORIGEM}")
s = serial.Serial(porta, 115200, timeout=1)
# ABRIR A PORTA REINICIA O TAB5 (USB-Serial-JTAG). Espera o boot assentar -
# 2 s sem nada chegando, no maximo 20 s - antes de pedir: pedido no meio do
# boot tem log do boot no meio do arquivo.
fim_boot = time.time() + 20
quieto = time.time()
while time.time() < fim_boot and time.time() - quieto < 2:
    if s.read(4096):
        quieto = time.time()
s.reset_input_buffer()
s.write(f"despejar {'-b ' if BINARIO else ''}{ORIGEM}\n".encode())

linhas   = []
dentro   = False
esperado = None
contadas = None
mudo_ate = time.time() + ESPERA_INICIO

while time.time() < mudo_ate:
    bruto = s.readline()
    if not bruto:
        continue
    mudo_ate = time.time() + (ESPERA_FLUXO if dentro else ESPERA_INICIO)
    linha = bruto.decode("utf-8", "replace").rstrip("\r\n")

    # Firmware sem o comando. Sem este teste o script ficava esperando o
    # >>>INICIO que nunca vem, e o sintoma era "travou" em vez de "grave o
    # firmware novo" - a mesma informacao, entregue tarde demais para servir.
    if "comando desconhecido" in linha:
        sys.exit("Este Tab5 ainda nao tem o comando 'despejar'.\n"
                 "Grave o firmware novo:  ./gravar.sh")

    if linha.startswith(">>>INICIO"):
        dentro = True
        partes = linha.split()
        if len(partes) >= 3:
            esperado = int(partes[2])
            print(f"    {esperado} bytes a caminho...")
        continue

    if linha.startswith(">>>FIM"):
        contadas = linha.split()[1]
        break

    if linha.startswith("erro:"):
        sys.exit(f"O Tab5 respondeu: {linha}")

    if dentro:
        # Log de outra tarefa que escapou do silencio (ex.: um driver com
        # nivel proprio): linha inteira de log, nao e do arquivo.
        # Binario: so base64 e do arquivo. Log de outra tarefa que escapou do
        # silencio, um printf de outro servico, linha vazia da serial: fora.
        if BINARIO and (not linha or not B64.match(linha)):
            continue
        linhas.append(linha)
        if len(linhas) % 200 == 0:
            print(f"    {len(linhas)} linhas", end="\r", flush=True)

s.close()

if contadas is None:
    if not dentro:
        sys.exit("O Tab5 nao respondeu.\n"
                 "  - o monitor serial esta aberto em outra janela? "
                 "a porta e de um processo so\n"
                 "  - o cartao esta no aparelho?")
    sys.exit(f"\nO despejo parou no meio ({len(linhas)} linhas). "
             "Rode de novo - arquivo pela metade nao serve para ajustar curva.")

if BINARIO:
    import base64
    # Linha a linha: cada uma sao 57 bytes inteiros (76 caracteres), entao
    # uma linha estragada na serial se acha e se conta, em vez de virar um
    # erro de "padding" no fim.
    ruins = [i for i, l in enumerate(linhas[:-1]) if len(l) != 76]
    if ruins:
        sys.exit(f"!!! {len(ruins)} linha(s) estragada(s) na serial (ex.: linha {ruins[0]}: "
                 f"{linhas[ruins[0]][:40]!r}) - rode de novo.")
    dados = b"".join(base64.b64decode(l) for l in linhas)
    open(DESTINO, "wb").write(dados)
    print(f"\n>>> {DESTINO}: {len(dados)} bytes")
    if esperado is not None and len(dados) != esperado:
        # Gravado mesmo assim: numa foto, 0,2% a menos e uma faixa torta, e
        # ela ainda serve para olhar. Para firmware, NAO - rode de novo.
        sys.exit(f"!!! o Tab5 disse {esperado} bytes e chegaram {len(dados)} (gravado assim mesmo) - "
                 "para foto serve; para o resto, rode de novo.")
    sys.exit(0)

corpo = "\n".join(linhas) + "\n"
open(DESTINO, "w").write(corpo)

# CONFERE O TAMANHO, e nao so a contagem de linhas. Byte perdido na serial nao
# apaga uma linha inteira - ele corrompe um numero no meio, e uma curva ajustada
# em cima de numero corrompido mente sem dar sinal.
recebido = len(corpo.encode())
print(f"\n>>> {DESTINO}: {len(linhas)} linhas, {recebido} bytes")
if esperado is not None and recebido != esperado:
    print(f"!!! o Tab5 disse {esperado} bytes e chegaram {recebido}.")
    print("    Provavel byte perdido - rode de novo antes de usar este arquivo.")
