# CLAUDE.md — memoria operativa ForgeCAD

Aggiornato **11 novembre 2026**.

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

**10/11 — CI GitHub.** Otto run falliti dopo build: offset180s e chiusura raccordo MacR0,06.
Budget offset900s; Newton smorzato come ultimo recupero, quarta rotazione+1e-14 di regressione.
Runner Ubuntu usava mirrorlistAPT ancora su Azure: corrette anche quelle, HTTPS/firme conservati.
`5ad19c4` codice e `994c771` workflow su origin/main. CI42789007301 **PASS Linux/macOS**:
Linux82suite:80PASS/2SKIP; Mac85:84PASS/1SKIP; kernel475/475 e raccordo PASS su entrambi.
Offset181,56s Linux/194,29s Mac; prova locale2core189s conferma limite180 insufficiente.
App GCC/Clang(pulita),kernel475 su entrambi,40rotazioni,mappa676-9751/check/4HTML PASS.
Artefatti verificati: download Linux da archivioHTTPS, zeroAzure; soli SKIP grafici previsti.
[Diagnosi e prove](docs/ci-riparazione-2026-11-10.md).

**11/11 — Poli NURBS.** Hook: griglie, refit vincolante, multifaccia, analitiche esatte,
G0/G1/trascinamento morbido e occlusione GPU fino `8bdfa20`: app GCC/Clang,11CTest,Wayland1×/2×,
Hook2/7 e CI Linux/macOS42852161648 PASS. Milioni di poli virtuali, gabbia ridotta.
[Hook](docs/poli-nurbs-Hook-corpo2-2026-11-09.md),[influenza](docs/poli-nurbs-trascinamento-morbido-2026-11-10.md),
[selezione](docs/poli-nurbs-selezione-visibile-2026-11-10.md),[occlusione](docs/poli-nurbs-occlusione-2026-11-10.md).
Hook corrente cambiato esternamente; prove storiche su copia congelata.
Nuovo `Sphere_deformed.prt`: F0 quadratica razionale9×5,24poli mossi; G1 legacy omette i nodi interni.
Implementati jet G1/G2 interni/cuciture e fasce affini alle calotte;20/20kernel PASS. File reale G2:residui<3e-12.
Schema16, G1 nella sessione e titolo/ribbon: app/runner,11CTest,Wayland1×/2×/multifaccia,Undo/codec PASS.
40/40kernel superfici e sintassi6sorgenti Clang PASS; mappa681/9800/334TU/0diagnostiche/check/4HTML PASS; `ddc02f9` su origin/main; CI42982288794 in corso.
Selftest Ribbon Wayland: cattura popup fallisce anche col modulo precedente; percorso poli PASS.
Refit incompatibile con G1/G2 rifiutato esplicitamente; usare la griglia esatta.
[Prove correnti e limiti](docs/poli-nurbs-sfera-G1-G2-2026-11-11.md).

**08/11 — Bottle e Shower Head.** Pool esatto:690duplicati,poli/pesi1042→135MiB;
Bottle RSS9,95→2,75GiB (-72%),30 B-rep byte-identici. Anche ultimo salvataggio:
33funzioni/8schizzi/182facce,codec67/Wayland/idle0frame/nuovo schizzo senza rebuild PASS.
Precisione1e-7..1e-10mm persistente,worker/UI/Undo; snapshot storico Mouse corretto.
[Prove](docs/bottle-memoria-precisione-2026-11-08.md); precisione fine Bottle non certificata.
Shower2,5mm **interno**:28facce/0errori/mesh,13superfici esterne originali;
`output/Shower-Head-spessore-interno-2.5mm.prt`:Undo/Redo/codec/no-cache/Wayland PASS.
App/33CTest,Offset27/Thicken6/Shell18/Sew12 e mappa652-9337/check/4HTML PASS;
riaprire l’app. [Prove e limiti](docs/shower-head-spessore-2026-11-08.md).
Pubblicato `a388270` su origin/main (08/11).
Snap90→5–10ms:[prove](docs/bottle-prestazioni-2026-11-08.md). 07/11:
[offset±3mm](docs/bottle-offset-2026-11-07.md),[guide](docs/bottle-ripetizione-2026-11-07.md),
[raccordi](docs/bottle-raccordi-2026-11-07.md) verificati; storico esteso
[archiviato](docs/history/claude-prima-bottle-prestazioni-2026-11-08.md).

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

Interventi precedenti: [archivio](docs/history/claude-prima-poli-nurbs-2026-11-09.md).
