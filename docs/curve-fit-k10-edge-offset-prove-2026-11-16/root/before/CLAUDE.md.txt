# CLAUDE.md — memoria operativa ForgeCAD

Aggiornato **16 novembre 2026**.

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
  [prestazioni](docs/viewport-performance.md), [review corrente](docs/code-review-2026-11-15.md). Rapporti datati e archivio
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

**16/11 — Ellisse per estremi completata con tre agenti GPT-6.1 Sol.**
Nuovo comando Schizzo:3 clic,4 estremi reali quotabili/vincolabili,centro derivato,
5 DOF,conica analitica,drag/anteprima/snap eTaglia. Codec70 legge i precedenti;
impronte tavole69 riconosciute anche con precisione diversa evisibilita multipla.
Build completa app/runner,24 CTest,probe core/Taglia/hash69,sanitizer mirati4TU
eWayland/GL PASS; mappa/check/HTML/archi PASS. 2669/2687 file invariati.
App aggiornata:riaprire. Poi proseguire i punti aperti della review su richiesta.
K10 rimane aperto nel rapporto sotto.
[Uso,prove e limiti](docs/ellisse-estremi-2026-11-16.md),
[memoria conservata](docs/history/claude-prima-ellisse-estremi-2026-11-16.md).

**16/11 — K10, proiezioni Plane.**
Tre agenti Sol: limite continuo Line/NURBS e poli postgiunta; app usa scarto finale.
App ricompilata. Dopo perdita staging:33 regressioni,24 CTest,5 kernel,3 app,
33 sanitizer su3 TU;16 geometrie/688 campate+264 di giunta PASS entro1e-7.
Mappa758/10958/378TU PASS; ripresa2601/2603 file invariati, sorgenti identiche.
Manifest iniziale perso, vecchio2590/2600 non ricertificato. K10 aperto:14/24 siti
parziali,10 da migrare più fallback;24 rilievi corretti,22 orfane candidate,
anteprime/pannelli da uniformare. K2 in produzione; nessuna nuova prova renderer.
[Prove e limiti](docs/curve-fit-k10-projection-2026-11-16.md).

**15/11 — K2 completato in produzione con due agenti GPT-6.1 Sol.**
Atlante carrier, fori/nesting, ponte analitico conservativo, SP-curve NURBS coerenti,
alias atomici/cancellazione; corretti i falsi rifiuti. App/runner,54test kernel,8CTest,
22ASan/UBSan/LSan mirati e mappa736/10570/361TU PASS.19/19rilievi iniziali corretti;
22orfane candidate. Budget finiti possono rifiutare casi validi; nessun nuovo test renderer.
[Prove,30regressioni e limiti](docs/sweep-k2-completamento-2026-11-15.md);
[stato conservato](docs/history/claude-prima-k10-composition-2026-11-15.md).

**15/11 — Altri14rilievi corretti con agenti GPT-6.1 Sol.**
K4/K5/K6, S1/S2, UI1–6 e PRE1–3: sweep/loft/lamine, numeri nonfiniti e angoli enormi,
picker, selezioni, unita/colori e anteprime coerenti. Un comando modeless fra workspace;
consultazione schede consentita. Build completa app/runner,74test kernel distinti/75esecuzioni
e34/34CTest PASS. Primo33/34: confronto double del test loft errato anche sulla baseline,
corretto senza arrotondare il prodotto. Mappa724/10429/353TU, zero diagnostica, check/HTML/archi PASS.
18/19rilievi allora risolti; K2 completato sopra. Nessun nuovo test OpenGL;
CAD/modifiche pregresse preservati. [Prove e limiti](docs/review-fix-next-2026-11-15.md).

**15/11 — Sweep K1/K3 corretti con due agenti GPT-6.1 Sol.**
Frame ortonormale e cuciture UV: app/runner,42 test kernel e16CTest PASS; mappa724/10414 PASS.
K4 e K2 poi corretti; riferimenti sopra. [Rapporto e prove](docs/sweep-review-fix-2026-11-15.md).

