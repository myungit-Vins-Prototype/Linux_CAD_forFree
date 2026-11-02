# Code review ForgeCAD (aggiornata) — bug, funzioni orfane, semplificazioni

> **Aggiornamento, stesso giorno: correzioni applicate (non ancora committate).** Corretti N1, N2, N3, N4, B1, B2, B3, B3b, B4, B5, B6, B7, B10, con regressioni (`StepRejectsCyclicOrientedEdges`, `StepRejectsSelfReferencingGeometry`, `IgesRejectsInvalidSplineParameters`, `IgesRejectsCyclicCompositeCurves`, `ReflectionRejectsDegenerateNormal`, `forgecad_expression_tests`) che falliscono sul codice precedente e passano su quello nuovo. B8 (plausibile), funzioni orfane e semplificazioni restano aperti. Dettagli e verifiche nella voce di `CLAUDE.md` «Correzione dei bug della code review».

Base analizzata: `origin/main` al commit **`7651724`** (2 novembre 2026, 07:22), identico al checkout locale. I numeri di riga si riferiscono a questo commit.

La review precedente (stessa data, altra chat) era stata fatta su una copia del 25 ottobre (`e2d76d8`). **Le correzioni descritte nella sua voce di `CLAUDE.md` non sono mai arrivate nel repository**: nessuna è presente in `7651724`. Questa revisione riparte quindi dal codice attuale, ricontrolla tutti i punti già noti e aggiunge quelli nuovi.

Nessun sorgente del repository è stato modificato. L'analisi è stata fatta su un clone cloud allo stesso commit; le due sole modifiche locali al clone (una guardia Qt 6.5 e i nomi delle librerie OCCT 7.6) servivano a compilare e sono descritte sotto.

## Come è stata fatta

| Strumento | Ambito | Esito |
|---|---|---|
| Build Clang 18 + Qt 6.4 + OCCT 7.6, avvisi estesi (`-Wunused-member-function`, `-Wunreachable-code-aggressive`, `-Wrange-loop-bind-reference`, …) | `forgecad` + `forgekernel` | 1 errore (B7), 18 avvisi distinti |
| Link con `--gc-sections --print-gc-sections`, incrocio con `grep` su tutto il repo e con `nm` sull'archivio del kernel | eseguibile `forgecad` | elenco delle funzioni orfane e solo-test |
| libFuzzer + ASan + UBSan, seed generati dal writer del kernel | `readStep` 15 min, `readIges` 4 min, `readBodyBinary` 8 min | **STEP: 2 difetti nuovi (N1, N2).** IGES: B1 riconfermato. `.body`: 3,6 milioni di input, nessun difetto |
| `forgekernel_tests` sotto ASan + UBSan, un test per volta, 60 s ciascuno | kernel, tutti i 458 test distinti | **nessuna segnalazione dei sanitizer.** 439 superati, 16 oltre i 60 s (soprattutto raccordi), 3 falliti per motivi d'ambiente o di tolleranza (vedi Limiti) |
| Sonde dedicate compilate sul codice reale | parser espressioni, lettore STEP | B4, B5, N1, N2 riprodotti |
| Lettura manuale mirata | codice nuovo dal 25/10 (raccordi variabili, svuotamento, riferimenti associativi, solver lineare dei vincoli, codec Zstandard) e controlli di robustezza | vedi sotto |

---

## 1. Stato dei punti della review del 25 ottobre

