# nome: Estresse
# cor: 0xF800
# periodo: 16
#
# Aperta o sistema inteiro ao mesmo tempo, para ver como ele se comporta:
#   - TELA:     centenas de figuras por quadro (quantas couberem no prazo)
#   - CPU:      o conjunto de Mandelbrot calculado linha a linha numa camada
#   - MEMORIA:  enche a memoria do Berry ate 60% do orcamento e solta
#   - CARTAO:   grava e rele um arquivo sem parar
#   - REDE:     um download a cada 15 s (precisa do Wi-Fi)
#   - FUNDO:    uma conta pesada na parte de fundo a cada 500 ms
# As fases trocam a cada 20 s; a ultima ("tudo junto") liga todas. Um toque
# passa para a proxima fase. A cada 10 s sai uma linha "estresse:" no log
# serial com os numeros (para comparar depois).
#
# A carga se AJUSTA sozinha: cada parte mede quanto levou e aumenta ou diminui
# o trabalho para caber na sua fatia do quadro. Assim o app aperta ao maximo
# sem estourar o prazo de 2 s por evento do sistema.

import math
import gc

# ---- fases -------------------------------------------------------------------
var FASES = ["Tela", "CPU", "Memoria", "Cartao", "Rede", "Tudo junto"]
var FASE_MS = 20000
var fase = 0
var fase_desde = 0

# Cada parte liga em quais fases (a ultima, "Tudo junto", liga todas).
def ligado(nome)
  return FASES[fase] == nome || fase == size(FASES) - 1
end

# ---- numeros do quadro --------------------------------------------------------
var quadros = 0          # quadros desde o ultimo resumo
var pior_ms = 0.0        # o quadro mais lento desde o ultimo resumo
var soma_ms = 0.0
var resumo_desde = 0
var fps = 0.0
var med_ms = 0.0
var pior_mostrado = 0.0
var erros = 0            # tudo o que falhou (cartao, rede, memoria)
var ultimo_erro = ""

def erro(txt)
  erros += 1
  ultimo_erro = txt
  sistema.log("estresse: ERRO " + txt)
end

# ---- TELA: figuras aleatorias -------------------------------------------------
var n_fig = 50           # quantas figuras por quadro (ajustado)
var FIG_ALVO_US = 25000  # fatia do quadro para as figuras

def figuras(x0, y0, l, a)
  var t0 = sistema.us()
  for i: 1 .. n_fig
    var x = x0 + math.rand() % l
    var y = y0 + math.rand() % a
    var c = tela.cor(math.rand() % 256, math.rand() % 256, math.rand() % 256)
    var tipo = i % 5
    if tipo == 0     tela.circulo(x, y, 4 + math.rand() % 30, c)
    elif tipo == 1   tela.retangulo(x, y, 8 + math.rand() % 60, 8 + math.rand() % 40, c)
    elif tipo == 2   tela.linha(x, y, x0 + math.rand() % l, y0 + math.rand() % a, c)
    elif tipo == 3   tela.triangulo(x, y, x + 30, y + 10, x + 10, y + 35, c)
    else             tela.texto("Tab5", x, y, 2, c)
    end
  end
  var gasto = sistema.us() - t0
  # Ajuste: 10% a mais ou a menos por quadro, entre 10 e 5000 figuras.
  if gasto < FIG_ALVO_US && n_fig < 5000 n_fig = int(n_fig * 1.1) + 1 end
  if gasto > FIG_ALVO_US && n_fig > 10   n_fig = int(n_fig * 0.9) end
end

# ---- CPU: Mandelbrot numa camada ---------------------------------------------
var MAND = 240           # lado da imagem (px)
var mand = nil           # a camada (criada no abrir)
var mand_linha = 0       # proxima linha a calcular
var mand_linhas = 2      # linhas por quadro (ajustado)
var mand_zoom = 1.0
var MAND_ALVO_US = 30000
var MAX_IT = 48

def mand_cor(it)
  if it >= MAX_IT return tela.PRETO end
  var k = it * 255 / MAX_IT
  return tela.cor(k, 255 - k, (k * 3) % 256)
end

def mandelbrot()
  var t0 = sistema.us()
  tela.alvo(mand)
  var escala = 3.0 / (MAND * mand_zoom)
  for j: 1 .. mand_linhas
    var py = mand_linha
    var ci = (py - MAND / 2) * escala
    for px: 0 .. MAND - 1
      var cr = (px - MAND * 0.7) * escala - 0.5 * (1 - 1 / mand_zoom)
      var zr = 0.0
      var zi = 0.0
      var it = 0
      while it < MAX_IT && zr * zr + zi * zi < 4.0
        var t = zr * zr - zi * zi + cr
        zi = 2 * zr * zi + ci
        zr = t
        it += 1
      end
      tela.pixel(px, py, mand_cor(it))
    end
    mand_linha += 1
    if mand_linha >= MAND
      mand_linha = 0
      mand_zoom *= 1.3                      # a proxima volta, mais perto
      if mand_zoom > 40 mand_zoom = 1.0 end
    end
  end
  tela.alvo()
  var gasto = sistema.us() - t0
  if gasto < MAND_ALVO_US && mand_linhas < 60 mand_linhas += 1 end
  if gasto > MAND_ALVO_US && mand_linhas > 1  mand_linhas -= 1 end
