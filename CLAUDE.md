# CLAUDE.md — memoria operativa ForgeCAD

Aggiornato **6 novembre 2026**.

[Cronologia](docs/history/claude-2026-11-02.md).

## Regole permanenti e decisioni

- Italiano; nessun `codex` nei nomi dei branch. `main` unico;
  `solver-mesh-gpu` è stato integrato e archiviato il 30 ottobre. CUDA e Metal restano backend opzionali negli stessi
  sorgenti, con fallback CPU.
- Aggiornare a ogni traguardo e prima della risposta finale. Rileggere e preservare le altre chat; distinguere
  implementato, verificato, sperimentale e proposto, con prove effettive e limiti.
- Mantenere **circa 200 righe e al massimo 16 KiB**, con non più di 10 interventi recenti sintetici. Aggiornare la voce
  tematica; dettagli in rapporti `docs/` o archivio. Niente log grezzi o comandi ripetitivi.
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
- Non modificare i CAD originali per le prove; usare copie congelate. Non chiudere processi utente. Dichiarare aggiornato
  l'eseguibile soltanto dopo la compilazione completa; una build del kernel o del runner non aggiorna l'app già aperta.

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
| Documenti e revisioni | `cad_document_io*`, `cad_document_read*`, `cad_native_revisions*`, `cad_workspace*`: `.prt` con firma FCAD, formato 66 (piani; 65: unione loft; 64: corrispondenze; 63: variazioni quote); Zstandard dal 56 e lettura legacy zlib. Definizione e cache B-rep/mesh distinte, salvataggio atomico, recupero, consultazione concorrente e lock della modifica. Check-out/check-in, ripristino, rilascio coordinato e recupero catalogo. |
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
  pertinente e verificare `python3 tools/generate_architecture.py --check`; controllare collegamenti bidirezionali e
  HTML, distinguendo call/include/reference. CMake non rigenera la mappa. HTML/JSON sono generati e ignorati: non forzarne l'aggiunta. [Istruzioni](docs/architecture/README.md).
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
  diverse da quelle locali non ricertificate. CI storiche non ricertificano queste modifiche.
- Accelerazione: nessun guadagno globale 10× dimostrato. Benefici e limiti dei backend sono specifici dei casi misurati. Lock
  cooperativi e release locali non costituiscono autenticazione o permessi di rete; Windows e pacchetto autonomo di rilascio
  non sono certificati. Priorità proposte nella roadmap.

- Snap Intersezione completato: app/11CTest/Wayland/Undo/codec/mappa586-8138 PASS; fixture legacy corretta.
  [Dettagli](docs/snap-intersezione-2026-11-04.md). Divisione facce coincidenti rinviata.
- Loft: controlli/codec64 verificati: [rapporto](docs/loft-corrispondenze-2026-11-04.md).
  Hook: pieghe/unione/codec65 verificati. G1 punta OK; influenza25% riduce rigonfiamento, limiti fra pezze conservati. [Dettagli](docs/loft-hook-2026-11-04.md).

## Ultimi interventi — esiti consolidati

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
**06/11 — Superfici/storia.** `3188714` pubblicato.
R20schema8:117copie/GUI/codec/no-cache,15CTest PASS; schema7:18CTest PASS.
[Superfici](docs/confronto-flatten-quilt-creo-2026-11-06.md).
Unione168:1188facce/1solido,GUI155s/Undo/codec,92kernel+9CTest PASS.
[Unione](docs/knifehandle-unione-rilievi-2026-11-06.md).
Storia incrementale:29CTest/Wayland/B-rep invariati,1–5ms;
[storia](docs/rigenerazione-incrementale-2026-11-06.md).
HoneyRepeat:13solidi, app/Wayland/Undo/codec/test PASS; circolare2/12istanze262/2767ms.
Taglia superficie: pannello unico, conversioni/5CTest PASS; [prove](docs/honeyrepeat-taglio-tra-superfici-2026-11-06.md).
Booleane:9CTest/48kernel/GUI PASS; HoneyRepeat senza Corpo3:1solido/2096facce/47s.
[Dettagli](docs/booleane-selezione-rettangolo-2026-11-06.md).
e761f43 su main;467kernel/37CTest PASS,CI in corso; [prove](docs/ci-raccordi-2026-11-06.md).
Cucitura Surface219 Corpo9/10: conserva SP-curve dense, senza fit inutile oltre32kcampate.
App/11CTest/5filtri kernel PASS; Wayland:solido91facce/0bordi liberi/29,7s,
Undo/Redo/genealogia/codec PASS. Mappa624/8822/check/HTML XCB PASS.
Originale invariato; QA cache4GiB, rigenerazione integrale non verificata; commit/push richiesti il 06/11.
[Indagine](docs/surface219-cucitura-2026-11-06.md).

