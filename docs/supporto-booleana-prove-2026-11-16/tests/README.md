# Supporto: prove indipendenti finali, 16 novembre 2026

Nessun binario, oggetto o documento CAD è incluso in questa cartella. Il CAD originale rimane congelato e non è stato modificato. La copia consegnabile è in `output/Supporto_unito.prt`, fuori dalle prove; identità e hash sono in `release/delivery-copy-result.json`.

## Esiti e limiti

- Baseline: 24 tentativi reali worker/app (snapshot e rigenerato senza cache), 8 unioni riuscite e 16 rifiutate. Le 176 asserzioni di validazione erano tutte PASS, ma non rappresentano 24 successi. Il controllo negativo esplicito richiede tutti i successi: 200 controlli, 16 FAIL attesi.
- Stage finale: 424 controlli, zero errori, tutte le 24 unioni riuscite. Due oggetti kernel privati congelati sono dichiarati nel link.
- Release finale: 424 controlli, zero errori, tutte le 24 unioni riuscite. `private_overlay_objects` è vuoto: solo gli oggetti app e l'archivio kernel della build completa corrente. Le fonti core coincidono con il freeze.
- Certificatore permanente: 51 asserzioni. Il candidato conservato ha SHA06f613580054c6de37f46738ccfc39d42c4c0965732f59415ab4db03590e2ad3; il file integrato è stato solo riformattato da root, SHA f053cc39a74c22b91721f53cb026f688058bf9e84b5401d8e63cc9bf0e11cbe8. Token C++ identici.
- Compatibilità privata: 27 controlli aggiuntivi, totale 78. Imprint pubblico valido/FaceId invalido, sei operazioni ellittiche Unite/Intersect/Subtract nei due ordini, validità, volume entro limiti dell'operazione, genealogia e immutabilità sorgenti.
- ASan/UBSan/LSan: 78 PASS, exit0, nessun diagnostico né suppression. Strumentati fk_boolean.cpp, fk_boolean_boundary.cpp e driver (include le 51 asserzioni); il resto del kernel rimane Release. Il primo log sandbox conserva il limite LSan/ptrace: il successivo run unrestricted dello stesso binario completa LSan senza errori. Non è una certificazione dell'intera app sotto sanitizer.
- Retry: 15 controlli PASS con fault injection su una copia privata del TU, dopo l'effettivo uso della nuova catena sui corpi originali. Solo domain_error tenta un rebuild legacy con opzioni immutate e history vuota; il vecchio percorso rifiuta il caso originale senza pubblicare history parziale. logic_error e OperationCancelled propagano senza retry. Non si afferma che il retry sia intervenuto nel normale caso riuscito.
- Consegna: 65 controlli PASS. SaveAs con nuovo UUID, ultima feature Union base0/tools2,5, un solo solido chiuso, Save atomico, riapertura, rebuild integrale senza cache, Undo/Redo. Definizione e B-rep finali byte-identici dopo ricostruzione. Il file salvato in /tmp è stato copiato senza sovrascrittura in output e ricontrollato SHA.
- I witness geometrici derivano da vertici, curve esatte e punti interni faccia: sono controlli diagnostici, non una prova continua dell'intero volume. I bound continui della nuova pcurve sono verificati separatamente. I volumi sono integrati sul B-rep esatto; differenze piccole fra ordini non sono dichiarate errori geometrici senza analisi della quadratura.
- I sintetici esplorativi passavano già sulla baseline: compatibilità, NON regressioni negative. La sweep di epsilon contiene 16 tentativi tutti riusciti. Le righe storiche `expected_loft_volume` dei primi G1 usavano una formula lineare e non sono un valore atteso G1; sono conservate come esplorazione, non come asserzioni. Non esiste ancora un piccolo CAD sintetico negativo equivalente all'originale.

## Correzioni del fixture di consegna conservate

`currentDocument()` conserva la preferenza di visibilità ModelBody e normalizza i flag salvati: non richiedere che ogni operando consumato abbia visible=false nel documento. Il controllo finale usa `isBodyDrawn` sui soli `isShapeBody`, più `resultBodiesBefore(-1)=={6}`. I piani di riferimento possono rimanere visibili. Le fonti e i log dei due tentativi iniziali sono in release; un include mancante nel driver è conservato nel suo log. Nessuna modifica produzione per questi aggiustamenti.

## Riproduzione

Ripristinare i file sorgenti/script togliendo solo l'ultimo suffisso `.txt` in una cartella temporanea. I driver usano percorsi assoluti registrati nei comandi. La prova reale richiede il file originale dell'utente (`/tmp/supporto-review/Supporto.original.prt`, SHA79887e91d75feb748af19fcdb14cd469cc40e74abab03e3cbeaa12e2d3271f7f) che non viene incluso. Le 51 asserzioni permanenti kernel sono invece portabili senza CAD/Qt.

- Replay Release: `python3 build.py --app-release`; ambiente `QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_QPA_PLATFORMTHEME_QT6=`; eseguire `regeneration-app-release release-final.json --expect-all-success`.
- Copia: `python3 build.py --app-release --output-copy`; eseguire `regeneration-delivery-app-release /tmp/cartella-nuova/Supporto_unito.prt`. La destinazione deve essere assente; lo script rifiuta sovrascritture. Il file output definitivo già esiste: non rilanciare usando quella destinazione.
- Sanitizer: `python3 build_certificates.py --extended --sanitize`; quindi ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 sul binario. Nessuna suppression. Il file JSON registra flag, fonti e limiti di strumentazione.
- Retry: compilare fk_boolean_injected.cpp con -std=c++17 -O0 -g -fno-fast-math -ffp-contract=off e include stage/kernel + kernel; linkare probe.cpp + oggetto iniettato + helper congelato + libforgekernel.a -pthread. Il driver legge i dump originali body-202.bin/body-205.bin, esclusi dall'archivio. Questi sono rigenerabili dal CAD con i probe core conservati nel gruppo core.

I gruppi baseline/stage/sanitizer/release/retry/exploration rimangono distinti. Gli archivi storici `tests-baseline` e i manifest precedenti non sono sovrascritti. Manifest.json contiene SHA256 e dimensioni di ogni artefatto, escluso se stesso.
