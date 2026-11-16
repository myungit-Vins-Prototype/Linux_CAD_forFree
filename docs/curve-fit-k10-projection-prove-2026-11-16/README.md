# Prove K10 proiezioni su Plane, 16 novembre 2026

Archivio dei nuovi replay dopo l’interruzione. Sorgenti e binari del prodotto sono rimasti gli stessi; la vecchia cartella temporanea non ancora archiviata è andata persa. Nessun log qui viene presentato come recupero di quello staging.

- `root-replay/`: build incrementale, 24 CTest, runner 33 casi, filtro Projected, baseline di modulo, grafo, hash iniziali della ripresa e audit finale.
- `tests-replay/`: 33 casi ASan/UBSan/LSan mirati e tre prove applicative; tentativo GTK fallito conservato accanto ai risultati finali.
- `independent-replay/`: coefficienti originali e output, certificati continui con Fraction, giunti, 16 testimoni baseline e inventario dei siti aperti.

Le istruzioni specifiche sono nei README dei due agenti. Script e sorgenti hanno suffisso `.txt` per restare fuori dal catalogo C++/CMake. Gli script root possono essere eseguiti direttamente con `python3 <nome>.py.txt`; quelli degli agenti indicano come ricreare lo staging e adattare i percorsi. Oggetti, librerie e binari non sono distribuiti.

Il test permanente si ripete con `ctest --test-dir forgecad-release -R '^forgekernel_projection_fit_validation_tests$' --output-on-failure`. Per il confronto baseline, lo script root estrae solo `fk_project.cpp/.h` dal Git HEAD registrato e collega l’oggetto precedente prima dell’archivio corrente; non ricostruisce l’archivio iniziale perso. Verificare la revisione prima di ripetere il confronto dopo futuri commit. La baseline del modulo è inclusa sotto `root-replay/baseline/`.

Il nuovo snapshot di conservazione parte dalla ripresa e non sostituisce una misura storica non recuperabile. Gli hash dei cinque sorgenti finali coincidono con quelli registrati nella conversazione prima dell’interruzione. Il sanitizer strumenta tre TU e lascia il restante kernel Release; i probe offscreen non certificano OpenGL. I controlli campionati e le prove continue restano distinti.

`SHA256SUMS` copre tutti i file dell’archivio, escluso il manifest stesso. `root-replay/final-audit.json` registra gli hash dei binari correnti e le dipendenze di link. `root-replay/final-sources/` conserva i sorgenti finali. Il rapporto principale riporta campo di validità e limiti della correzione.
