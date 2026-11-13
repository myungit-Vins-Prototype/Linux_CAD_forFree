# CLAUDE.md — memoria operativa ForgeCAD

Aggiornato **13 novembre 2026**.

[Cronologia](docs/history/claude-2026-11-02.md).

## Regole permanenti e decisioni

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
| Vista e anteprime | `forgeCad2026_viewport*`, `cad_display_cache*`: camera, picking, rendering e passate separati; anteprime asincrone cancellabili e riuso del B-rep pronto alla conferma. Riapertura editor: riuso immediato scena implementato il 13/11, verifiche in corso. Cache CPU/GL e rilascio della memoria; backend GPU per carichi specifici, senza sostituire la geometria esatta. |
| Schizzi e vincoli | `cad_types.h`, `cad_constraints*`, `cad_sketch_refs*`, `cad_expression*`: schizzi 2D/3D, cerchio3/asole/punti, polilinee, quote e solver, snap univoci e riferimenti associativi a piani/assi/bordi/curve. Cache automatiche nascoste; conversioni esplicite conservate; costruzione fuori modifica opzionale. Le entità locali visibili hanno priorità nella quotatura. |
| Solidi e superfici | `cad_forge*`, `kernel/fk_*`: primitive, estrusione (anche fino al prossimo corpo), rivoluzione, sweep/loft, booleane, sformo, svuotamento/spessore, filettatura, cucitura, taglio/estensione, reti U/V e poli NURBS. Estendi apre il pannello prima della scelta dei bordi. Raccordi costanti, conici, variabili e tra facce; percorsi tangenti con leggi indipendenti e maniglie R. |
| Storia e genealogia | `cad_history*`, `cad_element_names*`, viewport e storyboard: schizzi/piani come passi autonomi, riordino, rollback, soppressione, riparazione e Undo/Redo. Specchi/ripetizioni sono un evento con corpi separati ed esclusioni; conservano identità e dipendenze. |
| Documenti/revisioni | `cad_document_io*`, `cad_document_read*`, `cad_native_revisions*`, `cad_workspace*`: `.prt` con firma FCAD, formato 68 (nomi sorgenti associative; 67:precisione; 66:piani; 65: unione loft; 64: corrispondenze; 63: variazioni quote); Zstandard dal 56 e lettura legacy zlib. Definizione e cache B-rep/mesh distinte, salvataggio atomico, recupero, consultazione concorrente e lock della modifica. Check-out/check-in, ripristino, rilascio coordinato e recupero catalogo. |
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
- App Linux: `forgecad-release/forgecad`; riaprirla dopo la build e verificare il collegamento effettivo.
  Non attribuire vecchi hash/test al nuovo HEAD.
- Architettura: prima delle modifiche consultare descrizioni e connessioni fra funzioni in `docs/architecture/index.html`/`graph.json`
  (richiesta permanente 13/11); `python3 tools/open_architecture.py` aggiorna all’apertura. Dopo modifiche strutturali aggiornare
  catalogo, rigenerare, `--check` e verificare HTML/collegamenti bidirezionali. CMake non rigenera; HTML/JSON ignorati.
  [Procedura](docs/architecture/README.md), dettagli permanenti in `AGENTS.md`.
- Approfondimenti pertinenti: [roadmap](docs/roadmap.md), [schizzi](docs/sketch-tools.md),
  [storia](docs/storyboard-history.md), [tavole/revisioni](docs/drawing-revisions-study.md),
  [prestazioni](docs/viewport-performance.md), [review corrente](docs/code-review-2026-11-02.md). Rapporti datati e archivio
  conservano riproduttori, misure e prove storiche.

## Limiti ancora utili alla prosecuzione

- Ripetizioni variabili: solo funzione/schizzi diretti, quote cliccabili, corpi esclusi; codec63/ID e ricalcolo isolato.
  App/13CTest/Wayland/mappa PASS [04/11](docs/ripetizioni-variabili-2026-11-04.md).
  Indagine girante13/11: Ripetizione3 lineare/Y/corpo intero; Estrusione4 corpo nuovo da Schizzo3.
  Su copia in memoria: Unisci a Rivoluzione1, Una funzione, circolareZ/3/360 e quota15+1 =>15/16/17mm;
  B-rep unione/ripetizione senza errori. Originale SHA256 invariato; nessuna modifica app o prova UI nuova.
  Grafico consultato. [Istruzioni e limiti](docs/girante-variazioni-2026-11-13.md).

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

