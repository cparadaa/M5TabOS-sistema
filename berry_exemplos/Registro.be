# nome: Registro
# cor: 0x2DFF
# periodo: 1000
#
# REGISTRO - bateria, corrente e GPS gravados no cartao, COM O APP FECHADO.
# O formato do Azimutal: a janela mostra ao vivo, GRAVAR liga a parte de
# fundo e ela segue gravando depois que a janela fecha (pastilha na barra de
# status com quantas amostras foram). PARAR desliga. Funciona sem GPS: as
# colunas dele so ficam vazias.
#
# COMO O FUNDO FUNCIONA: sistema.fundo(true, ms) faz o sistema chamar fundo()
# a cada ms, com a janela aberta ou fechada. E a MESMA maquina: as listas que
# fundo() enche, a janela le - sem trava, porque os dois nunca rodam juntos.
# Dentro de fundo() nao ha tela, teclado, camera nem bip: so ler, gravar no
# cartao e avisar. Cada chamada tem 200 ms de prazo.
#
# MEMORIA: as amostras ficam em duas listas de inteiros, no maximo MAX - umas
# 12 KB no aparelho (veja sistema.memoria() na aba Agora). O historico de
# verdade vai para o cartao: /sdcard/apps/Berry/Registro/registro-AAAA-MM-DD.csv,
#   hora,bateria %,volts,mA,lat,lon,alt,satelites
# uma linha por amostra. Sem relogio acertado nao ha dia: fica so na memoria.

import string

var INTERVALO = 10000    # ms entre amostras
var MAX = 720            # amostras na memoria (2 h a cada 10 s); as velhas saem
var BARRA_A = 96
var M = 24
var FUNDO_COR = 0x18E3
var COR_PCT = 0x2DFF
var COR_MA = 0xFD20

var pct = []             # bateria % de cada amostra (-1 = sem estimativa)
var ma = []              # corrente de cada amostra (mA; + = carregando)
var gravadas = 0         # linhas que foram para o cartao nesta gravacao
var arquivo = ""         # o csv de hoje ("" = relogio sem acerto: so na memoria)
var avisou_baixa = false
var aba = 0              # 0 = Agora, 1 = Grafico
var bt_gravar = [0, 0, 0, 0]
var bt_abas = [[0, 0, 0, 0], [0, 0, 0, 0]]

# ---- a amostra (fundo e janela) ----------------------------------------------
def nome_do_dia()
  var d = sistema.data()          # "dd/mm/aaaa" ou "" sem relogio
  if d == "" return "" end
  var p = string.split(d, "/")
  return "registro-" + p[2] + "-" + p[1] + "-" + p[0] + ".csv"
end

def amostrar()
  var e = sistema.energia()
  pct.push(e["pct"])
  ma.push(int(e["ma"]))
  if size(pct) > MAX pct.remove(0) ma.remove(0) end
  return e
end

# A PARTE DE FUNDO: uma amostra por chamada, a cada INTERVALO.
def fundo()
  var e = amostrar()
  if arquivo == "" arquivo = nome_do_dia() end   # o relogio pode ter acertado agora
  if arquivo != ""
    var linha = string.format("%s,%d,%.3f,%d", sistema.hora(), e["pct"], e["volts"], int(e["ma"]))
    var g = sistema.gps()
    if g != nil && g["fix"]
      linha += string.format(",%.6f,%.6f,%.1f,%d", g["lat"], g["lon"], g["alt"], g["sats"])
    else
      linha += ",,,,"
    end
    if cartao.anexar(arquivo, linha + "\n") gravadas += 1 end
  end
  sistema.fundo_estado(str(gravadas) + " amostra(s)", gravadas)
  # Bateria baixa e descarregando: um aviso na barra, uma vez so.
  var p = e["pct"]
  if p >= 0 && p <= 15 && e["ma"] < 0
    if !avisou_baixa
      sistema.avisar("Bateria em " + str(p) + "%")
      avisou_baixa = true
    end
  elif p > 20
    if avisou_baixa sistema.avisar() end
    avisou_baixa = false
  end
end

def gravando() return sistema.fundo() end

def ligar_desligar()
  if gravando()
    sistema.fundo(false)
  else
    gravadas = 0
    arquivo = nome_do_dia()
    sistema.fundo(true, INTERVALO)
  end
end

# ---- a janela ------------------------------------------------------------------
def cor_bat(p)
  if p < 0 return tela.CINZA end
  if p <= 15 return tela.VERMELHO end
  if p <= 40 return tela.AMARELO end
  return tela.VERDE
end

def barra()
  var L = tela.largura()
  tela.retangulo(0, 0, L, BARRA_A, FUNDO_COR)
  var x = M
  if L >= 900   # deitado: cabe o titulo
    tela.texto("Registro", x, 30, 4.2)
    x += tela.medir("Registro", 4.2) + 30
  end
  var nomes = ["Agora", "Gráfico"]
  for i: 0 .. 1
    var l = tela.medir(nomes[i], 3) + 44
    tela.arredondado(x, 16, l, 64, 32, i == aba ? 0x2C7F : 0x3186)
    tela.texto(nomes[i], x + 22, 36, 3)
    bt_abas[i] = [x, 16, l, 64]
    x += l + 14
  end
  var on = gravando()
  var bl = 220
  bt_gravar = [L - M - bl, 12, bl, 72]
  tela.botao(bt_gravar[0], bt_gravar[1], bl, 72, on ? "PARAR" : "GRAVAR", on ? tela.VERMELHO : tela.VERDE)
