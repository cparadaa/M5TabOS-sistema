# nome: Tocador
# cor: 0x2E5E
# periodo: 500
#
# UM PLAYER DE MUSICA EM BERRY - so a interface. Quem toca e o tocador do
# sistema (modulo 'musica'): ele acha a pasta Music do cartao, toca com o app
# fechado, mostra na barra e no miniplayer da central, tem o X para parar e
# calcula o espectro. O script so desenha e chama - nenhuma linha de audio.
#
# Copie para /sdcard/apps/Berry/ e reinicie. Musicas: pasta Music (ou
# Música) na raiz do cartao, MP3 ou WAV, com subpastas.

import string

var CIANO = tela.cor(40, 200, 240)
var FUNDO = tela.cor(28, 32, 36)
var sub = ""           # a subpasta aberta ("" = a raiz da pasta de musica)
var itens = []         # musica.lista(sub)
var topo = 0           # rolagem da lista, em linhas
var arrasto = nil      # [y do dedo, topo] durante o arraste
var picos = []         # o pico de cada barra do espectro (cai devagar)
var LINHA = 76

def ler_pasta()
  var l = musica.lista(sub)
  if l == nil && sub != "" sub = "" l = musica.lista(sub) end
  itens = l != nil ? l : []
  topo = 0
end

