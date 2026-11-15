# h264_dec (Hiroki Kawakami, MIT) - o decodificador H.264 do servico de video
# (services/tab5_video). Codigo de terceiro, com a licenca ao lado; a origem
# exata esta em ORIGEM.txt e o que mudamos nele, em MUDANCAS.md.
#
# Junta os dois componentes do projeto original (h264_dec e vdec_common): os
# nucleos comuns (vdec_kernels_*) so servem a este decodificador aqui.
idf_component_register(
    SRCS
        "src/bits.c"
        "src/cabac.c"
        "src/cabac_tables.c"
        "src/deblock.c"
        "src/decoder.c"
        "src/inter.c"
        "src/intra.c"
        "src/kernels_c.c"
        "src/macroblock.c"
        "src/params.c"
        "src/rows.c"
        "src/slice.c"
        "src/tables.c"
        "src/transform.c"
        "src/vdec_kernels_c.c"
    INCLUDE_DIRS "include")

# No P4, as rotinas quentes em assembly da extensao PIE (SIMD de 128 bits).
if(IDF_TARGET STREQUAL "esp32p4")
    target_sources(${COMPONENT_LIB} PRIVATE "src/kernels_p4.S" "src/vdec_kernels_p4.S")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE H264_DEC_PIE VDEC_PIE)
endif()

# -O2 como no original (o -Os do resto do sistema custa velocidade aqui).
target_compile_options(${COMPONENT_LIB} PRIVATE -O2)

# Medicao por etapa (bancada 'video h264' imprime): idf.py ... -DH264_DEC_PROFILE=1
if(H264_DEC_PROFILE)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC H264_DEC_PROFILE)
endif()
