# nome: Azimutal
# cor: 0x5D1F
# periodo: 1000
#
# AZIMUTAL EM BERRY - o ceu dos satelites (skyplot) lido do GPS no Grove, e a
# gravacao da constelacao COM O APP FECHADO. O mesmo app do sistema em C,
# refeito em script: aba Ceu (o mapa polar ao vivo, com o rastro de cada
# satelite), aba Lista (cada um com o sinal) e aba Dias (os arquivos gravados).
#
# GRAVAR liga a parte de fundo: uma vez por segundo ela le a constelacao (a
# fila do GPS precisa ser esvaziada - ver gnss em docs/BERRY.md) e a cada
# minuto grava no cartao, em /sdcard/apps/Berry/Azimutal/satelites-AAAA-MM-DD.csv
# (o dia UTC do proprio GPS), o mesmo formato do Azimutal em C:
#   POS,hh:mm:ss,lat,lon,alt,hdop,qualidade,sats    onde o receptor estava
#   SAT,hh:mm:ss,sistema,prn,elevacao,azimute,snr   um por satelite
# So grava com fix (sem fix a geometria do receptor e de almanaque, falsa) e
# so satelite com posicao FRESCA (o parser segura a ultima quando a GSV chega
# sem posicao - gravar isso repetiria o mesmo ponto como se fosse novo).
#
# MEMORIA: o rastro guarda no maximo RASTRO_MAX pontos por satelite (um por
# minuto: 3 h), empacotados num inteiro cada - ~60 KB no pior caso.

import math
import string

var BARRA_A = 96
var M = 20
var DESPEJO_MS = 60000   # uma amostra por minuto: um satelite leva horas para cruzar o ceu
var RASTRO_MAX = 180
var FRESCO_S = 90        # posicao mais velha que isto nao e observacao nova
var CORES = {"GPS": 0x07E0, "GLONASS": 0xF8A0, "GALILEO": 0x5D1F, "BEIDOU": 0xFFE0,
             "QZSS": 0xF81F, "SBAS": 0xAD55}

var sats = nil           # a ultima rodada (lista de mapas de gnss.satelites)
var rastros = {}         # "GPS-12" -> [elev * 1000 + azim, ...] (um por minuto)
var ultimo_ms = 0        # quando foi a ultima amostra de minuto
var gravadas = 0         # linhas SAT gravadas nesta gravacao
var arquivo = ""
var aviso_gravacao = ""  # por que a ultima amostra nao gravou ("" = gravou)
var dias = nil           # os arquivos da aba Dias (lidos ao entrar nela)
var aba = 0              # 0 = Ceu, 1 = Lista, 2 = Dias
var bt_abas = []
var bt_gravar = [0, 0, 0, 0]

# ---- a constelacao (janela e fundo) --------------------------------------------
# Ordena por insercao (a lista do Berry nao tem sort): 'antes(a, b)' diz se a
# vem antes de b. Listas pequenas - dezenas de itens.
def ordenar(l, antes)
  var i = 1
  while i < size(l)
    var j = i
    while j > 0 && antes(l[j], l[j - 1])
      var tmp = l[j] l[j] = l[j - 1] l[j - 1] = tmp
      j -= 1
    end
    i += 1
  end
end

def dois(n) return n < 10 ? "0" + str(n) : str(n) end

def nome_do_dia(t)
  return "satelites-" + str(t["ano"]) + "-" + dois(t["mes"]) + "-" + dois(t["dia"]) + ".csv"
end

def guardar_rastro(s)
  var k = s["sis"] + "-" + str(s["prn"])
  var r = rastros.find(k)
  if r == nil r = [] rastros[k] = r end
  r.push(s["elev"] * 1000 + s["azim"])
  if size(r) > RASTRO_MAX r.remove(0) end
end

def gravar(t)
  if !t["valida"] aviso_gravacao = "sem hora do GPS ainda" return end
  if !t["fix"] aviso_gravacao = "sem fix: a geometria seria falsa" return end
  arquivo = nome_do_dia(t)
  var hora = dois(t["hora"]) + ":" + dois(t["min"]) + ":" + dois(t["seg"])
  var texto = ""
  var g = sistema.gps()
  if g != nil && g["fix"]
    texto += string.format("POS,%s,%.6f,%.6f,%.1f,,1,%d\n", hora, g["lat"], g["lon"], g["alt"], g["sats"])
  end
  var n = 0
  for s: sats
    if s["pos"] && s["idade"] >= 0 && s["idade"] <= FRESCO_S
      texto += "SAT," + hora + "," + s["sis"] + "," + str(s["prn"]) + "," + str(s["elev"]) + "," +
               str(s["azim"]) + "," + str(s["snr"]) + "\n"
      n += 1
    end
  end
  if n == 0 aviso_gravacao = "nenhum satelite com posicao fresca" return end
  if cartao.anexar(arquivo, texto)
    gravadas += n
    aviso_gravacao = ""
    dias = nil   # a aba Dias rele
  else
    aviso_gravacao = "o cartao recusou a gravacao"
  end
