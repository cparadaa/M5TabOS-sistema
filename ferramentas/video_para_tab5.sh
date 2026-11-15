#!/bin/bash
# VIDEO PARA O TAB5 - converte qualquer video para o que o app Video toca:
#   <base>.avi  video MJPEG, 720 de largura (a tela do Tab5), no maximo 25
#               quadros por segundo, SEM som
#   <base>.mp3  o som, 44100 Hz, estereo, 128 kbps
# Copie os DOIS para a pasta Videos do cartao: o app toca o .avi e o som do
# .mp3 de mesmo nome (o som manda no tempo do video).
#
# Uso:   ferramentas/video_para_tab5.sh <entrada> [saida_base]
#   ferramentas/video_para_tab5.sh filme.mkv              -> filme.avi + filme.mp3 (na pasta atual)
#   ferramentas/video_para_tab5.sh filme.mkv /Volumes/SD/Videos/filme
#
# Precisa do ffmpeg:  brew install ffmpeg
set -e

if ! command -v ffmpeg > /dev/null 2>&1 || ! command -v ffprobe > /dev/null 2>&1; then
  echo "ffmpeg nao encontrado. Instale com:  brew install ffmpeg" >&2
  exit 1
fi
if [ -z "$1" ] || [ ! -f "$1" ]; then
  echo "uso: $0 <entrada> [saida_base]" >&2
  exit 1
fi

ENTRADA="$1"
# Sem o segundo argumento: o nome da entrada, sem a extensao, na pasta atual.
BASE="${2:-$(basename "${ENTRADA%.*}")}"

# QUADROS POR SEGUNDO: os da entrada, mas nunca mais que 25 (o decodificador
# do Tab5 nao passa muito disso em 720 de largura).
FPS_ORIGINAL=$(ffprobe -v error -select_streams v:0 -show_entries stream=r_frame_rate \
               -of default=noprint_wrappers=1:nokey=1 "$ENTRADA" | head -1)
FPS=$(awk -v f="$FPS_ORIGINAL" 'BEGIN {
  n = split(f, a, "/"); v = (n == 2 && a[2] > 0) ? a[1] / a[2] : a[1] + 0
  if (v <= 0 || v > 25) v = 25
  printf "%.3f", v }')

# O VIDEO: largura 720 e altura par proporcional (scale=720:-2); MJPEG com
# croma 4:2:0 (o que o decodificador do P4 le); -q:v 5 = boa qualidade, quadros
# de ~40 KB; -an = sem som (o som vai no .mp3).
echo "video: ${BASE}.avi  (720 de largura, ${FPS} quadros/s)"
ffmpeg -v error -stats -y -i "$ENTRADA" -an \
  -vf "scale=720:-2,fps=${FPS}" -c:v mjpeg -pix_fmt yuvj420p -q:v 5 "${BASE}.avi"

# O SOM: so se a entrada tem faixa de audio.
if [ -n "$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_type \
           -of default=noprint_wrappers=1:nokey=1 "$ENTRADA")" ]; then
  echo "som:   ${BASE}.mp3  (44100 Hz, estereo, 128 kbps)"
  ffmpeg -v error -stats -y -i "$ENTRADA" -vn -ar 44100 -ac 2 -c:a libmp3lame -b:a 128k "${BASE}.mp3"
else
  echo "a entrada nao tem som: so o .avi foi gerado (toca mudo)"
fi

echo "pronto. Copie para a pasta Videos do cartao: ${BASE}.avi e ${BASE}.mp3"
