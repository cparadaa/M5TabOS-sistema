# nome: Camera Berry
# cor: 0xFD20
# orientacao: fixo
#
# A camera do Tab5 em Berry: o preview ao vivo e a foto no cartao.
#
# O preview NAO passa pelo script: a camera escreve cada quadro direto na
# tela (na faixa pedida em camera.ligar), e o desenhar() so pinta o que fica
# em volta - e o que quiser POR CIMA do preview (aqui, a grade dos tercos).
# Por isso o app fica liso mesmo com o script desenhando a cada quadro.
#
# A foto sai em 1200x1600 e vai para a pasta do app no cartao
# (/sdcard/apps/Berry/Camera Berry/foto_0001.bmp...). O preview so existe com o app
# em pe - por isso o '# orientacao: fixo' acima (em pe mesmo com o teclado
# fisico encaixado, que deita os outros apps).

var BARRA = 170          # a faixa de baixo: botoes e o estado
var M = 24
var grade = true
var fotos = 0            # tiradas nesta abertura
var estado_ant = ""      # para o bip so uma vez quando a foto termina

def abrir()
  # O preview na area toda menos a barra de baixo. Ele liga logo depois do
  # abrir(): camera.ligada() ja vale no primeiro desenhar.
  camera.ligar(0, tela.altura() - BARRA)
  sistema.acordado(true)          # a tela nao apaga com a camera aberta
end

def desenhar_grade(f, l)
  var y0 = f[0]
  var a = f[1]
  var c = tela.cor(255, 255, 255)
  tela.linha(l / 3, y0, l / 3, y0 + a, c)
  tela.linha(2 * l / 3, y0, 2 * l / 3, y0 + a, c)
  tela.linha(0, y0 + a / 3, l, y0 + a / 3, c)
  tela.linha(0, y0 + 2 * a / 3, l, y0 + 2 * a / 3, c)
end

def desenhar(tudo)
  var l = tela.largura()
  var a = tela.altura()
  var f = camera.faixa()          # [y, altura] do preview, ou nil

  if f == nil
    tela.texto(camera.erro() != "" ? camera.erro() : "ligando a camera...", l / 2, (a - BARRA) / 2, 3,
               tela.CINZA_CLARO, tela.CENTRO)
  elif grade
    desenhar_grade(f, l)
  end

  # A barra de baixo: FOTO, GRADE e o que esta acontecendo.
  var y = a - BARRA + 16
  var e = camera.foto_estado()
  var ocupado = e == "capturando" || e == "gravando"
  tela.botao(M, y, l - 2 * M - 220, 90, ocupado ? "SALVANDO..." : "FOTO",
             ocupado ? tela.CINZA : tela.LARANJA)
  tela.botao(l - M - 200, y, 200, 90, grade ? "SEM GRADE" : "GRADE", tela.cor(50, 55, 65))

  var txt = ""
  if e == "ok"          txt = "salva: " + camera.ultima()
  elif e == "falhou"    txt = "a foto falhou (cartao?)"
  elif ocupado          txt = e + "..."
  else                  txt = str(fotos) + " foto(s) nesta abertura"
  end
  tela.texto(txt, M, y + 108, 2.5, e == "falhou" ? tela.VERMELHO : tela.CINZA_CLARO)

  # O bip quando a foto termina de gravar (uma vez).
  if e != estado_ant
    if e == "ok" sistema.bip(1760, 60) end
    estado_ant = e
  end
end

def tocar(x, y)
  var l = tela.largura()
  var a = tela.altura()
  var by = a - BARRA + 16
  if tela.dentro(x, y, M, by, l - 2 * M - 220, 90)
    if camera.foto()
      fotos += 1
      sistema.bip(1200, 30)        # o clique do obturador
    end
  elif tela.dentro(x, y, l - M - 200, by, 200, 90)
    grade = !grade
  end
end

def fechar()
  camera.desligar()               # o sistema tambem desliga ao fechar
end
