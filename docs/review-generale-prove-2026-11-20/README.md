# Riproduttori della review del 20 novembre

Eseguire dalla radice del checkout Linux con il kernel compilato in `forgecad-release`. I driver usano il kernel pubblico e stampano anche i casi di controllo. I file di esempio dell'utente non servono.

```bash
area=$(mktemp -d)
prove=docs/review-generale-prove-2026-11-20
cp "$prove/cucitura.cpp.txt" "$area/cucitura.cpp"
cp "$prove/scambio.cpp.txt" "$area/scambio.cpp"
c++ -O1 -std=c++17 -ffp-contract=off -Ikernel "$area/cucitura.cpp" forgecad-release/kernel/libforgekernel.a -pthread -o "$area/cucitura"
c++ -O1 -std=c++17 -ffp-contract=off -Ikernel "$area/scambio.cpp" forgecad-release/kernel/libforgekernel.a -pthread -o "$area/scambio"
"$area/cucitura"
"$area/scambio"
```

Nel primo driver confrontare i due ordini disgiunti con 9 mm³ e i due annidati con 7 mm³. Nel secondo il cubo ha sempre sei facce e volume positivo prossimo a 1 mm³. L'entità STEP è rilevata direttamente nel testo esportato. IGES usa la modalità superfici. I due driver sono diagnostici e stampano le misure anche quando sono sbagliate. La seguente prova sul wrap usa invece asserzioni e termina con errore sul falso successo conosciuto.

```bash
cp kernel/tests/test_mapping_normal_validation.cpp "$area/original.cpp"
patch -o "$area/prova.cpp" "$area/original.cpp" < "$prove/pesi-variabili.patch.txt"
c++ -O1 -std=c++17 -fno-fast-math -ffp-contract=off -Ikernel -Ikernel/tests "$area/prova.cpp" kernel/tests/fk_test_main.cpp forgecad-release/kernel/libforgekernel.a -pthread -o "$area/mapping"
"$area/mapping" MappingNormalDiagonalRankOne
```

Per riprodurre il nuovo caso procedurale, il driver seguente stampa la deviazione dichiarata dal wrap e lo scarto sulla cima del rilievo, prima con la generatrice piatta e poi con quella deformata.

```bash
cp "$prove/estrusione-wrap.cpp.txt" "$area/estrusione-wrap.cpp"
c++ -O1 -std=c++17 -ffp-contract=off -Ikernel "$area/estrusione-wrap.cpp" forgecad-release/kernel/libforgekernel.a -pthread -o "$area/estrusione-wrap"
"$area/estrusione-wrap"
```

`copertura.tsv` è il catalogo dei file dopo le modifiche. Le colonne sui chiamanti e sui chiamati contano gli altri file interessati da chiamate statiche o riferimenti a funzione, esclusi gli include. `segnalazioni.tsv` conserva le 483 diagnostiche grezze del primo passaggio completo Cppcheck 2.22.0. Un file può generare segnalazioni riferite a un header incluso. La classificazione del programma non assegna priorità o stato di bug nel registro.

Per ripetere il passaggio con la stessa versione dell'analizzatore, generare dall'inventario una lista dei file con suffisso `.cpp` o `.h` e usare:

```bash
awk -F '\t' 'NR > 1 && $1 ~ /\.(cpp|h)$/ { print $1 }' "$prove/copertura.tsv" > "$area/file-cpp.txt"
cppcheck --language=c++ --file-list="$area/file-cpp.txt" --check-level=reduced --enable=warning,performance,portability --std=c++17 --library=qt --library=posix --max-configs=1 -j1 -I. -Ikernel -Ikernel/tests --xml --xml-version=2 2> "$area/diagnostica.xml"
```

Per le due diagnostiche di macro OCCT è stata ripetuta l'analisi di `kernel/occt/fk_occt_import.cpp`, `kernel/tests/fk_test_profiles.h` e `kernel/tests/fk_test_util.h` con `'-DHandle(x)=opencascade::handle<x>'`. La mappa AST usa separatamente la configurazione effettiva di CMake. Entrambe le viste hanno limiti sui backend delle altre piattaforme.
