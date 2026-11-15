# menu: a base publica do GitHub (so os apps do sistema)
# VARIANTE SISTEMA - a base publica: so os apps do sistema (a lista
# m5_apps_sistema do main/CMakeLists.txt), sem o Berry. E o que a copia do
# GitHub (ferramentas/publicar.sh) compila. Um app pessoal novo fica de fora
# sozinho, sem ninguem lembrar de nada.
#     ./gravar.sh sistema
set(M5_BERRY OFF)
set(M5_SO_SISTEMA ON)
set(M5_SEM_APPS_VARIANTE "")
set(M5_VARIANTE_ROTULO "sistema")