**15/11 — Cache B-spline M1/M2 corrette con 3 agenti GPT-6.1 Sol.**
Shared_ptr conservato nei loop; copie curve/superfici con cache atomiche, assegnazione snapshot e CAS.
Cinque regressioni normale/ASan/TSan PASS; M1 isolata 200×8 thread e riproduttore M2 PASS.
Build completa app/runner e 47/47 CTest PASS. Runner generale: 325 casi senza errori, poi timeout300s,
non certificazione completa. Mappa724/10410/353TU, check e archi/HTML PASS. Nessun test renderer nuovo.
App aggiornata allora; gli altri rilievi iniziali sono stati corretti negli interventi sopra.
[Correzioni, prove e limiti](docs/cache-bspline-2026-11-15.md).

**15/11 — Review con 4 agenti, incluse anteprime.** 19 difetti (5 P1, 12 P2, 2 P3) e 22 orfane
ricontrollate. Sweep deformato/sovrapposto e cucitura UV; memoria M1/M2; solver non finito e UI.
Tre P2 anteprime: estrusione vecchia pronta, Loft senza worker, sorgenti mapping incoerenti;
matrice 15 gruppi. 45 test mirati PASS (recupero fuori sandbox), 16 Sweep correnti PASS; 475 solo baseline
precedente. Mappa 723/10397/352TU PASS. Nessun leak permanente certificato né nuovo test OpenGL;
review mirata. [Review, prove e limiti](docs/code-review-2026-11-15.md),
[anteprime](docs/review-2026-11-15/anteprime.md). Stato successivo sopra.

**15/11 — exhaustMainfold, sweep e vincoli.** Frame Bishop e tangenza tramite catene Coincidente
implementati; 96 casi/scale/pose, codec, Undo/Redo, app e mappa verificati. CAD originale invariato;
volume nullo NURBS poi corretto in K3. [Sweep](docs/exhaust-mainfold-sweep-2026-11-15.md),
[solver e limiti](docs/vincoli-tangenza-transitiva-2026-11-15.md); dettagli in archivio.

Schede documenti14/11: prove e limiti nel [rapporto](docs/schede-documenti-2026-11-14.md) e nella [memoria prima delle ellissi](docs/history/claude-prima-ellisse-estremi-2026-11-16.md).

**14/11 — CT0578, template e tavole 2D.** Cache ottimizzate: totale17,96→14,54s; restano~6s genealogia.
Template .drwt, cartigli e applicazione Undo; centri/assi selettivi, sezioni locali, snap esatti e controlli
viste quotate. Codec foglio30/documento14/cache16, migrazione modello+tavola. App/runner, Wayland/PDF,
35CTest2D,30Part23,47CT0578 e mappa718/10333 PASS. [CT0578/template](docs/ct0578-tavole-template-2026-11-14.md),
[agganci](docs/part23-quote-associative-cache-2026-11-14.md), [sezioni](docs/sezione-locale-snap-profondita-2026-11-14.md).
Dettagli conservati nella memoria precedente e nell'[archivio](docs/history/claude-prima-exhaust-2026-11-15.md).

Part23/raccordi/storyboard14/11: esiti, limiti e riferimenti nella [memoria prima della prosecuzione review](docs/history/claude-prima-review-next-2026-11-15.md).

Contorno apparente13/11: dettagli nella [memoria prima delle correzioni sweep](docs/history/claude-prima-sweep-review-fix-2026-11-15.md).

Riferimenti/anteprime13/11: esiti e limiti nell’[archivio prima delle schede](docs/history/claude-prima-schede-2026-11-14.md).

Interventi11–12/11 (Applicatore4, poli NURBS, memoria, KnifeHandle e quote2D): esiti, limiti e rapporti
conservati nella [memoria precedente alla review](docs/history/claude-prima-review-2026-11-15.md).
