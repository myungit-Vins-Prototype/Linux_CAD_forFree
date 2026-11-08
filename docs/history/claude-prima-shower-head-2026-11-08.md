# CLAUDE.md — memoria operativa ForgeCAD

Aggiornato **8 novembre 2026**.

[Cronologia](docs/history/claude-2026-11-02.md).

## Regole permanenti e decisioni

- Italiano; nessun `codex` nei nomi dei branch. `main` unico;
  `solver-mesh-gpu` è stato integrato e archiviato il 30 ottobre. CUDA e Metal restano backend opzionali negli stessi
  sorgenti, con fallback CPU.
- Aggiornare a ogni traguardo e prima della risposta finale; rileggere e preservare le altre chat. Distinguere
  implementato/verificato/sperimentale/proposto con prove e limiti.
- **Circa 200 righe, massimo 16 KiB e 10 interventi recenti**. Aggiornare le voci tematiche;
  dettagli in `docs/`/archivio, senza log grezzi o comandi ripetitivi.
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
| Finestra e interfaccia | `forgeCad2026_gui*`, `forgeCad2026_dialogs*`, `forgeCad2026_storyboard*`: ribbon, menu, Fusion chiaro/scuro, pannelli incorporati agganciabili sinistra/destra o liberi, altezza piena opzionale e preferenze persistenti. |
| Vista e anteprime | `forgeCad2026_viewport*`, `cad_display_cache*`: camera, picking, rendering e passate separati; anteprime asincrone cancellabili e riuso del B-rep pronto alla conferma. Cache CPU/GL e rilascio della memoria; backend GPU per carichi specifici, senza sostituire la geometria esatta. |
| Schizzi e vincoli | `cad_types.h`, `cad_constraints*`, `cad_sketch_refs*`, `cad_expression*`: schizzi 2D/3D, asole/punti, quote e solver, snap e riferimenti associativi a piani/assi/bordi/curve. Cache automatiche nascoste; conversioni esplicite conservate; costruzione fuori modifica opzionale. Le entità locali visibili hanno priorità nella quotatura. |
| Solidi e superfici | `cad_forge*`, `kernel/fk_*`: primitive, estrusione (anche fino al prossimo corpo), rivoluzione, sweep/loft, booleane, sformo, svuotamento/spessore, filettatura, cucitura, taglio/estensione, reti U/V e poli NURBS. Estendi apre il pannello prima della scelta dei bordi. Raccordi costanti, conici, variabili e tra facce; percorsi tangenti con leggi indipendenti e maniglie R. |
| Storia e genealogia | `cad_history*`, `cad_element_names*`, viewport e storyboard: schizzi/piani come passi autonomi, riordino, rollback, soppressione, riparazione e Undo/Redo. Specchi/ripetizioni sono un evento con corpi separati ed esclusioni; conservano identità e dipendenze. |
| Documenti e revisioni | `cad_document_io*`, `cad_document_read*`, `cad_native_revisions*`, `cad_workspace*`: `.prt` con firma FCAD, formato 67 (precisione; 66:piani; 65: unione loft; 64: corrispondenze; 63: variazioni quote); Zstandard dal 56 e lettura legacy zlib. Definizione e cache B-rep/mesh distinte, salvataggio atomico, recupero, consultazione concorrente e lock della modifica. Check-out/check-in, ripristino, rilascio coordinato e recupero catalogo. |
| Tavole 2D | `cad_drawing_*`: `.drw`, viste proiettate/ausiliarie, sezioni composte, dettagli e viste interrotte, più fogli, scale foglio/vista, quote e tolleranze, datum/GD&T, badge persistenti, rugosità Ra/Rz e PDF vettoriale. Agganci alle geometrie esatte e nomi genealogici; portabilità senza cache del modello nei casi migrabili. |
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
- Test pertinenti, inclusi persistenza/Undo/storia se coinvolti; Qt offscreen non certifica OpenGL: usare la sessione grafica.
  Distinguere piattaforma, skip, prove correnti, storiche e riferite dall'utente.
- Sul checkout Linux abituale l'app è `forgecad-release/forgecad`; serve riaprirla per caricare una nuova build. Verificare
  la destinazione effettiva del collegamento, senza attribuire vecchi hash o test al nuovo HEAD.
- Architettura: `python3 tools/open_architecture.py` aggiorna all’apertura; dopo modifiche strutturali aggiornare
  catalogo, rigenerare, `--check` e verificare HTML/collegamenti bidirezionali. CMake non rigenera; HTML/JSON ignorati.
  [Procedura](docs/architecture/README.md), dettagli permanenti in `AGENTS.md`.
- Approfondimenti pertinenti: [roadmap](docs/roadmap.md), [schizzi](docs/sketch-tools.md),
  [storia](docs/storyboard-history.md), [tavole/revisioni](docs/drawing-revisions-study.md),
  [prestazioni](docs/viewport-performance.md), [review corrente](docs/code-review-2026-11-02.md). Rapporti datati e archivio
  conservano riproduttori, misure e prove storiche.

## Limiti ancora utili alla prosecuzione

