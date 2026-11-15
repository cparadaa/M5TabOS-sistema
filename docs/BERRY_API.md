# Referência rápida das APIs Berry do M5tabOS

Tudo o que um app em Berry pode chamar, numa página. A explicação de cada
parte, com exemplos, está em [BERRY.md](BERRY.md); os apps de exemplo estão
em [`berry_exemplos/`](../berry_exemplos) (o [`Mostruario.be`](../berry_exemplos/Mostruario.be)
usa toda a API).

Esta lista é tirada das tabelas de registro dos módulos
(`berry_modulos_registrar`, [`main/services/berry_apps.cpp`](../main/services/berry_apps.cpp);
cada módulo mora em [`main/services/berry/`](../main/services/berry)).
Função nova num módulo tem que entrar nos dois lugares: aqui e no BERRY.md.

## Aprender a linguagem Berry

| Onde | O quê |
|---|---|
| <https://berry.readthedocs.io> | **Manual oficial** da linguagem: tipos, `var`, `def`, `class`, `for`, `try`, listas, mapas, módulos padrão |
| <https://github.com/berry-lang/berry> | O código-fonte e o README com a sintaxe em uma página |
| <https://tasmota.github.io/docs/Berry/> | Introdução prática e exemplos (do projeto Tasmota). **Atenção:** os módulos de hardware de lá (`gpio`, `wire`, `tasmota`, `lv`...) **não existem** aqui - no M5tabOS o hardware é pelos módulos desta página |

### A linguagem em 20 linhas

```berry
var n = 0                       # variável (inteiro 32 bits, real é float)
var nome = "Tab5"               # texto (UTF-8)
var l = [1, 2, 3]               # lista:  l.push(4)  l[0]  size(l)
var m = {"a": 1, "b": 2}        # mapa:   m["c"] = 3  m.contains("a")  m.keys()

def dobro(x) return x * 2 end   # função (declare ANTES de quem a chama)

if n > 0 print("positivo")
elif n == 0 print("zero")
else print("negativo") end

for i: 0 .. 9 n += i end        # 0 a 9, inclusive
for x: l print(x) end           # cada item da lista
while n > 0 n -= 1 end

var s = format("%d itens, %.1f%%", 3, 12.5)   # como o printf
try
  var v = json.load(cartao.ler("dados.json"))
except .. as e, msg
  print("erro: " + msg)
end

class Ponto var x, y def init(x, y) self.x = x self.y = y end end
```

## As funções que o sistema chama no seu script

Todas opcionais. Defina só as que usar.

| Função | Quando |
|---|---|
| `abrir()` | o app abriu |
| `desenhar(tudo)` | hora de pintar (`tudo` = a área chegou limpa) |
| `tocar(x, y)` | um toque (clique) |
| `dedo(x, y, pressionado)` | o dedo na tela, a cada 20 ms; `false` quando solta |
| `tecla(texto, codigo)` | tecla do teclado físico ou virtual |
| `aviso(nome)` | chegou algo de um serviço pedido em `# avisos:` |
| `fechar()` | a janela vai fechar |
| `fundo()` | a parte de fundo (com `sistema.fundo(true)`) |

**Cabeçalho do arquivo** (comentários no topo):
`# nome:` · `# cor: 0x07E0` · `# periodo: ms` · `# avisos: espnow, mesh, gps, volumes, arquivos` ·
`# orientacao: em_pe | deitado | girar | fixo` - com o teclado físico encaixado
todo app fica deitado, menos `fixo` (sempre em pé: quem usa `camera`)

## `tela` - desenho

Só dentro de `desenhar()` (ou com `tela.alvo(camada)`). Coordenadas da área do app.

