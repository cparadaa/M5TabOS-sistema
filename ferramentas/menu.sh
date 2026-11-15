#!/bin/bash
# O MENU DO M5tabOS - tudo o que se faz com o projeto, sem decorar comando.
#
#   ferramentas/menu.sh     (ou dois cliques no M5tabOS.command, no Finder,
#                            ou o botao "Menu" na barra do VSCode)
#
# Cada opcao chama o script de sempre - o menu nao faz nada que eles nao
# facam; ele so lembra quais existem. Opcao cujo script nao esta nesta copia
# (a publica nao tem o publicar.sh, por exemplo) nao aparece.
cd "$(dirname "$0")/.." || exit 1

PUBLICO="../M5TabOS-sistema"   # a copia do GitHub (ver docs/GITLAB.md)

pausa() { printf "\n(Enter para voltar ao menu) "; read -r _; }

publicar() {
    if [ ! -d "$PUBLICO/.git" ]; then
        echo "Nao achei a copia publica em $PUBLICO."; return
    fi
    echo ">>> 1/3 trazendo o que ja esta no GitHub"
    git -C "$PUBLICO" pull --ff-only || { echo "o pull falhou - resolva antes de publicar"; return; }
    echo ">>> 2/3 gerando a copia (e compilando)"
    ferramentas/publicar.sh "$PUBLICO" --compilar || return
    echo ">>> 3/3 o que vai para o GitHub:"
    git -C "$PUBLICO" show --stat --format="%h %s" | head -40
    printf "\nEnviar para o GitHub agora? Digite 'sim' para enviar: "
    read -r R
    if [ "$R" = "sim" ]; then
        git -C "$PUBLICO" push && echo ">>> publicado"
    else
        echo "Nao enviado. O commit ficou na copia local; envie depois com:"
        echo "  git -C $PUBLICO push"
    fi
}

while true; do
    clear 2>/dev/null
    VERSAO="$(head -1 version.txt 2>/dev/null)"
    RAMO="$(git branch --show-current 2>/dev/null)"
    echo "M5tabOS $VERSAO   (branch: ${RAMO:-?})"
    echo
    OPC=()
    item() { OPC+=("$1"); printf "  %d) %s\n" "${#OPC[@]}" "$2"; }
    item gravar   "Gravar no Tab5 (escolhe a variante)"
    item monitor  "So o monitor serial (sem gravar)"
    [ -x ferramentas/compilar_tudo.sh ] && item tudo     "Compilar todas as variantes (o que o CI faz)"
    [ -x testes/checar_be.sh ]          && item berry    "Conferir os apps Berry de exemplo"
    [ -x ferramentas/m5burner.sh ]      && item m5burner "Gerar o .bin do M5Burner"
    [ -x ferramentas/lancar.sh ]        && item lancar   "Lancar uma versao nova"
    [ -x ferramentas/publicar.sh ]      && item publicar "Publicar a base no GitHub"
    echo "  0) Sair"
    printf "\nOpcao: "
    read -r N
    [ "$N" = "0" ] && exit 0
    [[ "$N" =~ ^[0-9]+$ ]] && [ "$N" -ge 1 ] && [ "$N" -le "${#OPC[@]}" ] || continue
    echo
    case "${OPC[$((N-1))]}" in
        gravar)   ./gravar.sh ;;
        monitor)  ( source ./ambiente.sh > /dev/null && idf.py -p "$(ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | head -1)" monitor ) ;;
        tudo)     ferramentas/compilar_tudo.sh; pausa ;;
        berry)    testes/checar_be.sh; pausa ;;
        m5burner)
            printf "Variante (Enter = m5burner; ou berry): "; read -r V
            printf "Chip P4 (Enter = v1; ou v3): "; read -r C
            ferramentas/m5burner.sh ${V:-m5burner} ${C:-v1}; pausa ;;
        lancar)
            echo "patch = so correcoes   minor = algo novo   (regras em docs/VERSIONAMENTO.md)"
            printf "Tipo (patch/minor): "; read -r T
            [ -n "$T" ] && ferramentas/lancar.sh "$T"
            echo; echo "Para enviar ao GitLab: git push origin main --tags"; pausa ;;
        publicar) publicar; pausa ;;
    esac
done
