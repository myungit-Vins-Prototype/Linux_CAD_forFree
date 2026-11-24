# CLAUDE.md — memoria operativa ForgeCAD

Aggiornato **17 novembre 2026**.

[Cronologia](docs/history/claude-2026-11-02.md); [stato prima di Part23](docs/history/claude-prima-part23-2026-11-14.md).

## Regole permanenti e decisioni

- **17/11: utente autorizza commit/push delle modifiche accumulate, poi proseguire la review.**
- Italiano; nessun `codex` nei nomi dei branch. `main` unico;
  `solver-mesh-gpu` è stato integrato e archiviato il 30 ottobre. CUDA e Metal restano backend opzionali negli stessi
  sorgenti, con fallback CPU.
- Aggiornare a ogni traguardo e prima della risposta finale; preservare le altre chat e distinguere
  implementato/verificato/sperimentale/proposto con prove e limiti.
- **Circa 200 righe, massimo 16 KiB e 10 interventi recenti**. Aggiornare le voci tematiche;
  dettagli in `docs/`/archivio, senza log grezzi o comandi ripetitivi.
- Correzioni generali del kernel; niente regole per singoli CAD/spigoli.
- **Geometria ingegneristica esatta**: coordinate e calcoli in `double`, curve analitiche/NURBS e solidi B-rep. Mesh,
  campioni di display, pixel, `float` e `QVector3D` servono alla visualizzazione/selezione, mai a booleane, misure o volumi.
  No CSG su mesh.
- Un solo kernel di produzione: `kernel/`, namespace `ForgeCad::Kernel`, C++17 puro senza Qt e senza OCCT. Anche STEP/IGES
  usano il kernel proprio. OpenCASCADE è facoltativo e confinato ai test di confronto in `kernel/occt_tests`; non deve
  entrare nelle dipendenze o nei flag dell'app.
- Tolleranze: seguire [politica unica](docs/tolerance-policy.md), `kernel/fk_precision.h` e `cad_kernel.h`. Distinguere
  risoluzione, connessione, fit e budget locali; non aumentare tolleranze o indebolire test per nascondere errori.
  La genealogia e i riferimenti topologici devono restare espliciti: non riassociare automaticamente per sola vicinanza.
- Conservare compatibilità dei documenti e valori delle enum persistite. Codice nuovo in moduli mirati `cad_*`/`kernel/fk_*`,
  evitando di ingrandire i coordinatori. Stile Qt, membri con `_`, quattro spazi e graffa sulla stessa riga.
- Prove su copie congelate, senza modificare CAD originali o chiudere processi utente. Eseguibile aggiornato solo dopo
  build completa; kernel/runner non aggiornano l’app aperta.

## Mappa essenziale e stato implementato

CAD parametrico 2D/3D Linux/macOS, C++17/Qt6/OpenGL3.3 Core. Finestra storica `PdfWindow`;
`pdfReader.c`, `pdfView.c` e binari PDF sono legacy esclusi.

