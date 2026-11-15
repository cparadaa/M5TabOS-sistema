// APPS EM BERRY - um arquivo .be no cartao vira um app na tela inicial.
//
// ONDE: /sdcard/apps/Berry/<nome>.be (a pasta e criada no boot). Lidos no
// boot e de novo quando o cartao volta do pendrive (ou 'berry reler' no
// console): o app novo aparece na tela inicial sem reiniciar.
//
// O CABECALHO (opcional, nas primeiras linhas do arquivo):
//     # nome: Contador          o nome sob o icone (padrao: o nome do arquivo)
//     # cor: 0x07FF             a cor do icone, RGB565 (padrao: ciano)
//     # periodo: 1000           redesenhar sozinho a cada N ms (padrao: 0, so quando pedir)
//     # avisos: espnow, mesh      redesenhar NA HORA quando o servico publica (e aviso(nome))
//
// AS FUNCOES QUE O SISTEMA CHAMA (todas opcionais):
//     abrir()                   o app abriu (depois do codigo de fora das funcoes rodar)
//     desenhar(tudo)            desenhe a tela inteira: a area chega limpa (preta) e o
//                               quadro vai ao ar de uma vez, sem piscar. Com
//                               sistema.acumular(true) ela NAO chega limpa: tudo =
//                               false pede so o que mudou (camadas: tela.camada)
//     tocar(x, y)               clique, relativo a area do app
//     dedo(x, y, pressionado)   o dedo por nivel (arrasto), e uma vez ao sair
//     tecla(texto, codigo)      teclado fisico OU o virtual do sistema (teclado.abrir)
//     aviso(nome)               um servico de '# avisos:' publicou ("espnow", "mesh", "gps")
//     fechar()                  a janela vai fechar
//     fundo()                   a PARTE DE FUNDO: com sistema.fundo(true [, ms]) o sistema
//                               a chama a cada ms (padrao 1000), com a janela aberta ou
//                               fechada - a mesma maquina, as mesmas variaveis. Sem tela,
//                               teclado, camera nem bip; prazo de 200 ms por chamada
//
// OS MODULOS (ver docs/BERRY.md): 'tela' (desenho, so dentro de desenhar),
// 'sistema' (redesenho, periodo, hora, energia, IMU, GPS, rede, aviso, bip...),
// 'teclado' (o virtual do sistema), 'cartao' (arquivos na pasta do app),
// 'espnow', 'mesh', 'camera' e 'gnss' (os satelites do GPS). A lista tambem esta em testes/checar_be.c e
// testes/berry_falso.be - mudou aqui, muda la.
//
// CADA APP TEM A SUA MAQUINA: criada ao abrir, apagada ao fechar - um app nao
// enxerga as variaveis de outro, e fechar devolve toda a memoria. Com o fundo
// rodando, fechar a janela NAO apaga: a maquina vive ate o fundo parar (e a
// coleta de lixo roda na hora em que a janela fecha). Tudo roda na
// tarefa do Berry (berry_vm.hpp), com prazo: um laco infinito vira erro na
// tela do app, nao trava o aparelho.
#pragma once

#include "app.h"

extern "C" {
#include "berry.h"
}

#define BERRY_APPS_MAX 16
#define BERRY_PASTA    "Berry"   // /sdcard/apps/Berry

// Le a pasta e registra cada .be como app. No boot, depois do tab5_sd_iniciar
// e antes da home montar a ordem dos icones.
void berry_apps_iniciar(void);

// Rele a pasta. De qualquer tarefa MENOS a interface (le o cartao); a troca
// dos icones acontece depois, em berry_apps_atender. Sem cartao, nada muda.
void berry_apps_reler(void);

// Na interface, com a tela inicial no ar: aplica a ultima releitura. true =
// a lista mudou (quem chama remonta a ordem dos icones e redesenha).
bool berry_apps_atender(void);

// O app_t e de um app Berry? (a tela inicial so oferece Remover a estes)
bool berry_apps_eh_meu(const app_t* app);

// REMOVE o app: em segundo plano, para a parte de fundo dele, manda o .be e a
// pasta de dados (apps/Berry/<nome>/) para a lixeira do cartao - recuperavel
// pelo Arquivos ate esvaziar - e rele a pasta. Da interface (nao bloqueia).
void berry_apps_remover(const app_t* app);

// Console ('berry tempos'): quanto custa a entrega a tarefa do Berry e quanto
// o script leva em cada evento. 'zerar' recomeca a contagem.
void berry_apps_tempos(bool zerar);

// Console ('berry mem'): o que cada maquina viva usa (apps com a janela ou o
// fundo, e a do console, se houver), as camadas, e o total contra o
// orcamento do Berry (berry_mem.h).
void berry_apps_mem(bvm* console);

// Poe os modulos 'tela' e 'sistema' numa maquina nova (berry_vm_nova chama).
void berry_modulos_registrar(bvm* vm);
