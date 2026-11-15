// A CAMERA NO NAVEGADOR DO PC - a parte /camera/ do servidor web (tab5_web),
// no mesmo esquema do Terminal, do Editor e do Mesh.
//
// VIDEO QUADRO A QUADRO, NAO UM FLUXO. A pagina pede um JPEG atras do outro
// (quadro.jpg); cada pedido pega o proximo frame da camera no tamanho do
// video (tab5_camera_pedir_quadro), comprime no codificador JPEG do chip e
// responde. Um fluxo MJPEG infinito prenderia a UNICA tarefa do servidor -
// o Terminal, o Mesh e o Editor parariam de responder enquanto o video
// estivesse aberto; quadro a quadro, eles entram entre um quadro e outro.
//
//   GET quadro.jpg?alta=1   o proximo quadro DEITADO, como o sensor ve: 640x480
//                           (960x720 com 'alta') - a pagina gira para mostrar
//   GET foto.jpg            o quadro inteiro do sensor, 1200x1600 (para baixar)
//
// A CAMERA LIGA SOZINHA no primeiro quadro pedido e DESLIGA
// CAMERA_OCIOSA_MS depois do ultimo (fechar a aba, trocar de app no portal):
// abrir a parte nao liga o sensor. Ela e a do sistema (hal/tab5_camera,
// contagem de referencia): o app Camera aberto no aparelho ao mesmo tempo
// segue funcionando - cada um com o seu tamanho.
#pragma once

// Abre a parte da camera no servidor (/camera/). De qualquer tarefa.
// false = ver tab5_web_abrir.
bool tab5_camera_web_ligar(void);
