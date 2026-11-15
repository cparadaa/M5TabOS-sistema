# nome: Mostruario
# cor: 0xF81F
# periodo: 16
#
# O mostruario da API Berry: uma aba para cada parte do sistema. E o molde
# para um app novo - copie, apague as abas que nao interessam.
#
#   Desenho   todas as formas, cores, alinhamento de texto, animacao por dt
#   Sensores  IMU (aceleracao e giro), bateria, data e hora, GPS
#   Teclado   campo de texto: teclado virtual do sistema ou o fisico
#   Dedo      desenhar com o dedo (o dedo() recebe o arrasto)
#   Cartao    arquivos na pasta do app: gravar, anexar, ler, listar
#   Rede      Wi-Fi, ESP-NOW (enviar e receber), Mesh (estado, mensagens, nos)
#   (a camera tem o app proprio: Camera.be)
#   Sistema   bip, aviso na barra, tela acesa, periodo
#
# Teclado fisico: as setas esquerda/direita trocam de aba (fora da aba Teclado).

import string
import math

var ABAS = ["Desenho", "Sensores", "Teclado", "Dedo", "Cartao", "Rede", "Sistema"]
# O periodo de cada aba: animacao a cada varredura, relogio a cada segundo,
# ou 0 (so redesenha quando algo muda - toque, tecla).
var PERIODOS = [16, 16, 500, 0, 0, 1000, 1000]   # Teclado: 500 pisca o cursor
var ABA_A = 70          # altura da barra de abas
var M = 24              # margem
var aba = 0

# Listas fixas ficam AQUI, criadas uma vez: uma lista criada dentro do
# desenhar() nasce de novo a cada quadro (~35 us + ~4 us por item).
var CORES = [tela.PRETO, tela.BRANCO, tela.VERMELHO, tela.VERDE, tela.AZUL, tela.AMARELO,
             tela.CIANO, tela.LARANJA, tela.CINZA, tela.CINZA_CLARO, tela.ROXO, tela.ROSA]
var PALETA = [tela.BRANCO, tela.AMARELO, tela.CIANO, tela.VERDE, tela.ROSA, tela.LARANJA]

# ---- estado de cada aba ----------------------------------------------------
# Desenho: a bola que quica, andando por tempo (px/s), nao por quadro.
var bx = 100.0
var by = 100.0
var vx = 420.0
var vy = 300.0

# Teclado
var campo = ""
var linhas = []

# Dedo: o desenho mora numa CAMADA (uma imagem guardada). O dedo() pinta nela
# e anota o pedaco novo; o quadro so desenha os pedacos novos na tela
# (sistema.acumular) - nunca o desenho inteiro de novo.
var DEDO_Y = 70           # onde a tela de pintura comeca, abaixo do cabecalho da aba
var pintura = nil         # a camada (criada na primeira vez que a aba abre)
var novos = []            # pedacos pintados desde o ultimo quadro: [x0, y0, x1, y1, cor]
var ultimo = nil          # o ultimo ponto do traco em curso ([x, y]), nil = dedo fora
var pontos = 0
var cor_dedo = 0
var cabecalho_sujo = false   # a cor ou a contagem mudou: refaz so o cabecalho

# Cartao
var aberturas = 0
var notas = ""
var arquivos = []

# Rede
var enviadas = 0

# Sistema
var com_aviso = false
var acesa = false

# ---- ajudantes ---------------------------------------------------------------
# Uma funcao so pode ser chamada por codigo que vem DEPOIS dela no arquivo.
def f2(v) return string.format("%.2f", v) end
def f1(v) return string.format("%.1f", v) end

# Tres botoes lado a lado na largura que houver.
def largura_botao(l) return (l - 2 * M - 40) / 3 end

def ler_cartao()
  var t = cartao.ler("notas.txt")
  notas = t != nil ? t : ""
  arquivos = cartao.lista()
end

def mudar_aba(i)
  if i < 0 i = size(ABAS) - 1 end
  if i >= size(ABAS) i = 0 end
  aba = i
  sistema.periodo(PERIODOS[i])
  if aba != 2 teclado.fechar() end
  if aba == 4 ler_cartao() end
  # So a aba Dedo acumula: as outras redesenham tudo a cada quadro.
  sistema.acumular(aba == 3)
  if aba == 3 && pintura == nil
    pintura = tela.camada(tela.largura(), tela.altura() - ABA_A - DEDO_Y)
  end