end

# Uma volta: le a constelacao e, a cada minuto, guarda o rastro (e grava, se
# estiver gravando). Chamada pelo fundo() a cada segundo e pelo desenhar().
def volta()
  var l = gnss.satelites()
  if l != nil sats = l end
  if sats == nil return end
  var agora = sistema.ms()
  if ultimo_ms != 0 && agora - ultimo_ms < DESPEJO_MS return end
  ultimo_ms = agora
  for s: sats
    if s["pos"] && s["idade"] >= 0 && s["idade"] <= FRESCO_S guardar_rastro(s) end
  end
  if sistema.fundo() gravar(gnss.tempo()) end
end

# A PARTE DE FUNDO: a cada segundo (a fila do GPS guarda ~2 s).
def fundo()
  if !gnss.ligado() gnss.ligar() end
  volta()
  sistema.fundo_estado(str(gravadas) + " linha(s)", gravadas)
end

def gravando() return sistema.fundo() end

def ligar_desligar()
  if gravando()
    sistema.fundo(false)
  else
    gravadas = 0
    ultimo_ms = 0          # a primeira rodada completa ja grava
    aviso_gravacao = ""
    sistema.fundo(true, 1000)
  end
end

# ---- a janela ------------------------------------------------------------------
def abrir()
  gnss.ligar()
end

def fechar()
  # Gravando, o fundo segue com a assinatura; senao a maquina e apagada e o
  # sistema solta o GPS sozinho.
end

def barra()
  var L = tela.largura()
  tela.retangulo(0, 0, L, BARRA_A, 0x18E3)
  var x = M
  if L >= 1000
    tela.texto("Azimutal", x, 30, 4.2)
    x += tela.medir("Azimutal", 4.2) + 30
  end
  var nomes = ["Céu", "Lista", "Dias"]
  bt_abas = []
  for i: 0 .. 2
    var l = tela.medir(nomes[i], 3) + 40
    tela.arredondado(x, 16, l, 64, 32, i == aba ? 0x2C7F : 0x3186)
    tela.texto(nomes[i], x + 20, 36, 3)
    bt_abas.push([x, 16, l, 64])
    x += l + 12
  end
  var on = gravando()
  var bl = 210
  bt_gravar = [L - M - bl, 12, bl, 72]
  tela.botao(bt_gravar[0], bt_gravar[1], bl, 72, on ? "PARAR" : "GRAVAR", on ? tela.VERMELHO : tela.VERDE)
end

def cor_de(sis) var c = CORES.find(sis) return c != nil ? c : tela.CINZA end

# O ponto do ceu: centro = zenite (90 graus), borda = horizonte, norte em cima.
def ponto(cx, cy, R, elev, azim)
  var r = R * (90 - elev) / 90.0
  var a = azim * math.pi / 180.0
  return [cx + int(r * math.sin(a)), cy - int(r * math.cos(a))]
end

def ceu(cx, cy, R)
  var grade = 0x4208
  tela.circulo(cx, cy, R, grade, false)
  tela.circulo(cx, cy, R * 2 / 3, grade, false)
  tela.circulo(cx, cy, R / 3, grade, false)
  tela.linha(cx - R, cy, cx + R, cy, grade)
  tela.linha(cx, cy - R, cx, cy + R, grade)
  tela.texto("N", cx, cy - R - 34, 3, tela.CINZA_CLARO, tela.CENTRO)
  tela.texto("S", cx, cy + R + 8, 3, tela.CINZA_CLARO, tela.CENTRO)
  tela.texto("L", cx + R + 10, cy - 12, 3, tela.CINZA_CLARO)
  tela.texto("O", cx - R - 30, cy - 12, 3, tela.CINZA_CLARO)
  # Os rastros: por onde cada satelite passou (um ponto por minuto).
  for k: rastros.keys()
    var r = rastros[k]
    var c = cor_de(string.split(k, "-")[0])
    var ant = nil
    for v: r
      var p = ponto(cx, cy, R, v / 1000, v % 1000)
      if ant != nil tela.linha(ant[0], ant[1], p[0], p[1], c) else tela.pixel(p[0], p[1], c) end
      ant = p
    end
  end
  if sats == nil return end
  # Os satelites de agora: cheio = com sinal (snr > 0), vazado = so visto.
  for s: sats
    if s["pos"]
      var p = ponto(cx, cy, R, s["elev"], s["azim"])
      var c = cor_de(s["sis"])
      var raio = s["snr"] > 0 ? 10 + s["snr"] / 8 : 10
      tela.circulo(p[0], p[1], raio, c, s["snr"] > 0)
      tela.texto(str(s["prn"]), p[0] + raio + 4, p[1] - 10, 2.2, tela.BRANCO)
    end
  end
end

def contar()
  var n = 0 var comp = 0 var sinal = 0
  if sats != nil
    for s: sats
      n += 1
      if s["pos"] comp += 1 end
      if s["snr"] > 0 sinal += 1 end
    end
  end
  return [n, comp, sinal]
