#!/bin/sh
# Disinstallazione di ForgeCAD.
#
#   packaging/linux/uninstall-desktop-integration.sh            integrazione dell'utente corrente
#   packaging/linux/uninstall-desktop-integration.sh --purge    anche le impostazioni (~/.config/ForgeCAD)
#   sudo packaging/linux/uninstall-desktop-integration.sh --system [prefisso]
#                                                               installazione di sistema (cmake --install),
#                                                               prefisso predefinito /usr/local
#
# Per l'utente toglie quello che ha messo install-desktop-integration.sh: voce
# del menu, icone, tipo MIME dei .prt e le associazioni a forgecad.desktop in
# mimeapps.list (copia di sicurezza accanto, *.forgecad-bak). Il binario e i
# documenti non si toccano.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SIZES="16 24 32 48 64 128 256 512"

refresh() {  # $1 = cartella share
    update-mime-database "$1/mime" >/dev/null 2>&1 || true
    update-desktop-database "$1/applications" >/dev/null 2>&1 || true
    gtk-update-icon-cache -f -t "$1/icons/hicolor" >/dev/null 2>&1 || true
    if command -v kbuildsycoca6 >/dev/null 2>&1; then kbuildsycoca6 >/dev/null 2>&1 || true; fi
}

remove_icons() {  # $1 = cartella hicolor
    for size in $SIZES; do
        rm -f "$1/${size}x${size}/apps/forgecad.png" "$1/${size}x${size}/mimetypes/application-x-forgecad-part.png"
    done
    rm -f "$1/scalable/apps/forgecad.svg" "$1/scalable/mimetypes/application-x-forgecad-part.svg" 2>/dev/null || true
}

# Toglie forgecad.desktop da tutte le associazioni di un mimeapps.list; le
# righe che restano senza programmi spariscono.
clean_associations() {
    [ -f "$1" ] || return 0
    grep -q 'forgecad\.desktop' "$1" || return 0
    cp "$1" "$1.forgecad-bak"
    awk -F= '
        /^[^#[][^=]*=/ {
            key = $1
            value = substr($0, length(key) + 2)
            n = split(value, apps, ";")
            kept = ""
            for (i = 1; i <= n; i++)
                if (apps[i] != "" && apps[i] != "forgecad.desktop") kept = kept apps[i] ";"
            if (kept == "") next
            print key "=" kept
            next
        }
        { print }
    ' "$1.forgecad-bak" > "$1"
    echo "  associazioni tolte da $1 (copia: $1.forgecad-bak)"
}

if [ "${1:-}" = "--system" ]; then
    PREFIX=${2:-/usr/local}
    MANIFEST="$ROOT/forgecad-cuda-build/install_manifest.txt"
    if [ -f "$MANIFEST" ]; then
        # I file scritti da cmake --install, con il prefisso usato allora.
        while IFS= read -r file; do rm -f "$file"; done < "$MANIFEST"
    fi
    rm -f "$PREFIX/bin/forgecad" "$PREFIX/share/applications/forgecad.desktop" "$PREFIX/share/mime/packages/forgecad-mime.xml"
    remove_icons "$PREFIX/share/icons/hicolor"
    refresh "$PREFIX/share"
    echo "ForgeCAD rimosso da $PREFIX."
    exit 0
fi

DATA=${XDG_DATA_HOME:-$HOME/.local/share}
CONFIG=${XDG_CONFIG_HOME:-$HOME/.config}
rm -f "$DATA/applications/forgecad.desktop" "$DATA/mime/packages/forgecad-mime.xml"
remove_icons "$DATA/icons/hicolor"
clean_associations "$CONFIG/mimeapps.list"
clean_associations "$DATA/applications/mimeapps.list"
refresh "$DATA"
if [ "${1:-}" = "--purge" ]; then
    rm -rf "$CONFIG/ForgeCAD"
    echo "  impostazioni rimosse ($CONFIG/ForgeCAD)"
fi
echo "Integrazione di ForgeCAD rimossa per l'utente $(id -un)."
