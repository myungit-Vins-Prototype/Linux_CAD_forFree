# CLAUDE.md — memoria operativa ForgeCAD

Aggiornato il **3 novembre 2026**. Leggere questo file e `AGENTS.md` all'avvio; approfondire solo l'area del lavoro
richiesto. Questa sintesi descrive ciò che resta implementato, i limiti utili e gli ultimi interventi.

La cronologia precedente è conservata integralmente in [archivio al 2 novembre](docs/history/claude-2026-11-02.md). **Non
caricare l'archivio per intero nel contesto**: cercare un argomento o una data e leggere solo la voce pertinente. Le sue
diciture «in corso» o «non committato» sono storiche; per lo stato attuale prevalgono questa sintesi e i sorgenti.

## Regole permanenti e decisioni

- Lavorare e comunicare in italiano. Non usare `codex` nei nomi dei branch. Lo sviluppo è unificato su `main`;
  `solver-mesh-gpu` è stato integrato e archiviato il 30 ottobre. CUDA e Metal restano backend opzionali negli stessi
  sorgenti, con fallback CPU.
- Aggiornare questa memoria a ogni traguardo e prima della risposta finale, anche per indagini e documentazione. Rileggere
  prima di modificare; preservare le informazioni delle altre chat. Distinguere implementato, verificato, sperimentale e
  proposto; indicare verifiche effettive, limiti e lavoro aperto.
- Mantenere **circa 200 righe e al massimo 16 KiB**, con non più di 10 interventi recenti sintetici. Aggiornare la voce
  tematica invece di accumulare diagnosi, tentativi e successivi stati della stessa modifica. Spostare i dettagli in rapporti
  `docs/` o nell'archivio, senza perderli. Non inserire log grezzi, lunghe liste di hash o comandi di ogni passaggio.
- **Geometria ingegneristica esatta**: coordinate e calcoli in `double`, curve analitiche/NURBS e solidi B-rep. Mesh,
  campioni di display, pixel, `float` e `QVector3D` servono alla visualizzazione/selezione, mai a booleane, misure o volumi.
  Nessun CSG su mesh.
- Un solo kernel di produzione: `kernel/`, namespace `ForgeCad::Kernel`, C++17 puro senza Qt e senza OCCT. Anche STEP/IGES
  usano il kernel proprio. OpenCASCADE è facoltativo e confinato ai test di confronto in `kernel/occt_tests`; non deve
  entrare nelle dipendenze o nei flag dell'app.
- Tolleranze: seguire [politica unica](docs/tolerance-policy.md), `kernel/fk_precision.h` e `cad_kernel.h`. Distinguere
  risoluzione, connessione, fit, parametri e budget locali; non aumentare tolleranze o indebolire test per nascondere errori.
  La genealogia e i riferimenti topologici devono restare espliciti: non riassociare automaticamente per sola vicinanza.
- Conservare compatibilità dei documenti e valori delle enum persistite. Codice nuovo in moduli mirati `cad_*`/`kernel/fk_*`,
  evitando di ingrandire i coordinatori. Stile Qt, membri con `_`, quattro spazi e graffa sulla stessa riga.
- Non modificare i CAD originali per le prove; usare copie congelate. Non chiudere processi utente. Dichiarare aggiornato
  l'eseguibile soltanto dopo la compilazione completa; una build del kernel o del runner non aggiorna l'app già aperta.

## Mappa essenziale e stato implementato

ForgeCAD è un CAD parametrico 2D/3D per Linux e macOS, in C++17/Qt6 con viewport OpenGL 3.3 Core. `PdfWindow` è il nome
storico della finestra principale. `pdfReader.c`, `pdfView.c` e vecchi binari PDF sono legacy, esclusi dall'app.