| # | Punto | Stato al 2/11 | Riferimento attuale |
|---|---|---|---|
| B1 | IGES 126: `K`/`M` non validati, lettura fuori da `knots` | **ancora presente**: il fuzzer lo ritrova in meno di 4 minuti (UBSan, indice negativo in `IgesGeometry::curve`) | `kernel/fk_iges.cpp:679-694` |
| B2 | IGES 128: dimensioni dal file non validate | **ancora presente** | `kernel/fk_iges.cpp:755-765` |
| B3 | IGES: `integer()` fa `int(lround(x))` senza limiti; overflow in `3 + 2*n` | **ancora presente** | `fk_iges.cpp:526-529`, `fk_iges.cpp:1260` |
| B3b | IGES 102 (curva composta) che rimanda a sé stessa: ricorsione senza limite | **ancora presente** (lettura del codice; lo stack del crash del fuzzer passa proprio da qui) | `fk_iges.cpp:597-602` |
| B4 | Parser espressioni: prodotto implicito davanti a cifre | **ancora presente**, rimisurato: `1,000.5` → 0.5, `1.000,5` → 0.5, `1.2.3` → 0.36, `12 5` → 60 | `cad_expression.cpp:147-150` (`startsPrimary`), `164-181` |
| B5 | Parser espressioni: ricorsione senza limite | **ancora presente**: 20.000 parentesi OK, 200.000 → segmentation fault | `cad_expression.cpp:153`, `182` |
| B6 | `GlassCompositor` non liberato su `aboutToBeDestroyed` | **ancora presente** (il distruttore lo libera, la lambda no) | `forgeCad2026_viewport_render.cpp:122-129` |
| B7 | Qt ≥ 6.5 richiesto ma non dichiarato | **ancora presente**: con Qt 6.4 `forgeCad2026_gui.cpp` non compila | `forgeCad2026_gui.cpp:2720`, `CMakeLists.txt:54` |
| B8 | `runWhileResponsive` e riferimenti durante il loop annidato | **plausibile**, meccanismo precisato sotto | `forgeCad2026_viewport_document.cpp:633`, `2029`, `2254` |
| B9 | `fk_offset.cpp`, `start` negativo | **chiuso: non raggiungibile.** Ogni taglio ha già superato il controllo di molteplicità (`count(K,c) ≥ p`), quindi `last ≥ 0`; i tagli duplicati sono già filtrati alla riga 1027 | `kernel/fk_offset.cpp:950-990` |
| B10 | `Transform3::reflection`: messaggio «normale nulla» irraggiungibile | **ancora presente** | `kernel/fk_math.cpp:88-90` |
| 2c | Self-test della ribbon nell'eseguibile di produzione | **ancora presente**: `forgeCad2026_ribbon_preview.cpp` è in `forgecad` (CMakeLists.txt:124) e il progetto non usa `--gc-sections`, quindi `RibbonPreview::runSelfTest` resta nel binario | `forgeCad2026_ribbon_preview.cpp:622` |

**B8, meccanismo concreto.** In `rebuildBody(extrusions_[i], i)` il riferimento `body` punta dentro `extrusions_`. Dentro `rebuildBody` si fa `const QVector<ExtrusionObject> bodies = extrusions_;`, quindi il buffer diventa condiviso (copy-on-write). Se durante `loop.exec()` un timer o uno slot in coda chiama un metodo **non const** su `extrusions_`, il vettore si stacca e alloca un nuovo buffer: `body = std::move(result)` scrive allora nel buffer vecchio, ormai posseduto solo da `bodies`, e il risultato della rigenerazione **va perso senza errori**. Nei punti controllati (anteprima in `startPreviewJob`, inizio di `updateHover`) l'accesso a `extrusions_` è in sola lettura, e paint e salvataggio di recupero si fermano con `responsiveWork_`. Non ho trovato un percorso che lo inneschi, ma non ho seguito tutti gli slot raggiungibili: resta "plausibile". La correzione robusta è non passare riferimenti dentro `extrusions_`: `rebuildBody` può ricevere l'indice e riassegnare `extrusions_[index]` dopo il loop.

---

## 2. Bug nuovi

### N1 — ALTA · confermato (fuzzer + build Release) — STEP: blocco infinito su ORIENTED_EDGE ciclico
`kernel/fk_step.cpp:1208-1211`

```cpp
while (file.at(edgeId).type() == "ORIENTED_EDGE") {
    const Entity &oriented = file.at(edgeId);
    sense = sense == logical(oriented.args().at(4));
    edgeId = reference(oriented.args().at(3));
}
```

Un `ORIENTED_EDGE` che rimanda a sé stesso (o due che si rimandano a vicenda) fa girare il ciclo all'infinito, senza `cancellationPoint()`. Riproduzione minima: in un file STEP valido scritto dal kernel (un cilindro, letto in 0,00 s) ho cambiato **un solo riferimento**, `#30=ORIENTED_EDGE('',*,*,#25,.T.)` → `…,#30,.T.)`. `readStep` resta bloccato oltre 15 s (il caso del fuzzer, 7,5 KB, oltre 5 minuti). Nell'app l'import gira in `runWhileResponsive` (`forgeCad2026_viewport_document.cpp:1665`), che esclude l'input utente fino alla fine del calcolo e non offre annullamento: in pratica l'unica via d'uscita è terminare il processo.

