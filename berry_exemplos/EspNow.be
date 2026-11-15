# nome: ESP-NOW Berry
# cor: 0x07FF
# periodo: 1000
# avisos: espnow
#
# O app ESP-NOW do sistema, refeito em Berry: a prova de que um app de uso
# real cabe num script. Barra de estado (radio, canal, quem foi ouvido), a
# conversa em baloes, rolagem com o dedo, e um campo que escreve pelo teclado
# virtual do sistema ou pelo fisico. O radio, o historico e o aviso com o app
# fechado sao do servico do sistema - o script so mostra e envia.
#
# '# avisos: espnow' faz o sistema redesenhar o app NA HORA em que uma
# mensagem chega (e chamar aviso("espnow")); o periodo de 1 s e so para o
# "ultimo ha N s" da barra.

import string

var BARRA_A = 88         # a barra de cima
var CAMPO_A = 96         # a barra de baixo (campo + Enviar)
var M = 16
var PAD = 14             # margem dentro do balao
var TAM = 2.6            # o texto da mensagem (ver letra())
var LH = 30              # altura de uma linha dele
var LETRAS = [2.6, 3.2, 3.8, 4.6]   # o botao "Aa" da barra passa de um para o outro
var nivel_letra = 0
var aa = [0, 0]          # onde o botao "Aa" foi desenhado (x0, x1)
var CANAIS = [0, 1, 6, 11]   # 0 = o do roteador; 1, 6 e 11 nao se sobrepoem
var CORES_MAC = []       # cor estavel por aparelho (preenchida no abrir)
var MAX_TXT = 250        # o maximo que o radio leva numa mensagem

var rascunho = ""
var rolagem = 0          # px que a conversa desceu para mostrar as antigas
var rol_max = 0
var dedo_y = nil         # arrasto da conversa em curso
var pilula = [0, 0]      # onde a pilula do canal foi desenhada (x0, x1)
var medidas = {}         # indice -> [largura, texto, linhas, largura do balao] (ver medida())

# ---- ajudantes -------------------------------------------------------------------
def cor_do_mac(mac)
  var h = 0
  for i: 0 .. size(mac) - 1 h = (h * 31 + string.byte(mac[i])) % 9973 end
  return CORES_MAC[h % size(CORES_MAC)]
end

def sem_ultimo(s)
  var i = size(s) - 1
  while i > 0 && (string.byte(s[i]) & 0xC0) == 0x80 i -= 1 end
  if i <= 0 return "" end
  return s[0 .. i - 1]
end

# Quebra o texto em linhas que cabem em 'larg' (so no desenhar: usa tela.medir).
def quebrar(txt, larg)
  var linhas = []
  var atual = ""
  for w: string.split(txt, " ")
    var tenta = atual == "" ? w : atual + " " + w
    if tela.medir(tenta, TAM) <= larg
      atual = tenta
    else
      if atual != "" linhas.push(atual) end
      # Palavra maior que a linha inteira: corta.
      while size(w) > 1 && tela.medir(w, TAM) > larg
        var k = size(w) - 1
        while k > 1 && tela.medir(w[0 .. k - 1], TAM) > larg k -= 1 end
        linhas.push(w[0 .. k - 1])
        w = w[k .. size(w) - 1]
      end
      atual = w
    end
  end
  if atual != "" || size(linhas) == 0 linhas.push(atual) end
  return linhas
end

# A quebra e a largura do balao sao caras (medem palavra por palavra): ficam
# guardadas por INDICE, conferidas pelo texto - comparar texto nao cria nada,
# e se o historico andar (a mais antiga sai), a medida e refeita sozinha.
def medida(i, m, larg)
  var q = medidas.find(i)
  if q != nil && q[0] == larg && q[1] == m["texto"] return q end
  if size(medidas) > 300 medidas = {} end
  var linhas = quebrar(m["texto"], larg)
  var w = 0
  for t: linhas
    var x = tela.medir(t, TAM)
    if x > w w = x end
  end
  var bl = w + 2 * PAD
  if bl < 200 bl = 200 end
  q = [larg, m["texto"], linhas, bl]
  medidas[i] = q
  return q
end

def enviar()
  if rascunho == "" || !espnow.pronto() return end
  if espnow.enviar(rascunho)
    rascunho = ""
    rolagem = 0
  else
    sistema.bip(220, 120)   # fila cheia ou radio fora: o texto fica no campo
  end
end

# O TAMANHO DA LETRA da conversa: troca o texto e a linha juntos, e joga fora
# as medidas guardadas (foram feitas com o tamanho velho).
def letra(n)
  if n < 0 || n >= size(LETRAS) n = 0 end
  nivel_letra = n
  TAM = LETRAS[n]
  LH = int(TAM * 8 + 9.5)
  medidas = {}
end