| Area | Moduli e comportamento attuale |
| --- | --- |
| Finestra e interfaccia | `forgeCad2026_gui*`, `forgeCad2026_dialogs*`, `forgeCad2026_storyboard*`: ribbon, menu, Fusion chiaro/scuro, pannelli incorporati agganciabili sinistra/destra o liberi, altezza piena opzionale e preferenze persistenti. |
| Vista e anteprime | `forgeCad2026_viewport*`, `cad_display_cache*`: camera, picking, rendering e passate separati; anteprime asincrone cancellabili e riuso del B-rep pronto alla conferma. Cache CPU/GL e rilascio della memoria; backend GPU per carichi specifici, senza sostituire la geometria esatta. |
| Schizzi e vincoli | `cad_types.h`, `cad_constraints*`, `cad_sketch_refs*`, `cad_expression*`: schizzi 2D/3D, asole/punti, quote e solver, snap e riferimenti associativi a piani/assi/bordi/curve. Cache automatiche nascoste; conversioni e sezioni esplicite visibili anche di costruzione. Le entità locali visibili hanno priorità nella quotatura. |
| Solidi e superfici | `cad_forge*`, `kernel/fk_*`: primitive, estrusione (anche fino al prossimo corpo), rivoluzione, sweep/loft, booleane, sformo, svuotamento/spessore, filettatura, cucitura, taglio/estensione, reti U/V e poli NURBS. Estendi apre il pannello prima della scelta dei bordi. Raccordi costanti, conici, variabili e tra facce; percorsi tangenti con leggi indipendenti e maniglie R. |
| Storia e genealogia | `cad_history*`, `cad_element_names*`, viewport e storyboard: schizzi/piani come passi autonomi, riordino, rollback, soppressione, riparazione e Undo/Redo. Specchi/ripetizioni sono un evento con corpi separati ed esclusioni; conservano identità e dipendenze. |
| Documenti e revisioni | `cad_document_io*`, `cad_document_read*`, `cad_native_revisions*`, `cad_workspace*`: `.prt` con firma FCAD, formato corrente 61 (versi G1/G2); Zstandard dal 56 e lettura legacy zlib. Definizione e cache B-rep/mesh distinte, salvataggio atomico, recupero, consultazione concorrente e lock della modifica. Check-out/check-in, ripristino, rilascio coordinato e recupero catalogo. |
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

- `FORGECAD_BUILD_OCCT_TESTS=OFF` esclude i confronti OCCT; senza OCCT vengono saltati con avviso e `forgekernel_tests` non
  esiste. I test indipendenti restano disponibili. `FORGECAD_ENABLE_CUDA=OFF` consente una build CPU anche con toolkit
  installato; per le altre opzioni vedere CMake/README.
- Eseguire i test pertinenti al cambiamento, inclusi persistenza/Undo/storia quando coinvolti. Le prove Qt offscreen non
  attestano la resa OpenGL: usare una sessione grafica per quella. Indicare sempre piattaforma, skip e differenza fra esiti
  della sessione, storici e riferiti dall'utente.
- Sul checkout Linux abituale l'app è `forgecad-release/forgecad`; serve riaprirla per caricare una nuova build. Verificare
  la destinazione effettiva del collegamento, senza attribuire vecchi hash o test al nuovo HEAD.
- Vista architetturale: avviare `python3 tools/open_architecture.py` per controllo/aggiornamento all'apertura. Dopo
  cambiamenti strutturali aggiornare descrizioni/gruppi in `tools/generate_architecture.py`, rigenerare sulla build
  pertinente e verificare `python3 tools/generate_architecture.py --check`; controllare collegamenti in entrambi i versi e
  resa HTML, distinguendo call/include/reference. La compilazione CMake non rigenera la mappa. `docs/architecture/index.html`
  e `graph.json` sono generati e ignorati da Git: non forzarne l'aggiunta. [Istruzioni](docs/architecture/README.md).
- Approfondimenti da aprire solo se pertinenti: [roadmap](docs/roadmap.md), [schizzi](docs/sketch-tools.md),
  [storia](docs/storyboard-history.md), [tavole/revisioni](docs/drawing-revisions-study.md),
  [prestazioni](docs/viewport-performance.md), [review corrente](docs/code-review-2026-11-02.md). Rapporti datati e archivio
  conservano riproduttori, misure e prove storiche.