end

# ---- MEMORIA: encher e soltar --------------------------------------------------
var blocos = []          # cada bloco e uma lista grande
var BLOCO = 8192         # itens por bloco
var mem_pico = 0
var mem_ciclos = 0       # quantas vezes encheu e soltou

def memoria()
  var m = sistema.memoria()
  if m["berry"] > mem_pico mem_pico = m["berry"] end
  # Solta ao passar de 60% do orcamento (ou de 300 blocos, uns 2,4 MB: a
  # contagem do sistema pode demorar a acompanhar).
  if m["berry"] > m["orcamento"] * 6 / 10 || size(blocos) >= 300
    blocos = []
    gc.collect()
    mem_ciclos += 1
    return
  end
  try
    var b = []
    b.resize(BLOCO)
    for i: 0 .. 63 b[i * 128] = i end   # tocar a memoria de verdade
    blocos.push(b)
  except .. as e, msg
    erro("memoria: " + str(msg))
    blocos = []
    gc.collect()
  end
end

# ---- CARTAO: gravar e reler -----------------------------------------------------
var ARQ = "estresse.txt"
var cart_bytes = 0       # quanto o arquivo deveria ter
var cart_linhas = 0
var cart_leituras = 0
var cart_conferido_ms = 0

def cartao_trabalho(agora)
  var linha = format("%010d quadro %d fase %s figuras %d\n", agora, quadros, FASES[fase], n_fig)
  if cartao.anexar(ARQ, linha)
    cart_bytes += size(linha)
    cart_linhas += 1
  else
    erro("cartao: anexar falhou")
  end
  # A cada 2 s rele tudo e confere o tamanho.
  if agora - cart_conferido_ms > 2000
    cart_conferido_ms = agora
    var t = cartao.ler(ARQ)
    cart_leituras += 1
    if t == nil
      erro("cartao: ler devolveu nil")
    elif size(t) != cart_bytes
      erro(format("cartao: lido %d, esperado %d", size(t), cart_bytes))
      cart_bytes = size(t)
    end
    # Perto do limite de 64 KB da leitura: comeca de novo.
    if cart_bytes > 48000
      if cartao.gravar(ARQ, "") cart_bytes = 0 else erro("cartao: zerar falhou") end
    end
  end
end

# ---- REDE: um download a cada 15 s ----------------------------------------------
var URL = "http://example.com/"
var rede_ok = 0
var rede_falhas = 0
var rede_ms = 0          # quanto o ultimo levou
var rede_pedido_ms = -15000

def rede_trabalho(agora)
  var e = rede.estado()
  if e == "pronto" || e == "erro"
    var r = rede.resposta()
    if r != nil && r["status"] == 200
      rede_ok += 1
      rede_ms = r["ms"]
    else
      rede_falhas += 1
      erro("rede: " + (r != nil ? str(r["status"]) + " " + str(r["erro"]) : "sem resposta"))
    end
  elif e == "parado" && agora - rede_pedido_ms > 15000
    rede_pedido_ms = agora
    if !rede.pedir(URL, 16384) erro("rede: pedido recusado") end
  end
end

# ---- FUNDO: conta pesada fora do desenhar ----------------------------------------
# O fundo() tem prazo de 200 ms (passou, o sistema encerra o app): a conta
# se ajusta para levar uns 100 ms, metade do prazo.
var fundo_vezes = 0
var fundo_pior_ms = 0.0
var fundo_n = 500        # voltas da conta (ajustado)
var FUNDO_ALVO_MS = 100.0

def fundo()
  var t0 = sistema.us()
  var s = 0.0
  for i: 1 .. fundo_n s += math.sqrt(i) * math.sin(i) end
  var ms = (sistema.us() - t0) / 1000.0
  fundo_vezes += 1
  if ms > fundo_pior_ms fundo_pior_ms = ms end
  if ms < FUNDO_ALVO_MS && fundo_n < 100000 fundo_n = int(fundo_n * 1.2) + 1 end
  if ms > FUNDO_ALVO_MS && fundo_n > 50     fundo_n = int(fundo_n * 0.8) end
  sistema.fundo_estado("estresse", fundo_vezes)
end