| Area | Moduli e comportamento attuale |
| --- | --- |
| Finestra e interfaccia | `forgeCad2026_gui*`, `forgeCad2026_dialogs*`, `forgeCad2026_storyboard*`: ribbon, menu, Fusion chiaro/scuro/desktop persistente, pannelli incorporati agganciabili sinistra/destra o liberi, altezza piena opzionale e preferenze persistenti. |
| Vista e anteprime | `forgeCad2026_viewport*`, `cad_display_cache*`: camera, picking, rendering e passate separati; anteprime asincrone cancellabili e riuso del B-rep pronto alla conferma. Cache CPU/GL e rilascio della memoria; backend GPU per carichi specifici, senza sostituire la geometria esatta. |
| Schizzi e vincoli | `cad_types.h`, `cad_constraints*`, `cad_sketch_refs*`, `cad_expression*`: schizzi 2D/3D, cerchio3/asole/punti, polilinee, quote e solver, snap univoci e riferimenti associativi a piani/assi/bordi/curve. Cache automatiche nascoste; conversioni esplicite conservate; costruzione fuori modifica opzionale. Le entità locali visibili hanno priorità nella quotatura. |
| Solidi e superfici | `cad_forge*`, `kernel/fk_*`: primitive, estrusione (anche fino al prossimo corpo), rivoluzione, sweep/loft, booleane, sformo, svuotamento/spessore, filettatura, cucitura, taglio/estensione, reti U/V e poli NURBS. Estendi apre il pannello prima della scelta dei bordi. Raccordi costanti, conici, variabili e tra facce; percorsi tangenti con leggi indipendenti e maniglie R. |
| Storia e genealogia | `cad_history*`, `cad_element_names*`, viewport e storyboard: schizzi/piani come passi autonomi, riordino, rollback, soppressione, riparazione e Undo/Redo. Specchi/ripetizioni sono un evento con corpi separati ed esclusioni; conservano identità e dipendenze. |
| Documenti/revisioni | `cad_document_io*`, `cad_document_read*`, `cad_native_revisions*`, `cad_workspace*`: `.prt` con firma FCAD, formato 70 (ellisse per estremi; 69:contorni apparenti associativi; 68:nomi sorgenti; 67:precisione; 66:piani; 65: unione loft; 64: corrispondenze; 63: variazioni quote); Zstandard dal 56 e lettura legacy zlib. Definizione e cache B-rep/mesh distinte, salvataggio atomico, recupero, consultazione concorrente e lock della modifica. Check-out/check-in, ripristino, rilascio coordinato e recupero catalogo. |
| Tavole 2D | `cad_drawing_*`: `.drw`, viste proiettate/ausiliarie, sezioni composte/locali con contorno e profondita’, centri sui bordi scelti, dettagli e viste interrotte, più fogli, scale foglio/vista, quote e tolleranze, datum/GD&T, badge persistenti, rugosità Ra/Rz e PDF vettoriale. Agganci alle geometrie esatte e nomi genealogici; portabilità senza cache del modello nei casi migrabili. |
| Scambio e analisi | `kernel/fk_step*`, `fk_iges*`, `fk_exchange*`, `fk_mass*`: STEP/IGES propri, esportazione STL/OBJ, misure/massa, zebra e curvatura. Parsing numerico indipendente dal locale, controlli di cicli/profondità e dimensioni dei dati importati. |

## Compilazione, verifiche e documentazione

Dipendenze: CMake ≥ 3.21, compilatore C++17, Qt6 Widgets/OpenGL/OpenGLWidgets, OpenGL, zlib e zstd. Usare una build propria
di ogni macchina; non trasferire cache CMake o binari fra sistemi. Istruzioni complete: [README italiano](README.it.md) e
[CI](docs/ci.md).

```sh
cmake -S . -B forgecad-release -DCMAKE_BUILD_TYPE=Release -DFORGECAD_BUILD_VIEW_TESTS=ON
cmake --build forgecad-release --parallel 2
ctest --test-dir forgecad-release --output-on-failure
./forgecad-release/kernel/forgekernel_tests Shell
./forgecad-release/forgecad
```

- `FORGECAD_BUILD_OCCT_TESTS=OFF`: niente confronti OCCT/`forgekernel_tests`; restano i test indipendenti.
  Senza OCCT i confronti sono saltati con avviso. `FORGECAD_ENABLE_CUDA=OFF`: build CPU; altre opzioni in CMake/README.
- **17/11, richiesta utente:** test mirati all’area modificata; ripetere UI solo per modifiche UI, dipendenze coinvolte
  o regressioni concrete, non sistematicamente durante il lavoro kernel. Qt offscreen non certifica OpenGL.
  Distinguere piattaforma, skip, prove correnti, storiche e riferite dall'utente.
- 17/11, memoria: compilazioni/analisi seriali; ripulire solo ELF/archive ricreabili degli staging `/tmp` conclusi;
  conservare sorgenti, CAD, log e prove.