# ---- o ciclo de vida -------------------------------------------------------------
def abrir()
  var t = cartao.ler("letra.txt")   # a escolha da ultima vez (pasta do app no cartao)
  if t != nil letra(int(t)) end
  CORES_MAC = [tela.cor(135, 206, 235), tela.LARANJA, tela.cor(173, 255, 47), tela.ROSA,
               tela.cor(255, 215, 0), tela.CIANO, tela.cor(238, 130, 238), tela.CINZA_CLARO]
  espnow.janela(true)      # conversa na frente: receptor ligado, sem aviso na barra
  espnow.marcar_lidas()
end

def fechar()
  espnow.janela(false)
end

def aviso(nome)
  espnow.marcar_lidas()    # com a conversa aberta, o que chega ja nasce lido
end

# ---- desenho ---------------------------------------------------------------------
def desenhar_barra(l, est)
  var fundo = tela.cor(13, 17, 23)
  tela.retangulo(0, 0, l, BARRA_A, fundo)
  var cy = BARRA_A / 2
  var cod = est["codigo"]
  tela.circulo(34, cy, 15, cod == 3 ? tela.cor(173, 255, 47) : cod == 2 ? tela.LARANJA : tela.CINZA)
  tela.texto("ESP-NOW", 64, cy - 15, 3.6)
  var x = 64 + tela.medir("ESP-NOW", 3.6) + 24

  # A pilula do canal e um botao: laranja = canal fixo, azul = o do roteador.
  var fixo = espnow.canal_fixo()
  var canal = fixo ? "ch " + str(fixo) : cod == 3 ? "ch " + str(espnow.canal()) + " auto" : "ch auto"
  var pl = tela.medir(canal, 3) + 40
  tela.arredondado(x, cy - 33, pl, 66, 33, fixo ? tela.cor(120, 70, 10) : tela.cor(38, 60, 90))
  tela.texto(canal, x + 20, cy - 12, 3)
  pilula = [x, x + pl]
  x += pl + 14

  # "Aa": o tamanho da letra da conversa (a letra do botao cresce junto).
  var al = 76
  tela.arredondado(x, cy - 33, al, 66, 33, tela.cor(38, 60, 90))
  tela.texto("Aa", x + al / 2, cy - int((2.4 + 0.4 * nivel_letra) * 4), 2.4 + 0.4 * nivel_letra, tela.BRANCO, tela.CENTRO)
  aa = [x, x + al]
  x += al + 20

  # O resto, encolhido ate caber.
  var info
  if cod == 3
    var ms = espnow.ultimo()
    var ult = ms < 0 ? "nada ouvido" : ms < 60000 ? "ultimo " + str(ms / 1000) + "s" : "ultimo " + str(ms / 60000) + "min"
    info = str(espnow.vistos()) + " ouvido(s)  " + ult
    if l > 1000 info = espnow.mac() + "   " + info end
  else
    info = cod == 2 ? "ligando..." : cod == 4 ? "radio parado" : "fora do ar"
  end
  var tam = 3.0
  while tam > 1.8 && tela.medir(info, tam) > l - M - x tam -= 0.2 end
  tela.texto(info, x, cy - int(tam * 4), tam, tela.CINZA_CLARO)
  tela.linha(0, BARRA_A - 1, l, BARRA_A - 1, tela.cor(38, 42, 50))
end

def desenhar_campo(l, a)
  var y = a - CAMPO_A
  tela.retangulo(0, y, l, CAMPO_A, tela.cor(13, 17, 23))
  tela.linha(0, y, l, y, tela.cor(38, 42, 50))
  var bl = 170
  var fl = l - 3 * M - bl
  tela.arredondado(M, y + 14, fl, CAMPO_A - 28, 14, tela.cor(30, 33, 40))
  if rascunho == ""
    tela.texto(teclado.aberto() || teclado.fisico() ? "escreva..." : "toque para escrever",
               M + 18, y + 33, 2.8, tela.CINZA)
  else
    # Mostra o FIM do rascunho quando nao cabe (o cursor esta la).
    var t = rascunho
    while size(t) > 1 && tela.medir(t + "|", 2.8) > fl - 36 t = t[1 .. size(t) - 1] end
    tela.texto(t + "|", M + 18, y + 33, 2.8)
  end
  tela.botao(l - M - bl, y + 14, bl, CAMPO_A - 28, "Enviar",
             rascunho != "" && espnow.pronto() ? tela.CIANO : tela.CINZA)
end

