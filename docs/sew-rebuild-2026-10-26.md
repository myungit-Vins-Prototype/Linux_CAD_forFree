# Ricostruzione mouse2: cache locale dei campioni UV — 26 ottobre 2026

L'ottimizzazione del solver migliorava le catene sintetiche ma lasciava la ricostruzione mouse2 a circa36secondi. Questo intervento riduce il lavoro ripetuto nella rimappatura delle SP-curve durante la cucitura, senza cambiare precisione o algoritmo geometrico.

## Diagnosi

`FORGECAD_PROFILE` individua Cucitura28~17,5s e Solido cucito30~8,2s. `FK_PROFILE` e oggetti temporanei con soli cronometri distinguono: giunzioni aT~1ms, confronto bordi~0,24s, rimappatura sewnPCurve~17s. In due esecuzioni diagnostiche separate, il fitting delle rimappature assorbe~17,1s e la proiezione dei nodi~26ms; non sono tempi da sommare fra processi.

Il fitting richiede più volte lo stesso parametro, anche per le derivate numeriche. Ogni richiesta ricalcolava le proiezioni globali e la selezione del parametro sorgente. Le geometrie sono immutabili e questi campioni restituiscono lo stesso risultato.

Il tentativo preliminare di confrontare direttamente punti corrispondenti passava le regressioni ma non riduceva il costo principale: è stato rimosso integralmente. Perf hardware non era accessibile per le capability del sistema; nessun permesso/sysctl modificato, usati i cronometri del kernel e sonde temporanee.

## Implementazione

`kernel/fk_exchange.cpp`, solo callback del fitting in `sewnPCurve`:

- Cache locale `parametro→Vec3 UV`, senza cambiare il valore calcolato sui miss o la scelta del ramo della proiezione.
- Chiave dei64bit del double tramite memcpy: anche+0/-0 restano distinti, nessun arrotondamento o tolleranza sulle chiavi.
- Massimo4096voci; al limite si svuota e continua il calcolo normale. Distruzione a fine rimappatura, nessuna cache globale o nel documento. Limite in voci, non promessa di tetto RSS.
- `cancellationPoint` anche sui riusi; nessun mutex o dato condiviso fra worker.
- Stesso fitCurve, stessi campioni/derivate/tolleranze, stesse proiezioni sui miss e verifica finale indipendente in3D.

Con FK_PROFILE vengono stampati contatori delle sole rimappature che richiedono fitting. Su mouse2: **7293riusi su26311richieste**. Due chiamate raggiungono4096voci ed esercitano il ramo di svuotamento. La cache viene rilasciata al ritorno della funzione.

## Misure sul documento reale

Riferimento: runner conservato prima di questo intervento, già con la precedente ottimizzazione del solver. Nuovo e precedente condividono la stessa fixture e pipeline; la modifica produttiva è limitata al file kernel sopra. App e runner Release ricompilati.

Confronto senza profilazione:4processi nuovi per versione, ordine bilanciato, copie in memoria ricostruite da zero, qualità1 e tassellazione CPU. Timer = lettura+load con geometria/mesh, firme fuori timer. Broad phase GPU disattivata, nessuna build/mappa/sonda concorrente. Cache filesystem non svuotata; clock/temperatura/carichi esterni non controllati. Non sono tempi di framerate o del rendering GL.

| Versione | Mediana | Minimo–massimo |
|---|---:|---:|
| Prima | 35.596s | 35.380–35.710s |
| Dopo | 31.377s | 31.180–31.514s |

**11.9% di tempo in meno**, rapporto precedente/nuovo **1.134×**. Il risultato riguarda la ricostruzione di questo documento, non tutti i modelli.

Il singolo profilo diagnostico separato passa35,904→31,859s: Cucitura28~17,36→13,32s. Il volume di Solido cucito30 resta~8s e non è stato modificato; anche Loft6 resta un costo significativo. Il guadagno dei cronometri non viene confuso con il confronto ripetuto senza profiler.

## Correttezza e verifiche

Originale mouse2.prt SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617` invariato, nessun salvataggio del documento. Tutti i16B-rep e le mesh di49.279triangoli visibili hanno firme identiche fra versioni e tutte le ripetizioni; errori/visibilità identici.

Nuovo caso permanente `SewMappedSamplesRetainDistanceLimit` in kernel/tests/test_offset.cpp: piccola bombatura sotto/sopra la tolleranza geometrica, entrambi gli ordini, topologia valida e input immutato. Stesso sorgente di test collegato contro kernel precedente9/9SewPASS e kernel finale9/9SewPASS;19/19OffsetPASS. Runner mirato compilato con i flag CMake e riferimenti OCCT disponibili, non dichiarata esecuzione dell'intera suite kernel.

42/42CTest applicativiPASS in11,22s, inclusi solver, mesh/export, CUDA e storia/anteprime. Ricollegati infine anche i runner standalone CUDA, broad phase ed export alla libreria kernel finale:6/6test pertinenti ripetutiPASS. QA OpenGL nativa dopo ricostruzione completa di mouse2: tre riaperture della superficie planare, prima immagine e cache ricreata identiche (zero pixel diversi), zero errori GL, B-rep originali/input conservati.

Mappa aggiornata:494file/6678relazioni,235TU senza diagnostiche; --checkPASS,79.498siti entro le righe e chiamate ForgeSew→sewSheets→sewModel più test verificate in entrata/uscita. HTML/JSON della mappa ignorati Git.

Codice C++17 comune a macOS/Linux, non legato a CUDA o Metal. Compilazione e QA nativeMac ancora da eseguire. Nessun backport a main, installazione desktop, commit o push in questo intervento.

[Dati, campioni e hash](sew-rebuild-2026-10-26.json). Runner/libreria precedenti, sonde e log in `forgecad-linux-solver/benchmarks/sew-rebuild-2026-10-26`; confronto ripetuto in `measures`. Source/binari di prova conservati prima della modifica; JSON con profili e benchmark distinti.

## Riproduzione

```sh
cmake --build forgecad-linux-solver --target forgecad forgecad_view_tests -j4
python3 tools/benchmark_document_mesh.py /percorso/mouse2.prt \
  --build-dir forgecad-linux-solver --reference-binary /percorso/runner-precedente \
  --output /percorso/nuova-directory --no-profile --repeats 4
FK_PROFILE=1 FORGECAD_PROFILE=1 QT_QPA_PLATFORM=offscreen \
  forgecad-linux-solver/forgecad_view_tests --document-mesh-benchmark /percorso/mouse2.prt --rebuild
```

Prossimi punti: costo del volume necessario all'orientamento dei solidi cuciti e costruzione Loft6, con verifiche geometriche dedicate. Il lavoro su GPU e il solver restano separati da questo risultato.

Prosecuzione completata sullo stesso branch: [quadrature delle proprietà di massa per faccia in parallelo](mass-parallel-2026-10-26.md), con confronto separato rispetto alla versione che include già questa cache UV.
