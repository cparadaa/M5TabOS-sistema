#!/usr/bin/env python3
# Manda comandos ao console do Tab5 pela serial e devolve o que ele respondeu.
# Feito para medir sem o idf.py monitor (e para os agentes do Claude usarem).
#
#   ferramentas/serial_cmd.py musica
#   ferramentas/serial_cmd.py "musica" "@10" "musica"       <- @10 = escuta 10 s
#   ferramentas/serial_cmd.py --filtro "secou|musica" --log /tmp/x.txt @60
#   ferramentas/serial_cmd.py --reset @15                     <- reinicia e pega o boot
#
# Cada argumento e uma linha de comando do console, ou uma pausa "@<segundos>"
# (escutando o log o tempo todo). Depois de cada comando espera a resposta
# assentar (--assentar, 1,5 s sem nada novo; no maximo --max s).
#
#   --log ARQ      grava TUDO o que chegou (o stdout pode vir filtrado)
#   --filtro RE    no stdout so as linhas que casam (o --log continua inteiro)
#   --reset        reinicia o Tab5 antes (como o esptool faz no USB-Serial-JTAG)
#
# FECHE O MONITOR ANTES: a porta e de um processo so.
# Comandos que GRAVAM na NVS (ble ligar, espnow canal, energia sono...) ficam
# gravados - devolva o valor do usuario depois do teste.

import sys, os, glob, time, re, argparse

try:
    import serial
except ImportError:
    for py in (os.path.join(os.environ.get("IDF_PYTHON_ENV_PATH", ""), "bin", "python"),
               os.path.expanduser("~/.m5tabos/idf-5.5-venv/bin/python")):
        if os.path.exists(py) and os.environ.get("_SERIAL_REEXEC") != "1":
            os.environ["_SERIAL_REEXEC"] = "1"
            os.execv(py, [py, os.path.abspath(__file__)] + sys.argv[1:])
    sys.exit("Falta o pyserial (venv do IDF: ~/.m5tabos/idf-5.5-venv).")

ap = argparse.ArgumentParser()
ap.add_argument("passos", nargs="*")
ap.add_argument("--porta")
ap.add_argument("--log")
ap.add_argument("--filtro")
ap.add_argument("--reset", action="store_true")
ap.add_argument("--assentar", type=float, default=1.5)
ap.add_argument("--max", type=float, default=20.0)
a = ap.parse_args()

portas = [a.porta] if a.porta else sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
if not portas:
    sys.exit("Nenhuma porta USB do Tab5. Ele esta ligado e conectado?")
try:
    s = serial.Serial(portas[0], 115200, timeout=0.2)
except Exception as e:
    sys.exit(f"Nao abriu {portas[0]}: {e} (o monitor esta aberto?)")

filtro = re.compile(a.filtro) if a.filtro else None
log = open(a.log, "w", encoding="utf-8") if a.log else None
resto = ""

def ler(segundos, assentar=None):
    """Le ate 'segundos'; com 'assentar', para antes se ficar quieto esse tempo."""
    global resto
    fim = time.time() + segundos
    quieto = time.time()
    while time.time() < fim:
        b = s.read(4096)
        if b:
            quieto = time.time()
            txt = resto + b.decode("utf-8", "replace").replace("\r", "")
            linhas = txt.split("\n")
            resto = linhas.pop()
            for l in linhas:
                if log: log.write(l + "\n")
                if not filtro or filtro.search(l): print(l, flush=True)
        elif assentar and time.time() - quieto > assentar:
            break

if a.reset:
    s.dtr = False; s.rts = True; time.sleep(0.2); s.rts = False
    print(">>> reiniciado", flush=True)
else:
    ler(0.5, 0.3)   # o que estava no meio do caminho
for p in a.passos:
    if p.startswith("@"):
        print(f">>> escutando {p[1:]} s", flush=True)
        ler(float(p[1:]))
        continue
    print(f">>> {p}", flush=True)
    s.write((p + "\r").encode())
    ler(a.max, a.assentar)
ler(0.3, 0.2)
if resto:
    if log: log.write(resto + "\n")
    if not filtro or filtro.search(resto): print(resto)
if log: log.close()
s.close()
