# K10 mapping: regressioni finali

Il test permanente proposto è `kernel/tests/test_mapping_carrier_validation.cpp`.

Il vecchio percorso pubblico accetta il caso degree6 e restituisce una portante completamente piana: il punto target Z=.001 ha distanza geometrica almeno .001 dal piano Z=0, contro tolleranza 1e-6. Il degree27 invece viene rifiutato conservativamente, quindi non viene contato come accettazione errata. Probe originale e output sono `composition.cpp`, `baseline.log`.

La suite richiede il nuovo helper direttamente (optional non vuoto), poi invoca l'API pubblica. Controlla portante originale stesso parametro, punto target proiettato sul risultato, bordo nello stesso parametro sorgente, checkBody, immutabilità binaria di source/target, history, deviation e roundtrip. Il campionamento denso è una regressione indipendente, non il certificato continuo. Le grandi distanze same-UV sul percorso legacy non sono da sole un errore geometrico, perché il legacy riparametrizza: prova geometrica distinta tramite proiezione del picco e repro piano Z0.

Copertura: 12 varianti polynomial/rational × Extrusion/BSpline × scale .01/1/1000 con pose, 4 quarter-turn/trim/hole, degree27 single-span, fallback legacy diagonale e cilindro periodico con normalOffset, invalid tolerance, callback failure, cancellazione iniziale e callback, history/deviation atomiche, due source discrepanze entro tolleranza BRep ma oltre budget fit (vertice/finUV), due fallimenti conservativi al bordo naturale non risolto. I successi posed/quarter-turn hanno margine interno; i corrispondenti casi che toccano il dominio naturale restano test separati di rifiuto atomico, senza epsilon/clamp.

Versione V5 storica: **8/8 gruppi PASS**, 1.19 s, maxRSS 6580 KiB. 18 varianti di successo nel nuovo percorso (12 matrice +4 quarter-turn/fori +degree27 +piccolo scarto vertice) e2 varianti legacy (diagonale/cilindro periodico offset). Dodici scenari di rifiuto/cancellazione:4 tolleranze invalide, dominio esterno, eccezione callback,2 cancellazioni,2 discrepanze source,2 bordi naturali non risolti. Totale32 scenari in8 gruppi.

Baseline pubblico vecchio + helper/bounds nuovi per confronto: **4/8**, 1.27 s; tre gruppi geometrici e il gruppo deviazione del piccolo scarto vertice falliscono (`test-baseline-final.log`). I controlli same-parameter verificano anche il requisito nuovo di conservare il carrier; il baseline può riparametrizzare. Errore bordo vs mappa originale alla scala.01: circa8.8e-6 contro1e-8; il picco target non proietta entro tolleranza.

Il repro originale ripetuto sul fixed (`composition-fixed-final.log`): degree6 successo, reported1.6232661779043149e-8, carrier_error0, checkBody0; baseline carrier_error.001 e carrier completamente piano Z0. Degree27 ora successo, reported1.6634142883304682e-8, carrier_error0, checkBody0; baseline era un rifiuto conservativo. Il valore surface_pole_absZ0 stampato dal probe fixed non descrive la portante Extrusion (viene calcolato solo per BSpline): non usarlo come prova di planarità del fixed.

La V4 falliva scale1000 per perdita di correlazione della traslazione nella valutazione affine intervallare. V5 usa forma equivalente a+(b-a)*t senza cambiare budget, e tutte le12 varianti passano. Log preservato `test-fixed-v4-scale1000-limit.log`. Il primo output sul bordo naturale è conservato come trascrizione dichiarata `test-fixed-v2-natural-boundary.transcript.txt`; i casi restano riproducibili nel gruppo finale esplicito.

Comandi: `python3 build-tests.py`, `./tests-fixed`; baseline `python3 build-tests.py --baseline`, `./tests-baseline`. Compilazione da compile_commands esistenti, oggetti privati prima dell'archivio Release. Nessuna build condivisa o sorgente produzione modificata da questi test.

Sanitizer finale: **8/8 PASS**,8.98s, maxRSS692992KiB, ASAN+UBSAN+LSAN senza diagnostiche (`sanitizer-final.log`). Test + fk_mapping_carrier + fk_surface_mapping + fk_curve_fit_bounds strumentati, resto archivio Release e main test non strumentati. Esecuzione con `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`, fuori dal vincolo ptrace sandbox. `build-sanitizer.py` riproduce la compilazione privata.