# ---- o sistema chama ---------------------------------------------------------
def abrir()
  mand = tela.camada(MAND, MAND)
  fase_desde = sistema.ms()
  resumo_desde = fase_desde
  sistema.acordado(true)          # a tela nao apaga durante o teste
  sistema.fundo(true, 500)
  cartao.gravar(ARQ, "")
  sistema.log("estresse: comecou")
end

def fechar()
  sistema.fundo(false)
  sistema.acordado(false)
  blocos = []
  gc.collect()
  if mand != nil tela.liberar(mand) mand = nil end
  sistema.log(format("estresse: fim - %d erro(s), ultimo: %s", erros, ultimo_erro))
end

var primeiro = true      # o primeiro desenhar avisa no log (a janela rodou)

def resumo(agora)
  var dur = (agora - resumo_desde) / 1000.0
  fps = quadros / dur
  med_ms = soma_ms / (quadros > 0 ? quadros : 1)
  pior_mostrado = pior_ms
  var m = sistema.memoria()
  var en = sistema.energia()
  sistema.log(format("estresse: fase %s | %.1f qps, quadro %.1f ms (pior %.0f) | figuras %d, mandel %d lin | " +
                     "berry %d KB (pico %d) de %d | cartao %d lin %d leit | rede %d ok %d falha | " +
                     "fundo %d x%d voltas (pior %.0f ms) | %.0f mA %d%% | erros %d",
                     FASES[fase], fps, med_ms, pior_ms, n_fig, mand_linhas,
                     m["berry"] / 1024, mem_pico / 1024, m["orcamento"] / 1024, cart_linhas, cart_leituras,
                     rede_ok, rede_falhas, fundo_vezes, fundo_n, fundo_pior_ms, en["ma"], en["pct"], erros))
  quadros = 0
  soma_ms = 0.0
  pior_ms = 0.0
  resumo_desde = agora
end

def desenhar(tudo)
  var t0 = sistema.us()
  var agora = sistema.ms()
  if primeiro sistema.log("estresse: primeiro quadro") primeiro = false end
  var l = tela.largura()
  var a = tela.altura()

  if agora - fase_desde > FASE_MS
    fase = (fase + 1) % size(FASES)
    fase_desde = agora
    sistema.log("estresse: fase " + FASES[fase])
  end

  tela.limpar(tela.PRETO)
  var topo = 200                       # o painel dos numeros fica em cima
  if ligado("Tela")    figuras(0, topo, l, a - topo) end
  if ligado("CPU")     mandelbrot() end
  if mand != nil       tela.colar(mand, l - MAND - 10, topo + 10) end
  if ligado("Memoria") memoria() end
  if ligado("Cartao")  cartao_trabalho(agora) end
  if ligado("Rede")    rede_trabalho(agora) end

  # Painel.
  var m = sistema.memoria()
  var en = sistema.energia()
  var resta = (FASE_MS - (agora - fase_desde)) / 1000
  tela.retangulo(0, 0, l, topo - 6, tela.cor(20, 20, 40))
  tela.texto(format("ESTRESSE  fase %d/%d: %s  (%ds)", fase + 1, size(FASES), FASES[fase], resta),
             12, 8, 3, tela.AMARELO)
  tela.texto(format("%.1f quadros/s   quadro %.1f ms   pior %.0f ms", fps, med_ms, pior_mostrado),
             12, 48, 2.5, tela.BRANCO)
  tela.texto(format("figuras %d   Mandelbrot %d lin/quadro   fundo %d (pior %.0f ms)",
                    n_fig, mand_linhas, fundo_vezes, fundo_pior_ms), 12, 80, 2, tela.CIANO)
  tela.texto(format("Berry %d KB (pico %d) de %d KB   enchidas %d", m["berry"] / 1024,
                    mem_pico / 1024, m["orcamento"] / 1024, mem_ciclos), 12, 108, 2, tela.CIANO)
  tela.texto(format("cartao %d linhas %d leituras   rede %d ok %d falhas (%d ms)",
                    cart_linhas, cart_leituras, rede_ok, rede_falhas, rede_ms), 12, 136, 2, tela.CIANO)
  tela.texto(format("%.0f mA  %d%%   erros %d %s", en["ma"], en["pct"], erros, ultimo_erro),
             12, 164, 2, erros > 0 ? tela.VERMELHO : tela.VERDE)

  # Conta do quadro.
  var ms = (sistema.us() - t0) / 1000.0
  quadros += 1
  soma_ms += ms
  if ms > pior_ms pior_ms = ms end
  if agora - resumo_desde >= 10000 resumo(agora) end
  sistema.redesenhar()
end

def tocar(x, y)
  fase = (fase + 1) % size(FASES)
  fase_desde = sistema.ms()
  sistema.log("estresse: fase " + FASES[fase] + " (toque)")
end