- App Linux: `forgecad-release/forgecad`; riaprirla dopo la build e verificare il collegamento effettivo.
  Non attribuire vecchi hash/test al nuovo HEAD.
- Architettura: prima delle modifiche consultare descrizioni e connessioni fra funzioni in `docs/architecture/index.html`/`graph.json`
  (richiesta permanente 13/11); `python3 tools/open_architecture.py` aggiorna all’apertura. Dopo modifiche strutturali aggiornare
  catalogo, rigenerare, `--check` e verificare HTML/collegamenti bidirezionali. CMake non rigenera; HTML/JSON ignorati.
  [Procedura](docs/architecture/README.md), dettagli permanenti in `AGENTS.md`.
- Approfondimenti pertinenti: [roadmap](docs/roadmap.md), [schizzi](docs/sketch-tools.md),
  [storia](docs/storyboard-history.md), [tavole/revisioni](docs/drawing-revisions-study.md),
  [prestazioni](docs/viewport-performance.md), [review corrente](docs/code-review-2026-11-15.md). Rapporti datati e archivio
  conservano riproduttori, misure e prove storiche.

## Limiti ancora utili alla prosecuzione

- Ripetizioni variabili: solo funzione/schizzi diretti, quote cliccabili, corpi esclusi; codec63/ID e ricalcolo isolato.
  App/13CTest/Wayland/mappa PASS [04/11](docs/ripetizioni-variabili-2026-11-04.md).
  Indagine girante13/11: Ripetizione3 lineare/Y/corpo intero; Estrusione4 corpo nuovo da Schizzo3.
  Su copia in memoria: Unisci a Rivoluzione1, Una funzione, circolareZ/3/360 e quota15+1 =>15/16/17mm;
  B-rep unione/ripetizione senza errori. Originale SHA256 invariato; nessuna modifica app o prova UI nuova.
  Grafico consultato. [Istruzioni e limiti](docs/girante-variazioni-2026-11-13.md).

- Svuotamento `Superfici.prt`: originali aperto/chiuso0,5/1mm verificati. Fixture scalata2,5, anche ruotata0,42, ora
  riesce a1,25mm dopo correzione proiezioneUV; aperto aggiuntivo PASS. Non è prova universale per superfici libere.
  [Prove e costo](docs/review-k10-offset-pose-2026-11-17.md).
- Unione `Flacone3`, Corpo1/4: RAW attuale PASS nei due ordini sugli stessi record storici; inverso con connessione
  tollerante1,475e-7. Recupero app conservato; nessuna garanzia globale. [Prove](docs/review-boolean-order-tabs-2026-11-17.md).
- Raccordi liberi, sformo generale, collasso dei raccordi nello svuotamento e alcune geometrie di contorno apparente restano
  limitati ai casi verificati. Le reti U/V sono celle Coons G0, non una superficie Gordon globale G1/G2. I controlli
  campionati dei poli non certificano l'assenza globale di auto-intersezioni.
- Riferimenti storici: conversioni di costruzione nei formati 57–59 indistinguibili dalle cache automatiche restano nascoste
  finché riconvertite; la promozione non duplica i vincoli. Per migrare vecchie tavole senza cache occorre prima aprire con
  la geometria storica e salvare la tavola migrata; riferimenti assenti o spostati possono richiedere verifica, senza
  riaggancio per prossimità.
- Review del 2 novembre: B8 irrobustito e verificato con reingresso iniettato, senza evento normale riprodotto;
  orfane e semplificazioni restano proposte. Contesto GL e pan verificati su Wayland/XCB; macOS e combinazioni Qt/OCCT
  diverse da quelle locali non ricertificate. CI storiche non ricertificano queste modifiche.
- Accelerazione: nessun guadagno globale 10× dimostrato. Benefici e limiti dei backend sono specifici dei casi misurati. Lock
  cooperativi e release locali non costituiscono autenticazione o permessi di rete; Windows e pacchetto autonomo di rilascio
  non sono certificati. Priorità proposte nella roadmap.

