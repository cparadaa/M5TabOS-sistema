# libvterm 0.3.3 (emulador de terminal VT100/xterm) - ver ORIGEM.txt.
set(src encoding keyboard mouse parser pen screen state unicode vterm)
list(TRANSFORM src PREPEND "libvterm/src/")
list(TRANSFORM src APPEND ".c")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS "libvterm/include"
                       PRIV_INCLUDE_DIRS "libvterm/src")

# Codigo de terceiros, sem mexer: os avisos dele nao viram erro aqui.
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu99 -Wno-unused-parameter -Wno-sign-compare
                       -Wno-unused-function -Wno-unused-variable -Wno-implicit-fallthrough
                       -Wno-missing-field-initializers -Wno-maybe-uninitialized)
