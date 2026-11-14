# CLAUDE.md — memoria operativa ForgeCAD

Aggiornato **14 novembre 2026**.

[Cronologia](docs/history/claude-2026-11-02.md); [stato prima di Part23](docs/history/claude-prima-part23-2026-11-14.md).

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
| Vista e anteprime | `forgeCad2026_viewport*`, `cad_display_cache*`: camera, picking, rendering e passate separati; anteprime asincrone cancellabili e riuso del B-rep pronto alla conferma. Cache CPU/GL e rilascio della memoria; backend GPU per carichi specifici, senza sostituire la geometria esatta. |
| Schizzi e vincoli | `cad_types.h`, `cad_constraints*`, `cad_sketch_refs*`, `cad_expression*`: schizzi 2D/3D, cerchio3/asole/punti, polilinee, quote e solver, snap univoci e riferimenti associativi a piani/assi/bordi/curve. Cache automatiche nascoste; conversioni esplicite conservate; costruzione fuori modifica opzionale. Le entità locali visibili hanno priorità nella quotatura. |
| Solidi e superfici | `cad_forge*`, `kernel/fk_*`: primitive, estrusione (anche fino al prossimo corpo), rivoluzione, sweep/loft, booleane, sformo, svuotamento/spessore, filettatura, cucitura, taglio/estensione, reti U/V e poli NURBS. Estendi apre il pannello prima della scelta dei bordi. Raccordi costanti, conici, variabili e tra facce; percorsi tangenti con leggi indipendenti e maniglie R. |
| Storia e genealogia | `cad_history*`, `cad_element_names*`, viewport e storyboard: schizzi/piani come passi autonomi, riordino, rollback, soppressione, riparazione e Undo/Redo. Specchi/ripetizioni sono un evento con corpi separati ed esclusioni; conservano identità e dipendenze. |
| Documenti/revisioni | `cad_document_io*`, `cad_document_read*`, `cad_native_revisions*`, `cad_workspace*`: `.prt` con firma FCAD, formato 69 (contorni apparenti associativi; 68:nomi sorgenti; 67:precisione; 66:piani; 65: unione loft; 64: corrispondenze; 63: variazioni quote); Zstandard dal 56 e lettura legacy zlib. Definizione e cache B-rep/mesh distinte, salvataggio atomico, recupero, consultazione concorrente e lock della modifica. Check-out/check-in, ripristino, rilascio coordinato e recupero catalogo. |
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

**14/11 — CT0578, icone e template.** Cache curve/piani e dettagli ritagliati prima della visibilita’:
viste11,83→8,33s, totale17,96→14,54s; restano~6s di genealogia;12viste/47quote e originali invariati.
Icone 2D distinte. `File > Template dei fogli`: cartiglio/testi/campi/logo/stile, .drwt, default per
formato/orientamento, nuovi fogli/formato iniziale e applicazione con Undo; foglio30 condizionale.
App/runner,34/34CTest e3 ripetuti dopo codec, Wayland/PDF/default reali PASS.
[Prove, tempi, immagini e hash](docs/ct0578-tavole-template-2026-11-14.md).

**14/11 — Tavole 2D.** Centri/assi selettivi, raggio mobile/leader corto, sezioni locali esatte.
Foglio29/documento14, assi/note14 corretti; firma indipendente da visibilita'/cache parziali.
Cache16 migra specchi di funzione/derivati delle copie di lavoro; check-in/altri snapshot preservati.
Recupero29/29Part23 su B-rep identico,47/47CT0578;8/8persistenza e3cicli isolati PASS.
Migrazione: riavvio/salvataggio modello+tavola. Falso blocco sezioni quotate corretto, rimozione protetta.
Guida zero/taglio su ortogonale/appoggio; snap esatti a estremi/medi/centri/quadranti/bordi,
indicatore, Tab/Alt e profili riusati, senza ricalcolo dei tagli ai valori. App/runner completi,35/35CTest2D e Wayland PASS; ultimo Part23:
30/30quote dopo Applica/salva/riapri, originali invariati. Mappa718/10333/351TU,zero diagnostica/check/HTML PASS.
Riavviare app; vista > Inserisci > Sezione locale. Avvisi contorni/piano locale conservati.
[Cache/agganci](docs/part23-quote-associative-cache-2026-11-14.md);
[guida/snap e prove](docs/sezione-locale-snap-profondita-2026-11-14.md);
[precedenti e limiti](docs/history/claude-prima-guida-profondita-2026-11-14.md).

