# Integrazione di ForgeCAD con il desktop Linux

Icona dell'applicazione, voce nel menu e associazione dei documenti `.prt`
(tipo MIME `application/x-forgecad-part`, riconosciuto dalla firma `FCAD`
all'inizio del file: i `.prt` di altri CAD non vengono presi).

## File

| File | Contenuto |
|---|---|
| `icons/forgecad.svg` | Icona dell'applicazione (sorgente vettoriale) |
| `icons/forgecad-document.svg` | Icona dei documenti `.prt` |
| `icons/hicolor/<N>x<N>/apps/forgecad.png` | Icona dell'applicazione a 16–512 px |
| `icons/hicolor/<N>x<N>/mimetypes/application-x-forgecad-part.png` | Icona dei documenti a 16–512 px |
| `icons/forgecad.qrc` | Le icone incluse nel binario (icona della finestra) |
| `packaging/linux/forgecad.desktop.in` | Voce del menu (`@FORGECAD_EXEC@` = percorso del binario) |
| `packaging/linux/forgecad-mime.xml` | Definizione del tipo dei documenti |
| `packaging/linux/install-desktop-integration.sh` | Installazione per l'utente corrente |
| `packaging/linux/uninstall-desktop-integration.sh` | Disinstallazione (utente o sistema) |

## Installazione

Dopo la compilazione (`cmake --build forgecad-cuda-build -j`), per l'utente
corrente e senza permessi di root:

```sh
packaging/linux/install-desktop-integration.sh                 # usa forgecad-cuda-build/forgecad
packaging/linux/install-desktop-integration.sh /percorso/forgecad
```

Lo script copia le icone in `~/.local/share/icons/hicolor`, il tipo MIME in
`~/.local/share/mime/packages`, crea `~/.local/share/applications/forgecad.desktop`
con il percorso del binario, aggiorna le cache (anche quella di KDE) e imposta
ForgeCAD come programma predefinito per i `.prt`.

Per un'installazione di sistema (binario in `bin`, il resto in `share`):

```sh
cmake --install forgecad-cuda-build --prefix /usr/local   # con sudo se serve
```

## Disinstallazione

```sh
packaging/linux/uninstall-desktop-integration.sh                  # integrazione dell'utente
packaging/linux/uninstall-desktop-integration.sh --purge          # anche le impostazioni (~/.config/ForgeCAD)
sudo packaging/linux/uninstall-desktop-integration.sh --system    # installazione di sistema in /usr/local
sudo packaging/linux/uninstall-desktop-integration.sh --system /usr   # con un altro prefisso
```

Per l'utente si tolgono voce del menu, icone e tipo MIME, e `forgecad.desktop`
da tutte le associazioni di `mimeapps.list` (prima si salva una copia,
`mimeapps.list.forgecad-bak`); le righe rimaste senza programmi spariscono.
Con `--system` si tolgono il binario e i file di `cmake --install` (anche
quelli elencati in `forgecad-cuda-build/install_manifest.txt`, se c'e'). I
documenti e la cartella del progetto non si toccano.

## Rigenerare i PNG

Se si modificano gli SVG:

```sh
for s in 16 24 32 48 64 128 256 512; do
    rsvg-convert -w $s -h $s icons/forgecad.svg -o icons/hicolor/${s}x${s}/apps/forgecad.png
    rsvg-convert -w $s -h $s icons/forgecad-document.svg -o icons/hicolor/${s}x${s}/mimetypes/application-x-forgecad-part.png
done
```
