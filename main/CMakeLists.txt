# GLOB_RECURSE pega os fontes de main/ E de qualquer subpasta - assim cada
# aplicativo mora na sua propria pasta sem ninguem tocar neste arquivo.
#
# Filtrando por extensao de fonte (em vez do "*.*" que o PlatformIO gera por
# padrao) para nao jogar headers e .yml dentro de SRCS.
#
# CONFIGURE_DEPENDS faz o ninja reavaliar o glob a cada build, para um .cpp novo
# entrar sem precisar apagar .pio/build. MAS ele e proibido em "script mode", e
# o IDF roda este arquivo em script mode (cmake -P) para descobrir as
# dependencias do componente antes de configurar. CMAKE_SCRIPT_MODE_FILE so
# existe nessa passagem - e o jeito de distinguir as duas.
if(CMAKE_SCRIPT_MODE_FILE)
    FILE(GLOB_RECURSE app_sources
         ${CMAKE_CURRENT_SOURCE_DIR}/*.c
         ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
else()
    FILE(GLOB_RECURSE app_sources CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/*.c
         ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
endif()

# APPS FORA DESTE BUILD, sem apagar a pasta: -DM5_SEM_APPS="cobra;radar" tira
# as pastas main/app_cobra/ e main/app_radar/ dos fontes e da lista de apps.
# E o que o ferramentas/m5burner.sh usa para o firmware publicado nao levar o
# que ainda e projeto em andamento.
set(M5_SEM_APPS "" CACHE STRING "Apps (nome da pasta sem app_) fora deste build")

# OS APPS DO SISTEMA, na ordem de fabrica da home (ver "A LISTA DE
# APLICATIVOS" abaixo). E tambem o que a variante sistema e a berry levam, e o
# que ferramentas/publicar.sh copia para o repositorio publico.
set(m5_apps_sistema mesh espnow config arquivos notas musica video terminal editor tarefas energia imu camera exemplo)

# QUEM FICA DE FORA: os de -DM5_SEM_APPS, os da VARIANTE (variantes/*.cmake,
# ver docs/VARIANTES.md) e, com M5_SO_SISTEMA, todo app que nao e do sistema.
set(m5_apps_fora ${M5_SEM_APPS} ${M5_SEM_APPS_VARIANTE})
# O Editor e de Berry: so existe na variante que tem o Berry.
if(NOT M5_BERRY)
    list(APPEND m5_apps_fora editor)
endif()
if(M5_SO_SISTEMA)
    file(GLOB m5_todas LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/app_*)
    foreach(d ${m5_todas})
        get_filename_component(n ${d} NAME)
        string(REGEX REPLACE "^app_" "" n ${n})
        if(IS_DIRECTORY ${d} AND NOT n IN_LIST m5_apps_sistema)
            list(APPEND m5_apps_fora ${n})
        endif()
    endforeach()
endif()
foreach(n ${m5_apps_fora})
    list(FILTER app_sources EXCLUDE REGEX "/main/app_${n}/")
endforeach()

# O BERRY SO NA VARIANTE QUE O PEDE: sem ele, services/berry_* e services/berry/
# (os modulos) nem compilam (o componente components/berry tambem sai - ver o CMakeLists.txt de cima),
# e services/apps_de_fora.cpp vira a versao vazia (M5_COM_BERRY=0).
if(NOT M5_BERRY)
    list(FILTER app_sources EXCLUDE REGEX "/main/services/berry[_/]")
endif()

# INCLUDE_DIRS "." poe o proprio main/ no caminho de busca. Sem isso, um
# arquivo em main/app_azimutal/ teria que escrever #include "../app.h"; com isso,
# escreve #include "app.h" de qualquer profundidade - MAS SO enquanto app.h
# estiver direto dentro de "." (main/). NAO E RECURSIVO: cada entrada aqui e
# UMA pasta exata, nao uma arvore inteira - diferente do GLOB_RECURSE acima,
# que pega .cpp em qualquer profundidade sozinho.
#
# kernel/, hal/ e services/ nasceram (reorganizacao dos
# arquivos que ate entao ficavam soltos direto em main/, agrupando por
# camada - ver o proprio nome das pastas). Os headers de la (app.h,
# tab5_grove.hpp, tab5_mesh.hpp...) pararam de ser achados por #include
# "app.h" batendo so em "." - por isso cada pasta nova entra aqui, e nenhum
# #include em nenhum .cpp do projeto precisou mudar.
#
# compartilhado/ traz o contrato de mensagens com o firmware do C6. Ele NAO
# entra em SRCS - e so header, e o glob acima nem passa por la.
# LDFRAGMENTS: linker.lf manda o .bss dos apps e servicos para a PSRAM (ver o
# cabecalho daquele arquivo para a regra de quem pode entrar).
# O CARIMBO DE COMPILACAO ("Compilado em", em Ajustes > Sobre). Era o
# __DATE__/__TIME__ dentro do app_config.cpp - e ele so muda quando AQUELE
# arquivo recompila: mexer em qualquer outra parte do sistema deixava a data
# velha. Agora um .c gerado no build (main/carimbo.cmake) e reescrito a cada
# 'idf.py build'; a tarefa que o gera roda sempre, antes da libmain.
set(m5_carimbo_c "${CMAKE_CURRENT_BINARY_DIR}/m5tabos_compilado.c")
if(NOT CMAKE_SCRIPT_MODE_FILE)
    execute_process(COMMAND ${CMAKE_COMMAND} -DOUT=${m5_carimbo_c}
                            -P ${CMAKE_CURRENT_SOURCE_DIR}/carimbo.cmake)
    list(APPEND app_sources ${m5_carimbo_c})
endif()

# A LISTA DE APLICATIVOS SAI DAS PASTAS. Toda pasta main/app_<nome>/ e um app,
# e o app_t dela se chama APP_<NOME> (em maiusculas): app_mesh -> APP_MESH. O
# kernel/apps.cpp inclui o arquivo gerado aqui. Assim um app entra copiando a
# pasta e sai apagando a pasta - ninguem edita lista nenhuma, e um build sem
# os apps pessoais (a copia publica) compila do mesmo jeito.
#
# Ordem de fabrica: os apps do sistema na ordem abaixo, depois os outros em
# ordem alfabetica. A ordem que vale na home e a do usuario (gravada na NVS
# por nome - ver ajustes_ordem_apps), entao isto so decide o primeiro boot.
set(m5_apps_inc "${CMAKE_CURRENT_BINARY_DIR}/apps_lista.inc")
if(NOT CMAKE_SCRIPT_MODE_FILE)
    file(GLOB m5_app_dirs LIST_DIRECTORIES true CONFIGURE_DEPENDS
         ${CMAKE_CURRENT_SOURCE_DIR}/app_*)
    set(m5_apps_achados "")
    foreach(d ${m5_app_dirs})
        if(IS_DIRECTORY ${d})
            get_filename_component(n ${d} NAME)
            string(REGEX REPLACE "^app_" "" n ${n})
            if(NOT n IN_LIST m5_apps_fora)
                list(APPEND m5_apps_achados ${n})
            endif()
        endif()
    endforeach()
    list(SORT m5_apps_achados)
    set(m5_apps_ordem "")
    foreach(n ${m5_apps_sistema})
        if(n IN_LIST m5_apps_achados)
            list(APPEND m5_apps_ordem ${n})
        endif()
    endforeach()
    foreach(n ${m5_apps_achados})
        if(NOT n IN_LIST m5_apps_ordem)
            list(APPEND m5_apps_ordem ${n})
        endif()
    endforeach()
    set(m5_txt "// Gerado por main/CMakeLists.txt a partir das pastas main/app_* - nao editar.\n")
    foreach(n ${m5_apps_ordem})
        string(TOUPPER ${n} N)
        string(APPEND m5_txt "APP(APP_${N})\n")
    endforeach()
    if(EXISTS "${m5_apps_inc}")
        file(READ "${m5_apps_inc}" m5_antes)
    endif()
    if(NOT "${m5_antes}" STREQUAL "${m5_txt}")
        file(WRITE "${m5_apps_inc}" "${m5_txt}")
    endif()
endif()

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "." "kernel" "hal" "services" "../compartilhado"
                       EMBED_TXTFILES "../idiomas/en.po"
                       LDFRAGMENTS "linker.lf")

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_custom_target(m5tabos_carimbo
                      COMMAND ${CMAKE_COMMAND} -DOUT=${m5_carimbo_c}
                              -P ${CMAKE_CURRENT_SOURCE_DIR}/carimbo.cmake
                      BYPRODUCTS ${m5_carimbo_c}
                      VERBATIM)
    add_dependencies(${COMPONENT_LIB} m5tabos_carimbo)
endif()

# O SCCB DA CAMERA PASSA PELA TRAVA DO I2C INTERNO. O esp_video
# cria o canal do sensor com sccb_new_i2c_io(); o link desvia essa chamada para
# hal/tab5_sccb.cpp, que usa as transacoes do M5GFX (serializadas por porta).
# Ver o aviso grande naquele arquivo - sem editar componente de terceiros.
# O --undefined obriga o linker a puxar hal/tab5_sccb.o da libmain: ela e lida
# ANTES da esp_video, e sem isto ninguem ainda pediu o __wrap_ quando ela passa.
# O DMA2D PURO DO PROPRIO DRIVER DPI (esp_async_fbcpy): copia de retangulo em
# linha corrida, sem o motor de escala do PPA. O header e privado do esp_lcd;
# o codigo ja esta na biblioteca dele, so falta o caminho do .h.
target_include_directories(${COMPONENT_LIB} PRIVATE "$ENV{IDF_PATH}/components/esp_lcd/priv_include")

# O PORTAL DO NAVEGADOR (services/tab5_web_portal): a raiz do servidor web.
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/services/tab5_web_portal.html" TEXT)
# A CAMERA NO NAVEGADOR (services/tab5_camera_web): servida da flash.
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/services/tab5_camera_web.html" TEXT)
# A PAGINA DO MESH NO NAVEGADOR (services/tab5_mesh_web): servida da flash.
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/services/tab5_mesh_web.html" TEXT)
# O TERMINAL NO NAVEGADOR (services/tab5_terminal_web): a pagina e o xterm.js
# comprimido (services/xterm/ORIGEM.txt) - o PC nao precisa de internet.
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/services/tab5_terminal_web.html" TEXT)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/services/xterm/xterm.js.gz" BINARY)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/services/xterm/xterm.css.gz" BINARY)
target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/services/xterm/addon-fit.js.gz" BINARY)

# Quem precisa saber se o Berry esta no binario (services/apps_de_fora.cpp,
# o comando 'berry' do console) pergunta por M5_COM_BERRY.
if(M5_BERRY)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE M5_COM_BERRY=1)
    # O APP DE EXEMPLO VAI DENTRO DO FIRMWARE: no primeiro boot com um cartao
    # sem apps/Berry, o sistema o grava la (services/berry_apps.cpp) - quem
    # grava pelo M5Burner ja comeca com um app Berry na tela inicial.
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/../berry_exemplos/Ola.be" TEXT)
    # A PAGINA DO EDITOR WEB (services/berry_web): servida da flash, inteira.
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_SOURCE_DIR}/services/berry_web.html" TEXT)
else()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE M5_COM_BERRY=0)
endif()

# Onde mora o apps_lista.inc gerado la em cima.
target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# O TINYUSB LE A CONFIGURACAO DAQUI. A biblioteca procura "tusb_config.h"
# pelo nome; o arquivo mora em main/ (services/tab5_pendrive.cpp tambem o
# usa) e esta pasta entra so no caminho de busca dela. O nome do componente
# muda se um dia o TinyUSB vier de copia local em vez do registro.
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_nome tinyusb)
else()
    set(tinyusb_nome espressif__tinyusb)
endif()
idf_component_get_property(tusb_lib ${tinyusb_nome} COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=sccb_new_i2c_io"
                                                 "-Wl,--undefined=__wrap_sccb_new_i2c_io")

# O CANAL DE DMA DA TELA E APAGADO PARADO (hal/tab5_dma.cpp): o
# esp_lcd_panel_del do painel DPI apaga o canal do DW-GDMA ainda rodando, e com
# a camera mantendo o grupo de pe a tela que acorda caia ao reusar o canal.
# E A INTERRUPCAO DESSE DMA NASCE NO NIVEL 3 E SEMPRE NO NUCLEO 1 (a piscada
# azul: a ISR atrasada deixava o painel um quadro sem imagem).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=dw_gdma_del_channel"
                                                 "-Wl,--wrap=dw_gdma_new_channel"
                                                 "-Wl,--wrap=dw_gdma_channel_register_event_callbacks"
                                                 "-Wl,--undefined=__wrap_dw_gdma_del_channel"
                                                 "-Wl,--undefined=__wrap_dw_gdma_new_channel"
                                                 "-Wl,--undefined=__wrap_dw_gdma_channel_register_event_callbacks")

# PROTECAO DO BRANCO DA CAMERA. O esp_video pega a lista de algoritmos do ISP
# em esp_ipa_pipeline_get_config(); o desvio devolve a mesma lista com o IPA
# proprio no fim (hal/tab5_camera_ipa.c, ver o porque la). Mesmo --undefined:
# e ele que puxa o .o, e com ele a entrada na secao .esp_ipa_detect.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_ipa_pipeline_get_config"
                                                 "-Wl,--undefined=__wrap_esp_ipa_pipeline_get_config")

# MARCOS DE TROCA DE CLOCK. Toda troca de frequencia do esp_pm passa
# por rtc_clk_cpu_freq_set_config_fast() - e antes dela o IDF reajusta o
# barramento da PSRAM. Desviada para hal/tab5_marcos.cpp, que anota o instante
# para o monitor cruzar com o frame atrasado. Mesmo --undefined do SCCB.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=rtc_clk_cpu_freq_set_config_fast"
                                                 "-Wl,--undefined=__wrap_rtc_clk_cpu_freq_set_config_fast")

# Mais suspeitos do frame atrasado: setores do cartao e sincronia de cache
# grande (hal/tab5_marcos.cpp).
# O C6 DORMINDO ASSOCIADO (hal/tab5_c6_sono.cpp): na remontagem do SDIO o
# esp_hosted pergunta se o host voltou de um deep sleep - e so ai NAO reseta o
# C6. O desvio responde "voltei por GPIO" enquanto o enlace e remontado.
# O esp_event_post engole o STA_START repetido do rearme na remontagem
# (senao: netif_add duplicado e panic no lwIP).
# O eh_host_port_restart_host nao reinicia na falha esperada da desmontagem
# (o C6 some antes de o deinit parar a leitura).
# O eh_host_port_sdio_wait_slave_intr deixa a tarefa sdio_read estacionar
# antes do deinit (apagada a forca em transito, ela derrubava o sistema).
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=eh_host_port_wakeup_reason_get"
                                                 "-Wl,--wrap=esp_wifi_remote_channel_rx"
                                                 "-Wl,--wrap=esp_wifi_internal_tx"
                                                 "-Wl,--wrap=esp_wifi_internal_tx_by_ref"
                                                 "-Wl,--wrap=eh_host_feat_rpc_request"
                                                 "-Wl,--wrap=eh_host_feat_rpc_request_sync"
                                                 "-Wl,--wrap=eh_host_feat_rpc_request_sync_best_effort"
                                                 "-Wl,--wrap=eh_host_feat_rpc_send"
                                                 "-Wl,--wrap=esp_event_post"
                                                 "-Wl,--wrap=eh_host_port_restart_host"
                                                 "-Wl,--wrap=eh_host_port_sdio_wait_slave_intr"
                                                 "-Wl,--undefined=__wrap_eh_host_port_wakeup_reason_get")

target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=sdmmc_read_sectors"
                                                 "-Wl,--wrap=sdmmc_write_sectors"
                                                 "-Wl,--wrap=esp_cache_msync"
                                                 "-Wl,--undefined=__wrap_sdmmc_read_sectors"
                                                 "-Wl,--undefined=__wrap_sdmmc_write_sectors"
                                                 "-Wl,--undefined=__wrap_esp_cache_msync")

# O HEAP DA PSRAM NO BOOT (tab5_heap_psram.cpp): toda reserva grande na PSRAM
# confere o cabecalho do heap antes - e onde o M5.begin caia.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=heap_caps_calloc"
                                                 "-Wl,--undefined=__wrap_heap_caps_calloc")