| Função | Devolve |
|---|---|
| `tela.largura()` · `tela.altura()` | int (px) |
| `tela.limpar([cor])` | - |
| `tela.texto(txt, x, y [, tam [, cor [, alinhamento]]])` | - |
| `tela.medir(txt [, tam])` | int (largura em px) |
| `tela.cortar(txt, largura [, tam])` | texto cortado com `...` |
| `tela.retangulo(x, y, l, a, cor [, cheio])` | - |
| `tela.arredondado(x, y, l, a, raio, cor [, cheio])` | - |
| `tela.circulo(x, y, r, cor [, cheio])` | - |
| `tela.triangulo(x0, y0, x1, y1, x2, y2, cor [, cheio])` | - |
| `tela.linha(x0, y0, x1, y1, cor)` | - |
| `tela.pixel(x, y, cor)` | - |
| `tela.botao(x, y, l, a, txt [, cor])` | - |
| `tela.dentro(x, y, rx, ry, rl, ra)` | bool (vale fora do `desenhar`) |
| `tela.cor(r, g, b)` | int (cor RGB565) |
| `tela.camada(l, a)` | camada (imagem guardada, até 8 por app) |
| `tela.alvo(c)` · `tela.alvo()` | desenhar na camada / de volta na tela |
| `tela.colar(c, x, y [, rx, ry, rl, ra])` | - |
| `tela.liberar(c)` | - |

Constantes: `tela.ESQUERDA` `CENTRO` `DIREITA` · `tela.PRETO` `BRANCO` `VERMELHO`
`VERDE` `AZUL` `AMARELO` `CIANO` `LARANJA` `CINZA` `CINZA_CLARO` `ROXO` `ROSA`

## `sistema` - tempo, energia, sensores, avisos

| Função | Devolve |
|---|---|
| `sistema.redesenhar()` | - |
| `sistema.periodo([ms])` | int (o período atual) |
| `sistema.acumular([sim])` | bool |
| `sistema.dt()` | real (s desde o quadro anterior) |
| `sistema.ms()` · `sistema.us()` | int (desde o boot) |
| `sistema.agora()` | int (epoch UTC, 0 sem relógio) |
| `sistema.hora()` · `sistema.data()` | `"14:05:09"` · `"14/10/2026"` (ou `""`) |
| `sistema.hora_de(epoch)` | `"14:05"` |
| `sistema.bateria()` | int (%) |
| `sistema.energia()` | `{"pct", "volts", "ma"}` |
| `sistema.imu()` · `sistema.giro()` | `[x, y, z]` (g · graus/s) ou `nil` |
| `sistema.repouso()` | bool (tela apagada) |
| `sistema.recursos()` | `[{"recurso", "quem", "s"}]` - o que o sistema está usando e por quem (a câmera que o script ligou aparece com o nome dele) |
| `sistema.gps()` | `{"fix", "lat", "lon", "alt", "sats"}` ou `nil` |
| `sistema.wifi()` | texto |
| `sistema.rede()` | `{"ssid", "ip", "rssi", "texto"}` |
| `sistema.bip(hz, ms)` | - |
| `sistema.avisar(txt [, nivel])` · `sistema.avisar()` | põe / tira aviso na barra. `nivel`: `"info"` (nada exige a pessoa), `"atencao"` (o padrão) ou `"urgente"` (exige ação); outro texto vale `"atencao"` |
| `sistema.acordado(sim)` | - (tela não apaga) |
| `sistema.log(txt)` | - (log serial) |
| `sistema.fundo(sim [, ms])` · `sistema.fundo()` | liga/desliga · bool |
| `sistema.fundo_estado(txt [, n])` | - (texto da pastilha) |
| `sistema.memoria()` | `{"app", "berry", "orcamento"}` (bytes) |

## `teclado`

| Função | Devolve |
|---|---|
| `teclado.abrir([tipo])` · `teclado.fechar()` | - (tipo do campo: `"texto"` (padrão), `"ascii"`, `"senha"`, `"numero"`, `"busca"`, `"paragrafo"`, `"codigo"`, `"comando"`) |
| `teclado.aberto()` · `teclado.fisico()` | bool |

Constantes: `teclado.ENTER` `APAGAR` `ESC` `TAB` `ESQUERDA` `DIREITA` `CIMA` `BAIXO` `DEL`

## `cartao` - arquivos do app

Só a pasta do app: `/sdcard/apps/Berry/<nome do app>/`. Só nome de arquivo, sem pasta.

| Função | Devolve |
|---|---|
| `cartao.ler(nome)` | texto (até 64 KB) ou `nil` |
| `cartao.gravar(nome, texto)` | bool |
| `cartao.anexar(nome, texto)` | bool |
| `cartao.existe(nome)` | bool |
| `cartao.lista()` | lista de nomes |

## `arquivos` - os discos (vê tudo, altera só a própria pasta)

