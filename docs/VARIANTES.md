# Variantes: um sistema, mais de um binário

O M5tabOS é **um só código** que gera mais de um firmware. O núcleo (kernel,
compositor, serviços, HAL, console) existe uma vez: uma correção feita nele vale
para todas as variantes, porque é o mesmo arquivo. As variantes mudam só **o que
entra** no binário - nunca o código do sistema.

| variante | o que tem | para quê | grava com |
|---|---|---|---|
| **completa** (padrão) | todos os apps em C, inclusive os pessoais e de laboratório | o seu dia a dia | `./gravar.sh` |
| **sistema** | só os apps do sistema | a base pública (a cópia do GitHub, `ferramentas/publicar.sh`) | `./gravar.sh sistema` |
| **m5burner** | uma cópia da **berry**: os apps do sistema + Berry, com o app de exemplo `Ola.be` instalado no primeiro boot | o firmware do M5Burner (`ferramentas/m5burner.sh`), cru: gravar apaga os ajustes | `./gravar.sh m5burner` |
| **berry** | os apps do sistema + apps em [Berry](BERRY.md) (`.be` no cartão, sem compilar) | instalar apps sem compilar | `./gravar.sh berry` |

**Sem decorar comando:** `./gravar.sh` sem nada (ou o botão **Gravar** do
VSCode) pergunta qual variante gravar - Enter repete a última. Para o resto
(compilar todas, M5Burner, lançar versão, publicar no GitHub), o menu
`ferramentas/menu.sh`: dois cliques em **`M5tabOS.command`** no Finder, ou o
botão **Menu** do VSCode.

Cada uma compila na sua pasta: `build/` (completa), `build_<variante>/`, e
`_v3` no fim para o P4 v3.x. Onde cada uma vai parar (GitHub, M5Burner) e o CI
que compila todas a cada push: [GITLAB.md](GITLAB.md).

A versão gravada diz qual é: `0.8.0` e `0.8.0-berry` (em Ajustes, no console e
no log de boot).

## Como uma variante é feita

Um arquivo em `variantes/<nome>.cmake` com quatro linhas:

```cmake
set(M5_BERRY ON)                 # o Berry entra no binário?
set(M5_SO_SISTEMA ON)            # só os apps do sistema (os pessoais ficam de fora sozinhos)?
set(M5_SEM_APPS_VARIANTE "")     # e mais estes apps de fora ("cobra;radar")
set(M5_VARIANTE_ROTULO "berry")  # o sufixo da versão ("" = nenhum)
```

O resto é automático:

- `M5_BERRY OFF`: o componente `components/berry` nem é compilado, os fontes
  `main/services/berry_*` saem do build, e o símbolo `M5_COM_BERRY` vale 0.
- Os apps de fora (os de `M5_SEM_APPS_VARIANTE` e, com `M5_SO_SISTEMA`, todo
  app que não está na lista `m5_apps_sistema` do `main/CMakeLists.txt`) saem
  dos fontes **e** da lista de apps - a pasta continua no repositório, para as
  outras variantes.
- Cada variante compila na sua pasta (`build_<variante>/`, `_v3` para o P4 v3),
  com o `sdkconfig` dentro dela.

## A fronteira: o núcleo não sabe que o Berry existe

O Berry se liga ao sistema por **um ponto de extensão genérico**, os "apps de
fora" (`kernel/app.h`):

```c
void sistema_apps_de_fora_iniciar(void);   // boot: registra os apps do cartão
bool sistema_apps_de_fora_atender(void);   // home: aplica uma releitura
void sistema_apps_de_fora_reler(void);     // o cartão mudou (pendrive)
```

Quem implementa é **um arquivo só**, `main/services/apps_de_fora.cpp`: na
variante berry ele chama os apps Berry; na completa as três funções não fazem
nada. O `core.cpp` e o `tab5_pendrive.cpp` chamam essas funções e mais nada.
O comando `berry` do console é o único outro trecho com `#if M5_COM_BERRY`.

**A regra para não deixar as variantes se separarem:** o que é do sistema vai
para o núcleo, nunca para dentro de `services/berry_*`. Uma melhoria descoberta
por causa do Berry (o texto mais rápido, o colar direto na tela, a serial que
volta depois do pendrive) é do sistema - e todas as variantes ganham.

## A regra de ouro: todo commit compila todas

```bash
ferramentas/compilar_tudo.sh
```

Compila cada variante (com `v3`, também as do P4 v3.x) e confere os apps Berry
de exemplo (`testes/checar_be.sh`). No GitLab, o pipeline faz o mesmo a cada
push. Um commit só está pronto quando **tudo
compilou**. Uma mudança no núcleo que quebra uma variante quebra o sistema.

## Uma variante nova

1. Criar `variantes/<nome>.cmake` (copie um dos que existem).
2. `ferramentas/compilar_tudo.sh` passa a compilá-la sozinho.
3. Se ela liga uma peça opcional nova (como o Berry), a peça se pluga por um
   ponto de extensão do núcleo, como os apps de fora - nunca com `#if` espalhado
   pelo kernel.
