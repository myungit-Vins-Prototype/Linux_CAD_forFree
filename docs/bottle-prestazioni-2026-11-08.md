# Bottle — CPU a riposo e schizzo lento

8 novembre 2026. Indagine conclusa, correzioni compilate e verificate nei casi descritti.

Richiesta: CPU indicata al 12,5% senza interazione, nuovo schizzo lento dentro Bottle.prt e viewport a scatti.
Processo reale ForgeCAD, PID 2355; checkout Linux /home/myungit/Linux_CAD_forFree.
La cartella /Users indicata dalla chat non esiste sull'host.

## Riproduzione prima della correzione

- Copia congelata: /tmp/bottle-idle-2026-11-08/Bottle.prt, 92.332.491 byte.
- SHA256 iniziale uguale all'originale: 9710b1ce21eb920382ba266df44349deca0726f122a9d26d29d71468c970455d.
- File corrente con 31 feature: Raccordo31/B30 visibile, 163 facce, 2.559.315 poli B-spline,
  123.537 triangoli di visualizzazione; non e' il documento Bottle delle prove del7novembre.
- Campioni reali /proc: un primo intervallo di3 s con tutti i thread allo0%; successivamente
  thread principale90,7% di un core. RSS app circa10,7 GiB, nessuno swap. Il valore ps296% era medio storico.
- Benchmark viewport isolato XCB, Intel UHD P630, MSAA16: rotazione16,30 ms,zoom16,21 ms,
  hover0,51 ms,orbita12,66 ms. Non riproduce da solo il blocco dell'interfaccia.
- Sonda Qt offscreen su copia: lettura3,03 s,loadDocument7,04 s,16 worker dopo caricamento;
  creazione schizzo0 ms,prima cache riferimenti7,90 s,376 segmenti/308 curve,
 12 eventi di solo movimento91–96 ms. CPU cumulativa49,0 s,RSS massimo8,91 GiB.
- Ripetizione con stack: cache7,48 s,eventi88–93 ms. La prima cache richiama
  elementNames/resolveHistory/faceBox/cachedPatchTree/bezierPatches; il movimento ripete
  curveGeometry/validateKnotVector e cachedBezierSegments da snapIntersection.
- Profilazione diretta dell'app esistente impedita dai permessi perf/ptrace del sistema;
  nessuna modifica a sysctl,nessuna chiusura o manipolazione del processo utente.

## Correzioni implementate

- cad_curve_solver.cpp: cache transiente per thread delle Converted NURBS positive-degree,
  massimo512 voci; chiave sugli array Qt condivisi e sul grado. Conservare gli array impedisce
  riuso degli indirizzi e consente l'invalidazione copy-on-write per poli,pesi,nodi.
  Si riusano anche le cache Bezier delle curve kernel immutabili; nessun cambiamento geometrico.
- forgeCad2026_viewport_interaction.cpp: hover Seleziona senza snap esterno; rimosso
  il secondo snap incondizionato sul medesimo evento. Disegno e trascinamenti conservano gli snap.
- forgeCad2026_viewport_selection.cpp: un job a priorità bassa per il precalcolo della catena
  storica, evitando16 worker che costruiscono contemporaneamente le stesse cache condivise.
- tests/test_viewport.cpp: invalidazione poli/pesi/nodi/grado,espulsione senza
  invalidare puntatori vivi,hover senza cache e successivo disegno con cache.

## Risultati dopo la correzione

- App completa `forgecad-release/forgecad` e runner `forgecad_view_tests` ricompilati.
  L'applicazione utente gia aperta continua a eseguire la versione precedente; occorre salvarne
  il lavoro e riaprire ForgeCAD. Nessuna chiusura automatica del suo processo.
- Sonda sullo stesso snapshot: un job cache invece di16; nuovo schizzo0 ms.
  Hover Seleziona in Wayland senza preparare riferimenti:12 eventi,0,0009–0,036 ms.
- Strumento Punto dopo cache: primo movimento27,91 ms,successivi6,48–10,26 ms,
  media7,40 ms contro circa90 ms iniziali. Strumento Linea: primo movimento27,61 ms,successivi4,91–6,82 ms; evita anche lo snap
  gia eseguito dall'inferenza. Dettagli nel log `line-after.log`.
- Vista isolata XCB: rotazione14,06 ms,zoom13,14 ms,hover0,40 ms,orbita9,80 ms.
  Finestra completa Wayland1658x965/MSAA16: rotella16,72 ms,orbita26,96 ms;
  primo hover49,34 ms. Nessuna certificazione di tutte le combinazioni hardware o impostazioni.
- La preparazione della cache storica su28 corpi termina in10,3–14,5 s nei lanci misurati.
  Dopo la fine: zero fotogrammi spontanei nei successivi intervalli da1 s;
  nell'ultimo intervallo CPU utente+0,0022 s,pari a circa0,22% di un core.
  E' un'attivita finita dopo caricamento/modifiche, non un ciclo di rigenerazione permanente.
- 11 CTest mirati PASS: snap intersezioni/automatici,riferimenti/uso/workflow,
  maniglie,quote/costruzione,editing,schizzi fissi densi,quadranti,spline.
  Le nuove regressioni verificano COW,pesi/nodi/grado e durata dei puntatori dopo espulsione.
- Un dodicesimo test,`forgecad_mouse_snapshot_tests`,fallisce sull'asserzione di riduzione
  dei blocchi B-rep. Riprodotto lo stesso errore ricompilando i tre sorgenti di produzione
  modificati da questo intervento nelle rispettive versioniHEAD,con il resto del checkout
  identico. Il problema e' quindi presente anche senza queste correzioni; non e' stato
  mascherato o indebolito il test. Log `baseline-mouse-snapshot.log` e `tests.log`.
- Mappa locale rigenerata:645 file/9133 relazioni,0 erroriAST,`--check` PASS;
  verificati gruppi e relazioni entrata/uscita dei moduli interessati,HTML aperto in Chrome
  headless con controllo visivo del dettaglio curve. HTML/JSON restano ignorati daGit.

## Limiti rimasti e integrita dei dati

La prima creazione dei riferimenti automatici richiede ancora7,5–14,3 s: risolve la genealogia
storica e i box delle superfici dense. La prima selezione esatta nella finestra completa ha
richiesto8,60 s; le due successive27,59/46,58 ms. Queste attese iniziali non sono eliminate.
Il consumo di memoria del modello resta elevato: circa8,7–8,9 GiB di RSS massimo nella sonda.
La correzione evita lavoro ripetuto e competizione tra worker; non semplifica le superfici,
non cambia tolleranze e non modifica la definizione geometrica del file.

L'originale e' stato solamente letto. Durante l'indagine e' cambiato esternamente
(mtime osservato08/11/2026 10:27:20CET,94.835.104 byte,
SHA25663da67a96448063175b7b374f18bc46fb814db4cd2d14138601065ea0a8a50fb).
Tutte le misure riportate restano riferite alla copia congelata iniziale; nessun risultato
prodotto dalla sonda e' stato salvato nel documento utente.

Sorgenti e prove: `cad_curve_solver.cpp`,`forgeCad2026_viewport_interaction.cpp`,
`forgeCad2026_viewport_selection.cpp`,`tests/test_viewport.cpp`;
aggiornati catalogo architetturale e memoria condivisa,con archivio precedente.
Sonde, file e log in`/tmp/bottle-idle-2026-11-08/`. Le misure sono locali,sotto carico variabile,
e non vanno interpretate come un'accelerazione globale garantita.
