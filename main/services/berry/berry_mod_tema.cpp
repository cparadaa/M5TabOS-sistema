// Modulo Berry 'tema' - as cores e medidas do tema central (tab5_tema.hpp)
// para os apps em Berry. Sao so constantes: o script escreve tema.FUNDO em vez
// de um numero de cor, e acompanha sozinho quando a paleta do sistema mudar.
//
//   tela.limpar(tema.FUNDO)
//   tela.arredondado(x, y, l, a, tema.RAIO_M, tema.SUPERFICIE)
//
// Ver berry_apps.cpp (o registro) e berry_interno.hpp (membro_t).

#include "tab5_tema.hpp"
#include "berry_interno.hpp"

const membro_t BERRY_MOD_TEMA[] = {
  // superficies
  { "FUNDO", nullptr, TEMA_FUNDO },
  { "SUPERFICIE", nullptr, TEMA_SUPERFICIE },
  { "SUPERFICIE_ALTA", nullptr, TEMA_SUPERFICIE_ALTA },
  { "LINHA", nullptr, TEMA_LINHA },
  // texto
  { "TEXTO", nullptr, TEMA_TEXTO },
  { "TEXTO_2", nullptr, TEMA_TEXTO_2 },
  { "TEXTO_3", nullptr, TEMA_TEXTO_3 },
  // destaque
  { "DESTAQUE", nullptr, TEMA_DESTAQUE },
  { "DESTAQUE_SUAVE", nullptr, TEMA_DESTAQUE_SUAVE },
  { "SOBRE_DESTAQUE", nullptr, TEMA_SOBRE_DESTAQUE },
  // estados
  { "SUCESSO", nullptr, TEMA_SUCESSO },
  { "ALERTA", nullptr, TEMA_ALERTA },
  { "PERIGO", nullptr, TEMA_PERIGO },
  { "INFO", nullptr, TEMA_INFO },
  // cores de app
  { "APP_AZUL", nullptr, TEMA_APP_AZUL },
  { "APP_CIANO", nullptr, TEMA_APP_CIANO },
  { "APP_VERDE", nullptr, TEMA_APP_VERDE },
  { "APP_LIMA", nullptr, TEMA_APP_LIMA },
  { "APP_AMARELO", nullptr, TEMA_APP_AMARELO },
  { "APP_LARANJA", nullptr, TEMA_APP_LARANJA },
  { "APP_VERMELHO", nullptr, TEMA_APP_VERMELHO },
  { "APP_ROSA", nullptr, TEMA_APP_ROSA },
  { "APP_ROXO", nullptr, TEMA_APP_ROXO },
  { "APP_GRAFITE", nullptr, TEMA_APP_GRAFITE },
  // cantos, espacos e letras
  { "RAIO_P", nullptr, TEMA_RAIO_P },
  { "RAIO_M", nullptr, TEMA_RAIO_M },
  { "RAIO_G", nullptr, TEMA_RAIO_G },
  { "MARGEM", nullptr, TEMA_MARGEM },
  { "LETRA_LEGENDA", nullptr, TEMA_LETRA_LEGENDA },
  { "LETRA_CORPO", nullptr, TEMA_LETRA_CORPO },
  { "LETRA_SUBTITULO", nullptr, TEMA_LETRA_SUBTITULO },
  { "LETRA_TITULO", nullptr, TEMA_LETRA_TITULO },
  { "LETRA_GRANDE", nullptr, TEMA_LETRA_GRANDE },
  { "TOQUE_MIN", nullptr, TEMA_TOQUE_MIN },
  { nullptr },
};