end

def linha_valor(rotulo, valor, y, cor)
  tela.texto(rotulo, M, y, 2.6, tela.CINZA_CLARO)
  tela.texto(valor, M + 230, y - 6, 3.4, cor != nil ? cor : tela.BRANCO)
end

def pag_agora(y0)
  var e = sistema.energia()
  var p = e["pct"]
  tela.texto(p >= 0 ? str(p) + "%" : "--", M, y0 + 24, 12, cor_bat(p))
  var c = int(e["ma"])
  var y = y0 + 150
  linha_valor("Tensão", string.format("%.2f V", e["volts"]), y)
  y += 56
  linha_valor("Corrente", (c > 0 ? "+" : "") + str(c) + " mA" + (c > 0 ? "  carregando" : ""), y,
              c > 0 ? tela.VERDE : nil)
  y += 56
  var g = sistema.gps()
  if g == nil
    linha_valor("GPS", "sem GPS no Grove", y, tela.CINZA)
  elif !g["fix"]
    linha_valor("GPS", "procurando (" + str(g["sats"]) + " sat)", y, tela.AMARELO)
  else
    linha_valor("GPS", string.format("%.5f, %.5f  (%d sat)", g["lat"], g["lon"], g["sats"]), y)
  end
  y += 80
  # A GRAVACAO
  if gravando()
    var onde = arquivo != "" ? arquivo : "só na memória (relógio sem acerto)"
    linha_valor("Gravando", str(gravadas) + " amostra(s), a cada " + str(INTERVALO / 1000) + " s", y, tela.VERMELHO)
    y += 56
    linha_valor("Arquivo", onde, y, tela.CINZA_CLARO)
    y += 56
    tela.texto("Pode fechar o app: a gravação continua (pastilha na barra).", M, y, 2.4, tela.CINZA)
  else
    linha_valor("Gravação", "parada - toque em GRAVAR", y, tela.CINZA)
  end
  y += 70
  # O QUE ESTE APP CUSTA: a maquina dele e todas as do Berry, contra o teto.
  var m = sistema.memoria()
  linha_valor("Memória", string.format("%.1f KB (Berry: %.0f de %.0f KB)", m["app"] / 1024.0,
              m["berry"] / 1024.0, m["orcamento"] / 1024.0), y, tela.CINZA_CLARO)
end

def pag_grafico(y0)
  var L = tela.largura()
  var A = tela.altura()
  var n = size(pct)
  if n < 2
    tela.texto("Sem amostras ainda.", M, y0 + 40, 3.4)
    tela.texto("Toque em GRAVAR: uma amostra a cada " + str(INTERVALO / 1000) + " s.", M, y0 + 100, 2.6, tela.CINZA_CLARO)
    return
  end
  var gx = M + 70
  var gy = y0 + 30
  var gl = L - gx - M - 70
  var ga = A - gy - 90
  # A grade: 0, 25, 50, 75 e 100 % a esquerda.
  for k: 0 .. 4
    var yy = gy + ga - k * ga / 4
    tela.linha(gx, yy, gx + gl, yy, 0x3186)
    tela.texto(str(k * 25), M, yy - 10, 2.2, COR_PCT)
  end
  # A corrente na escala dela: o maior modulo das amostras, a direita.
  var mmax = 100
  for v: ma if v > mmax mmax = v elif -v > mmax mmax = -v end end
  tela.texto("+" + str(mmax), gx + gl + 8, gy - 10, 2.0, COR_MA)
  tela.texto("-" + str(mmax), gx + gl + 8, gy + ga - 10, 2.0, COR_MA)
  tela.linha(gx, gy + ga / 2, gx + gl, gy + ga / 2, 0x4208)   # 0 mA
  # No maximo um ponto a cada 2 px: amostra demais nao vira mais linha.
  var k = n
  if k > gl / 2 k = gl / 2 end
  var xa = nil var ya = nil var yb = nil
  for j: 0 .. k - 1
    var i = j * (n - 1) / (k - 1)
    var x = gx + j * gl / (k - 1)
    var p = pct[i] < 0 ? 0 : pct[i]
    var y1 = gy + ga - p * ga / 100
    var y2 = gy + ga / 2 - ma[i] * (ga / 2) / mmax
    if xa != nil
      tela.linha(xa, ya, x, y1, COR_PCT)
      tela.linha(xa, yb, x, y2, COR_MA)
    end
    xa = x ya = y1 yb = y2
  end
  var minutos = (n - 1) * INTERVALO / 60000
  tela.texto(str(n) + " amostras, últimos " + str(minutos) + " min", gx, gy + ga + 24, 2.4, tela.CINZA_CLARO)
  tela.texto("bateria %", gx + gl - 300, gy + ga + 24, 2.4, COR_PCT)
  tela.texto("mA", gx + gl - 60, gy + ga + 24, 2.4, COR_MA)
end

def desenhar(tudo)
  barra()
  if aba == 0 pag_agora(BARRA_A) else pag_grafico(BARRA_A) end
end

def tocar(x, y)
  var b = bt_gravar
  if tela.dentro(x, y, b[0], b[1], b[2], b[3])
    ligar_desligar()
    return
  end
  for i: 0 .. 1
    b = bt_abas[i]
    if tela.dentro(x, y, b[0], b[1], b[2], b[3]) aba = i end
  end
end
