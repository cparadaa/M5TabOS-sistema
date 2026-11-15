# Apps em Berry

Um app em [Berry](https://berry.readthedocs.io) é **um arquivo `.be` no cartão**.
Não precisa compilar nada nem instalar ferramenta: escreva num editor de texto,
copie para o cartão (pelo modo pendrive, o ícone aparece assim que ejetar).

**Todas as funções numa página só, mais links para aprender a linguagem:
[BERRY_API.md](BERRY_API.md).**

Os exemplos estão em [`berry_exemplos/`](../berry_exemplos): o [`Ola.be`](../berry_exemplos/Ola.be)
é o primeiro passo, e o [`Mostruario.be`](../berry_exemplos/Mostruario.be) usa **toda** a API,
uma aba por parte do sistema. É o molde para um app novo. O
[`Registro.be`](../berry_exemplos/Registro.be) é o molde de um app que **continua
rodando com a janela fechada** (ver [Parte de fundo](#parte-de-fundo)), e o
[`Azimutal.be`](../berry_exemplos/Azimutal.be) é o Azimutal do sistema refeito em
script (céu, satélites e gravação em segundo plano).

## Instalar

O firmware traz um app de exemplo, o **Ola Berry** (`berry_exemplos/Ola.be`):
no primeiro boot com um cartão que ainda não tem `apps/Berry/`, o sistema o
grava lá. Ele mostra, em abas, o que um script faz - hora e rede, botões e
bip, aviso na barra, dado salvo no cartão, o acelerômetro, desenho com o dedo
e os discos. Apagado, não volta.

1. Copie o `.be` para a pasta **`apps/Berry/`** do cartão. A pasta é criada
   sozinha no primeiro boot com cartão.
2. Os apps são lidos no boot (até 16) e **de novo quando o cartão volta do
   modo pendrive**: copiou pelo computador e ejetou, o ícone aparece na tela
   inicial sem reiniciar. Copiou de outro jeito (cartão no leitor), reinicie
   ou use `berry reler` no console.
3. O app aparece na tela inicial com o nome do arquivo, ou o do cabeçalho.

Outro jeito de instalar: no app **Arquivos**, tocar num `.be` (do pendrive,
por exemplo) copia para `apps/Berry/` e o ícone aparece na hora.

## Remover

**Segure o ícone** do app na tela inicial (como para mudar de lugar) e
**arraste até a faixa "Remover"** que aparece no topo; solte e confirme. O
`.be` e a pasta de dados do app (`apps/Berry/<nome>/`) vão para a **lixeira
do cartão** - o Arquivos recupera até esvaziar. Os apps do sistema não
mostram a faixa. Apagar ou mover o `.be` pelo Arquivos também tira o ícone
na hora.

## O arquivo

```berry
# nome: Meu App          (opcional) o nome sob o ícone
# cor: 0x07E0            (opcional) a cor do ícone, RGB565
# periodo: 1000          (opcional) redesenhar sozinho a cada N ms
# avisos: espnow         (opcional) redesenhar NA HORA quando chega algo: espnow, mesh, gps, volumes, arquivos
# orientacao: em_pe      (opcional) em_pe (padrao), deitado ou girar (segue o aparelho)

var contador = 0         # o que fica fora das funções roda uma vez, ao abrir

def desenhar(tudo)
  tela.texto("Contador: " + str(contador), 40, 40, 4)
  tela.botao(40, 120, 300, 90, "+1")
end

def tocar(x, y)
  if tela.dentro(x, y, 40, 120, 300, 90) contador += 1 end
end
```

Uma função só pode ser chamada por código que vem **depois** dela no arquivo
(o Berry recusa um nome que ainda não viu): escreva os ajudantes em cima.

### As funções que o sistema chama (todas opcionais)

| função | quando |
|---|---|
| `abrir()` | o app abriu (depois do código de fora das funções) |
| `desenhar(tudo)` | hora de pintar. A área chega **limpa** e o quadro vai ao ar de uma vez, sem piscar: desenhe a tela inteira, sempre (`tudo` é `true`). Com `sistema.acumular(true)` ela deixa de chegar limpa - ver [Camadas e acumular](#camadas-e-acumular) |
| `tocar(x, y)` | um clique, em coordenadas da área do app (as mesmas do desenho). Depois dele o sistema redesenha |
| `dedo(x, y, pressionado)` | o dedo **enquanto** está na tela (arrastar, desenhar, jogo), a cada 20 ms; uma última vez com `pressionado = false` quando ele sai. Depois de cada uma o sistema redesenha |
| `tecla(texto, codigo)` | uma tecla, do teclado **físico ou do virtual**: `texto` é o que foi digitado (UTF-8), ou `""` e `codigo` diz qual tecla especial (`teclado.ENTER`, `APAGAR`...) |
| `aviso(nome)` | um serviço pedido em `# avisos:` publicou algo (`"espnow"`, `"mesh"`, `"gps"`, `"volumes"` - um pendrive entrou ou saiu, `"arquivos"` - a cópia andou ou terminou). O sistema já redesenha sozinho depois; a função é para o que mais o app quiser fazer (marcar como lido, tocar um bip) |
| `fechar()` | a janela vai fechar |
| `fundo()` | a **parte de fundo**: com `sistema.fundo(true)` o sistema a chama de tempos em tempos, com a janela aberta **ou fechada** - ver [Parte de fundo](#parte-de-fundo) |

## Módulo `tela`

Coordenadas relativas à área do app (0,0 é o canto de cima, abaixo da barra).
As funções que desenham só valem **dentro de `desenhar()`**.

| função | o que faz |
|---|---|
| `tela.largura()`, `tela.altura()` | o tamanho da área. Com o teclado virtual aberto, `altura()` já desconta ele |
| `tela.texto(txt, x, y [, tam [, cor [, alinhamento]]])` | texto; `tam` N = N×8 px de altura (padrão 3, aceita 2.5). `alinhamento`: `tela.ESQUERDA` (padrão), `CENTRO` ou `DIREITA` - `x` é o começo, o meio ou o fim do texto |
| `tela.medir(txt [, tam])` | a largura em px que esse texto ocuparia |
| `tela.cortar(txt, largura [, tam])` | o texto cortado para caber em `largura` px, com `...` no fim quando cortou (acento nunca quebra). Para nome de música, de contato, de arquivo |
| `tela.retangulo(x, y, l, a, cor [, cheio])` | retângulo (cheio por padrão) |
| `tela.arredondado(x, y, l, a, raio, cor [, cheio])` | retângulo de cantos redondos |
| `tela.circulo(x, y, r, cor [, cheio])` | círculo |
| `tela.triangulo(x0, y0, x1, y1, x2, y2, cor [, cheio])` | triângulo |
| `tela.linha(x0, y0, x1, y1, cor)` | linha |
| `tela.pixel(x, y, cor)` | um ponto |
| `tela.botao(x, y, l, a, txt [, cor])` | o botão do sistema |
| `tela.limpar([cor])` | pinta a área inteira |
| `tela.dentro(x, y, rx, ry, rl, ra)` | o ponto está no retângulo? (para o `tocar`; vale fora do `desenhar`) |
| `tela.cor(r, g, b)` | uma cor a partir de 0–255 |
| `tela.camada(l, a)` | cria uma imagem guardada (ver abaixo) |
| `tela.alvo(c)` / `tela.alvo()` | desvia todo desenho de `tela` para a camada `c` / volta para a tela |
| `tela.colar(c, x, y [, rx, ry, rl, ra])` | copia a camada (ou o pedaço `rx, ry, rl, ra` dela) para a tela |
| `tela.liberar(c)` | apaga a camada e devolve a memória |
| `tela.PRETO`, `BRANCO`, `VERMELHO`, `VERDE`, `AZUL`, `AMARELO`, `CIANO`, `LARANJA`, `CINZA`, `CINZA_CLARO`, `ROXO`, `ROSA` | cores prontas |

## Módulo `sistema`

| função | o que faz |
|---|---|
| `sistema.redesenhar()` | pede para chamar `desenhar` de novo |
| `sistema.periodo([ms])` | troca o `# periodo:` com o app aberto (0 = só quando pedir); sem argumento, devolve o de agora. Ex.: 16 numa aba animada, 1000 numa de relógio |
| `sistema.acumular([sim])` | `true`: o quadro não chega limpo, só desenhe o que mudou (ver abaixo) |
| `sistema.dt()` | segundos entre este quadro e o anterior **na tela**. Para animar por tempo: `x += velocidade * sistema.dt()` |
| `sistema.ms()` | milissegundos desde o boot (para medir tempo) |
| `sistema.us()` | microssegundos, para medir trechos curtos (só a diferença entre duas leituras vale) |
| `sistema.hora()` | `"14:05:09"` no fuso local, ou `""` sem relógio acertado |
| `sistema.hora_de(epoch)` | `"14:05"` no fuso local, para a hora de uma mensagem (`""` se 0) |
| `sistema.data()` | `"14/10/2026"`, ou `""` |
| `sistema.agora()` | segundos desde 1970 (UTC), ou 0 |
| `sistema.bateria()` | carga em % |
| `sistema.energia()` | `{"pct", "volts", "ma"}`; `ma` > 0 = carregando |
| `sistema.imu()` | `[ax, ay, az]` em g, ou `nil`. O app que usa ganha leitura nova a cada quadro |
| `sistema.giro()` | `[gx, gy, gz]` em graus/s, ou `nil`. O giroscópio só fica ligado enquanto algum script chama (5 s sem chamar, desliga e volta na chamada seguinte) |
| `sistema.repouso()` | `true` com a tela apagada. Para a parte de fundo espaçar o próprio ritmo: cada `fundo()` acorda o aparelho do sono leve |
| `sistema.gps()` | `nil` sem GPS no Grove; senão `{"fix", "lat", "lon", "alt", "sats"}` |
| `sistema.wifi()` | o estado do Wi-Fi em texto |
| `sistema.rede()` | `{"ssid", "ip", "rssi", "texto"}`; `ip` vazio = sem conexão |
| `sistema.bip(hz, ms)` | um bip |
| `sistema.avisar(txt [, nivel])` / `sistema.avisar()` | põe / tira um aviso na barra de status. `nivel`: `"info"`, `"atencao"` (padrão) ou `"urgente"` |
| `sistema.acordado(sim)` | `true`: a tela não apaga enquanto o app estiver aberto |
| `sistema.log(txt)` | escreve no log serial |
| `sistema.fundo(sim [, ms])` / `sistema.fundo()` | liga / desliga a [parte de fundo](#parte-de-fundo) (chama `fundo()` a cada `ms`, padrão 1000, mínimo 100); sem argumento, diz se está rodando |
| `sistema.fundo_estado(txt [, n])` | o texto da pastilha na barra de status enquanto o fundo roda (`"42 amostras"`) |
| `sistema.memoria()` | `{"app", "berry", "orcamento"}` em bytes: o que **esta** máquina usa, o que todas as máquinas Berry somam, e o teto delas |

## Camadas e acumular

Por padrão o app redesenha **a tela inteira** a cada quadro: é o jeito simples,
e numa tela de texto e botões custa poucos ms. Mas desenho custa por pixel, e
há duas situações em que redesenhar tudo é desperdício: **uma coisa pequena se
mexendo numa tela cheia**, e **um desenho que só cresce** (pintar com o dedo,
um gráfico que anda). Para elas:

**`sistema.acumular(true)`** - o quadro deixa de chegar limpo: o que você
desenhou continua na tela. O `desenhar(tudo)` passa a dizer o que fazer:

- `tudo = true`: a área **foi** limpa (o app abriu, a tela girou, o teclado
  abriu, voltou do bloqueio, ou você acabou de ligar o acumular) - desenhe tudo;
- `tudo = false`: desenhe **só o que mudou** desde o último quadro, e apague
  você mesmo o que saiu do lugar (um retângulo preto por cima).

**Camadas** - imagens guardadas na memória, do tamanho que quiser (até 8 por
app, somando 4 telas). Depois de `tela.alvo(c)`, toda função de `tela`
desenha nela - **em qualquer evento**, não só no `desenhar()`. `tela.alvo()`
volta para a tela, e `tela.colar(c, x, y)` põe a camada na tela. Use para o
que é caro de desenhar e muda pouco (o fundo de um jogo, um gráfico) e para
guardar o que não pode ser refeito (uma pintura).

```berry
var pintura = tela.camada(tela.largura(), 800)
var novos = []
sistema.acumular(true)

def dedo(x, y, pressionado)
  if !pressionado return end
  tela.alvo(pintura)                      # pinta na camada: fica guardado
  tela.circulo(x, y, 6, tela.AMARELO)
  tela.alvo()
  novos.push([x, y])                      # e anota para o próximo quadro
end

def desenhar(tudo)
  if tudo
    tela.colar(pintura, 0, 0)             # a área foi limpa: a pintura inteira
  else
    for p: novos tela.circulo(p[0], p[1], 6, tela.AMARELO) end   # só o novo
  end
  novos = []
end
```

A aba **Dedo** do [`Mostruario.be`](../berry_exemplos/Mostruario.be) é este exemplo completo. Colar
custa por pixel como desenhar (ver o `Bench.be`): cole a camada inteira só
quando `tudo`, e pedaços (`rx, ry, rl, ra`) para restaurar o fundo sob algo
que se mexe.

## Módulo `teclado`

O teclado virtual é **o do sistema** (o mesmo da senha do Wi-Fi): o app só
abre e fecha. As teclas chegam no `tecla(texto, codigo)`, igual às do teclado
físico - o mesmo código serve aos dois.

| função | o que faz |
|---|---|
| `teclado.abrir([tipo])`, `teclado.fechar()` | mostra / esconde o virtual, na base da área. Aberto, `tela.altura()` diminui: desenhe até ela. O `tipo` diz o que o campo é, como nos apps do sistema: `"texto"` (o padrão), `"ascii"` (servidor, usuário), `"senha"`, `"numero"` (só dígitos), `"busca"`, `"paragrafo"` (maiúscula no começo da frase e acentos; a tecla verde é nova linha), `"codigo"`, `"comando"`. Aberto, chamar de novo só troca o tipo. A tecla verde chega como `teclado.ENTER` em todos |
| `teclado.aberto()` | ele está na tela? (o ✕ dele fecha sozinho e manda `ESC`) |
| `teclado.fisico()` | há teclado físico no ExtPort1? |
| `teclado.ENTER`, `APAGAR`, `ESC`, `TAB`, `ESQUERDA`, `DIREITA`, `CIMA`, `BAIXO`, `DEL` | os códigos das teclas especiais |

```berry
def tecla(texto, codigo)
  if texto != ""                    campo += texto
  elif codigo == teclado.ENTER      enviar(campo)  campo = ""
  elif codigo == teclado.APAGAR     campo = sem_ultimo(campo)   # ver Mostruario.be
  end
end
```

## Módulo `cartao`

Arquivos na pasta do app, **`/sdcard/apps/Berry/<nome do app>/`** (criada sozinha), ao lado dos `.be` - tudo o que é de Berry fica dentro de `apps/Berry/`.
Só o nome do arquivo, sem pasta: o script não sai da pasta dele.
É a única porta para arquivos: o `open()`, o `load("arquivo")` e o `save()` do
Berry não abrem nada num script (devolvem erro) - quem abre o `.be` é o sistema,
na hora de carregar o app.

O script também não mexe na memória por fora do Berry: não há o módulo
`introspect`, o `bytes` não aceita ponteiro (`bytes(ptr, n)`) e atributo que
começa com `.` (os internos) não aceita atribuição.

| função | o que faz |
|---|---|
| `cartao.ler(nome)` | o arquivo inteiro como texto (até 64 KB), ou `nil` se não existe |
| `cartao.gravar(nome, texto)` | substitui o conteúdo; `true` se gravou |
| `cartao.anexar(nome, texto)` | acrescenta no fim (um log, um CSV) |
| `cartao.existe(nome)` | o arquivo existe? |
| `cartao.lista()` | os nomes dos arquivos da pasta |

Para guardar estrutura (listas, mapas), use o `json` do Berry:
`cartao.gravar("estado.json", json.dump(estado))` e `json.load(cartao.ler("estado.json"))`.

## Módulo `arquivos`

O cartão inteiro e o pendrive, pelo motor de arquivos do sistema. **A regra:
o script enxerga tudo, mas só altera a própria pasta** (`/sdcard/apps/Berry/<nome do app>/`).
O que está fora, ele lista e **copia para dentro** - e lê depois de trazer.
Nada fora da pasta é gravado, apagado, movido ou renomeado por script. As
senhas do sistema e os nomes que começam com `.` não aparecem. Exemplo
completo: [`Importar.be`](../berry_exemplos/Importar.be).

| função | o que faz |
|---|---|
| `arquivos.volumes()` | os discos de agora: `[{"raiz", "tipo", "nome", "disponivel"}]` (`"/sdcard"`; `"/usb"` com pendrive) |
| `arquivos.pasta()` | o caminho absoluto da pasta do app |
| `arquivos.lista(pasta [, filtro])` | o que há em **qualquer** pasta (absoluta: `"/usb/Rock"`; relativa: dentro da pasta do app, `""` = ela): `[{"nome", "tipo", "pasta", "bytes", "data", "caminho"}]`, pastas primeiro. `tipo`: `"pasta"`, `"musica"`, `"imagem"`, `"texto"`, `"berry"`, `"outro"`. `filtro`: `".mp3;.wav"`. `nil` = disco ausente ou pasta que não abre |
| `arquivos.copiar(origem [, subpasta])` | **traz** um arquivo ou pasta (ou uma lista deles, até 64) de qualquer disco para a pasta do app (ou uma subpasta dela que já exista). Em segundo plano: `true` = começou; senão, o texto do motivo (`"já há uma cópia em andamento"`) |
| `arquivos.estado()` | `{"fase", "pct", "itens", "feitos", "atual", "erro", "meu"}` - fase `"parado"`, `"contando"`, `"copiando"`, `"feito"`, `"falhou"`, `"cancelado"`; `meu` = foi este app que começou |
| `arquivos.cancelar()` | para a cópia que este app começou |
| `arquivos.ler(caminho)` | o conteúdo como texto (até 64 KB) - **só dentro da pasta do app** (`"sub/x.txt"`) |
| `arquivos.apagar(caminho)`, `arquivos.renomear(caminho, nome)`, `arquivos.criar_pasta(nome)` | **só dentro da pasta do app**; apagar vai para a lixeira do cartão |

- **Uma cópia por vez no aparelho** (a do app Arquivos inclusive): peça
  `# avisos: arquivos` para saber quando terminou, e mostre `estado()`.
- A cópia continua com o app fechado; o progresso aparece também na barra.
- Caminho relativo nunca sai da pasta do app: `".."`, `"/"` no começo e `"\\"`
  dão `arquivos_error`.
- O `cartao` continua o jeito simples para arquivos soltos na pasta do app
  (`cartao.gravar("estado.json", ...)`); o `arquivos` acrescenta subpastas e
  o resto dos discos.

## Módulo `espnow`

O rádio, o histórico e o aviso com o app fechado são do serviço do sistema: o
app só mostra e envia. O [`EspNow.be`](../berry_exemplos/EspNow.be) é o app ESP-NOW do sistema refeito
em Berry - o exemplo de um app de uso real.

| função | o que faz |
|---|---|
| `espnow.estado()` | `{"codigo", "texto"}`: 3 = no ar, 2 = ligando, 1 = Wi-Fi nunca ligado, 4 = rádio parado, 0 = C6 sem o firmware; `texto` diz o que fazer |
| `espnow.pronto()` | está no ar? |
| `espnow.enviar(texto)` | difusão no canal (todos ouvem); `true` se entrou na fila |
| `espnow.enviar_para(mac, texto)` | direto para um MAC (`"AA:BB:CC:DD:EE:01"`); `false` se o MAC é inválido |
| `espnow.enviar_todos(texto [, mac])` | repete o envio nos canais 1 a 13 (difusão, ou direto se vier `mac`) e volta ao canal de antes. **O Wi-Fi cai por alguns segundos** e volta sozinho |
| `espnow.n()`, `espnow.msg(i)` | quantas mensagens guardadas e a de índice `i` (0 = a mais antiga): `{"texto", "minha", "quando", "de", "envio", "espressif"}`. `envio` (as minhas): 1 saindo, 2 saiu pela antena, 3 falhou. `espressif`: chegou no formato do componente esp-now (senão, texto puro) |
| `espnow.total()` | só sobe a cada mensagem - compare para saber se chegou algo |
| `espnow.janela(sim)` | `true` com a conversa na tela (receptor ligado, o que chega não vira aviso na barra); `false` ao fechar |
| `espnow.marcar_lidas()`, `espnow.nao_lidas()` | o contador de não lidas |
| `espnow.canal()`, `espnow.canal_fixo()`, `espnow.fixar_canal(c)` | o canal em uso; o fixo (0 = segue o roteador); fixar (0, ou 1 a 13) |
| `espnow.mac()`, `espnow.vistos()`, `espnow.ultimo()` | o meu MAC, quantos aparelhos já foram ouvidos, ms desde a última recebida (-1 = nada) |

## Módulo `mesh`

Leitura, mais uma escrita com limites - um script não sai falando num canal público: `mesh.enviar` só manda mensagem **direta** para um nó.

| função | o que faz |
|---|---|
| `mesh.estado()` | `{"ativo", "no_ar", "nos", "nao_lidas"}` |
| `mesh.n_msgs()`, `mesh.msg(i)` | as mensagens recentes (até 8; `0` = a mais nova): `{"de", "nome", "texto", "quando", "canal", "direta", "snr", "mqtt", "minha"}`, ou `nil` |
| `mesh.total()` | só sobe a cada mensagem - compare para saber se chegou algo (com `# avisos: mesh`) |
| `mesh.n_nos()`, `mesh.no(i)` | os nós conhecidos (`0` = o nosso): `{"num", "id", "nome", "curto", "snr", "idade", "bateria", "saltos", "mqtt", "favorito"}`, mais `"lat"`/`"lon"` quando ele mandou posição |
| `mesh.meu()` | o número do nosso nó |
| `mesh.enviar(numero, texto)` | mensagem direta para o nó `numero`; `true` = entrou na fila. Recusa (`false`): número `0`, difusão (`0xFFFFFFFF`) ou o próprio nó; texto vazio ou com mais de 200 bytes; e mais de um envio a cada 10 s no aparelho inteiro |

## Módulo `camera`

O preview ao vivo e a foto, com o mesmo serviço do app Câmera. O
[`Camera.be`](../berry_exemplos/Camera.be) é o exemplo completo.

O preview **não passa pelo script**: a câmera escreve cada quadro direto na
tela, numa faixa da largura inteira. O `desenhar()` continua chegando como
sempre; a faixa do preview só não é apagada, e o que você desenhar nela (uma
mira, uma grade) sai por cima da imagem. Com a câmera ligada o app é
redesenhado a cada quadro (~30 por segundo): mantenha o `desenhar()` leve.

| função | o que faz |
|---|---|
| `camera.ligar([y [, altura]])` | liga o preview de `y` até `y + altura` (padrão: a área toda abaixo de `y`). O topo e a altura são arredondados para múltiplos de 8. Só com o app **em pé** (`# orientacao: em_pe`, o padrão) |
| `camera.desligar()` | desliga (o sistema também desliga sozinho ao fechar o app e no repouso, e religa ao acordar) |
| `camera.ligada()` | o preview está no ar? Liga logo depois do evento que pediu: no `desenhar()` seguinte já vale |
| `camera.faixa()` | `[y, altura]` do preview no ar (já arredondado), ou `nil` |
| `camera.erro()` | por que não ligou (`""` = nada) |
| `camera.foto([nome])` | tira a foto (1200×1600) e grava `<nome>.bmp` na pasta do app; sem nome, `foto_0001.bmp`, `foto_0002.bmp`... `false` = desligada ou já há uma em curso |
| `camera.foto_estado()` | `""`, `"capturando"`, `"gravando"`, `"ok"` ou `"falhou"` |
| `camera.ultima()` | o nome do último arquivo gravado |

Mais a biblioteca padrão do Berry: `string`, `math`, `json`, `time`, `global`...

## Módulo `gnss`

Os **satélites** que o receptor GPS do Grove vê (as sentenças GSV) - o que
desenha um céu como o do [`Azimutal.be`](../berry_exemplos/Azimutal.be). A
posição continua em `sistema.gps()`.

| função | o que faz |
|---|---|
| `gnss.ligar()` | começa a receber a constelação. `false` = o GPS já tem outro assinante (um só por vez) |
| `gnss.desligar()`, `gnss.ligado()` | solta / diz se está recebendo. Apagar a máquina do app solta sozinho |
| `gnss.satelites()` | lista de `{"sis", "prn", "elev", "azim", "snr", "pos", "idade"}`, ou `nil` sem rodada completa. `pos` falso = visto mas sem posição (não desenhe); `idade` = segundos desde a última posição observada |
| `gnss.versao()` | muda a cada rodada nova |
| `gnss.tempo()` | data e hora do **próprio GPS**, em UTC: `{"valida", "fix", "ano", "mes", "dia", "hora", "min", "seg"}` |

- **Chame `gnss.satelites()` pelo menos a cada 1-2 s** (um `# periodo: 1000`,
  ou `sistema.fundo(true, 1000)`). É o script que esvazia a fila do GPS - ela
  guarda uns 2 s; cheia, o que chega depois se perde e a foto envelhece.
- **Para gravar, só posição fresca** (`idade` pequena): quando a GSV chega sem
  posição, o parser segura a última - gravar isso repetiria o mesmo ponto.

## Módulo `musica`

O **tocador do sistema** (`services/tab5_musica`) - o mesmo do app Música. O
script é só a interface: o serviço acha a pasta de música do cartão, lista,
toca **com o app fechado**, mostra o que toca na barra e no miniplayer da
central (com o X que para), encerra sozinho depois de 15 min pausado e
calcula o espectro. Nenhuma linha de áudio no script. Exemplo completo:
[`Tocador.be`](../berry_exemplos/Tocador.be).

| função | o que faz |
|---|---|
| `musica.pasta()` | o nome da pasta de música (`"Music"`, `"Música"`...), ou `nil` sem cartão |
| `musica.pendrive()` | o nome do pendrive no USB-A, ou `nil` sem pendrive. As músicas dele: `musica.lista("/usb")`, `musica.tocar("/usb", i)` - o pendrive inteiro é a "pasta" (aceita subpastas: `"/usb/Rock"`) |
| `musica.lista([sub])` | o conteúdo da subpasta (`""` = a raiz): lista de `{"nome", "titulo", "pasta", "caminho"}`, pastas primeiro. `titulo` = o nome sem a extensão; `caminho` = o que `playlist_por` e `duracao` recebem. `nil` = sem cartão ou a pasta não existe |
| `musica.tocar(sub, i)` | toca a pasta `sub` a partir do item `i` de `lista(sub)` (tem que ser música). `true` = foi. Tocar o título no miniplayer abre **este** app |
| `musica.pausar()` | tocar/pausar |
| `musica.proxima()`, `musica.anterior()` | pula. Anterior depois de 3 s volta ao começo da mesma |
| `musica.ir_para(ms)` | busca |
| `musica.parar()` | desliga o tocador |
| `musica.estado()` | `{"ativo", "tocando", "titulo", "artista", "album", "pos", "dur", "idx", "n", "pasta", "playlist", "caminho", "aleatorio", "repetir", "erro", "kbps", "hz"}` - `pos`/`dur` em ms, do que **sai** no alto-falante |
| `musica.espectro()` | as `musica.FAIXAS` (32) barras, 0..255, graves primeiro, do som que sai agora; `nil` = nada saindo |
| `musica.volume([v])` | o volume do sistema, 0..100 (o mesmo de Ajustes) |
| `musica.cor([c])` | a cor de destaque do tocador (`tela.cor`): a pastilha da barra e o miniplayer |
| `musica.aleatorio([b])` | embaralhar a fila (a que toca continua); devolve como ficou |
| `musica.repetir([r])` | `0` toca até o fim e para, `1` repete a fila toda, `2` repete a mesma música |
| `musica.playlists()` | `[{"nome", "id"}]` - `nome` para mostrar, `id` para as funções abaixo; a `Favoritas` primeiro |
| `musica.playlist(id)` | as faixas: `[{"caminho", "titulo"}]`; `nil` = não existe |
| `musica.playlist_criar(nome)` | cria vazia; devolve o `id`, ou `nil` (já existe, sem cartão) |
| `musica.playlist_apagar(id)` | apaga o `.m3u` (as músicas ficam) |
| `musica.playlist_por(id, caminho)`, `musica.playlist_tirar(id, caminho)` | `caminho` = o de `lista()` ou `playlist()`; `false` = já estava / não estava |
| `musica.playlist_tocar(id, [i])` | toca a playlist a partir da faixa `i` (sem `i`: do começo, ou uma qualquer com o aleatório) |
| `musica.capa()` | o arquivo da capa do que toca (JPEG/PNG), ou `nil` |
| `musica.duracao(caminho)` | quanto dura, em ms, sem tocar (lê só o cabeçalho) |

- **Caminhos**: `sub` e `caminho` são relativos à pasta de música do cartão
  (`"Rock"`, `"Rock/x.mp3"`) ou absolutos num disco (`"/usb/Rock/x.mp3"`, o
  pendrive). Os dois cabem na mesma playlist; a faixa de um pendrive que não
  está plugado é pulada. Para saber quando um pendrive entra ou sai, peça
  `# avisos: volumes`.
- **Um tocador só no aparelho.** `musica.tocar` de um app troca a fila de
  quem estava tocando (inclusive do app Música).
- **O espectro só é calculado enquanto alguém lê**: chame `musica.espectro()`
  a cada quadro (`sistema.periodo(33)` tocando). Parou de ler - janela
  fechada, tela apagada - o serviço para de calcular sozinho em 1 s.
- **Não precisa de `sistema.fundo()`**: a música segue com a janela fechada
  por conta do serviço.

## Módulo `rede`

Internet (HTTP e HTTPS) **sem esperar**: `rede.pedir(url [, max_bytes])` só começa
(`true`/`false`; um pedido por vez no aparelho todo) e o download roda em segundo
plano. No `desenhar()`, olhe `rede.estado()` (`"parado"`, `"baixando"`, `"pronto"`,
`"erro"`) e leia `rede.resposta()` - `{"status", "texto", "tipo", "final", "ms", "erro"}`
- que libera o resultado. Também: `rede.ip()` (`{"conectado", "ip", "mascara", "gateway", "dns1", "dns2", "ipv6", "mac", "nome"}`),
`rede.cancelar()` e `rede.codificar(texto)` (texto para URL). Padrão de 64 KB de
corpo, no máximo 256 KB. Ao fechar o app, o pedido é cancelado. Detalhes e exemplo
em [BERRY_API.md](BERRY_API.md).

## Módulo `ajustes`

As preferências da pessoa. `ajustes.brilho([pct])` e `ajustes.volume([pct])` leem e
mudam (0 a 100); `ajustes.tela_segundos()`, `ajustes.casa()` (`[lat, lon]` ou `nil`)
e `ajustes.fuso()` (minutos do UTC) só leem.

## Desempenho: quanto custa cada coisa

Medido no Tab5 com o [`Bench.be`](../berry_exemplos/Bench.be) (rode você também: ele mostra a
tabela na tela e grava no log). Um quadro liso a 60 Hz tem **~16 ms**; o
`desenhar()` inteiro precisa caber nisso.

| operação | custo |
|---|---|
| conta, variável, `if` | < 1 µs |
| chamar o sistema (`tela.cor`, `sistema.ms`...) | ~4 µs |
| montar texto (`"x" + str(n)`) | ~15 µs |
| **criar** lista `[1,2,3]` / mapa `{"a":1}` | ~35 / ~23 µs |
| `tela.texto`, 20 letras (tam 3 / tam 5) | ~340 / ~710 µs |
| `tela.botao` | ~560 µs |
| retângulo 50×40 / 300×28 | ~36 / ~95 µs |
| círculo r24, cantos redondos 120×80 | ~90 / ~165 µs |
| linha de 100 px | ~60 µs |

O que isso quer dizer na prática:

- **Desenho custa por pixel**, não por chamada: um retângulo grande custa
  mais que dez pequenos. Não pinte fundo que já é preto (a área chega limpa).
- **Texto é o desenho mais caro.** Uns 15 textos por quadro já são ~4 ms. Em
  tela animada, escreva só o que muda; rótulo fixo grande é caro toda vez.
- **Não crie listas e mapas dentro do `desenhar()`**: o que é fixo (cores,
  tabelas, nomes) vai para fora das funções e é criado uma vez.
- **Periodo só onde anima.** `sistema.periodo(16)` na tela que mexe e `0` nas
  outras: parado, o app não gasta nada (ver o `Mostruario.be`).
- `berry tempos` no console mostra o seu app por dentro: quanto do quadro é
  desenho, quanto é script, e qual função de `tela` pesa mais.

## Parte de fundo

Um app pode seguir trabalhando com a janela fechada - gravar, vigiar, avisar -
do mesmo jeito que o Azimutal e a Trilha em C:

```
def fundo()                       # uma volta do trabalho, curta
  amostras.push(sistema.bateria())
  cartao.anexar("log.csv", sistema.hora() + "," + str(sistema.bateria()) + "\n")
  sistema.fundo_estado(str(size(amostras)) + " amostras")
end

def tocar(x, y)
  if tela.dentro(x, y, 40, 40, 300, 90)
    sistema.fundo(!sistema.fundo(), 10000)   # GRAVAR / PARAR, a cada 10 s
  end
end
```

- **A mesma máquina.** Janela e fundo veem as **mesmas variáveis**: o que
  `fundo()` guarda, `desenhar()` mostra. Os dois nunca rodam ao mesmo tempo
  (uma tarefa só para todo o Berry), então não há trava a pôr.
- **Fechar a janela não apaga a máquina** enquanto o fundo roda: `fechar()` é
  chamado, as camadas são devolvidas e o lixo é coletado na hora. Ao reabrir,
  o código de fora das funções **não** roda de novo - só `abrir()`: o estado
  continua. Quando o fundo para com a janela fechada, a máquina é apagada.
- **No fundo não há janela.** `tela`, camadas, `teclado`, `camera`,
  `sistema.bip`, `acordado`, `periodo` e `acumular` dão o erro `fundo_error`.
  `cartao`, `sistema.avisar`, `espnow`, `mesh` e as leituras valem.
- **Prazo de 200 ms** por chamada de `fundo()`: enquanto ela roda, um toque na
  tela espera. Faça uma volta curta e deixe o intervalo (`ms`) fazer o resto.
- **Visível sempre.** Enquanto roda, uma pastilha na barra de status mostra o
  nome do app (e o `fundo_estado`). Para pelo próprio app (`sistema.fundo(false)`,
  também de dentro de `fundo()`), pelo console (`fundo parar <nome>`) ou por um
  erro no script.
- **Custo:** nenhuma tarefa nova - o laço é uma vaga do pool do sistema e o
  Berry roda na tarefa de sempre. O que fica é a máquina (dezenas de KB na
  PSRAM). Até 4 partes de fundo ao mesmo tempo, somando as do sistema.

## Limites e segurança

- **Cada app tem sua máquina.** Ela é criada ao abrir e apagada ao fechar (ou
  quando o fundo para): um app não vê as variáveis de outro, e fechar devolve
  toda a memória (o Berry usa a PSRAM, 32 MB).
- **Orçamento de memória.** Todas as máquinas Berry juntas (apps, fundos,
  console) não passam de **4 MB**. Passou, o script para com `memory_error` -
  um laço que enche uma lista não come a memória da câmera e do sistema. Um app
  típico usa de 10 a 50 KB (`sistema.memoria()` e `berry mem` mostram). As
  camadas (`tela.camada`) são à parte, com o limite delas.
- **Prazo.** Cada chamada (`desenhar`, `tocar`...) tem 2 s; o código de fora das
  funções tem 5 s. Passou, o script para com o erro `tempo_esgotado` - que
  **não** é pego por `try/except`: um laço que engole erros também para.
- **Erro não derruba o aparelho.** A mensagem aparece na tela do app (nos erros de sintaxe, com a linha);
  feche, corrija o arquivo e abra de novo.
- **Script não é C++.** Serve bem para telas, lógica e controle. Jogo rápido ou
  processamento pesado continuam sendo apps em C++ (ver [ESCREVENDO_APPS.md](ESCREVENDO_APPS.md)).

## Conferir no Mac antes de copiar

```
testes/checar_be.sh                  (todos de berry_exemplos/)
testes/checar_be.sh MeuApp.be
```

Com o mesmo Berry do firmware, ele **compila** cada arquivo (sintaxe, nome não
declarado) e depois **roda** com módulos de mentira
([`testes/berry_falso.be`](../testes/berry_falso.be)): abre o app, toca cada ponto da faixa de cima (as
abas) e uma grade na tela toda, manda teclas e arrasta o dedo, deitado e em pé.
Tipo errado (`tela.texto(5, ...)`), índice fora da lista, desenho fora do
`desenhar()` aparecem com o arquivo e a linha - no Mac, não no aparelho.

## Testar pelo console

O console serial roda Berry direto, na mesma máquina entre comandos:

```
berry 1+2*3
berry "def dobro(x) return x*2 end"
berry dobro(21)
berry rodar apps/Berry/Ola.be
berry reler                    (rele a pasta: icones novos sem reiniciar)
berry tempos                   (quanto o script leva em cada evento)
berry tempos zerar             (idem, e recomeca a contagem)
berry mem                      (memoria de cada maquina viva, e o orcamento)
```

O `berry tempos` mostra, para o app aberto, quantas vezes cada função rodou e
a média e o pior tempo dela (só o Berry, sem o sistema). Use para achar o que
pesa: um `desenhar()` de 3 ms anima liso; um de 30 ms já perde quadros.
