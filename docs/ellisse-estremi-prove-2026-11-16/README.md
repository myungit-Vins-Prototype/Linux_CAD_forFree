# Prove ellisse per estremi, 16 novembre 2026

- `root/`: baseline di 2687 file, fonti congelate, build intermedia/finale, 24 CTest, Wayland/OpenGL, mappa e conservazione.
- `tests/`: probe geometrici/solver/Taglia e compatibilità impronte69; fonti e istruzioni nei file `results.txt` e negli script.
- `core/compat69/`: baseline del matcher delle tavole e hash finali della compatibilità.

Gli script/sorgenti archiviati hanno suffisso `.txt` per restare fuori dal catalogo. Gli script root si eseguono con `python3 nome.py.txt`; i probe privati indicano percorsi e comandi. Non sono distribuiti oggetti, binari o librerie. Prima di ripetere confrontare gli hash dei sorgenti e ricompilare l’app sulla macchina corrente.

Il test permanente si ripete con `ctest --test-dir forgecad-release -R '^forgecad_ellipse_extremes_tests$' --output-on-failure`; per Wayland usare il runner con `--ellipse-extremes --gl`. Le impostazioni delle prove root sono isolate in `/tmp`.

I risultati sanitizer riguardano quattro TU e il probe; il resto del kernel/offset non è strumentato. Il tentativo con dipendenze GUI e il link minimo finale sono distinti, senza soppressioni. La prima build con sorgenti ancora in integrazione e quella finale congelata sono conservate separatamente.

`root/final-audit.json` registra conservazione, hash dei sorgenti e dei binari, link senza OCCT. `SHA256SUMS` copre l’archivio escluso il manifest stesso. `root/change.diff` confronta con le copie all’inizio dell’intervento, senza confondere modifiche pregresse e correnti.