end

# Apaga o ultimo caractere inteiro (UTF-8: os bytes 10xxxxxx sao continuacao).
def sem_ultimo(s)
  var i = size(s) - 1
  while i > 0 && (string.byte(s[i]) & 0xC0) == 0x80 i -= 1 end
  if i <= 0 return "" end
  return s[0 .. i - 1]
end

# ---- abrir: o que roda uma vez -------------------------------------------------
def abrir()
  # Persistencia: quantas vezes o app abriu, guardado no cartao.
  var t = cartao.ler("aberturas.txt")
  aberturas = (t != nil ? int(t) : 0) + 1
  cartao.gravar("aberturas.txt", str(aberturas))
  mudar_aba(0)
end

# ---- desenho de cada aba -------------------------------------------------------
def desenhar_abas(l)
  var w = l / size(ABAS)
  for i: 0 .. size(ABAS) - 1
    var cor = i == aba ? tela.cor(40, 90, 160) : tela.cor(30, 30, 30)
    tela.arredondado(i * w + 4, 6, w - 8, ABA_A - 12, 12, cor)
    tela.texto(ABAS[i], i * w + w / 2, 20, 2.5, i == aba ? tela.BRANCO : tela.CINZA_CLARO, tela.CENTRO)
  end
end

def aba_desenho(l, a)
  var y = ABA_A + M
  tela.texto("Formas", M, y, 3, tela.AMARELO)
  y += 50
  # 9 casas na largura que houver (em pe ou deitado).
  var c = (l - 2 * M) / 9
  var s = c - 16
  if s > 110 s = 110 end
  tela.retangulo(M, y, s, s, tela.VERMELHO)
  tela.retangulo(M + c, y, s, s, tela.VERMELHO, false)
  tela.arredondado(M + 2 * c, y, s, s, s / 5, tela.VERDE)
  tela.arredondado(M + 3 * c, y, s, s, s / 5, tela.VERDE, false)
  tela.circulo(M + 4 * c + s / 2, y + s / 2, s / 2, tela.AZUL)
  tela.circulo(M + 5 * c + s / 2, y + s / 2, s / 2, tela.AZUL, false)
  tela.triangulo(M + 6 * c, y + s, M + 6 * c + s / 2, y, M + 6 * c + s, y + s, tela.LARANJA)
  tela.triangulo(M + 7 * c, y + s, M + 7 * c + s / 2, y, M + 7 * c + s, y + s, tela.LARANJA, false)
  for i: 0 .. 60
    tela.pixel(M + 8 * c + (i * 7) % s, y + (i * 13) % s, tela.BRANCO)
  end
  y += s + 30

  # Paleta: as cores prontas e uma feita com tela.cor(r, g, b).
  var q = (l - 2 * M) / size(CORES)
  for i: 0 .. size(CORES) - 1
    tela.retangulo(M + i * q, y, q - 8, 40, CORES[i])
    tela.retangulo(M + i * q, y, q - 8, 40, tela.CINZA, false)
  end
  y += 50
  q = (l - 2 * M) / 32
  for i: 0 .. 31
    tela.retangulo(M + i * q, y, q, 30, tela.cor(i * 8, 255 - i * 8, 128))
  end
  y += 50

  # Alinhamento: o mesmo x, tres jeitos. tela.medir diz a largura.
  var cx = l / 2
  tela.linha(cx, y, cx, y + 130, tela.CINZA)
  tela.texto("esquerda", cx, y, 3, tela.CIANO, tela.ESQUERDA)
  tela.texto("centro", cx, y + 45, 3, tela.CIANO, tela.CENTRO)
  tela.texto("direita", cx, y + 90, 3, tela.CIANO, tela.DIREITA)
  var msg = "medir: " + str(tela.medir("centro", 3)) + " px"
  tela.texto(msg, M, y + 45, 2, tela.CINZA_CLARO)
  y += 150

  # Animacao por tempo: dt e o tempo real entre quadros na tela.
  var caixa_a = a - y - M
  if caixa_a < 80 return end
  tela.retangulo(M, y, l - 2 * M, caixa_a, tela.CINZA, false)
  var dt = sistema.dt()
  if dt > 0.1 dt = 0.1 end       # depois de uma pausa, nao pula
  bx += vx * dt
  by += vy * dt
  var r = 24
  if bx < r            bx = r;            vx = -vx end
  if bx > l - 2*M - r  bx = l - 2*M - r;  vx = -vx end
  if by < r            by = r;            vy = -vy end
  if by > caixa_a - r  by = caixa_a - r;  vy = -vy end
  tela.circulo(M + int(bx), y + int(by), r, tela.AMARELO)
  tela.texto("dt " + f1(dt * 1000) + " ms", M + 12, y + 8, 2, tela.CINZA_CLARO)