Fora da pasta do app: só listar e **copiar para dentro**. Dentro: tudo.

| Função | Devolve |
|---|---|
| `arquivos.volumes()` | `[{"raiz", "tipo", "nome", "disponivel"}]` |
| `arquivos.pasta()` | o caminho da pasta do app |
| `arquivos.lista(pasta [, filtro])` | `[{"nome", "tipo", "pasta", "bytes", "data", "caminho"}]` ou `nil` |
| `arquivos.copiar(origem_ou_lista [, subpasta])` | `true` (começou) ou o texto do motivo |
| `arquivos.estado()` | `{"fase", "pct", "itens", "feitos", "atual", "erro", "meu"}` |
| `arquivos.cancelar()` | - |
| `arquivos.ler(caminho)` | texto (até 64 KB) - só na pasta do app |
| `arquivos.apagar(c)` · `renomear(c, nome)` · `criar_pasta(nome)` | bool - só na pasta do app |

## `musica` - o tocador do sistema

| Função | Devolve |
|---|---|
| `musica.pasta()` | nome da pasta de música ou `nil` |
| `musica.pendrive()` | nome do pendrive no USB-A ou `nil` |
| `musica.lista([sub])` | `[{"nome", "titulo", "pasta", "caminho"}]` ou `nil` |
| `musica.tocar(sub, i)` | bool |
| `musica.pausar()` · `proxima()` · `anterior()` · `parar()` | - |
| `musica.ir_para(ms)` | - |
| `musica.estado()` | `{"ativo", "tocando", "titulo", "artista", "album", "pos", "dur", "idx", "n", "pasta", "playlist", "caminho", "aleatorio", "repetir", "erro", "kbps", "hz"}` |
| `musica.espectro()` | lista de `musica.FAIXAS` (32) valores 0..255, ou `nil` |
| `musica.volume([v])` | int 0..100 |
| `musica.cor([c])` | int |
| `musica.aleatorio([b])` | bool |
| `musica.repetir([r])` | int (0 não, 1 fila, 2 a mesma) |
| `musica.playlists()` | `[{"nome", "id"}]` |
| `musica.playlist(id)` | `[{"caminho", "titulo"}]` ou `nil` |
| `musica.playlist_criar(nome)` | id ou `nil` |
| `musica.playlist_apagar(id)` | bool |
| `musica.playlist_por(id, caminho)` · `playlist_tirar(id, caminho)` | bool |
| `musica.playlist_tocar(id [, i])` | bool |
| `musica.capa()` | caminho do JPEG/PNG ou `nil` |
| `musica.duracao(caminho)` | int (ms) |

Caminhos: relativos à pasta de música (`"Rock/x.mp3"`) ou absolutos num disco (`"/usb/Rock/x.mp3"`).

## `idioma` - o idioma da interface

Quem escolhe é a pessoa, em Ajustes > Idioma; o app só lê.

| Função | Devolve |
|---|---|
| `idioma.atual()` | o código: `"pt"`, `"en"`, `"fr"`... |
| `idioma.nome()` | o nome no próprio idioma: `"Português"`, `"English"`... |
| `idioma.tr(texto)` | a frase do sistema no idioma atual (`"Cancelar"` → `"Cancel"`); sem tradução, a mesma |
| `idioma.escolher(pt, en)` | a frase do app: `pt` em português, `en` em qualquer outro idioma |

```berry
tela.botao(20, 20, 300, 80, idioma.escolher("Começar", "Start"), tela.VERDE)
```

## `transferencia` - copiar e colar texto

A mesma área de transferência do sistema: o que se copia no Mesh, no ESP-NOW
ou nas Notas aparece aqui, e o contrário. Um texto só, até 4 KB.

| Função | Devolve |
|---|---|
| `transferencia.copiar(texto)` | `true` se guardou |
| `transferencia.colar()` | o texto copiado (`""` = vazia) |

## `rede` - internet (HTTP e HTTPS)

O script **nunca espera a rede**: `rede.pedir()` só começa o pedido e volta na hora;
o download roda em segundo plano. Olhe `rede.estado()` no `desenhar()` (o app é
redesenhado quando o pedido termina) e leia `rede.resposta()` quando estiver pronto.
Um pedido por vez no aparelho inteiro, e a resposta é só de quem pediu.
Ao fechar o app, o pedido é cancelado e a resposta não lida é descartada.

