#!/bin/sh
# Integrazione con il desktop per l'utente corrente (niente root): icona
# dell'applicazione, voce nel menu, tipo MIME dei documenti .prt con la sua
# icona e ForgeCAD come programma predefinito per aprirli.
#
#   packaging/linux/install-desktop-integration.sh [percorso/del/binario/forgecad]
#
# Per disinstallare: packaging/linux/uninstall-desktop-integration.sh
#
# Senza argomenti usa forgecad-cuda-build/forgecad del repository.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DATA=${XDG_DATA_HOME:-$HOME/.local/share}
ICONS="$DATA/icons/hicolor"

refresh() {
    update-mime-database "$DATA/mime" >/dev/null 2>&1 || true
    update-desktop-database "$DATA/applications" >/dev/null 2>&1 || true
    gtk-update-icon-cache -f -t "$ICONS" >/dev/null 2>&1 || true
    # Dolphin e le altre applicazioni KDE leggono una cache propria.
    if command -v kbuildsycoca6 >/dev/null 2>&1; then kbuildsycoca6 >/dev/null 2>&1 || true; fi
}

# Compatibilita': --uninstall passa allo script di disinstallazione.
if [ "${1:-}" = "--uninstall" ]; then
    exec "$ROOT/packaging/linux/uninstall-desktop-integration.sh"
fi

EXEC=${1:-"$ROOT/forgecad-cuda-build/forgecad"}
EXEC=$(cd "$(dirname "$EXEC")" && pwd)/$(basename "$EXEC")
[ -x "$EXEC" ] || { echo "Binario non trovato: $EXEC (compila prima con cmake --build forgecad-cuda-build)" >&2; exit 1; }

for size in 16 24 32 48 64 128 256 512; do
    install -Dm644 "$ROOT/icons/hicolor/${size}x${size}/apps/forgecad.png" "$ICONS/${size}x${size}/apps/forgecad.png"
    install -Dm644 "$ROOT/icons/hicolor/${size}x${size}/mimetypes/application-x-forgecad-part.png" \
        "$ICONS/${size}x${size}/mimetypes/application-x-forgecad-part.png"
done
# Le versioni vettoriali sono facoltative (bastano i PNG): la cartella
# scalable a volte appartiene a root, lasciata da un altro installatore.
install -Dm644 "$ROOT/icons/forgecad.svg" "$ICONS/scalable/apps/forgecad.svg" 2>/dev/null &&
    install -Dm644 "$ROOT/icons/forgecad-document.svg" "$ICONS/scalable/mimetypes/application-x-forgecad-part.svg" 2>/dev/null ||
    echo "Avviso: icone SVG non installate ($ICONS/scalable non scrivibile), restano i PNG." >&2
install -Dm644 "$ROOT/packaging/linux/forgecad-mime.xml" "$DATA/mime/packages/forgecad-mime.xml"
mkdir -p "$DATA/applications"
# Il percorso nel campo Exec va tra virgolette se contiene spazi.
case "$EXEC" in
    *" "*) QUOTED="\"$EXEC\"" ;;
    *) QUOTED="$EXEC" ;;
esac
sed "s|@FORGECAD_EXEC@|$QUOTED|" "$ROOT/packaging/linux/forgecad.desktop.in" > "$DATA/applications/forgecad.desktop"
refresh
xdg-mime default forgecad.desktop application/x-forgecad-part 2>/dev/null || true
echo "ForgeCAD integrato: $EXEC"
echo "  menu: $DATA/applications/forgecad.desktop"
echo "  tipo: application/x-forgecad-part (*.prt con la firma FCAD)"