**13/11 — Strumenti e riferimenti schizzo.** Cerchio per tre punti analitico; arco3 senza raggio nell'anteprima;
Polilinea sostituisce Linea anche nelle barre/ribbon salvate (`cad_toolbar.h`), enum legacy conservate.
Badge e conferma condividono un solo aggancio, origine prioritaria; intersezioni mantengono le due curve.
Sorgenti, entita e numeri dei vincoli rotti identificati nel pannello/storyboard; appoggi per nome/faccia.
Codec68 conserva i nomi dopo eliminazione/riapertura; lettura legacy conservata. Vecchi file gia rotti senza
nome conservato mostrano l'ID disponibile. App e runner completi, 24/24CTest e Wayland/GL PASS;
QA visuale arco3/cerchio3/badge PASS. Mappa688file/9911relazioni/337AST senza errori, check/HTML PASS.
Grafico consultato per responsabilita/chiamanti; istruzione permanente registrata in AGENTS e qui.
[Prove e limiti](docs/schizzi-strumenti-riferimenti-2026-11-13.md). Riaprire l'app; nessun CAD originale modificato.

**12/11 — Applicatore4.** Raccordi: fix SplitA/mitre/NURBS; app/kernel/storia/mappa PASS.
R22/R23=0,20 e R22=0,30/R23=0,14:19funzioni PASS; massimo aperto. [Prove](docs/applicatore4-raccordi-2026-11-12.md).
Storyboard: fix fusioni/cache/rollback. App/19CTest/CAD19/codec/rebuild/GUI/mappa PASS.
`ce84735` storyboard; altri nel commit corrente. [Prove](docs/applicatore4-storyboard-2026-11-12.md).
Pubblicazione: build completa/25CTest/62Blend/38History/5Unify/NURBS6/mappa PASS.

**11/11 — Badge CT0578.** Riassegnazione libera riserve e riparte dal numero richiesto;
modifiche ordinarie conservano numeri/riserve. App/4CTest/Wayland/codec1..47 e10..56/mappa PASS.
Originale invariato; riaprire app. [Prove e limite Tab](docs/ct0578-badge-ripartenza-2026-11-11.md).

**10/11 — Checkbox e tema.** Elenchi col fondo dei campi; palette distinta per etichetta/spunta.
Visualizza/Tema: Chiaro/Scuro/Segui il desktop persistente, applicato anche alle finestre aperte.
App/runner/3CTest e Wayland(segnali simulati)/pixel/menu PASS.
Limite diagnostica Wayland; mappa/HTML PASS.
[Prove e limiti](docs/tema-checkbox-2026-11-10.md); riaprire l’app.

**10/11 — CI GitHub.** Offset900s, Newton smorzato e mirrorlistAPT corretti;
`5ad19c4`/`994c771` su origin/main. CI42789007301 PASS Linux80/2skip,Mac84/1skip,kernel475/475;
GCC/Clang/mappa PASS. [Prove](docs/ci-riparazione-2026-11-10.md).