| Função | Devolve |
|---|---|
| `rede.ip()` | `{"conectado", "ip", "mascara", "gateway", "dns1", "dns2", "ipv6", "mac", "nome"}` ou `nil` sem interface de Wi-Fi |
| `rede.pedir(url [, max_bytes])` | bool - `true` = começou. `url` começa com `http://` ou `https://` (até 255 letras; segue redirecionamentos). `max_bytes`: padrão 64 KB, no máximo 256 KB (o que passar é cortado). `false` = já há um pedido em curso (de qualquer app), há uma resposta não lida de OUTRO app, ou URL inválida. Uma resposta não lida do próprio app é descartada |
| `rede.estado()` | `"parado"`, `"baixando"`, `"pronto"` ou `"erro"` |
| `rede.resposta()` | `{"status", "texto", "tipo", "final", "ms", "erro"}` quando `"pronto"` ou `"erro"`, e **libera** o resultado (o estado volta a `"parado"`); `nil` se não há resposta. `status` 0 = não conectou (`erro` diz por quê); `tipo` = Content-Type; `final` = a URL depois dos redirecionamentos |
| `rede.cancelar()` | - (desiste do pedido, ou joga fora a resposta não lida) |
| `rede.codificar(texto)` | o texto pronto para uma URL (`"a b&c"` → `"a+b%26c"`) |

```berry
# nome: Clima
var temp = ""

def tocar(x, y)                      # um toque começa o pedido
  var q = rede.codificar("Sao Paulo")
  if !rede.pedir("https://wttr.in/" + q + "?format=3") temp = "ocupado" end
end

def desenhar(tudo)
  var e = rede.estado()
  if e == "baixando"
    temp = "buscando..."
  elif e == "pronto" || e == "erro"
    var r = rede.resposta()          # lê e libera
    temp = e == "pronto" ? r["texto"] : r["erro"]
  end
  tela.texto(temp, 20, 20, 3, tela.BRANCO)
end
```

## `ajustes` - as preferências da pessoa

Brilho e volume o script pode mudar (é o que a barra de cima faz); o resto é só leitura.

| Função | Devolve |
|---|---|
| `ajustes.brilho([pct])` | int 0..100 - com argumento aplica na hora e grava. Na escala do olho, a mesma da barra de Ajustes; 0 não apaga a tela (há um mínimo) |
| `ajustes.volume([pct])` | int 0..100 - o mesmo de `musica.volume` |
| `ajustes.tela_segundos()` | int - segundos sem toque até a tela apagar |
| `ajustes.casa()` | `[lat, lon]` ou `nil` se a pessoa não configurou |
| `ajustes.fuso()` | int - minutos a partir do UTC (`-180` = UTC-3) |

## `camera`

| Função | Devolve |
|---|---|
| `camera.ligar([y [, altura]])` · `camera.desligar()` | bool · - |
| `camera.ligada()` | bool |
| `camera.faixa()` | `[y, altura]` ou `nil` |
| `camera.erro()` | texto |
| `camera.foto([nome])` | bool (grava `.bmp` na pasta do app) |
| `camera.foto_estado()` | `""`, `"capturando"`, `"gravando"`, `"ok"`, `"falhou"` |
| `camera.ultima()` | nome do último arquivo |

## `espnow`

| Função | Devolve |
|---|---|
| `espnow.estado()` | `{"codigo", "texto"}` |
| `espnow.pronto()` | bool |
| `espnow.enviar(texto)` | bool |
| `espnow.enviar_para(mac, texto)` | bool - direto para um MAC (`"AA:BB:CC:DD:EE:01"`) |
| `espnow.enviar_todos(texto [, mac])` | bool - em todos os canais (1 a 13); sem `mac`, difusão. O Wi-Fi cai por alguns segundos e volta sozinho |
| `espnow.n()` · `espnow.msg(i)` | int · `{"texto", "minha", "quando", "de", "envio", "espressif"}` |
| `espnow.total()` | int (sobe a cada mensagem) |
| `espnow.janela(sim)` | - |
| `espnow.nao_lidas()` · `espnow.marcar_lidas()` | int · - |
| `espnow.canal()` · `canal_fixo()` · `fixar_canal(c)` | int |
| `espnow.mac()` · `vistos()` · `ultimo()` | texto · int · int (ms) |

