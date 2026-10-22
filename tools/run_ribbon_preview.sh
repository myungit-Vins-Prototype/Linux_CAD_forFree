#!/usr/bin/env bash
set -euo pipefail
ribbon_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
ribbon_binary="$ribbon_root/forgecad-release/ribbon-preview/forgecad-ribbon-preview"
if [[ ! -x "$ribbon_binary" ]]; then
    echo 'Compilare la prova con:' >&2
    echo 'cmake -S . -B forgecad-release -DFORGECAD_BUILD_RIBBON_PREVIEW=ON' >&2
    echo 'cmake --build forgecad-release --target forgecad_ribbon_preview -j4' >&2
    exit 1
fi
exec "$ribbon_binary" "$@"
