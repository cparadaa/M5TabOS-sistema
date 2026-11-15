# nome: Importar
# cor: 0x5D1F
# avisos: arquivos, volumes
#
# O modulo 'arquivos': navegar pelo cartao e pelo pendrive e TRAZER um
# arquivo para a pasta do app. A regra do sistema: o script ve os discos
# inteiros, mas so altera a propria pasta (apps/Berry/Importar/) - o que
# esta fora, ele copia para dentro e ai le.
#
# Abas: DISCOS (navegar e tocar num arquivo para trazer) e MEUS (o que ja foi
# trazido; tocar num texto mostra o comeco dele).

import string

var aba = 0            # 0 = discos, 1 = meus
var pasta = ""         # "" = a lista de discos; senao, o caminho aberto
var itens = []         # o que a tela mostra
var pagina = 0
var texto = nil        # o texto aberto na aba MEUS (nil = a lista)
var msg = ""

var LIN = 70           # altura de uma linha da lista
var TOPO = 200         # onde a lista comeca

def por_pagina() return (tela.altura() - TOPO - 110) / LIN end

def carregar()
  pagina = 0
  texto = nil
  if aba == 1
    itens = arquivos.lista("")
  elif pasta == ""
    itens = []
    for v: arquivos.volumes()
      if v["disponivel"]
        itens.push({"nome": v["tipo"] + (v["nome"] != "" ? "  " + v["nome"] : ""),
                    "pasta": true, "caminho": v["raiz"], "tipo": "pasta", "bytes": 0})
      end
    end
  else
    itens = arquivos.lista(pasta)
    if itens == nil itens = [] msg = "a pasta não abriu" end
  end
end

def subir()
  var i = size(pasta) - 1
  while i > 0 && pasta[i] != "/" i -= 1 end
  if pasta == "/sdcard" || pasta == "/usb" pasta = "" else pasta = pasta[0 .. i - 1] end
  carregar()
end

def tamanho(b)
  if b >= 1048576 return format("%.1f MB", b / 1048576.0) end
  if b >= 1024 return format("%.1f KB", b / 1024.0) end
  return str(b) + " B"
end

def desenhar(tudo)
  var L = tela.largura()
  tela.botao(20, 16, 220, 70, "DISCOS", aba == 0 ? tela.AZUL : tela.CINZA)
  tela.botao(260, 16, 220, 70, "MEUS", aba == 1 ? tela.AZUL : tela.CINZA)
  if (aba == 0 && pasta != "") || texto != nil tela.botao(L - 200, 16, 180, 70, "< Voltar") end

  # O andamento da copia (o sistema copia em segundo plano).
  var e = arquivos.estado()
  var linha = msg
  if e["meu"] && e["fase"] == "copiando" linha = "trazendo " + e["atual"] + "  " + str(e["pct"]) + "%"
  elif e["meu"] && e["fase"] == "feito"  linha = "pronto: está em MEUS"
  elif e["meu"] && e["fase"] == "falhou" linha = "falhou: " + e["erro"]
  end
  var titulo = aba == 1 ? "apps/Berry/Importar" : (pasta == "" ? "Discos" : pasta)
  tela.texto(tela.cortar(titulo, L - 40, 3), 20, 110, 3)
  tela.texto(tela.cortar(linha, L - 40, 2.5), 20, 152, 2.5, tela.CINZA_CLARO)

  if texto != nil
    var y = TOPO
    for l: string.split(texto, "\n")
      if y > tela.altura() - 60 break end
      tela.texto(tela.cortar(l, L - 40, 2.5), 20, y, 2.5)
      y += 34
    end
    return
  end

  var n = por_pagina()
  var y = TOPO
  for i: pagina * n .. pagina * n + n - 1
    if i >= size(itens) break end
    var it = itens[i]
    var cor = it["pasta"] ? tela.AMARELO : (it["tipo"] == "texto" ? tela.CIANO : tela.BRANCO)
    tela.texto(tela.cortar((it["pasta"] ? "[ ] " : "") + it["nome"], L - 220, 3), 20, y, 3, cor)
    if !it["pasta"] tela.texto(tamanho(it["bytes"]), L - 20, y, 2.5, tela.CINZA_CLARO, tela.DIREITA) end
    y += LIN
  end
  if size(itens) == 0 tela.texto("(vazio)", 20, TOPO, 3, tela.CINZA) end
  if size(itens) > n
    var by = tela.altura() - 100
    tela.botao(20, by, 220, 80, "ANTES", pagina > 0 ? tela.AZUL : tela.CINZA)
    tela.botao(260, by, 220, 80, "DEPOIS", (pagina + 1) * n < size(itens) ? tela.AZUL : tela.CINZA)
  end
end

def tocar(x, y)
  var L = tela.largura()
  if tela.dentro(x, y, 20, 16, 220, 70) aba = 0 msg = "" carregar() return end
  if tela.dentro(x, y, 260, 16, 220, 70) aba = 1 msg = "" carregar() return end
  if tela.dentro(x, y, L - 200, 16, 180, 70)
    if texto != nil texto = nil elif aba == 0 && pasta != "" subir() end
    return
  end
  var n = por_pagina()
  if size(itens) > n && y >= tela.altura() - 100
    if x < 240 && pagina > 0 pagina -= 1 end
    if x >= 260 && x < 480 && (pagina + 1) * n < size(itens) pagina += 1 end
    return
  end
  if texto != nil || y < TOPO return end
  var i = pagina * n + (y - TOPO) / LIN
  if i >= size(itens) return end
  var it = itens[i]
  if aba == 1
    if it["tipo"] == "texto"
      texto = arquivos.ler(it["nome"])
      if texto == nil texto = "(não consegui ler)" end
    end
  elif it["pasta"]
    pasta = it["caminho"]
    carregar()
  else
    var r = arquivos.copiar(it["caminho"])
    msg = r == true ? "trazendo " + it["nome"] + "..." : "não deu: " + r
  end
end

# A copia andou ou terminou, ou um disco entrou/saiu: o sistema ja
# redesenha; aqui so atualiza a lista.
def aviso(nome)
  if nome == "volumes" && aba == 0 pasta = "" carregar() end
  if nome == "arquivos" && aba == 1 && texto == nil carregar() end
end

carregar()