## `mesh` (Meshtastic, leitura e mensagem direta)

| Função | Devolve |
|---|---|
| `mesh.estado()` | `{"ativo", "no_ar", "nos", "nao_lidas"}` |
| `mesh.n_msgs()` · `mesh.msg(i)` | int · `{"de", "nome", "texto", "quando", "canal", "direta", "snr", "mqtt", "minha"}` |
| `mesh.total()` | int |
| `mesh.n_nos()` · `mesh.no(i)` | int · `{"num", "id", "nome", "curto", "snr", "idade", "bateria", "saltos", "mqtt", "favorito"[, "lat", "lon"]}` |
| `mesh.meu()` | int (nosso nó) |
| `mesh.enviar(numero, texto)` | bool - mensagem **direta** para um nó (o `num` de `mesh.no(i)`); `true` = entrou na fila de envio |

`mesh.enviar` tem limites, porque um script não sai falando num canal público:
só mensagem direta para **um** nó (nunca difusão), recusa (`false`) o número `0`,
o `0xFFFFFFFF` (difusão) e o próprio nó, texto vazio ou com mais de 200 bytes,
e aceita **no máximo uma a cada 10 s no aparelho inteiro** (somando todos os apps).

O inteiro do Berry aqui tem 32 bits com sinal: número de nó acima de
2147483647 aparece **negativo** em `mesh.no(i)["num"]` e `mesh.meu()` (a
difusão `0xFFFFFFFF` é `-1`). Use o número como veio; escrito por extenso
(`4294967295`) o Berry o corta para 2147483647, que é outro nó.

## `gnss` - satélites do GPS no Grove

| Função | Devolve |
|---|---|
| `gnss.ligar()` · `desligar()` · `ligado()` | bool |
| `gnss.satelites()` | `[{"sis", "prn", "elev", "azim", "snr", "pos", "idade"}]` ou `nil` |
| `gnss.versao()` | int (muda a cada rodada) |
| `gnss.tempo()` | `{"valida", "fix", "ano", "mes", "dia", "hora", "min", "seg"}` (UTC) |

## Biblioteca padrão do Berry disponível

| Módulo / função | Para quê |
|---|---|
| `print`, `str`, `int`, `real`, `bool`, `number`, `size`, `type`, `classname`, `isinstance`, `format`, `assert`, `compile` | funções de base |
| `import string` | `format`, `find`, `split`, `replace`, `count`, `toupper`, `tolower`, `startswith`, `endswith`, `byte`, `char`, `hex`, `tr`, `escape` |
| `import math` | `sin` `cos` `tan` `atan2` `sqrt` `pow` `exp` `log` `log10` `abs` `floor` `ceil` `round` `min` `max` `deg` `rad` `rand` `srand` `pi` ... |
| `import json` | `json.load(texto)` · `json.dump(valor)` |
| `import time` | `time.time()` · `time.dump(t)` · `time.clock()` |
| `import global` | ler/criar variáveis globais por nome |
| `import gc` | `gc.collect()` · `gc.allocated()` |
| `import strict` | obriga declarar variáveis com `var` |

Listas: `push` `pop` `insert` `remove` `find` `size` `resize` `clear` `concat` `reverse` `copy` `keys`.
Mapas: `find` `contains` `insert` `remove` `size` `keys`.

**Não existem aqui:** `open()`, `load("arquivo")`, `save()` (arquivo só pelo
módulo `cartao`) e o módulo `os`. Não use `input()`: não há quem digite no
console de um app (ele devolve `""`). Também não há o módulo `introspect`,
nem `bytes` apontando para um endereço (`bytes(ptr, n)`).

## Limites

- Memória: todas as máquinas Berry somam no máximo **4 MB**; passou, `memory_error`.
- Tempo: **2 s** por evento, **5 s** para abrir, **200 ms** por `fundo()`; passou, `tempo_esgotado` (não é pego por `try/except`).
- Até 16 apps em `apps/Berry/`, 8 camadas por app.
- Conferir no Mac antes de copiar: `testes/checar_be.sh MeuApp.be`.
