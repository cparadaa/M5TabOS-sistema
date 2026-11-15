# nome: Teste Quadros
# cor: 0x07FF
# periodo: 16
#
# Teste de velocidade: separa "o desenho e lento" de "a IMU e lenta".
#
# - Bolinha AMARELA: anda sozinha de um lado ao outro, pelo relogio
#   (sistema.ms). Nao depende da IMU. Se ela anda aos saltos, o problema e
#   a cadencia dos quadros, nao o sensor.
# - Bolinha VERDE: a IMU (incline o aparelho).
# - Numeros: quadros por segundo, o tempo do desenhar() em Berry, o maior
#   intervalo entre quadros no ultimo segundo, e quantas leituras NOVAS da
#   IMU chegaram por segundo (se for bem menor que os quadros, o sensor
#   esta devolvendo o mesmo valor repetido).

var t0 = sistema.ms()       # inicio da animacao
var ultimo = t0              # o quadro anterior
var janela = t0              # inicio da janela de 1 s das medidas
var quadros = 0
var novas = 0
var pior = 0
var ant_ax = nil
var fps = 0
var imu_s = 0
var pior_s = 0
var custo = 0

def desenhar(tudo)
  var ini = sistema.ms()
  var l = tela.largura()

  # Medidas, fechadas a cada segundo.
  var dt = ini - ultimo
  ultimo = ini
  if dt > pior pior = dt end
  quadros += 1
  if ini - janela >= 1000
    fps = quadros
    imu_s = novas
    pior_s = pior
    quadros = 0
    novas = 0
    pior = 0
    janela = ini
  end

  # A bolinha independente: vai e volta em 2 s, pelo relogio.
  var m = 60
  var curso = l - 2 * m
  var fase = ((ini - t0) % 4000) / 2000.0     # 0..2
  if fase > 1 fase = 2 - fase end
  tela.linha(m, 150, l - m, 150, tela.CINZA)
  tela.circulo(m + int(curso * fase), 150, 30, tela.AMARELO)

  # A IMU.
  var g = sistema.imu()
  var cx = l / 2
  var cy = 430
  tela.circulo(cx, cy, 180, tela.CINZA, false)
  tela.linha(cx - 180, cy, cx + 180, cy, tela.CINZA)
  tela.linha(cx, cy - 180, cx, cy + 180, tela.CINZA)
  if g != nil
    if ant_ax == nil || g[0] != ant_ax novas += 1 end
    ant_ax = g[0]
    tela.circulo(cx + int(g[0] * -170), cy + int(g[1] * 170), 26, tela.VERDE)
  else
    tela.texto("IMU sem leitura", cx - 150, cy - 12, 3, tela.VERMELHO)
  end

  tela.texto("quadros/s: " + str(fps) + "   pior intervalo: " + str(pior_s) + " ms", 30, 20, 3)
  tela.texto("desenhar(): " + str(custo) + " ms   IMU novas/s: " + str(imu_s), 30, 60, 3)

  custo = sistema.ms() - ini
end