- Ripetizioni variabili (04/11): codec63/ID quote, incremento per copia e ricalcolo isolato.
  Correzione richiesta: solo funzione/schizzi diretti, quote cliccabili nel viewport, corpi esclusi.
  App/13CTest/Wayland/mappa PASS. [Prove](docs/ripetizioni-variabili-2026-11-04.md).

- Svuotamento `Superfici.prt`: riusciti fondo piano aperto e guscio chiuso a 0,5/1 mm sul modello originale. La variante con
  geometria scalata 2,5 e ruotata 0,42 rad fallisce ancora il fit di un bordo offset; il prototipo con base locale non è
  integrato. Limite generale aperto.
- Unione `Flacone3`, Corpo 1/4: l'app gestisce entrambi gli ordini con un tentativo inverso validato dopo errore. La chiamata
  kernel grezza nell'ordine inverso può ancora fallire su un contatto tollerante.
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

**08/11 — Bottle, RAM/precisione.** Pool esatto:690 duplicati,poli/pesi1042→135MiB;
RSS caldo9,95→2,75GiB;30 B-rep byte-identici,codec67/Wayland PASS. Idle0frame/1,9s,
0,1msCPU; nuovo schizzo senza rebuild. Profilo1e-7..1e-10mm persistente,worker/UI/Undo.
App/32CTest (snapshot incluso)/mappa647-9254/check/HTML PASS; riaprire l'app.
Profilo fine sull'intero Bottle non certificato. [Prove](docs/bottle-memoria-precisione-2026-11-08.md).
Snap precedente90→5–10ms/app/11CTest/GL/mappa PASS; [prove](docs/bottle-prestazioni-2026-11-08.md).
07/11: [offset±3mm](docs/bottle-offset-2026-11-07.md) PASS; [guide](docs/bottle-ripetizione-2026-11-07.md):scarti0,33mm.
[Raccordi](docs/bottle-raccordi-2026-11-07.md):R1,675/93facce,app/10CTest/62Blend/Wayland/Undo/codec/GL/mappa PASS.
Storico dettagliato preservato [qui](docs/history/claude-prima-bottle-prestazioni-2026-11-08.md).

**07/11 — Onion2, sezioni/loft/raggio.** Sezioni app/8CTest/Wayland/Undo/codec PASS;
[sezioni](docs/onion2-sezioni-piani-2026-11-07.md). Loft3/4: sottoinsiemi persistiti/migrazione univoca;
app/13CTest/Wayland/Undo/codec/no-cache/rebuild PASS; `output/Onion2-superficie.prt`:1faccia,31,5107mm².
[Loft](docs/onion2-loft-schizzi-2026-11-07.md):mappa632/8958/check/4HTML PASS.
Raccordi: due poli valenza18 e due cambi di convessità/giunto; ricostruzione simultanea con calotta e collassi G1.
Tutti18 aR0,092797mm,73facce/0errori/genealogia completa; passo0,092798 rifiutato, massimo numerico verificato.
`output/Onion2-raccordato.prt`; originale invariato. App/6CTest/3polari/62Blend/Wayland UI/Undo/codec/no-cache/rebuild/GL PASS.
Mappa635/9017/check/5HTML bidirezionali PASS. App aggiornata, riaprire; massimo globale/massa non certificati.
[Rapporto](docs/onion2-raccordi-2026-11-07.md), [storico](docs/history/claude-prima-raggio-onion2-2026-11-07.md).

**07/11 — Onion, ripetizione/cucitura/raccordi.** Calcolo18totali/20°: Bezier razionali entro1e-7mm;
app/8CTest/Wayland/18giunti/Undo/codec/no-cache PASS, giunti<=1,53e-14mm; `output/Onion-bordi-combacianti.prt`.
Originale aggiornato esternamente17→18; ricalcolare dal pulsante dopo modifiche al loft.
Cucitura: poli collassati normalizzati entro arrotondamento e SP-curve preservate; app/14CTest/123kernel/Wayland/codec/mappa PASS.
Cuspide della lamina18: R0 esatto, limiti unilaterali/UI/maniglie;36facce/tangenza/mesh/Undo/codec/no-cache/GL PASS.
`output/Onion-raccordato.prt` R0,3→0; scartoR1,44e-9mm,contatto5,20e-11mm;app/21CTest/62Blend/12box/4pose PASS.
Mappa631/8939/check/15HTML PASS. Lamina aperta: disattivare Crea un solido o aggiungere fondo;
raggio positivo alla cuspide della lamina aperta resta non gestito. [Rapporto](docs/onion-ripetizione-cucitura-2026-11-07.md).
Dettagli precedenti: [archivio](docs/history/claude-prima-sezioni-onion2-2026-11-07.md).

**06/11 — Quote 2D.** Ricollegamento agganci rotti/singoli con snap, anteprima e conferma atomica;
metadati conservati. App/18CTest/offscreen/Wayland/mappa628-8882/check/HTML PASS. Pubblicato `56ae114` su origin/main.
[Prove e file](docs/ricollegamento-quote-2d-2026-11-06.md).