- Snap Intersezione completato: app/11CTest/Wayland/Undo/codec/mappa586-8138 PASS; fixture legacy corretta.
  [Dettagli](docs/snap-intersezione-2026-11-04.md). Divisione facce coincidenti rinviata.
- Loft: controlli/codec64 verificati: [rapporto](docs/loft-corrispondenze-2026-11-04.md).
  Hook: pieghe/unione/codec65 verificati. G1 punta OK; influenza25% riduce rigonfiamento, limiti fra pezze conservati. [Dettagli](docs/loft-hook-2026-11-04.md).

## Ultimi interventi — esiti consolidati

17/11 checkpoint: 40archivi/5712payload e hash sorgenti/binari verificati; mappa PASS. Commit/push autorizzati in corso, nuova CI da verificare.

**17/11 — UI11 menu cache/ribbon corretto; inventario review aggiornato.**
Due factory sostituite con addCadMenu: baseline13/17FAIL→17/17PASS, app/testtema/mappa PASS.
34rilievi corretti; K10parziale. Sei filoni K10 e altri limiti nell’indice; kernel/runner invariati.
[Elenco aperti](docs/review-punti-aperti-2026-11-17.md), [menu](docs/review-ui11-menu-cache-2026-11-17.md),
[quadratura](docs/review-k10-quadratura-2026-11-17.md), [memoria precedente](docs/history/claude-prima-menu-cache-stato-review-2026-11-17.md).

Q/E razionali17/11: app/6CTest/59/52mirati+SAN/ledger4+Fraction/mappa PASS.
[Q](docs/review-k10-pcurve-razionali-2026-11-17.md), [E](docs/review-k10-edge-razionali-2026-11-17.md).

Carrier istanziati17/11: app/6CTest/46mirati+SAN/Fraction/mappa PASS. [Rapporto](docs/review-k10-carrier-istanze-2026-11-17.md).

Mapping nonPlane17/11: app/8CTest/40mirati+SAN/API+Fraction52/mappa PASS; cap invariati,K10parziale.
[Rapporto](docs/review-k10-mapping-nonplane-2026-11-17.md).

Flacone3 RAW2/2 e schede/tema Wayland PASS; nessun fix,limiti di connessione e timeout documentati.
[Prove](docs/review-boolean-order-tabs-2026-11-17.md).

Offset/Superfici17/11: posa/scala/aperto corretti,cap invariati; app/21CTest/483kernel/19SAN/Fraction/mappa PASS.
[Prove, costo e limiti](docs/review-k10-offset-pose-2026-11-17.md).

17/11 K10 carrier2/3 razionali: app/16CTest/483kernel/67mirati+sanitizer/Fraction18fin/avvio/mappa PASS.
[Prove](docs/review-k10-carrier-razionali-2026-11-17.md).
K10 nonISO: app/22CTest/55mirati+SAN/Fraction/mappa PASS. [Prove e limiti](docs/review-k10-noniso-2026-11-17.md).

API legacy16/11:22candidate,16rimosse/6intenzionali;33bug invariati,app/10CTest/mappa PASS.
[Prove e limiti](docs/review-legacy-api-2026-11-16.md).

UI9/UI10: [prove storiche e limiti](docs/review-ui-colori-api-2026-11-16.md).

CI046685e Linux/macOS PASS: [log e perimetro](docs/ci-sheet-join-2026-11-16.md).

16/11 continuazioni Sol: app/28CTest/15mirati/SAN/Wayland/Fraction PASS,24/24parziali. [Limiti](docs/review-k10-boundary-2026-11-16.md).

K10 mitre/giunti/Mapping: app e27/22/26CTest PASS; certificati ISO/Plane parziali,
Newton e carrier generici campionati. [Mitre](docs/review-k10-mitre-2026-11-16.md),
[giunti](docs/review-k10-sheet-join-2026-11-16.md), [UV](docs/review-k10-mapping-uv-2026-11-16.md).
Dettagli conservati nella [memoria precedente](docs/history/claude-prima-legacy-api-2026-11-16.md).