Test V5 SHA256 bb29d7ca7ce5dc9fae9cc15d9ef8e9b9a0b7f101db9b8e1fa0ee49093c6cb0c5; manifest V5 in `v5.sha256`. Core finali: helper a65f3bae5e4ac5f676b2aa92340743e1fc2bf4ab1dfde291da368899e0fff236; mapping0c76d3b2b19c2c110712348f06f0a2680dd0d5f4ba8fa77f2c90c71ac07306a9; bounds ee02df4e2d8e2f920afaf06835d9eb49ffb1ce659425d8e0517a163bad83303a. Nessun criterio di iniettività continua introdotto da questi test; le famiglie esplicitamente legacy conservano il relativo limite.


## Guard di budget finale V6

L'audit tardivo ha trovato che una famiglia riconosciuta (fin BSP1 affine) oltre100000poli veniva classificata unsupported e poteva ricadere nel legacy. Il nono gruppo costruisce una fin BSP1 collineare100001poli su un corpo sorgente valido (checkBody0) e invoca solo wrapPlanarBodyOnCarrier: deve lanciare senza mutare history/deviation o sorgente. Non chiama il legacy costoso. **V5 FAIL0/1** (`test-budget-v5.log`), **V6 PASS**, suite finale **9/9** in1.27s (`test-fixed-final.log`). Famiglie/geometria/budget invariati; nuova correzione distingue risorsa non risolta da famiglia unsupported. Totale finale33scenari:18successi nuovo percorso+2legacy+13rifiuti/cancellazioni.

Test finale V6 SHA256403a1ac55bf1d4d1b11308cbb8aaef5c2c09b4698e6c9d58ff06dca00752765d. Helper V6 SHA256a98eb1941657e929c9ad18c7c9bd9918f1b2b13e7fc77e35bb7242713c831b68. Bounds/mapping identici V5. Lo storico8gruppi normale/sanitizer è preservato nei log *v5-8groups.log. Il tentativo di ripetere il baseline conV6 ha trovato l'archivio condiviso già aggiornato: PASS9/9, conservato come `test-current-archive-v6-9groups.log` e `tests-current-archive-v6`, NON come baseline storico. Rimangono valide le prove precedenti4/8 e il guardV5FAIL0/1. Il nono test usa direttamente il nuovo helper, quindi serve a isolare il guard tardivo. Lo script --baseline significa tecnicamente "senza oggetto override mapping"; l'effettiva versione del pubblico dipende dall'archivio linkato e va controllata tramite manifest/snapshot.


Sanitizer V6 finale: **9/9 PASS**,9.08s, maxRSS725956KiB, zero diagnostiche ASAN/UBSAN/LSAN (`sanitizer-final.log`). Ricompilate tutte4TU, con helper e test finali V6; bounds/mapping invariati V5. Leak detection attiva ed esecuzione fuori dal vincolo ptrace sandbox. Main test e restante archivio Release non strumentati. Manifest finale V6 `final.sha256`; manifest storico V5 `v5.sha256`.


## Confronto baseline definitivo su9gruppi

Compilato privatamente il vecchio `before/kernel/fk_surface_mapping.cpp` (SHA2564d77ca5f28ed84fddefe0ef41f7e5214ace5fb636b953da0a4fa4e807198730c) e linkato il suo oggetto PRIMA dell'archivio corrente, insieme a helper/boundsV6: **5/9 PASS**,1.34s (`test-baseline-before-v6-9groups.log`). Script riproducibile `build-before.py`, usando l'oggetto del test finale prodotto da `build-tests.py`. Questo confronto isola correttamente il vecchio percorso pubblico anche dopo l'aggiornamento della build condivisa. Le quattro differenze sono i tre gruppi geometrici e il piccolo scarto vertice non incluso nella deviazione. Il nono guard di budget passa qui perché invoca direttamente helperV6: la prova specifica della sua correzione resta V5FAIL0/1→V6PASS.

Risultato definitivo: baseline pubblico precedente5/9, produzione proposta9/9, sanitizer finale9/9. Nessun cambiamento al test congelato dopo SHA403a1ac5... .