**04/11 — Griglie, specchi e guide.** `3c644e2`: istanze condivise, B-rep2/cache10;
`9eff99c`: appoggi G1/riordino e F7/E25 verificati. Griglie/cache11: PRT190,5→88,7MB,
STEP745,1→458,7MB; shell928,5→642,7MB. App/runner/QA/suite PASS.
[Specchi](docs/riduzione-complessita-specchi-2026-11-03.md),[guide](docs/loft-controlli-guide-2026-11-04.md),
[griglie/budget](docs/riduzione-griglie-loft-step-2026-11-04.md),[F7/E25](docs/loft-selezione-facce-storiche-2026-11-04.md).
[Dettagli storici](docs/history/claude-prima-taglio-reciproco-2026-11-05.md).

**04/11 — Svuotamento e apertura.** Copia a501f4fd:31facce; taglio sole C0,
jet laterali raccordi, lift G1 cubico e NURBS locali; cache12, check-in immutabili.
Shell62facce/1mm/F0/F14,662punti errore max5,85e-8mm; Wayland/XCB OK/Undo/Redo/cache,
Wayland no-cache PASS. 11CTest,Blend62,Shell18,Offset26 e negativi PASS; app/runner aggiornati.
Mappa576/7987/276AST/check/8HTML PASS; copia84,29MB/26ID; `d0594e8` pubblicato.
[Diagnosi](docs/svuotamento-poli-non-finiti-2026-11-04.md).
Apertura copia17f72219: Apri Wayland3,87s,viewport2,46s; nuovo f695dd98 non provato.
Bernstein/U:224→72,64s/3,09×; Wayland26feature senza cache92,7s,B-rep identico.
App/ASan/UBSan PASS; mappa579/8023/278/check/5HTML PASS.
[Misure e limiti](docs/apertura-tempi-surface219-2026-11-04.md).

0. **03/11 — Schizzi/riferimenti/loft.** Costruzione visibile su richiesta, riferimenti transitori/pulizia
   con protezione dei consumatori (`4fafbaa`): app/Undo/codec/GUI verificati.
   Loft3sezioni: fit1e-9 e raccordo C2 nel budget, app/8CTest/42Loft/GUI PASS.
   Studio SolidCraft/FreeCAD: base adeguata; ID locali/mutazioni comuni proposti, nessuna migrazione.
   Sezioni proiettate `cb35a2e`, picker/specchi `1a8c60d`, pannelli/G1 `2e4366f` verificati.
   Rapporti e prove: [archivio](docs/history/claude-prima-cucitura-2026-11-06.md).

1. **02–03/11 — Guide/Loft9 (`e54a174`).** CI41513461524 su `ec08b12`:
   Linux56PASS/2SKIP,macOS60PASS/1SKIP;storico. Profili razionali/Hermite/guide;
   corner sorgente3,18° conservato. Cache8 migra copie loft/derivati;check-in immutabili,formato60.
   App/QA Wayland-XCB1→2→1,Undo/Redo,cache/no-cache PASS;CTest164PASS/1SKIP/0FAIL,kernel459/459,
   Clang/ASan/UBSan mirati PASS,LSan escluso ptrace;Mappa552/7635/261.
   [Forma](docs/loft-forma-guide-2026-11-03.md),[CI](docs/ci-riparazione-2026-11-02.md),
   [guide](docs/loft-guide-proiettate-2026-11-02.md),
   [unione/curve](docs/estrusione-unione-riferimenti-proiettati-2026-11-02.md).

2. **02/11 — Review/storyboard/estrusione (`d4f15fc`).** R1–R5, fino al corpo e genealogia;
   B8 solo reingresso iniettato. CTest151PASS/1SKIP/1FAIL,kernel457/458 (sformo preesistente);
   [dettagli, verifiche e limiti](docs/code-review-2026-11-02-correzioni.md).

3. **Storico 01–02/11.** OCCT solo nei test (`ab6fc71`); review `9321643`:
[checkpoint](docs/history/claude-checkpoint-prima-storyboard-2026-11-03.md).
Quote/normali/shell `de1448a`/`7651724`: [archivio](docs/history/claude-intersezioni-2026-11-04.md).
Schizzi/Estendi/unione `8121ed7`: [registro](docs/history/claude-prima-svuotamento-2026-11-04.md).