end

def barra(rotulo, v, maximo, x, y, w, cor)
  tela.texto(rotulo, x, y, 2.5)
  var meio = x + 90 + (w - 90) / 2
  tela.retangulo(x + 90, y, w - 90, 28, tela.cor(30, 30, 30))
  var p = v / maximo
  if p > 1 p = 1 end
  if p < -1 p = -1 end
  var n = int(p * (w - 90) / 2)
  if n >= 0 tela.retangulo(meio, y, n, 28, cor)
  else tela.retangulo(meio + n, y, -n, 28, cor) end
  tela.linha(meio, y, meio, y + 28, tela.BRANCO)
end

def aba_sensores(l, a)
  var y = ABA_A + M
  var g = sistema.imu()
  var gi = sistema.giro()
  var w = l / 2 - 2 * M
  tela.texto("Aceleracao (g)", M, y, 3, tela.AMARELO)
  tela.texto("Giro (graus/s)", l / 2 + M, y, 3, tela.AMARELO)
  if g == nil
    tela.texto("IMU sem leitura", M, y + 60, 3, tela.VERMELHO)
  else
    barra("x " + f2(g[0]), g[0], 1.5, M, y + 60, w, tela.VERMELHO)
    barra("y " + f2(g[1]), g[1], 1.5, M, y + 100, w, tela.VERDE)
    barra("z " + f2(g[2]), g[2], 1.5, M, y + 140, w, tela.AZUL)
    barra("x " + str(int(gi[0])), gi[0], 250, l / 2 + M, y + 60, w, tela.VERMELHO)
    barra("y " + str(int(gi[1])), gi[1], 250, l / 2 + M, y + 100, w, tela.VERDE)
    barra("z " + str(int(gi[2])), gi[2], 250, l / 2 + M, y + 140, w, tela.AZUL)
    # A bolha de nivel.
    var cx = l / 2
    var cy = y + 330
    tela.circulo(cx, cy, 120, tela.CINZA, false)
    tela.circulo(cx, cy, 40, tela.CINZA, false)
    tela.circulo(cx + int(g[0] * -110), cy + int(g[1] * 110), 22, tela.VERDE)
  end

  var e = sistema.energia()
  var yb = y + 220
  tela.texto("Bateria", M, yb, 3, tela.AMARELO)
  tela.texto((e["pct"] >= 0 ? str(e["pct"]) + "%  " : "") + f2(e["volts"]) + " V", M, yb + 45, 3)
  tela.texto((e["ma"] > 0 ? "carregando " : "") + str(int(e["ma"])) + " mA", M, yb + 85, 3)

  tela.texto("Relogio", M, yb + 150, 3, tela.AMARELO)
  var h = sistema.hora()
  tela.texto(h != "" ? sistema.data() + "  " + h : "sem acerto", M, yb + 195, 3)

  var xg = l - M - 380
  tela.texto("GPS", xg, yb, 3, tela.AMARELO)
  var p = sistema.gps()
  if p == nil
    tela.texto("sem GPS no Grove", xg, yb + 45, 2.5, tela.CINZA_CLARO)
  elif !p["fix"]
    tela.texto("sem fix (" + str(p["sats"]) + " sat)", xg, yb + 45, 2.5)
  else
    tela.texto(string.format("%.5f", p["lat"]), xg, yb + 45, 2.5)
    tela.texto(string.format("%.5f", p["lon"]), xg, yb + 80, 2.5)
    tela.texto(str(int(p["alt"])) + " m, " + str(p["sats"]) + " sat", xg, yb + 115, 2.5)
  end