**05/11 — KnifeHandle.** Unione NURBS/seam:6CTest/38Boolean/5Unify/42Loft/Wayland/STEP PASS.
[Unione](docs/knifehandle-booleana-2026-11-05.md). Loft7: inversione ai nodi elimina microcampata;
cache13 rigenera copie, check-in immutabili. App/9CTest/42Loft/G1/Wayland/codec/mappa PASS.
[Tangenza](docs/knifehandle-loft7-tangenza-2026-11-05.md).
Tagli4/5 e1/4: filtri box/Bezier, budget400k invariato.
1/4:2+3regioni/6scelte, risultato connesso12facce/0bordi liberi, lamina chiusa.
App/5CTest/130kernel/Wayland/Undo/codec/mappa598-8332/check/3HTML PASS.
[Tagli](docs/knifehandle-taglio-reciproco-2026-11-05.md).

**05/11 — Loft punti/piani.** Sezioni puntuali/G1 piano, codec66/hash65.
App/17CTest/42Loft/11Mass/Wayland PASS; mappa596/8294/check/7HTML PASS.
[Dettagli](docs/loft-punti-piani-2026-11-05.md).

**05/11 — UI/checkpoint/mappature.** `e1bde03` su origin/main,13CTest PASS.
ISO M1–M68, ribbon, loft440px, schizzi vuoti: app/GUI PASS.
[UI](docs/ui-ribbon-loft-filettature-2026-11-05.md).
Mappature:4pattern,Taglio12:F2–F5,multifaccia/posa rigida155, app/18CTest PASS.
`7894f64` nel checkpoint. [Prove](docs/riempimento-sviluppo-adattamento-2026-11-05.md).
**06/11 — Superfici/storia.** `3188714`/`bbd2533`/`a6248ea` pubblicati.
Ripetizione117copie,Unione168:1188facce/1solido/155s; [superfici](docs/confronto-flatten-quilt-creo-2026-11-06.md),
[unione](docs/knifehandle-unione-rilievi-2026-11-06.md). Storia incrementale1–5ms/29CTest/Wayland/B-rep invariati:
[rapporto](docs/rigenerazione-incrementale-2026-11-06.md). HoneyRepeat:13solidi/circolare2–12istanze/262–2767ms;
[taglio](docs/honeyrepeat-taglio-tra-superfici-2026-11-06.md),[booleane](docs/booleane-selezione-rettangolo-2026-11-06.md).
`e761f43`:467kernel/37CTest/CI PASS; [CI](docs/ci-raccordi-2026-11-06.md).
Surface219:91facce cucite/0bordi liberi/29,7s; app/11CTest/5kernel/Wayland/Undo/codec/mappa PASS,
ricalcolo integrale non verificato. [Cucitura](docs/surface219-cucitura-2026-11-06.md).
Cache lettura<=RAM/8 e4GiB configurabile; apertura28,361s/tavola1,261s/0errori,app/18CTest/Wayland PASS.
Loft36:derivate locali264→7,8s; storia senza cache137s/0errori,Svuota69s;app/20CTest/kernel/mappa PASS.
[Ricalcolo](docs/surface219-ricalcolo-2026-11-06.md),[apertura](docs/surface219-tavola-apertura-2026-11-06.md).
CT0578:fix UV36feature/0errori,17CTest/468kernel/Wayland/mappa PASS;CI42266591981 Linux75/2SKIP,macOS79/1SKIP.
[Prove](docs/ct0578-regressione-filetto-2026-11-06.md); dettagli precedenti nell'archivio Bottle08/11.

**04/11 — Griglie, specchi e guide.** `3c644e2`: istanze condivise, B-rep2/cache10;
`9eff99c`: appoggi G1/riordino e F7/E25 verificati. Griglie/cache11: PRT190,5→88,7MB,
STEP745,1→458,7MB; shell928,5→642,7MB. App/runner/QA/suite PASS.
[Specchi](docs/riduzione-complessita-specchi-2026-11-03.md),[guide](docs/loft-controlli-guide-2026-11-04.md),
[griglie/budget](docs/riduzione-griglie-loft-step-2026-11-04.md),[F7/E25](docs/loft-selezione-facce-storiche-2026-11-04.md).
[Dettagli storici](docs/history/claude-prima-taglio-reciproco-2026-11-05.md).

**04/11 — Svuotamento/apertura.** Cache12/poli, app/kernel/Wayland/XCB PASS, `d0594e8` pubblicato.
[Prove](docs/svuotamento-poli-non-finiti-2026-11-04.md); apertura17f72219:3,87s/Bernstein3,09×,
[misure](docs/apertura-tempi-surface219-2026-11-04.md), [dettagli](docs/history/claude-prima-bottle-2026-11-07.md).

Storico 01–03/11: Schizzi/riferimenti, Loft9/guide, review/storyboard e checkpoint:
[archivio completo precedente a Onion](docs/history/claude-prima-onion-2026-11-07.md).
Restano validi i limiti operativi sopra; esiti CI/kernel storici non ricertificano il nuovo lavoro.