## Limiti ancora utili alla prosecuzione

- Svuotamento `Superfici.prt`: riusciti fondo piano aperto e guscio chiuso a 0,5/1 mm sul modello originale. La variante con
  geometria scalata 2,5 e ruotata 0,42 rad fallisce ancora il fit di un bordo offset; il prototipo con base locale non è
  integrato. Nessuna soluzione universale per superfici libere dichiarata.
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
  diverse da quelle locali non ricertificate. Le vecchie CI verdi non attestano queste modifiche.
- Accelerazione: nessun guadagno globale 10× dimostrato. Benefici e limiti dei backend sono specifici dei casi misurati. Lock
  cooperativi e release locali non costituiscono autenticazione o permessi di rete; Windows e pacchetto autonomo di rilascio
  non sono certificati. Priorità proposte nella roadmap.

## Ultimi interventi — esiti consolidati

0. **03/11 — Pannelli, piani e verso G1/G2.** Area unica sotto il ribbon per aggancio/altezza
   piena/drag/resize; OK/Annulla puliscono il comando, Rimuovi per ogni riferimento del piano.
   Clic corretto: niente reset50ms durante la pressione, rebuild rimandato al rilascio; prove0/120/350ms.
   Inverti normale G1/G2 indipendente, anteprima e formato61; firme tavola60 compatibili.
   App completa; CTest15/15, Loft42/42, forma10/10, ribbon offscreen e prove mirate Wayland/XCB PASS.
   Mappa552/7642/261AST,8confini/check/HTML PASS. [Rapporto e prove](docs/ui-pannelli-verso-loft-2026-11-03.md).
   Suite native complete limitate da cattura popup/vecchio test diagnostico; originali intatti.

1. **02–03/11 — CI, guide proiettate e Loft9 (`e54a174`, pubblicato).** CI41513461524
   verde su `ec08b12`: Linux56PASS/2SKIP, macOS60PASS/1SKIP; esito storico.
   Loft9: algebra esatta/denominatore comune, modulo Hermite comune, guide curvilinee e correzione
   cubica pesata. Corner sorgente3,18° conservato. Cache8 migra le copie di lavoro con loft/derivati;
   check-in immutabili, formato60. App completa, Wayland/XCB1→2→1guide, Undo/Redo e cache/no-cache PASS.
   Consolidati164PASS/1SKIP GL/0FAIL su165, kernel459/459; Clang/ASan/UBSan mirati PASS, LSan escluso ptrace.
   Mappa552/7635/261AST verificata. [Rapporto completo](docs/loft-forma-guide-2026-11-03.md),
   [CI](docs/ci-riparazione-2026-11-02.md), [guide](docs/loft-guide-proiettate-2026-11-02.md).
   Unione/curve `dbed027`/`cc26a27`: [rapporto](docs/estrusione-unione-riferimenti-proiettati-2026-11-02.md).

2. **02/11 — Correzioni review, storyboard e nuova estrusione (`d4f15fc`, pubblicato).** R1–R5 corretti:
   parser/scambio STEP-B-rep robusti, occorrenze distinte e catalogo concorrente senza perdita dei lock.
   Agenti GUI/storyboard: picking, pan, highlight, rimappature, identità copie/Pierce/featureOnly,
   componenti DeleteFace e visibilità consumo/recupero tip. B8 solo con reingresso iniettato.
   «Fino al prossimo corpo» B-rep, dipendenze, versi, preview/genealogia; formato 60 invariato.
   App/runner PASS; CTest consolidato 151 PASS/1 SKIP/1 FAIL, kernel 457/458: sformo preesistente
   `test_draft.cpp:134`, scarto1,794e-6>1e-6. Robustezza5/5 anche sanitizer mirati, STEP20/20,
   IGES9/9, BodyIO2/2; GUI6/6 Wayland/XCB. Mappa531file/7305relazioni/249TU, check/HTML PASS.
   [Dettagli, prove e limiti](docs/code-review-2026-11-02-correzioni.md); originali intatti.