end

def aba_teclado(l, a)
  # Com o teclado aberto, tela.altura() ja desconta ele: 'a' e o que sobra.
  var y = ABA_A + M
  var fis = teclado.fisico()
  tela.texto(fis ? "Teclado fisico encaixado - e so digitar" : "Sem teclado fisico: use o virtual",
             M, y, 2.5, tela.CINZA_CLARO)
  tela.botao(l - M - 300, y - 10, 300, 64, teclado.aberto() ? "Fechar teclado" : "Abrir teclado",
             tela.CIANO)
  y += 70
  tela.arredondado(M, y, l - 2 * M, 70, 12, tela.cor(25, 25, 40))
  var cursor = (sistema.ms() / 500) % 2 == 0 ? "|" : ""
  tela.texto(campo + cursor, M + 16, y + 18, 3.5)
  y += 90
  tela.texto("Enter manda a linha para a lista:", M, y, 2.5, tela.AMARELO)
  y += 40
  var cabe = (a - y) / 40
  var ini = size(linhas) > cabe ? size(linhas) - cabe : 0
  for i: ini .. size(linhas) - 1
    tela.texto(str(i + 1) + ". " + linhas[i], M, y, 3)
    y += 40
  end
end

def cabecalho_dedo(l)
  var y0 = ABA_A
  tela.retangulo(0, y0, l, DEDO_Y, tela.PRETO)   # acumulando, o que estava ali continua: apaga
  tela.texto("Desenhe com o dedo", M, y0 + 12, 2.5, tela.CINZA_CLARO)
  tela.botao(l - M - 200, y0 + 6, 200, 56, "Limpar", tela.VERMELHO)
  tela.botao(l - M - 420, y0 + 6, 200, 56, "Cor", tela.cor(80, 80, 80))
  tela.circulo(l - M - 470, y0 + 34, 22, PALETA[cor_dedo % size(PALETA)])
  tela.texto(str(pontos) + " pontos", M + 400, y0 + 12, 2.5, tela.CINZA)
end

# Um pedaco de traco, grosso (3 linhas): na camada e na tela, o mesmo desenho.
def pedaco(s, dy)
  tela.linha(s[0], s[1] + dy, s[2], s[3] + dy, s[4])
  tela.linha(s[0] + 1, s[1] + dy, s[2] + 1, s[3] + dy, s[4])
  tela.linha(s[0], s[1] + dy + 1, s[2], s[3] + dy + 1, s[4])
end

# tudo = true: a area chegou limpa - cabecalho e a pintura inteira (colada da
# camada). tudo = false: so o que mudou desde o ultimo quadro.
def aba_dedo(l, a, tudo)
  var topo = ABA_A + DEDO_Y
  if tudo || cabecalho_sujo
    cabecalho_dedo(l)
    cabecalho_sujo = false
  end
  if tudo
    tela.colar(pintura, 0, topo)
  else
    for s: novos pedaco(s, topo) end
  end
  novos = []
end

def aba_cartao(l, a)
  var y = ABA_A + M
  tela.texto("Este app abriu " + str(aberturas) + " vez(es) - contado em aberturas.txt", M, y, 2.5)
  y += 50
  var bw = largura_botao(l)
  tela.botao(M, y, bw, 70, "Anexar linha", tela.VERDE)
  tela.botao(M + bw + 20, y, bw, 70, "Apagar notas", tela.VERMELHO)
  tela.botao(M + 2 * (bw + 20), y, bw, 70, "Reler", tela.CIANO)
  y += 100
  tela.texto("Arquivos em apps/Mostruario/:", M, y, 2.5, tela.AMARELO)
  var nomes = ""
  for n: arquivos nomes += n + "   " end
  tela.texto(nomes != "" ? nomes : "(nenhum)", M, y + 40, 2.5)
  y += 100
  tela.texto("notas.txt:", M, y, 2.5, tela.AMARELO)
  y += 40
  var ls = string.split(notas, "\n")
  var cabe = (a - y) / 36
  var mostradas = []
  for s: ls if s != "" mostradas.push(s) end end
  var ini = size(mostradas) > cabe ? size(mostradas) - cabe : 0
  for i: ini .. size(mostradas) - 1
    tela.texto(mostradas[i], M, y, 2.5)
    y += 36
  end
