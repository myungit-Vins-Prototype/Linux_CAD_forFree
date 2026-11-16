# K10 pareti thicken su Plane: regressioni finali

## Prova operativa baseline

Fonti/header e oggetti thicken, offset, sew, pcurve e open_offset congelati in before/ PRIMA delle build successive; before.sha256 e graph-before.json registrano snapshot e responsabilità. Il probe pubblico usa questi oggetti prima dell'archivio corrente. Directory di staging creata nuova, senza alterare prove storiche.

Source: lamina Plane con bordo NURBS grado27 single-span, graph regolare X=t e lobature fra i campioni legacy. Variante polynomial e rational con pesi positivi variabili, source/output checkBody0. ThickenOptions thickness.1, tolerance1e-6, offsetTolerance1e-7, sideForward. Il codice tenta fit pareti a1e-9.

| Source | normale baseline: errore stessa parametrizzazione | normale baseline: distanza geometrica della vera traslazione dalla parete | direzioneZ stesso source: distanza geometrica |
|---|---|---|---|
| polynomial | 5.7424750601786093e-5 | 5.7424750601754902e-5 | 2.3618943543665635e-16 |
| rational positivo variabile | 5.4162190778152527e-5 | 5.416219077815233e-5 | 1.4816730699805633e-17 |

Tutti i casi accettati, solido non-sheet, checkBody0, una parete generata dall'edge problematico, source binario immutato. Quindi la parete del percorso normale perde dettaglio di un contorno valido pur restituendo un B-rep accettato; la stessa operazione geometrica lungo Z conserva il contorno. Non è un semplice cambio di parametro o un rifiuto conservativo. Log probe-before.log; source originale del probe conservata come thicken_probe_before.cpp/.o. Il probe attuale adatta soltanto la coordinata V alla famiglia reale del carrier: BSpline rigata V=.5, Extrusion V=.05 fisico; non cambia i punti geometrici di riferimento.

## Test permanente e conteggi

File test_planar_thicken_validation.cpp SHA256f8864efc84c721d03f89cd5b58de146b1bddb971655ebff487aecfca5aa5ceb1. Sei gruppi,31varianti top-level:

- 6successi hidden graph: polynomial/rational × Forward/Backward/Both.
- 12successi posa sourcePlane3D con traslazione/rotazione, scale.01/1/1000, polynomial/rational e face.sensefalse/true con loop coerenti.
- 4successi trim annidati parziali[2.3,4.7] su dominio naturale[2,5], reverse/range negativo, TransformedCurve3 originale, fori, B-rep codec/roundtrip.
- 2successi controllo pubblico useDirection=true, non modificato, sugli stessi contorni nascosti.
- 5errori/cancellazione: thickness0/-1/NaN/Inf e OperationCancelled. Conservato e verificato esplicitamente il reset history all'ingresso già esistente; nessuna nuova promessa di atomicità history.
- 2successi compatibilità curva chiusa: Circle angolare originale e NURBS quadratica razionale nativa; entrambi si dividono in2pareti con giunzione di chiusura e roundtrip codec.

Tutti i26successi richiedono esito operativo, source/output checkBody0, volume positivo e coerente con areaSource×spessore. La parete problematica è identificata tramite genealogia Face←Edge originale, non per indice o vicinanza. Carrier originale Extrusion con generatrice preservata: grado/nodi/pesi confrontati sulla base BSP, includendo Trimmed/Transformed. Il confronto geometrico usa la V fisica e la vera traslazione del contorno originale lungo normale orientata; verifica densa entro1e-9 e nearest-support indipendente al picco nascosto. Per OGNI fin delle pareti identificate dall'edge problematico (due nel caso chiuso), vengono valutate le SP-curve EFFETTIVE del risultato contro la curva3D sul parametro reale dell'edge, entro1e-9, anche dopo codec. Gli altri edge rettilinei/fori rimangono controllati da checkBody e volume; non si sostiene che il test denso copra ogni bordo dell'intero solido. Source binario immutato in tutti i casi.

I campioni sono regressioni indipendenti, non il certificato di una superficie generica. Per Plane la costruzione preserva direttamente curva e traslazione, senza due fit cubici né perdita di pesi razionali. Rette mantengono il precedente shortcut Plane; le famiglie Other anche dietro Trimmed/Transformed rimangono legacy esplicito.

## Risultati finali e baseline

Core freeze fk_thicken.cpp SHA256d235fdfa46ecc301a1d88b659cc45df40f4de9368ca1133f5844f8d7c11e6ef0. **6/6 normali PASS**,1.55s,maxRSS6780KiB(test-fixed-final.log). Baseline test **2/6**,.37s(test-before-final.log), con i5oggetti snapshot prima archivio. I4gruppi normali baseline si arrestano al nuovo requisito di carrier originale Extrusion: NON vengono attribuiti4nuovi difetti geometrici; la prova quantitativa geometrica indipendente è quella delle2fixture pubbliche sopra. La compatibilità circolare baseline può avere geometria adeguata pur cambiando rappresentazione.

Probe corretto finale: normale e direzioneZ hanno errore stesso parametro0; distanza geometrica2.3618943543665635e-16 polynomial e1.4816730699805633e-17 rational, source/outputcheck0 e source immutato(probe-fixed-final.log). Nessun budget/tolleranza allargato.

Comandi privati: python3 build-probe.py; ./thicken-before; python3 build-tests.py; ./tests-fixed; python3 build-tests.py --before; ./tests-before. Snapshot oggetti precede le build root e le fonti sono incluse per riproduzione. Root integra il solo core e il nuovo test e gestisce build app, CTest e mappa; questi esiti globali non sono certificati dal runner privato.

## Limiti dichiarati

Correzione locale delle pareti il cui supporto sorgente è Plane strutturale e curva nota. Caps offsetFaces, ordine, sewing e genealogia restano il percorso normale precedente. Pareti su supporti curvi o callback Other rimangono nel fit legacy esplicito. Il vecchio reset history su errori/cancellazione è preservato; source immutabile verificato. Non viene introdotta una prova globale di iniettività dello spessore generale e non viene attribuita al test privato alcuna verifica renderer/app.


## Sanitizer e consegna

**6/6 ASAN/UBSAN/LSAN PASS**,4.70s,maxRSS480608KiB,zero diagnostiche(sanitizer-final.log). Strumentati esattamente2TU: fk_thicken.cpp e test_planar_thicken_validation.cpp. Restante archivio Release e main non strumentati. ASAN_OPTIONS=detect_leaks=1:halt_on_error=1, UBSAN_OPTIONS=halt_on_error=1; esecuzione fuori vincolo ptrace sandbox,exit0. Fonti core/test coincidono con freeze normale e integrazione root. build-sanitizer.py riproduce gli oggetti privati.

results.json contiene6gruppi/31varianti e risultati strutturati. final.sha256 include fonti/header/oggetti baseline, freeze core, test, script, binari, log e risultati. Nessun edit del test dopo SHA f8864efc comunicato a root.