3. **02/11 — OCCT soltanto nei test (`ab6fc71`).** Ricerca OCCT spostata in `kernel/occt_tests`, opzione facoltativa e
   README/CI aggiornati; app e kernel senza dipendenze OCCT. Configurazione minima senza OCCT verificata; l'utente conferma
   zero `HAVE_TK` nei flag dell'app dopo riconfigurazione. Test comparativi e CI successivi allo spostamento non confermati
   nel registro.
4. **02/11 — Bug della review (`9321643`).** Corretti cicli/ricorsioni STEP, conteggi e spline IGES, espressioni numeriche
   ambigue/profonde, rilascio GlassCompositor, compatibilità Qt 6.4, normale degenere nello specchio e catture C++17;
   aggiunte regressioni. Nel cloud: app/runner compilati, Expression e gruppi
   Reflection/Transform/Mirror/Step/Iges/Exchange/Offset/Shell/Thicken PASS; ASan/UBSan e fuzzing mirati senza i crash
   riprodotti sul vecchio codice. L'utente riferisce build/test locali riusciti, log non riletto. Mappa cloud verificata,
   vista locale da aggiornare con la propria build.
5. **02/11 — Quote costruzione, normali e svuotamento (`de1448a`, registro `7651724`).** Push completato con tutte le 16
   modifiche allora presenti. Quotatura diretta della costruzione coincidente con piani/assi; normale solo in
   definizione/hover, disattivabile da Visualizza e persistente; svuotamento delle giunzioni tolleranti con budget locali e
   SP-curve conservate. App completa compilata; Shell 18/18, Boolean 38/38, Offset 25/25 e CTest pertinenti 16/16 PASS.
   Quattro gusci reali verificati da pannello, anteprima, Undo/Redo e salvataggio/riapertura; limite trasformato distinto
   sopra.
6. **01/11 — Schizzi, Estendi e unione (`8121ed7`, registro `45d0ab1`).** Conversioni/sezioni esplicite visibili senza
   mostrare cache automatiche; formato 60 e promozione delle conversioni storiche. Estendi apre subito il pannello. Unione
   Corpo 1/4 di Flacone3 corretta nell'app in entrambi gli ordini. App compilata, regressioni e QA XCB/Wayland PASS; storia,
   persistenza e genealogia verificate. Incluse anche correzioni a raccordi e scambio STEP/IGES, preservando spline e
   intervalli esatti.
7. **31/10 — Raccordi avanzati e pannelli (`a485349`).** Quattro modalità Raggio, coniche con parametro ρ, variabili con 2–64
   stazioni, percorsi tangenti multipli con tabelle indipendenti e maniglie trascinabili. Pannelli compatti agganciabili,
   immagini adattate al tema, riferimenti schizzo associativi diretti. Build, regressioni e QA native documentate; limiti
   geometrici specifici conservati nei rapporti/archivio.
8. **30/10 — Tavole e portabilità.** Snap delle quote nel primo posizionamento e trascinamento, allineamento H/V, punte
   coincidenti, Alt/Esc e Undo unico; rugosità strutturate Ra/Rz, sezioni composte con mouse/snap. Portabilità CT0578 migrata
   con nomi/testimoni esatti: 48/48 quote, asse e nota verificati dopo salvataggio senza cache e trasferimento. Interventi
   implementati e collaudati, con limiti di migrazione indicati sopra.
9. **30/10 — Unificazione su main e CI.** Integrato e archiviato il ramo solver/GPU preservando il fallback CPU. Al
   checkpoint di unificazione: Linux 47 PASS/2 SKIP e macOS 51 PASS/1 SKIP, CUDA locale 7/7 e Metal in CI PASS; modelli reali
   confrontati senza attribuire tempi a benchmark. Questi esiti sono storici, non verifiche dell'ultimo HEAD. Rapporti
   `docs/unificazione-main-2026-10-30.json` e `docs/ci.md`.

Per riprendere lavori più vecchi, cercare nell'archivio o in Git e poi verificare i sorgenti attuali. Questa memoria non
sostituisce i rapporti di prova e non richiede di caricarli tutti.