end

def aba_rede(l, a)
  var y = ABA_A + M
  var r = sistema.rede()
  tela.texto("Wi-Fi", M, y, 3, tela.AMARELO)
  if r["ip"] != ""
    tela.texto(r["ssid"] + "   " + r["ip"] + "   " + str(r["rssi"]) + " dBm", M, y + 45, 2.5)
  else
    tela.texto(r["texto"], M, y + 45, 2.5)
  end
  y += 110

  tela.texto("ESP-NOW", M, y, 3, tela.AMARELO)
  if espnow.pronto()
    tela.texto("canal " + str(espnow.canal()) + ", " + str(espnow.total()) + " recebida(s)",
               M + 180, y + 4, 2.5)
    tela.botao(l - M - 300, y - 10, 300, 64, "Enviar ola", tela.VERDE)
    var n = espnow.n()
    var yy = y + 60
    var ini = n > 5 ? n - 5 : 0
    for i: ini .. n - 1
      var m = espnow.msg(i)
      tela.texto((m["minha"] ? "eu: " : m["de"] + ": ") + m["texto"], M, yy, 2.5,
                 m["minha"] ? tela.CIANO : tela.BRANCO)
      yy += 36
    end
    if n == 0 tela.texto("nada recebido ainda", M, yy, 2.5, tela.CINZA_CLARO) end
  else
    tela.texto("fora do ar (Wi-Fi desligado ou radio C6 de fabrica)", M + 180, y + 4, 2.5,
               tela.CINZA_CLARO)
  end
  y += 260

  tela.texto("Mesh", M, y, 3, tela.AMARELO)
  var h = mesh.estado()
  if h["ativo"]
    tela.texto((h["no_ar"] ? "no ar" : "mudo") + ", " + str(h["nos"]) + " no(s), " +
               str(h["nao_lidas"]) + " nao lida(s)", M + 120, y + 4, 2.5)
    # As mensagens recentes (0 = a mais nova) e o no mais proximo: so leitura.
    var yy = y + 50
    for i: 0 .. 1
      var m = mesh.msg(i)
      if m == nil break end
      tela.texto(sistema.hora_de(m["quando"]) + "  " + (m["nome"] != "" ? m["nome"] : "?") + ": " + m["texto"],
                 M, yy, 2.5, m["direta"] ? tela.CIANO : tela.BRANCO)
      yy += 36
    end
    var no = mesh.no(1)               # 0 e o nosso; 1 o ouvido mais recente
    if no != nil
      tela.texto("ultimo ouvido: " + no["nome"] + " (" + (no["mqtt"] ? "internet" : str(no["saltos"]) + " salto(s)") + ")",
                 M, yy, 2.5, tela.CINZA_CLARO)
    end
  else
    tela.texto("servico desligado (Ajustes > Mesh)", M + 120, y + 4, 2.5, tela.CINZA_CLARO)
  end
end

def aba_sistema(l, a)
  var y = ABA_A + M
  var bw = largura_botao(l)
  tela.botao(M, y, bw, 80, "Bip", tela.AMARELO)
  tela.botao(M + bw + 20, y, bw, 80, com_aviso ? "Tirar aviso" : "Aviso na barra", tela.LARANJA)
  tela.botao(M + 2 * (bw + 20), y, bw, 80, acesa ? "Deixar apagar" : "Tela acesa", tela.CIANO)
  y += 120
  tela.texto("ligado ha " + str(sistema.ms() / 1000) + " s", M, y, 3)
  tela.texto("periodo desta aba: " + str(sistema.periodo()) + " ms", M, y + 45, 3)
  tela.texto("area livre: " + str(l) + " x " + str(a) + " px", M, y + 90, 3)
  tela.texto("tela " + (acesa ? "acesa enquanto o app estiver aberto" : "apaga sozinha (padrao)"),
             M, y + 135, 3)
  tela.texto("teclado fisico: " + (teclado.fisico() ? "sim" : "nao"), M, y + 180, 3)
end