**11/11 — Poli NURBS.** Hook: griglie, refit vincolante, multifaccia, analitiche esatte,
G0/G1/trascinamento morbido e occlusione GPU fino `8bdfa20`: app GCC/Clang,11CTest,Wayland1×/2×,
Hook2/7 e CI Linux/macOS42852161648 PASS. Milioni di poli virtuali, gabbia ridotta.
[Hook](docs/poli-nurbs-Hook-corpo2-2026-11-09.md),[influenza](docs/poli-nurbs-trascinamento-morbido-2026-11-10.md),
[selezione](docs/poli-nurbs-selezione-visibile-2026-11-10.md),[occlusione](docs/poli-nurbs-occlusione-2026-11-10.md).
Hook corrente cambiato esternamente; prove storiche su copia congelata.
Nuovo `Sphere_deformed.prt`: F0 quadratica razionale9×5,24poli mossi; G1 legacy omette i nodi interni.
Implementati jet G1/G2 interni/cuciture e fasce affini alle calotte;20/20kernel PASS. File reale G2:residui<3e-12.
Schema16, G1 nella sessione e titolo/ribbon: app/runner,11CTest,Wayland1×/2×/multifaccia,Undo/codec PASS.
40/40kernel superfici e sintassi6sorgenti Clang PASS; mappa/check/HTML PASS; `ddc02f9` su origin/main; CI42982288794 in corso.
Selftest Ribbon Wayland: cattura popup fallisce anche col modulo precedente; percorso poli PASS.
Refit incompatibile con G1/G2 rifiutato esplicitamente; usare la griglia esatta.
[Prove correnti e limiti](docs/poli-nurbs-sfera-G1-G2-2026-11-11.md).

**08/11 — STL/OBJ automatico.** B-rep double, scarto<=0,01mm/angolo2°;
recupero combinato precisione/densita'. Sfere3scale: errore ~4× minore;
Shower3corpi:821845triangoli/4tentativi/B-rep invariati; app/10CTest PASS.
Mouse:F1/Raccordo18 fuori scarto, export rifiutato. Mappa652/9346/check/HTML PASS.
[Prove e limiti](docs/stl-qualita-automatica-2026-11-08.md).

**08/11 — Bottle/Shower e memoria.** Pool esatto:690duplicati,poli/pesi1042→135MiB;
Bottle RSS9,95→2,75GiB (-72%),30 B-rep byte-identici. Anche ultimo salvataggio:
33funzioni/8schizzi/182facce,codec67/Wayland/idle0frame/nuovo schizzo senza rebuild PASS.
Precisione1e-7..1e-10mm persistente,worker/UI/Undo; snapshot storico Mouse corretto.
[Prove](docs/bottle-memoria-precisione-2026-11-08.md); precisione fine Bottle non certificata.
Shower2,5mm **interno**:28facce/0errori/mesh,13superfici esterne originali;
`output/Shower-Head-spessore-interno-2.5mm.prt`:Undo/Redo/codec/no-cache/Wayland PASS.
App/33CTest,Offset27/Thicken6/Shell18/Sew12 e mappa/check/HTML PASS;
riaprire l’app. [Prove e limiti](docs/shower-head-spessore-2026-11-08.md).
`a388270` su origin/main.
Cambio file corretto: snapshot30corpi + >2GiB liberi trattenuti; 3cicli Wayland:Shower0,88–0,90GiB,0corpi/superfici vecchi.
[Prove e limiti](docs/cambio-documenti-memoria-2026-11-08.md); App/16CTest/GL PASS; mappa/HTML PASS.
Snap90→5–10ms:[prove](docs/bottle-prestazioni-2026-11-08.md). Storico:
[offset±3mm](docs/bottle-offset-2026-11-07.md),[guide](docs/bottle-ripetizione-2026-11-07.md),
[raccordi](docs/bottle-raccordi-2026-11-07.md);
[archivio](docs/history/claude-prima-bottle-prestazioni-2026-11-08.md).

Interventi 07/11 Onion/Onion2 (sezioni, loft, raccordi e cucitura):
[esiti e limiti conservati](docs/history/claude-prima-schizzi-2026-11-13.md).

**11/11 — KnifeHandle.** Blocco cache riferimenti:191feature/13521edge; solver escluso.
Proiezione diretta B-rep/genealogia e valenze in una scansione:0,34–0,40s contro>45s.
App/12CTest/Wayland/Undo/Redo/codec/mappa/check/HTML PASS; originale invariato. Riaprire app.
[Prove e storico](docs/knifehandle-schizzo-faccia-2026-11-11.md).

Quote2D06/11: snap/18CTest/Wayland, `56ae114` pubblicato: [prove](docs/ricollegamento-quote-2d-2026-11-06.md).
Dettagli: [archivio](docs/history/claude-prima-multifaccia-poli-2026-11-09.md).