UI8: [prove storiche e limiti](docs/review-theme-tabs-2026-11-16.md).

16/11 K12 cavità R0,5 corretto, budget invariati; app/34CTest/8varianti/6regressioni/SAN/Wayland/Fraction PASS.
[Audit734fin campionato e stato storico](docs/review-blend-cavity-2026-11-16.md).

K10 Helix/orfane e cucitura/PRE8: app/Wayland/sanitizer/Fraction e mappa PASS;
limiti campionati e dettagli nella [memoria precedente](docs/history/claude-prima-k10-noniso-2026-11-16.md),
[Helix](docs/review-k10-helix-orphans-2026-11-16.md), [cucitura](docs/review-k10-sew-preview-2026-11-16.md).

UV/PRE7: Plane e picker corretti e verificati; [prove e limiti](docs/review-k10-uv-preview-2026-11-16.md).

Supporto.prt/K11: Corpo2/3 e tre corpi uniti, app/24CTest/38Boolean/78sanitizer PASS;
output con UUID nuovo. Input prove SHA79887e91; originale successivo70e4ec5c non ricertificato.
[Dettagli e limiti](docs/supporto-booleana-2026-11-16.md).

PRE5/PRE6/UI2:11runner comuni,setup/footer/chiusura; app/24CTest/Wayland PASS.
[Prove e limiti](docs/review-preview-unification-2026-11-16.md).

Ellisse per estremi: tre clic, quattro quote/vincoli, centro derivato e codec70.
App/24CTest/sanitizer/Wayland/GL/mappa PASS. [Uso e limiti](docs/ellisse-estremi-2026-11-16.md).

K2 in produzione:app/54kernel/8CTest/22sanitizer PASS; risorsefinite possono rifiutarecurvevalide.
[Prove/limiti](docs/sweep-k2-completamento-2026-11-15.md).

15/11: K4/K5/K6,S1/S2,UI1–6,PRE1–3 corretti; app/74kernel/34CTest/mappa PASS.
[Dettagli e limiti conservati](docs/review-fix-next-2026-11-15.md).

K1/K3 corretti: app/42kernel/16CTest/mappa PASS. [Prove](docs/sweep-review-fix-2026-11-15.md).

Cache M1/M2: snapshot/CAS e copie atomiche; app/47CTest/5ASan/TSan PASS, timeout generale documentato.
[Prove e limiti](docs/cache-bspline-2026-11-15.md).

Review iniziale15/11:19difetti/22candidate orfane; nessun leak permanente certificato.
[Prove e stato](docs/code-review-2026-11-15.md); [memoria](docs/history/claude-prima-preview-unification-2026-11-16.md).

Sweep/vincoli15/11:96casi,codec/Undo,app/mappa; volumeNURBS poi correttoK3.
[Geometria](docs/exhaust-mainfold-sweep-2026-11-15.md),
[solver](docs/vincoli-tangenza-transitiva-2026-11-15.md); dettagli in memoria precedente.

Schede14/11: [prove/limiti](docs/schede-documenti-2026-11-14.md).

CT0578/tavole14/11: codec,app,Wayland/PDF eCTest PASS;
[prestazioni e template](docs/ct0578-tavole-template-2026-11-14.md),
[agganci](docs/part23-quote-associative-cache-2026-11-14.md),
[sezioni e snap](docs/sezione-locale-snap-profondita-2026-11-14.md).
Esiti,limiti eprove estese nella [memoria conservata](docs/history/claude-prima-k10-edge-offset-2026-11-16.md).

Archivi precedenti: [Part23/raccordi/storyboard](docs/history/claude-prima-review-next-2026-11-15.md),
[contorno apparente](docs/history/claude-prima-sweep-review-fix-2026-11-15.md),
[riferimenti/anteprime](docs/history/claude-prima-schede-2026-11-14.md),
[Applicatore4, poli, memoria, KnifeHandle e quote2D](docs/history/claude-prima-review-2026-11-15.md).
