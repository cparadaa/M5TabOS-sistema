# libssh2 1.11.1 (cliente SSH) com o mbedTLS do ESP-IDF - ver ORIGEM.txt.
#
# CMake proprio, e nao o do port (skuodi/libssh2_esp): aquele aplicava as
# flags (-Wno-error..., -DLIBSSH2_MBEDTLS) no PROJETO INTEIRO pelo
# idf_build_set_property. Aqui tudo e PRIVATE do componente.
set(src agent bcrypt_pbkdf blowfish chacha channel cipher-chachapoly comp crypt
        global hostkey keepalive kex knownhost mac mbedtls misc packet pem
        poly1305 publickey scp session sftp transport userauth
        userauth_kbd_packet version)
list(TRANSFORM src PREPEND "libssh2/src/")
list(TRANSFORM src APPEND ".c")

idf_component_register(SRCS ${src}
                       INCLUDE_DIRS "libssh2/include"
                       PRIV_INCLUDE_DIRS "libssh2/src" "."
                       PRIV_REQUIRES mbedtls esp_netif lwip)

target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H LIBSSH2_MBEDTLS)
# Codigo de terceiros, sem mexer: os avisos dele nao viram erro aqui.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format -Wno-maybe-uninitialized
                       -Wno-uninitialized -Wno-narrowing -Wno-unused-function
                       -Wno-unused-variable -Wno-unused-but-set-variable)