def desenhar(tudo)
  var l = tela.largura()
  var a = tela.altura()
  # As abas so mudam quando a area foi limpa (tudo); fora da aba Dedo o app
  # nao acumula, entao tudo e sempre true.
  if tudo desenhar_abas(l) end
  if   aba == 0 aba_desenho(l, a)
  elif aba == 1 aba_sensores(l, a)
  elif aba == 2 aba_teclado(l, a)
  elif aba == 3 aba_dedo(l, a, tudo)
  elif aba == 4 aba_cartao(l, a)
  elif aba == 5 aba_rede(l, a)
  else          aba_sistema(l, a)
  end
end

# ---- toque -------------------------------------------------------------------
def tocar(x, y)
  var l = tela.largura()
  if y < ABA_A
    mudar_aba(x / (l / size(ABAS)))
    return
  end
  var y0 = ABA_A + M
  if aba == 2
    if tela.dentro(x, y, l - M - 300, y0 - 10, 300, 64)
      if teclado.aberto() teclado.fechar() else teclado.abrir() end
    end
  elif aba == 3
    if tela.dentro(x, y, l - M - 200, ABA_A + 6, 200, 56)
      tela.alvo(pintura)
      tela.limpar()
      tela.alvo()
      novos = []
      pontos = 0
      sistema.acumular(false)   # a pintura mudou inteira: um quadro limpo (tudo = true)
      sistema.acumular(true)
    elif tela.dentro(x, y, l - M - 420, ABA_A + 6, 200, 56)
      cor_dedo += 1
      cabecalho_sujo = true
    end
  elif aba == 4
    var yb = y0 + 50
    var bw = largura_botao(l)
    if tela.dentro(x, y, M, yb, bw, 70)
      var h = sistema.hora()
      cartao.anexar("notas.txt", (h != "" ? h : str(sistema.ms())) + " - toque em anexar\n")
      ler_cartao()
    elif tela.dentro(x, y, M + bw + 20, yb, bw, 70)
      cartao.gravar("notas.txt", "")
      ler_cartao()
    elif tela.dentro(x, y, M + 2 * (bw + 20), yb, bw, 70)
      ler_cartao()
    end
  elif aba == 5
    if espnow.pronto() && tela.dentro(x, y, l - M - 300, y0 + 100, 300, 64)
      enviadas += 1
      espnow.enviar("ola do Berry #" + str(enviadas))
    end
  elif aba == 6
    var bw = largura_botao(l)
    if tela.dentro(x, y, M, y0, bw, 80)
      sistema.bip(880, 120)
    elif tela.dentro(x, y, M + bw + 20, y0, bw, 80)
      com_aviso = !com_aviso
      if com_aviso sistema.avisar("aviso do Mostruario") else sistema.avisar() end
    elif tela.dentro(x, y, M + 2 * (bw + 20), y0, bw, 80)
      acesa = !acesa
      sistema.acordado(acesa)
    end
  end
end

# O dedo por nivel: chamado enquanto o dedo esta na tela (e uma vez ao sair,
# com pressionado = false). So a aba Dedo usa.
def dedo(x, y, pressionado)
  if aba != 3 return end
  var topo = ABA_A + DEDO_Y
  if !pressionado || y < topo
    ultimo = nil
    return
  end
  var p = [x, y - topo]               # na coordenada da camada
  var de = ultimo != nil ? ultimo : p
  var s = [de[0], de[1], p[0], p[1], PALETA[cor_dedo % size(PALETA)]]
  # Pinta na camada (fica guardado) e anota para o proximo quadro.
  tela.alvo(pintura)
  pedaco(s, 0)
  tela.alvo()
  novos.push(s)
  ultimo = p
  pontos += 1
  if pontos % 20 == 0 cabecalho_sujo = true end   # a contagem, sem refazer a cada ponto
end

# ---- teclas: o teclado virtual e o fisico chegam aqui igual ------------------
def tecla(texto, codigo)
  if aba == 2
    if texto != ""
      campo += texto
    elif codigo == teclado.APAGAR
      campo = sem_ultimo(campo)
    elif codigo == teclado.ENTER
      if campo != "" linhas.push(campo) end
      campo = ""
    elif codigo == teclado.ESC
      teclado.fechar()
    end
    return
  end
  if codigo == teclado.ESQUERDA mudar_aba(aba - 1)
  elif codigo == teclado.DIREITA mudar_aba(aba + 1)
  end
end

def fechar()
  sistema.avisar()
end
