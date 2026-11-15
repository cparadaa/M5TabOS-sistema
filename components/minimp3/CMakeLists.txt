# minimp3 (lieff, CC0) - o decodificador de MP3 do tocador do sistema
# (services/tab5_musica). Componente proprio, e nao arquivo solto no main/:
# e codigo de terceiro, com a licenca dele ao lado, e o servico so o usa.
idf_component_register(SRCS "minimp3_impl.c"
                       INCLUDE_DIRS ".")