def tempo(ms)
  var s = ms / 1000
  return str(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + str(s % 60)
end

# ---- a geometria (tudo relativo ao tamanho da area: em pe ou deitado) --------
def g_espectro() return [20, 16, tela.largura() - 40, 170] end
def g_titulo_y() return 206 end
def g_barra() return [40, 290, tela.largura() - 80] end
def g_botoes_y() return 380 end
def g_lista_y() return 460 end

def desenhar_espectro(e)
  var r = g_espectro()
  tela.arredondado(r[0], r[1], r[2], r[3], 18, FUNDO)
  var f = musica.espectro()
  var n = musica.FAIXAS
  if size(picos) != n picos = [] for i: 0 .. n - 1 picos.push(0) end end
  var passo = (r[2] - 20) / n
  for i: 0 .. n - 1
    var v = f != nil ? f[i] : 0
    var h = v * (r[3] - 20) / 255
    if h > picos[i] picos[i] = h else picos[i] = picos[i] > 4 ? picos[i] - 4 : 0 end
    var x = r[0] + 10 + i * passo
    var base = r[1] + r[3] - 10
    # cor por barra: do ciano (graves) ao rosa (agudos)
    var c = tela.cor(40 + i * 7, 200 - i * 4, 240 - i * 2)
    if h > 0 tela.retangulo(x, base - h, passo - 4, h, c) end
    if picos[i] > 2 tela.retangulo(x, base - picos[i] - 3, passo - 4, 3, tela.BRANCO) end
  end
end

def desenhar(tudo)
  var e = musica.estado()
  desenhar_espectro(e)
  var L = tela.largura()

  # o que toca
  # tela.cortar: o sistema corta o que nao cabe, com "..." (nome longo de musica)
  var t = e["ativo"] ? e["titulo"] : "Nada tocando"
  tela.texto(tela.cortar(t, L - 60, 3.4), L / 2, g_titulo_y(), 3.4, tela.BRANCO, tela.CENTRO)
  var sub_txt = e["ativo"] ? e["artista"] + (e["tocando"] ? "" : "  (pausado)") : "Toque numa musica da lista"
  tela.texto(tela.cortar(sub_txt, L - 60, 2.6), L / 2, g_titulo_y() + 44, 2.6, tela.CINZA_CLARO, tela.CENTRO)

  # a linha de progresso (tocar nela busca)
  var b = g_barra()
  tela.arredondado(b[0], b[1] - 4, b[2], 8, 4, tela.CINZA)
  if e["ativo"] && e["dur"] > 0
    var feito = b[2] * e["pos"] / e["dur"]
    tela.arredondado(b[0], b[1] - 4, feito, 8, 4, CIANO)
    tela.circulo(b[0] + feito, b[1], 10, tela.BRANCO)
    tela.texto(tempo(e["pos"]), b[0], b[1] + 16, 2.4, tela.CINZA_CLARO)
    tela.texto(tempo(e["dur"]), b[0] + b[2], b[1] + 16, 2.4, tela.CINZA_CLARO, tela.DIREITA)
  end

  # anterior, tocar/pausar, proxima; volume nos cantos
  var y = g_botoes_y()
  tela.circulo(L / 2, y, 40, CIANO)
  if e["tocando"]
    tela.retangulo(L / 2 - 13, y - 16, 9, 32, tela.PRETO)
    tela.retangulo(L / 2 + 4, y - 16, 9, 32, tela.PRETO)
  else
    tela.triangulo(L / 2 - 10, y - 18, L / 2 - 10, y + 18, L / 2 + 20, y, tela.PRETO)
  end
  tela.triangulo(L / 2 - 110, y, L / 2 - 80, y - 16, L / 2 - 80, y + 16, tela.BRANCO)
  tela.triangulo(L / 2 + 110, y, L / 2 + 80, y - 16, L / 2 + 80, y + 16, tela.BRANCO)
  tela.texto("-", 40, y - 20, 4, tela.BRANCO)
  tela.texto("+", L - 40, y - 20, 4, tela.BRANCO, tela.DIREITA)
  tela.texto(str(musica.volume()) + "%", L - 90, y - 12, 2.4, tela.CINZA_CLARO, tela.DIREITA)

  # a lista (pastas primeiro); "<" volta uma pasta
  var ly = g_lista_y()
  var cab = musica.pasta()
  cab = (cab != nil ? cab : "sem cartao") + (sub != "" ? " / " + sub : "")
  tela.texto((sub != "" ? "<  " : "") + cab, 30, ly, 3, sub != "" ? CIANO : tela.BRANCO)
  var cabem = (tela.altura() - ly - 50) / LINHA
  if cabem < 1 cabem = 1 end
  for k: 0 .. cabem - 1
    var i = topo + k
    if i >= size(itens) break end
    var it = itens[i]
    var yy = ly + 50 + k * LINHA
    var toca = e["ativo"] && e["pasta"] == sub && !it["pasta"] && e["titulo"] != "" && it["titulo"] == e["titulo"]
    if toca tela.arredondado(20, yy, L - 40, LINHA - 8, 14, FUNDO) end
    var cor = it["pasta"] ? tela.AMARELO : (toca ? CIANO : tela.BRANCO)
    tela.texto(tela.cortar((it["pasta"] ? "[ ] " : "") + it["titulo"], L - 80, 2.8), 40, yy + 20, 2.8, cor)
  end
  if size(itens) == 0
    tela.texto("Copie MP3 ou WAV para a pasta Music do cartao", L / 2, ly + 90, 2.6, tela.CINZA_CLARO, tela.CENTRO)
  end

  # tocando: ~30 quadros/s para o espectro; parado: devagar
  sistema.periodo(e["tocando"] ? 33 : 500)
end

def tocar(x, y)
  var L = tela.largura()
  var e = musica.estado()
  var b = g_barra()
  var by = g_botoes_y()
  if y >= b[1] - 30 && y <= b[1] + 30 && e["ativo"] && e["dur"] > 0
    var dx = x - b[0]
    if dx < 0 dx = 0 end
    if dx > b[2] dx = b[2] end
    musica.ir_para(e["dur"] * dx / b[2])
  elif y >= by - 60 && y <= by + 60
    if x >= L / 2 - 60 && x <= L / 2 + 60
      if e["ativo"] musica.pausar()
      else
        for i: 0 .. size(itens) - 1 if !itens[i]["pasta"] musica.tocar(sub, i) break end end
      end
    elif x >= L / 2 - 150 && x < L / 2 - 60 musica.anterior()
    elif x > L / 2 + 60 && x <= L / 2 + 150 musica.proxima()
    elif x < 100 musica.volume(musica.volume() - 10)
    elif x > L - 100 musica.volume(musica.volume() + 10)
    end
  elif y >= g_lista_y() && y < g_lista_y() + 50
    if sub != ""   # "<": volta uma pasta
      var p = string.split(sub, "/")
      sub = ""
      for k: 0 .. size(p) - 2 sub += (k > 0 ? "/" : "") + p[k] end
      ler_pasta()
    end
  elif y >= g_lista_y() + 50
    var i = topo + (y - g_lista_y() - 50) / LINHA
    if i >= 0 && i < size(itens)
      if itens[i]["pasta"]
        sub = (sub != "" ? sub + "/" : "") + itens[i]["nome"]
        ler_pasta()
      else
        musica.tocar(sub, i)
      end
    end
  end
end

# Arrastar a lista rola.
def dedo(x, y, pressionado)
  if !pressionado arrasto = nil return end
  if y < g_lista_y() return end
  if arrasto == nil arrasto = [y, topo] return end
  var t = arrasto[1] - (y - arrasto[0]) / LINHA
  var max = size(itens) - 3
  if max < 0 max = 0 end
  topo = t < 0 ? 0 : (t > max ? max : t)
end

ler_pasta()
musica.cor(CIANO)   # a pastilha da barra e o miniplayer na cor deste player