# A conversa de baixo para cima: a mais nova encostada no campo; a rolagem
# desce o conjunto para mostrar as antigas. O que passa por cima da barra e
# coberto por ela, que e desenhada depois. Para assim que sai por cima: o que
# esta acima da tela nem e buscado.
def desenhar_conversa(l, topo, base, est)
  var n = espnow.n()
  if n == 0
    var txt = est["texto"] != "" ? est["texto"] : "nada por aqui ainda - escreva algo"
    tela.texto(txt, l / 2, (topo + base) / 2 - 12, 2.6, tela.CINZA, tela.CENTRO)
    rol_max = 0
    return
  end
  var bw = l * 78 / 100
  var y = base - M + rolagem
  var i = n - 1
  var m = espnow.msg(i)
  while m != nil
    # A anterior serve duas vezes: para o rotulo desta e, na proxima volta,
    # como a mensagem da vez - uma busca so por mensagem.
    var ant = i > 0 ? espnow.msg(i - 1) : nil
    var q = medida(i, m, bw - 2 * PAD)
    var linhas = q[2]
    var bl = q[3]
    # Rotulo do remetente so quando ele muda (uma sequencia nao repete o nome).
    var rotulo = ""
    if !m["minha"] && (ant == nil || ant["minha"] || ant["de"] != m["de"])
      rotulo = m["de"][12 .. 13] + m["de"][15 .. 16]
    end
    var h = 2 * PAD + size(linhas) * LH + 28 + (rotulo != "" ? 30 : 0)
    y -= h
    if y < base
      var x = m["minha"] ? l - M - bl : M
      tela.arredondado(x, y, bl, h, 18, m["minha"] ? tela.cor(30, 70, 130) : tela.cor(45, 45, 52))
      var ty = y + PAD
      if rotulo != ""
        tela.texto(rotulo, x + PAD, ty, 2.4, cor_do_mac(m["de"]))
        ty += 30
      end
      for t: linhas
        tela.texto(t, x + PAD, ty, TAM)
        ty += LH
      end
      # A hora e, embaixo: o estado do envio (as minhas) ou o dialeto (as recebidas).
      var meta = sistema.hora_de(m["quando"])
      var cor_meta = tela.CINZA_CLARO
      if m["minha"]
        var e = m["envio"]
        meta += e == 1 ? "  enviando" : e == 2 ? "  enviada" : e == 3 ? "  FALHOU" : ""
        if e == 3 cor_meta = tela.VERMELHO end
      else
        meta += m["espressif"] ? "  esp-now Espressif" : "  texto puro"
      end
      tela.texto(meta, x + bl - PAD, ty + 2, 2, cor_meta, tela.DIREITA)
    end
    y -= 12
    if y < topo break end   # saiu por cima: o resto nao aparece
    m = ant
    i -= 1
  end
  # Ate onde da para rolar: chegou na mais antiga, sabe-se exatamente; parou
  # antes, ha mais acima - a conta fecha quando a rolagem chegar la.
  if m == nil || i <= 0
    rol_max = rolagem + (topo + M - y > 0 ? topo + M - y : 0)
  else
    rol_max = rolagem + 100000
  end
end

def desenhar(tudo)
  var l = tela.largura()
  var a = tela.altura()       # com o teclado aberto, ja sem ele
  var est = espnow.estado()
  if rolagem > rol_max rolagem = rol_max end
  desenhar_conversa(l, BARRA_A, a - CAMPO_A, est)
  desenhar_barra(l, est)
  desenhar_campo(l, a)
end

# ---- toque, dedo e teclas ----------------------------------------------------------
def tocar(x, y)
  var l = tela.largura()
  var a = tela.altura()
  if y < BARRA_A
    if x >= aa[0] && x < aa[1]
      letra((nivel_letra + 1) % size(LETRAS))
      cartao.gravar("letra.txt", str(nivel_letra))
    elif x >= pilula[0] && x < pilula[1]
      # Roteador -> 1 -> 6 -> 11 -> roteador.
      var k = CANAIS.find(espnow.canal_fixo())
      espnow.fixar_canal(CANAIS[k == nil ? 0 : (k + 1) % size(CANAIS)])
    end
    return
  end
  if y >= a - CAMPO_A
    if x >= l - M - 170
      enviar()
    elif !teclado.fisico()
      if teclado.aberto() teclado.fechar() else teclado.abrir() end
    end
  end
end

# Arrastar a conversa: para cima mostra as mais novas, para baixo as antigas.
def dedo(x, y, pressionado)
  if !pressionado
    dedo_y = nil
    return
  end
  if y < BARRA_A || y >= tela.altura() - CAMPO_A return end
  if dedo_y != nil
    rolagem += y - dedo_y
    if rolagem < 0 rolagem = 0 end
    if rolagem > rol_max rolagem = rol_max end
  end
  dedo_y = y
end

def tecla(texto, codigo)
  if texto != ""
    if size(rascunho) + size(texto) <= MAX_TXT rascunho += texto end
  elif codigo == teclado.APAGAR
    rascunho = sem_ultimo(rascunho)
  elif codigo == teclado.ENTER
    enviar()
  elif codigo == teclado.ESC
    teclado.fechar()
  elif codigo == teclado.CIMA
    rolagem += 3 * LH
  elif codigo == teclado.BAIXO
    rolagem -= 3 * LH
    if rolagem < 0 rolagem = 0 end
  end
end
