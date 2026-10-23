#!/usr/bin/env bash
# Avvia la diagnostica senza terminare processi o cambiare il tema del sistema.
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
binary="${FORGECAD_BINARY:-$repo_root/forgecad-release/forgecad}"
if [[ ! -x "$binary" ]]; then
    printf 'Eseguibile ForgeCAD non trovato: %s\n' "$binary" >&2
    exit 1
fi
output_dir="$(mktemp -d "${TMPDIR:-/tmp}/forgecad-menu-diagnostics.XXXXXX")"
printf 'Diagnostica menu: %s\nApri un menu, cambia tema e riaprilo; poi chiudi questa istanza.\n' "$output_dir"
export FORGECAD_MENU_DIAGNOSTICS="$output_dir"
export QT_FORCE_STDERR_LOGGING=1
# Facoltativo: protocolli della sola istanza, per analizzare gli effetti del compositore.
if [[ "${FORGECAD_MENU_PROTOCOL_TRACE:-0}" == 1 ]]; then export WAYLAND_DEBUG=client; fi
exec "$binary" "$@" > "$output_dir/session.log" 2>&1
