#!/bin/bash
# Dois cliques aqui no Finder abrem o menu do M5tabOS no Terminal
# (gravar, compilar, M5Burner, lancar versao, publicar). Ver ferramentas/menu.sh.
cd "$(dirname "$0")" || exit 1
exec ferramentas/menu.sh
