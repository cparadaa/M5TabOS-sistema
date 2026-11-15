cmake_minimum_required(VERSION 3.16.0)

# A VERSAO DO SISTEMA mora em version.txt (uma linha, SemVer) e so muda pelo
# ferramentas/lancar.sh - as regras estao em docs/VERSIONAMENTO.md.
#
# O numero que vai para o binario (esp_app_get_description()->version, o que
# aparece em Ajustes, no console e no log de boot) diz tambem DE ONDE o build
# saiu:
#   0.8.0              - exatamente o commit da tag v0.8.0: a versao lancada
#   0.8.0+12.a1b2c3d   - 12 commits depois dela, no commit a1b2c3d
# Sem git (copia solta do codigo), fica so o numero do version.txt.
#
# LIDO NA CONFIGURACAO, nao a cada build: um commit novo refaz a configuracao
# (CMAKE_CONFIGURE_DEPENDS no logs/HEAD), mas mudanca ainda nao commitada nao
# aparece no numero - o "+N.hash" e "construido a partir de", nao "identico a".
file(STRINGS "${CMAKE_SOURCE_DIR}/version.txt" M5TABOS_VER LIMIT_COUNT 1)
string(STRIP "${M5TABOS_VER}" M5TABOS_VER)
if(NOT M5TABOS_VER MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "version.txt deve ter uma versao X.Y.Z, tem '${M5TABOS_VER}'")
endif()
set(PROJECT_VER "${M5TABOS_VER}")

find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --tags --long --match "v[0-9]*"
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE M5TABOS_DESC OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET RESULT_VARIABLE M5TABOS_GIT_RES)
  if(M5TABOS_GIT_RES EQUAL 0 AND M5TABOS_DESC MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)-([0-9]+)-g([0-9a-f]+)$")
    set(M5TABOS_TAG "${CMAKE_MATCH_1}")
    set(M5TABOS_N   "${CMAKE_MATCH_2}")
    set(M5TABOS_SHA "${CMAKE_MATCH_3}")

    # A VERSAO SO ANDA PARA FRENTE. version.txt abaixo da ultima tag e um
    # erro de mao (merge, revert, copia velha): pararia o build antes de um
    # binario "0.6" sair depois do 0.7 lancado.
    if(M5TABOS_VER VERSION_LESS M5TABOS_TAG)
      message(FATAL_ERROR "version.txt (${M5TABOS_VER}) esta ABAIXO da ultima versao lancada (v${M5TABOS_TAG}).")
    endif()
    if(NOT M5TABOS_N EQUAL 0)
      set(PROJECT_VER "${M5TABOS_VER}+${M5TABOS_N}.${M5TABOS_SHA}")
    endif()
  endif()

  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --absolute-git-dir
                  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                  OUTPUT_VARIABLE M5TABOS_GITDIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(EXISTS "${M5TABOS_GITDIR}/logs/HEAD")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${M5TABOS_GITDIR}/logs/HEAD")
  endif()
endif()
message(STATUS "M5tabOS ${PROJECT_VER}")

# REVISAO DO CHIP (ver sdkconfig.defaults.chip_v1 / _v3). O P4 v1.x e o v3.x
# pedem binarios diferentes. Cada revisao compila na SUA pasta de build, com o
# sdkconfig DENTRO dela - um sdkconfig gerado para uma revisao nao pode ser
# reaproveitado pela outra (as chaves de revisao so valem na primeira vez):
#     idf.py build                            -> build/,    P4 v1.x (padrao)
#     idf.py -B build_v3 -DM5_CHIP=v3 build   -> build_v3/, P4 v3.x
set(M5_CHIP "v1" CACHE STRING "Revisao do ESP32-P4: v1 (Tab5 ate aqui) ou v3")
if(NOT M5_CHIP MATCHES "^v[13]$")
  message(FATAL_ERROR "M5_CHIP deve ser v1 ou v3, e '${M5_CHIP}'")
endif()
set(SDKCONFIG_DEFAULTS "${CMAKE_SOURCE_DIR}/sdkconfig.defaults;${CMAKE_SOURCE_DIR}/sdkconfig.defaults.chip_${M5_CHIP}")
message(STATUS "M5tabOS para ESP32-P4 ${M5_CHIP}.x")

# A VARIANTE (ver docs/VARIANTES.md). O NUCLEO E UM SO: as variantes mudam apenas
# QUAIS apps entram e se o Berry entra - nunca o codigo do sistema. Uma
# correcao feita uma vez vale para todas.
#     idf.py build                                      -> build/,       completa
#     idf.py -B build_berry -DM5_VARIANTE=berry build   -> build_berry/, berry
set(M5_VARIANTE "completa" CACHE STRING "Variante do sistema: completa ou berry (variantes/*.cmake)")
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/variantes/${M5_VARIANTE}.cmake")
  message(FATAL_ERROR "M5_VARIANTE '${M5_VARIANTE}' nao existe em variantes/")
endif()
include("${CMAKE_SOURCE_DIR}/variantes/${M5_VARIANTE}.cmake")
# Cache (FORCE): os componentes (main/) leem daqui - e a variante que manda.
set(M5_BERRY ${M5_BERRY} CACHE BOOL "Berry dentro do binario (definido pela variante)" FORCE)
set(M5_SEM_APPS_VARIANTE "${M5_SEM_APPS_VARIANTE}" CACHE STRING "Apps fora por causa da variante" FORCE)
set(M5_SO_SISTEMA ${M5_SO_SISTEMA} CACHE BOOL "So os apps do sistema (definido pela variante)" FORCE)
if(NOT M5_BERRY)
  set(EXCLUDE_COMPONENTS berry)   # o interpretador nem e compilado
endif()
if(M5_VARIANTE_ROTULO)
  set(PROJECT_VER "${PROJECT_VER}-${M5_VARIANTE_ROTULO}")   # Ajustes/console/log dizem qual e
endif()
message(STATUS "M5tabOS variante ${M5_VARIANTE} (Berry: ${M5_BERRY})")

# Fora do padrao (outra revisao do chip ou outra variante), o sdkconfig mora
# DENTRO da pasta de build - um sdkconfig e de UMA configuracao.
if(NOT M5_CHIP STREQUAL "v1" OR NOT M5_VARIANTE STREQUAL "completa")
  set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# AVISO E AVISO, EM QUALQUER ESP-IDF. O ESP-IDF oficial (o da imagem do CI,
# espressif/idf) compila com -Wall -Werror -Wextra e troca o -Werror por
# -Werror=all: todo aviso vira erro. O que vem com o PlatformIO (o da maquina
# de desenvolvimento) traz essas tres opcoes COMENTADAS em tools/cmake/
# build.cmake - o mesmo codigo compilava aqui e quebrava no CI, com avisos de
# snprintf que "pode cortar" (format-truncation) em arquivos antigos. Tirando
# o -Werror da lista ANTES do project(), a troca nao acontece: os avisos
# continuam aparecendo (-Wall/-Wextra ficam), so nao param o build - e os dois
# ESP-IDF compilam igual. No do PlatformIO isto nao muda nada.
idf_build_get_property(m5_opcoes COMPILE_OPTIONS)
list(REMOVE_ITEM m5_opcoes "-Werror" "-Werror=all")
idf_build_set_property(COMPILE_OPTIONS "${m5_opcoes}")

project(M5tabOS)