**14/11 — Part23 raccordi e storyboard.** Split/SplitA, guide corte/ciclo, contorni piani e area:
R21:102,8s→63–65ms; due contorni R2:170facce/452edge, Wayland/anteprima/codec PASS.
Nuovo Part23:23feature; E17 sincronizzato col piano dello schizzo, appoggi nominati persi/ambigui
rifiutati senza spostare frame. Cambio storico non riprodotto. Rebuild/riapertura stabili; R23:kernel509ms,
UI624ms,riuso0ms; area originale>100s. Storyboard:11,7s→266ms/stesse38facce con box storici.
App/runner/17CTest/Wayland PASS; generale fermo alla cache legacy di Applicatore. Cavita'7/8 con
fallimento gia' presente; dettagli/kernel test precedenti e mappa699/10105 conservati nel
[rapporto](docs/part23-raccordi-2026-11-14.md) e [archivio completo](docs/history/claude-prima-tavole2d-2026-11-14.md).

**13/11 — Contorno apparente proiettato.** Richiesta: bordo immaginario della parte raggiata proiettato sul piano,
non sezione. Kernel analitico e trim B-rep; faccia/ramo persistenti, cache snap, Quota, etichetta sorgente e codec69.
App/runner completi, 18/18CTest e2avvii Wayland/GL PASS: geometria/trim, mouse, vincolo unico, raggio,
riapertura/Undo e piano esterno al corpo. Mappa695/10025/339TU, zero errori; check e HTML bidirezionale PASS.
Copia KnifeHandle: cache449ms, creazione3924ms, mouse/Undo/codec PASS, SHA originale invariato.
Nessun CAD originale modificato; riaprire l'app. Limiti: NURBS e contorni toroidali obliqui non implementati.
[Dettagli e limiti](docs/schizzi-contorno-apparente-2026-11-13.md).
Pubblicazione richiesta su `main`: consolidati gli interventi del 13/11; diff e mappa ricontrollati PASS.

**13/11 — Riferimenti e anteprima raccordo.** Scelta esplicita di bordi/facce/centri separata dagli snap puntuali;
un solo riferimento al clic, selezione locale conservata e vincolo scelto nel pannello. Proposte visive solo
nei comandi attivi: snap nominati nel disegno, proiezione/sezione e vincoli in Quota; nessuna in Selezione.
Il risultato completo riusato sostituisce anche gli stadi dello stesso corpo logico; la base resta disponibile
al picker dei bordi, senza superfici sovrapposte nel renderer. Nessun ricalcolo di geometria pronta.
App/runner completi, 18/18CTest e tre prove Wayland/GL PASS; mappa691/9966/338AST, zero errori, check/HTML PASS.
Girante su disco con storia diversa e senza il raccordo dell'immagine; prove su fixture/pannello reale, originale invariato.
[Verifiche attuali](docs/raccordo-anteprima-comandi-2026-11-13.md), [intervento precedente](docs/schizzi-riferimenti-hover-2026-11-13.md).

Anteprime/strumenti schizzo13/11: prove e limiti conservati nell’[archivio CT0578](docs/history/claude-prima-ct0578-template-2026-11-14.md).

**12/11 — Applicatore4.** Raccordi: fix SplitA/mitre/NURBS; app/kernel/storia/mappa PASS.
R22/R23=0,20 e R22=0,30/R23=0,14:19funzioni PASS; massimo aperto. [Prove](docs/applicatore4-raccordi-2026-11-12.md).
Storyboard: fix fusioni/cache/rollback. App/19CTest/CAD19/codec/rebuild/GUI/mappa PASS.
`ce84735` storyboard; altri nel commit corrente. [Prove](docs/applicatore4-storyboard-2026-11-12.md).
Pubblicazione: build completa/25CTest/62Blend/38History/5Unify/NURBS6/mappa PASS.

**11/11 — Poli NURBS.** Hook e sfera: jet G1/G2 interni, fasce affini, schema16; app/runner,
20+40kernel/11CTest/Wayland/codec e mappa PASS, `ddc02f9` pubblicato. Refit incompatibile rifiutato.
CI42982288794 era in corso; selftest cattura ribbon falliva anche prima; Hook cambiato esternamente.
[Prove](docs/poli-nurbs-sfera-G1-G2-2026-11-11.md), [storico completo](docs/history/claude-prima-hover-riferimenti-2026-11-13.md).

Bottle/Shower/memoria: prove, esiti e limiti conservati nell’[archivio prima di CT0578](docs/history/claude-prima-ct0578-template-2026-11-14.md).

**11/11 — KnifeHandle.** Blocco cache riferimenti:191feature/13521edge; solver escluso.
Proiezione diretta B-rep/genealogia e valenze in una scansione:0,34–0,40s contro>45s.
App/12CTest/Wayland/Undo/Redo/codec/mappa/check/HTML PASS; originale invariato. Riaprire app.
[Prove e storico](docs/knifehandle-schizzo-faccia-2026-11-11.md).

Quote2D06/11: snap/18CTest/Wayland, `56ae114` pubblicato: [prove](docs/ricollegamento-quote-2d-2026-11-06.md).
Dettagli: [archivio](docs/history/claude-prima-multifaccia-poli-2026-11-09.md).