end

def linha_info(rot, val, x, y, cor)
  tela.texto(rot, x, y, 2.4, tela.CINZA_CLARO)
  tela.texto(val, x + 190, y - 4, 3, cor != nil ? cor : tela.BRANCO)
end

def painel(x, y)
  var g = sistema.gps()
  var t = gnss.tempo()
  if g == nil
    linha_info("GPS", "nenhum no Grove", x, y, tela.CINZA)
  else
    linha_info("Fix", g["fix"] ? string.format("%.5f, %.5f", g["lat"], g["lon"]) : "procurando...", x, y,
               g["fix"] ? tela.VERDE : tela.AMARELO)
  end
  y += 48
  var c = contar()
  linha_info("Satélites", str(c[0]) + " vistos, " + str(c[1]) + " no céu, " + str(c[2]) + " com sinal", x, y)
  y += 48
  linha_info("Hora UTC", t["valida"] ? dois(t["hora"]) + ":" + dois(t["min"]) + ":" + dois(t["seg"]) : "--", x, y)
  y += 48
  if gravando()
    linha_info("Gravando", str(gravadas) + " linha(s), 1 por minuto", x, y, tela.VERMELHO)
    y += 48
    if aviso_gravacao != ""
      linha_info("Agora", aviso_gravacao, x, y, tela.AMARELO)
    else
      linha_info("Arquivo", arquivo, x, y, tela.CINZA_CLARO)
    end
  else
    linha_info("Gravação", "parada - GRAVAR segue com o app fechado", x, y, tela.CINZA)
  end
end

def pag_ceu(y0)
  var L = tela.largura()
  var A = tela.altura()
  if L > A   # deitado: o ceu a esquerda, o painel a direita
    var R = (A - y0 - 90) / 2
    ceu(M + 40 + R, y0 + 45 + R, R)
    painel(2 * R + M + 110, y0 + 40)
  else
    var R = (L - 2 * M - 80) / 2
    ceu(L / 2, y0 + 60 + R, R)
    painel(M, y0 + 2 * R + 130)
  end
end

def pag_lista(y0)
  var L = tela.largura()
  var A = tela.altura()
  if sats == nil
    tela.texto("Nenhuma rodada do GPS ainda.", M, y0 + 40, 3.2)
    return
  end
  # Do sinal mais forte para o mais fraco.
  var l = []
  for s: sats l.push(s) end
  ordenar(l, def (a, b) return a["snr"] > b["snr"] end)
  var y = y0 + 20
  tela.texto("sistema    prn   elev   azim   sinal", M, y, 2.4, tela.CINZA_CLARO)
  y += 44
  var barra_x = M + 560
  for s: l
    if y > A - 50 break end
    var c = cor_de(s["sis"])
    tela.texto(s["sis"], M, y, 2.6, c)
    tela.texto(str(s["prn"]), M + 200, y, 2.6)
    tela.texto(s["pos"] ? str(s["elev"]) : "-", M + 290, y, 2.6)
    tela.texto(s["pos"] ? str(s["azim"]) : "-", M + 390, y, 2.6)
    tela.texto(str(s["snr"]), M + 490, y, 2.6)
    var bl = (L - barra_x - M) * s["snr"] / 60
    if bl > L - barra_x - M bl = L - barra_x - M end
    if bl > 0 tela.retangulo(barra_x, y + 4, bl, 22, c) end
    y += 40
  end
end

def pag_dias(y0)
  if dias == nil
    dias = []
    for n: cartao.lista()
      if string.find(n, "satelites-") == 0 dias.push(n) end
    end
    ordenar(dias, def (a, b) return a < b end)   # o nome ja e a data: AAAA-MM-DD
  end
  var y = y0 + 24
  tela.texto("Arquivos em apps/Berry/Azimutal/ (um por dia, UTC):", M, y, 2.6, tela.CINZA_CLARO)
  y += 56
  if size(dias) == 0
    tela.texto("Nada gravado ainda - toque em GRAVAR.", M, y, 3)
    return
  end
  var A = tela.altura()
  var i = size(dias) - 1
  while i >= 0 && y < A - 50   # o mais novo em cima
    tela.texto(dias[i], M, y, 3)
    y += 48
    i -= 1
  end
end

def desenhar(tudo)
  volta()
  barra()
  if aba == 0 pag_ceu(BARRA_A)
  elif aba == 1 pag_lista(BARRA_A)
  else pag_dias(BARRA_A)
  end
end

def tocar(x, y)
  var b = bt_gravar
  if tela.dentro(x, y, b[0], b[1], b[2], b[3]) ligar_desligar() return end
  for i: 0 .. size(bt_abas) - 1
    b = bt_abas[i]
    if tela.dentro(x, y, b[0], b[1], b[2], b[3])
      aba = i
      if i == 2 dias = nil end   # entrar na aba rele a pasta
    end
  end
end
