# nome: Bench
# cor: 0xFFE0
# periodo: 16
#
# Mede quanto custa cada coisa num app Berry: o proprio Berry (laco, conta,
# texto, lista, chamada ao sistema) e cada desenho da tela. Um teste por
# quadro, cada um repetido N vezes; o resultado e o tempo POR OPERACAO, ja
# sem o custo do laco vazio. A tabela sai na tela e no log serial (para colar).

import string

var N = 200
var testes = []     # [nome, funcao] - a funcao faz UMA operacao
var res = []        # us por operacao, na ordem dos testes
var atual = 0
var vazio = 0.0     # us por volta de laco vazio
var area_y = 0      # onde os desenhos de teste caem (embaixo da tabela)

def t(nome, f) testes.push([nome, f]) end

# ---- o Berry -----------------------------------------------------------------
var x = 0
var txt = ""
t("laco vazio", def (i) end)
t("soma em variavel", def (i) x += i end)
t("chamada ao sistema", def (i) tela.cor(1, 2, 3) end)
t("texto + str(i)", def (i) txt = "valor " + str(i) end)
t("lista [1,2,3]", def (i) var l = [1, 2, 3] end)
t("mapa {a:1}", def (i) var m = {"a": 1} end)

# ---- a tela -----------------------------------------------------------------
t("retangulo 1x1", def (i) tela.retangulo(20, area_y, 1, 1, tela.VERMELHO) end)
t("retangulo 50x40", def (i) tela.retangulo(20, area_y, 50, 40, tela.VERMELHO) end)
t("retangulo 300x28", def (i) tela.retangulo(20, area_y, 300, 28, tela.VERDE) end)
t("retangulo vazado 50x40", def (i) tela.retangulo(20, area_y, 50, 40, tela.AZUL, false) end)
t("pixel", def (i) tela.pixel(20 + i % 100, area_y, tela.BRANCO) end)
t("linha 100 px", def (i) tela.linha(20, area_y, 120, area_y + 37, tela.BRANCO) end)
t("circulo r24", def (i) tela.circulo(60, area_y + 30, 24, tela.AMARELO) end)
t("circulo vazado r24", def (i) tela.circulo(60, area_y + 30, 24, tela.AMARELO, false) end)
t("arredondado 120x80", def (i) tela.arredondado(20, area_y, 120, 80, 20, tela.VERDE) end)
t("triangulo", def (i) tela.triangulo(20, area_y + 60, 60, area_y, 100, area_y + 60, tela.LARANJA) end)
t("texto 20 letras", def (i) tela.texto("Ola mundo do Berry!!", 20, area_y, 3) end)
t("texto 20 letras tam 5", def (i) tela.texto("Ola mundo do Berry!!", 20, area_y, 5) end)
t("medir 20 letras", def (i) tela.medir("Ola mundo do Berry!!", 3) end)
t("botao", def (i) tela.botao(20, area_y, 300, 70, "Botao") end)

# ---- camadas ------------------------------------------------------------------
# Criadas uma vez, ao abrir (fora do desenhar vale: a camada e uma imagem).
var cam_p = tela.camada(300, 200)
var cam_l = tela.camada(tela.largura(), 200)
t("colar camada 300x200", def (i) tela.colar(cam_p, 20, area_y - 80) end)
t("colar camada largura x200", def (i) tela.colar(cam_l, 0, area_y - 80) end)
t("colar pedaco 50x40", def (i) tela.colar(cam_p, 20, area_y, 10, 10, 50, 40) end)
t("texto 20 letras na camada", def (i)
  tela.alvo(cam_p)
  tela.texto("Ola mundo do Berry!!", 4, 4, 3)
  tela.alvo()
end)

def medir(f, n)
  var t0 = sistema.us()
  for i: 0 .. n - 1 f(i) end
  return (sistema.us() - t0) * 1.0 / n
end

def desenhar(tudo)
  var l = tela.largura()
  area_y = tela.altura() - 120

  # Um teste por quadro (cada um tem que caber no prazo de 2 s do desenhar).
  if atual < size(testes)
    var tt = testes[atual]
    var n = atual < 6 ? N * 10 : N
    var us = medir(tt[1], n)
    if atual == 0 vazio = us end
    var liquido = atual == 0 ? us : us - vazio
    res.push(liquido)
    sistema.log(string.format("bench %-24s %8.1f us", tt[0], liquido))
    atual += 1
    if atual == size(testes) sistema.periodo(0) end
  end

  tela.limpar()
  tela.texto("Custo por operacao (us)", 24, 16, 3, tela.AMARELO)
  var y = 64
  for i: 0 .. size(res) - 1
    tela.texto(testes[i][0], 24, y, 2.5)
    tela.texto(string.format("%.1f", res[i]), l - 24, y, 2.5, tela.CIANO, tela.DIREITA)
    y += 34
  end
  if atual < size(testes)
    tela.texto("medindo " + testes[atual][0] + "...", 24, y + 10, 2.5, tela.CINZA_CLARO)
  else
    tela.botao(24, y + 10, 300, 70, "Repetir", tela.CIANO)
  end
end

def tocar(px, py)
  if atual >= size(testes)
    res = []
    atual = 0
    sistema.periodo(16)
  end
end

