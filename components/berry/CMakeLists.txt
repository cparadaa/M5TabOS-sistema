# O INTERPRETADOR BERRY (github.com/berry-lang/berry, MIT - ver berry/LICENSE
# e berry/ORIGEM.txt). berry/ e a copia do upstream; tudo o que e do M5tabOS
# mora em port/ (configuracao, memoria, tabela de modulos) - menos as poucas
# linhas de seguranca listadas no ORIGEM.txt, marcadas "M5tabOS" nos fontes.
#
# AS TABELAS CONSTANTES. O Berry guarda strings e objetos fixos na flash, e
# essas tabelas sao geradas pela ferramenta coc (Python) a partir dos fontes
# e do berry_conf.h, em berry/generate/ (fora do git). Roda na configuracao do
# CMake; se o berry_conf.h mudar, apague berry/generate/ e reconfigure.
set(berry_dir ${CMAKE_CURRENT_SOURCE_DIR}/berry)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    idf_build_get_property(python PYTHON)
    if(NOT EXISTS ${berry_dir}/generate/be_const_strtab.h)
        file(MAKE_DIRECTORY ${berry_dir}/generate)
        execute_process(
            COMMAND ${python} ${berry_dir}/tools/coc/coc -o ${berry_dir}/generate
                    ${berry_dir}/src ${CMAKE_CURRENT_SOURCE_DIR}/port
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/port/berry_conf.h
            WORKING_DIRECTORY ${berry_dir}/tools/coc
            RESULT_VARIABLE coc_res)
        if(NOT coc_res EQUAL 0)
            message(FATAL_ERROR "Berry: o coc nao gerou as tabelas (berry/generate)")
        endif()
    endif()
endif()

file(GLOB berry_src ${berry_dir}/src/*.c)
idf_component_register(SRCS ${berry_src}
                            port/be_port.c port/be_modtab.c port/be_port_m5.c
                       INCLUDE_DIRS port ${berry_dir}/src
                       REQUIRES heap)

# Codigo de terceiros: os avisos dele nao sao nossos para consertar.
target_compile_options(${COMPONENT_LIB} PRIVATE -w)