**Correzione proposta:** limitare i passaggi (per esempio 64) oppure tenere un `std::set<int>` dei visitati e lanciare `std::domain_error("STEP: ORIENTED_EDGE ciclico")`.

### N2 — ALTA · confermato (build Release) — STEP: ricorsione infinita e crash su curve o superfici autoreferenziate
`kernel/fk_step.cpp:903-904` (`TRIMMED_CURVE`, `SURFACE_CURVE`, `SEAM_CURVE`, …), `1030-1032` (`OFFSET_SURFACE`); cache in `fk_step.cpp:820-828`

`curve(id)` e `surface(id)` inseriscono il risultato in cache solo **dopo** averlo costruito, quindi una curva che ha come base sé stessa richiama `curve()` all'infinito. Riproduzione: nello stesso cilindro ho fatto puntare un `EDGE_CURVE` a un nuovo `#N=TRIMMED_CURVE('',#N,…)`. Risultato: **segmentation fault** (stack overflow). L'import gira in un thread worker, quindi il crash chiude l'intera applicazione, con l'eventuale lavoro non salvato.

**Correzione proposta:** un insieme di ID "in costruzione" (se l'ID è già presente, lanciare `domain_error`), oppure un contatore di profondità come quelli già usati in `unitFactor` (riga 1065) e `styledColour` (riga 1099). La stessa guardia serve per B3b in IGES.

### N3 — BASSA · confermato in build — test del kernel legati a OCCT ≥ 7.8 senza dichiararlo
`kernel/CMakeLists.txt:218` collega `TKDESTEP`, `TKDEIGES` e `TKDE`, nomi introdotti in OCCT 7.8, mentre `CMakeLists.txt:75` fa `find_package(OpenCASCADE REQUIRED)` senza versione. Con OCCT 7.6 (Ubuntu 24.04, Debian 12) `forgekernel_tests` non si collega. Basta `find_package(OpenCASCADE 7.8 REQUIRED)`, oppure scegliere i nomi in base a `OpenCASCADE_VERSION` (`TKSTEP`/`TKIGES`/`TKXDESTEP`/`TKXDEIGES` per le versioni precedenti). Riguarda solo i test: l'app non usa OCCT.

### N4 — BASSA · avviso del compilatore — cattura di structured binding in C++17
`kernel/fk_offset.cpp:1922` e `1935`: le lambda `incident` e `initial` catturano `uses`, che viene da `for (const auto &[index, uses] : edgeUses)` (riga 1896). In C++17 non è ammesso: Clang 18 e AppleClang lo accettano con un avviso (`-Wc++20-extensions`), Clang < 16 lo rifiuta. Il progetto dichiara `CMAKE_CXX_STANDARD 17`. Correzione: `const auto &edgeFins = uses;` prima delle lambda, oppure cattura esplicita `[&, &uses = uses]`.

---

## 3. Funzioni orfane

Metodo: una funzione è **orfana** quando il linker la elimina da `forgecad` e `grep` sull'intero repository non trova altre occorrenze oltre a dichiarazione e definizione. Il solo dato del linker non basta: con `-O2` una funzione chiamata in un punto può essere inlined e la sua copia eliminata (è il caso di `initializeGlassShader`, che è usata). Tutti i moduli dell'archivio del kernel risultano collegati all'app.

### 3a. Orfane (nessun chiamante, nemmeno nei test)

| Funzione | Definizione | Nota |
|---|---|---|
| `CadViewport::setSelectedPointPosition` | `forgeCad2026_viewport_selection.cpp:707` | già nel report del 25/10 |
| `CadViewport::setEdgePickLinear` | `forgeCad2026_viewport_selection.cpp:221` | già nel report |
| `CadViewport::setExtendPickCallback`, `resumeExtendPick` | `forgeCad2026_viewport_selection.cpp:219`, `226` | **nuove**: residui del vecchio flusso Estendi, sostituito il 1/11 dal pannello aperto subito |
| `CadViewport::constraintsVisible` | `forgeCad2026_viewport_sketch.cpp:1261` | già nel report |
| `CadViewport::sectionEnabled` | `forgeCad2026_viewport.cpp:438` | già nel report |
| `CadViewport::geometrySnap`, `gridSnap` (getter) | `forgeCad2026_viewport.cpp:74`, `78` | già nel report |
| `CadViewport::basisMatrix`, `normalizedViewAngle` | `forgeCad2026_viewport_scene.cpp:21`, `56` | inoltrano soltanto a `ViewportCamera`, che le usa internamente |
| `CadViewport::extrusionNames` | `forgeCad2026_viewport_document.cpp:887` | già nel report |
| `CadViewport::ensureBodyDisplay` (singolare) | `forgeCad2026_viewport_document.cpp:206` | **nuova**: si usa solo `ensureBodyDisplays` |
| `StoryboardTree::firstFeature` | `forgeCad2026_storyboard.cpp:548` | già nel report |
| `DrawingSheetEditor::setModelProvider` | `cad_drawing_editor.cpp:3399` | già nel report |
| `ForgeCad::forgeIntersectRay` | `cad_forge.cpp:2398` | già nel report |
| `ForgeCad::modelBodyFeatures` | `cad_model_history.cpp:72` | già nel report |
| `ForgeCad::usesSketch` | `cad_feature_dependencies.cpp:196` | già nel report |
| `ForgeCad::commandIconNames` | `cad_icons.cpp:1562` | già nel report |
| `ForgeCad::nearestBodyEdge`, `appendFixedReferenceLine` | `cad_sketch_refs.cpp:133`, `530` | **nuove**: residui delle "copie con Fix", sostituite il 31/10 dai riferimenti associativi |
| `ForgeCad::worldToSketch(const Vec3&, int plane)` | `cad_kernel.cpp:30` | **nuova**: si usa solo l'overload con `SketchObject` |
| `Kernel::transformSurface` | `kernel/fk_transform.cpp:305` | già nel report |
| `Kernel::HelixCurve::length()` | `kernel/fk_helix.cpp:48` | già nel report |
| metodi locali segnalati dal compilatore | `fk_blend.cpp:368` (`otherFace`), `fk_blend_loop.cpp:107,112` (`inPlane`, `fit`), `fk_blend_surface.cpp:640` (`fit`), `fk_fill.cpp:239` (`band`), `fk_step.cpp:832` (`length`) | `values` in `cad_constraints.cpp` non compare più |

`CadViewport::resumeEdgePick` (nell'elenco del 25/10) ora è usata e non è più orfana. `drawSketchDimensions` (`forgeCad2026_viewport_render.cpp:484`) compare solo in un test.

### 3b. Usate solo dai test

Vanno tenute se servono come appigli per i test; altrimenti sono codice morto in produzione.

- **Viewport:** `createExtrusion(double, …)` (l'app usa l'overload con `ExtrusionObject`), `createSheetTrim(int, …)` (idem), `createRevolution`, `moveFeature`, `moveFeatureTo`, `setHistoryPosition`, `hasPendingSketchChanges`, `referencePicking`, `advanceWheelZoom`, `previewError`, `drawSketchDimensions`.
- **Disegno:** `DrawingSheetEditor::insertView`, `updateDimension(DrawingDimension)` (un solo argomento), `dimensionResult`, `axisResult`, `annotationResult`, `removeAxis`, `removeAnnotation`, `setSectionStyle`, `setViewScale`, `setDimensionNumbering`, `hasSectionLine`, `hasViewPlacement`, `cancelViewPlacement`, `isModified`; `drawingDimensionParenthesized`, `dimensionOfSection`.
- **Revisioni:** `RevisionStore::createDocument`, `checkoutAtVersion` (con `reserveDocument`, raggiunta solo da loro), `beginNativeCheckout`, `checkoutNativeRevision`, `resumeNativeCheckout`.
- **Riferimenti schizzo:** `appendProjectedSketchEntity`, `appendSketchContactReference`.
- **Varie:** `forgeClosestSheetPiece`, `HistoryGraphDialog::zoomFactor`, `StoryboardTree::historyRail`, `FunctionDialogPanel::backdropBlur`, `gpuBoxPairsProcessed`.
- **Kernel:** operatori di Eulero `kev`, `kef`, `kvfs`, `mekr`, `jekv`; `mirrorBody`, `orient3d`, `projectCurve` (la singola; l'app usa `projectCurves`); gli overload con `bool ruled` di `loftSolid`/`loftSheet` e quello senza `LoftOptions` di `ruledSurface` (l'app usa sempre le versioni con `LoftOptions`).

`FaceClassifier` non è più solo-test: dal 1/11 lo usa `nestedShell` in `fk_shell.cpp`.

---

## 4. Semplificazioni

### S1 — Helper duplicati (situazione attuale)

| Helper | Copie | Nota |
|---|---|---|
| `setError(QString*, const QString&)` | 7: `cad_datum`, `cad_extrude`, `cad_features`, `cad_forge`, `cad_kernel`, `cad_measure`, `cad_pattern` | identiche |
| famiglia `require` / `need` / `ok` | **13**: `require` in 8 file (si è aggiunto `forgeCad2026_ribbon_preview`), `need` in `cad_drawing_dimension`, `cad_drawing_silhouette`, `cad_drawing_visibility`, `ok` in `cad_drawing_file`, `cad_drawing_sheet`, `cad_native_revisions` | stessa idea con eccezioni diverse (`invalid_argument` / `runtime_error`) e argomenti `const char*` / `QString`: un header comune con due overload le copre tutte |
| `sameVersion` | 3 (`cad_drawing_file`, `cad_drawing_release`, `cad_drawing_release_ui`) | meglio un `operator==` su `DocumentVersion` |
| `uuidText`, `readUuid` | 2 (`cad_drawing`, `cad_native_revisions`) | identiche |
| `versionJson` | 2 (`cad_drawing`, `cad_revision_store`) | stesso schema |
| `toVec(QPointF)` | 3 (`cad_curve_solver`, `cad_forge`, `cad_sketch_edit`) | identiche |
| `cross2` | 3 (`cad_drawing_dimension`, `cad_drawing_visibility`, `fk_blend`) | **nuova** |
| `PlaneMotion` / `PlaneMap` | `cad_constraints.cpp`, `cad_sketch_edit.cpp` | stessa trasformazione affine |
| `binomial` | `fk_bernstein.cpp`, `fk_bspline_basis.cpp` | quella in `fk_bspline_basis.h` è già pubblica |
| `solve3` | `fk_hermite.cpp`, `fk_project.cpp` | da unificare in `fk_math` |
| `anyPerpendicular` | `fk_blend.cpp`, `fk_sweep.cpp` | algoritmi diversi con lo stesso nome |
| `faceNormal` | `fk_blend_surface`, `fk_boolean`, `fk_offset` | tre funzioni diverse con lo stesso nome |

Le quattro `signedArea` (`cad_sketch_offset`, `fk_primitives`, `fk_profile`, `fk_tessellate`) lavorano su tipi diversi e non sono duplicati.

### S2 — Tre copie della fattorizzazione di Cholesky (nuova)
La stessa fattorizzazione con regolarizzazione `mu` e le due sostituzioni compaiono in `cad_constraints.cpp:2142-2186` (percorso denso), in `kernel/fk_constraint_linear.h:221-241` (ramo denso di `minimumNormStep`, riga 188) e, a bande, in `bandSolve` (stesso file, riga 155). Il ramo denso di `cad_constraints.cpp` potrebbe costruire le `Rows` e chiamare `ConstraintLinear::minimumNormStep(…, StepMode::DenseComponents)`: resterebbe un solo punto dove regolare soglie e criteri di fallimento.

### S3 — Inoltri inutili
- `CadViewport::initializeGlassShader()` chiama soltanto `glassCompositor_.initialize()`.
- `CadViewport::basisMatrix` e `normalizedViewAngle` sono orfane (3a).

### S4 — Duplicazioni locali e piccole cose
- `cad_forge.cpp:347` e `354`: le lambda `local` e `localHandle` calcolano la stessa proiezione.
- `kernel/fk_step.cpp:1414`: `for` con `break` alla prima iterazione (lo segnala il compilatore); meglio `ofType(...).empty()` / `front()`.
- `forgeCad2026_ribbon_editor.cpp:24,43,44` e `forgeCad2026_ribbon_preview.cpp:355,358,362,372`: `for (const auto &v : QJsonArray)` lega un temporaneo; va scritto `for (const auto v : …)`.
- `kernel/fk_blend_paths.cpp:117-123`, `blendPathPoint`: la bisezione ricalcola `arcLength` dall'inizio del bordo per 48 volte a ogni stazione (e a ogni aggiornamento dei punti trascinabili). Un Newton su `|C'(t)|`, oppure la cache delle lunghezze già presente (`fk_curve_length_cache.h`), riduce il costo a poche quadrature.
- `Bernstein::evaluate` e `IsoAdapter::evaluate` (`fk_surface_algo.cpp`) allocano un `std::vector` a ogni chiamata dentro i cicli di Newton; `ScratchBuffer` esiste già.

---

## 5. Punti controllati e risultati sani

- **Codec Zstandard** (`cad_document_read.cpp:85-116`): dimensione dichiarata limitata, finestra limitata a 8 MiB, rifiuto di frame multipli e di input senza progresso. Nessun difetto trovato.
- **Lettura dei fogli `.drw`**: gli enum letti dal file (freccia, orientamento e posizione del testo, tolleranze) vengono validati dopo la lettura (`validateDrawingStyle`, `cad_drawing_sheet.cpp:44`; `cad_drawing_tolerances.h:21`).
- **Pannello Raccordo** (`forgeCad2026_dialogs_fillet.cpp`): callback copiate prima della chiamata durante il trascinamento (`forgeCad2026_fillet_handles.cpp`), `QPointer` sullo stato dell'anteprima, pulizia completa dei callback all'uscita.
- **Percorsi dei raggi variabili** (`fk_blend_paths.cpp`): bordi duplicati rifiutati, catene chiuse riconosciute, abbinamento delle tangenze reciproco.
- **Solver lineare dei vincoli** (`fk_constraint_linear.h`): le righe che `dot()` richiede ordinate sono ordinate e fuse (`cad_constraints.cpp:1773-1780`), i punti dei blocchi ordinati e deduplicati (`cad_constraints.cpp:1721-1722`).
- **Riferimenti associativi** (`cad_sketch_refs.cpp`): indici risolti per `featureId` con controllo dei limiti; anche le sezioni (tipo 11) passano dal controllo.
- **Svuotamento** (`fk_shell.cpp`): `nestedShell` verifica contenimento, volumi e `checkBody` prima di accettare il risultato; il fallback booleano resta disponibile.
- **Indici e contenitori**: nessun `indexOf()` usato come indice senza controllo; nessun range-for che modifica il contenitore che scorre; i `.first()`/`.last()` letti dal file sono preceduti da un controllo della dimensione.

## 6. Limiti

- **Lettura manuale non completa.** Su ~157k righe ho letto per intero il codice nuovo dal 25/10 elencato sopra; per il resto ho usato ricerche mirate. Non sono passati da una lettura riga per riga: `cad_drawing_editor.cpp`, `forgeCad2026_gui.cpp`, `forgeCad2026_viewport_sketch.cpp`, il resto di `cad_constraints.cpp`, booleane, raccordi e intersezioni del kernel.
- **Test del kernel con sanitizer:** 16 test sono stati interrotti a 60 s (sotto ASan sono molto più lenti) e quindi non sono coperti: nove raccordi, `ShellStitchedTolerantTangencies`, due rivoluzioni parziali e quattro round-trip STEP/IGES. Tre test falliscono nel mio ambiente senza indicare un difetto di ForgeCAD: `UnifyCylinders` (l'eccezione nasce dentro `ShapeUpgrade_UnifySameDomain` di OCCT 7.6, la libreria di riferimento del test), `ConeMatchesOcct` (confronto delle derivate del cono con OCCT 7.6) e `DraftCurvedAndConcaveSolids` (volume diverso di 1,8e-6 con tolleranza assoluta 1e-6, in una build -O1 con sanitizer: tolleranza fragile, da ricontrollare in Release con OCCT 7.8).
- **Fuzzing:** durate brevi (minuti, non ore). IGES non è stato fuzzato a lungo perché B1 lo ferma subito: andrebbe ripetuto dopo le correzioni.
- **clang-tidy e cppcheck** non ripetuti: nella review del 25/10 avevano dato soltanto falsi positivi e una segnalazione minore.
- **Test GUI e CTest** non eseguiti; build solo Linux (niente macOS).
- **Orfane:** l'analisi copre il solo eseguibile `forgecad`. Le funzioni chiamate via stringa (`invokeMethod`) o tramite moc risultano comunque raggiungibili e non sono state segnalate.
