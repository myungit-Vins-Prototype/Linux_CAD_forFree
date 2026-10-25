# Code review generale — 25 ottobre 2026

Base Git: `e2d76d8`, con le modifiche locali preesistenti alle icone e al manipolatore dei poli NURBS incluse nell'analisi. La review iniziale non applicava correzioni; lo stato degli interventi successivi è riportato sotto. Review svolta da quattro agenti su kernel, tavole, modello/persistenza e UI/viewport/build.

Inventario: 444 file C++/header/CUDA/Objective-C++ versionati, circa 142.741 righe; mappa di 456 file e 6.433 relazioni. È una revisione trasversale con letture mirate e riproduzioni, non una verifica formale di ogni riga. Le funzioni virtuali, i callback Qt e le API usate soltanto nei test non sono stati classificati automaticamente come orfani.

## Interventi selezionati dall’utente

Implementati R1, R2, R4, R5, R6, R7 e R8. R3 e rimozioni degli orfani restano rinviati. Completate anche le semplificazioni 1–6 e l’avviso aggiuntivo di Estrusione, successivamente autorizzati dall’utente. Le sedi e le riproduzioni nelle sezioni successive documentano il codice al momento della review, prima delle correzioni.

- **R1:** certificato sui coefficienti delle curve razionali Bézier proiettate; equivalenza non più decisa soltanto da campioni. I casi non certificabili restituiscono conservativamente `false`.
- **R2:** unità associate al documento tramite il contesto dei widget; campi numerici, quote, misure e anteprime non condividono più uno stato globale mutabile.
- **R4:** bypass delle feature soppresse prima della validazione degli appoggi/Pierce dello schizzo.
- **R5:** formato 54 conserva i nomi topologici support/Pierce e legge i formati 1–53. Confronto dei vecchi check-in normalizzato; compatibilità delle impronte delle tavole/rilasci 53 soltanto quando non vi sono nuovi nomi da proteggere. Nessuna migrazione dei documenti utente effettuata.
- **R6:** calcolo locale delle basi e derivate del loft, indipendente dalla precisione aggiuntiva di `long double`. Regressioni ARM su campate di un ULP, polinomiali/razionali e nei due versi.
- **R7:** area messaggi in sola lettura, testo completo selezionabile/copiabile e scorrimento orizzontale/verticale; pulsanti fuori dall’area scorrevole del form.
- **R8:** identità persistenti per funzione, condivise tra creazione/modifica e distinte per estensione/rete/poli. Migrazione solo delle preferenze non ambigue; riduzioni temporanee del viewport non sovrascrivono la dimensione preferita.

**Verifiche e distribuzione:** build Release di tutti i target riuscita; suite applicativa completa disponibile: **104 PASS e 1 SKIP OpenGL**, esclusi due test con modelli esterni assenti. Dopo l’ultima ricompilazione, **14/14 regressioni mirate PASS**. Include unità, soppressione, codec/revisioni, pannelli, storia, tavole, visitatori e impronta geometrica. Kernel: Boolean 37/37, Surface 38/38, Loft 42/42 e Unify 5/5PASS. QA del vero pannello offscreen nei temi chiaro/scuro: messaggio di 6.461 caratteri, scorrimento H/V, dimensione stabile e pulsanti accessibili. Installato MacOsCad con SHA256 `5ac062d544e18ba749e6aa4c795c24d8b3be93f002d68d0924d2feecd3e6b5ee`; backup in `/private/tmp/review-simplify-final-install/forgecad-before-update`. Riavviare l’app per caricare la build.

**Architettura finale:** 465 file, 6.525 relazioni, 229 unità, zero diagnostiche; controllo `--check` e collegamenti nei due versi superati. Snapshot HTML/JSON locali e ignorati da Git. Anteprima visiva della mappa non completata per restrizioni sandbox/browser; dati verificati.

**Limiti:** nessun collaudo manuale Cocoa/OpenGL in questo intervento; escluse due suite che richiedono modelli esterni mancanti. I test mirati si sovrappongono alla suite estesa: non sommare i conteggi. Il parametro `double` dell’API di proiezione resta incapace di rappresentare l’interno di una campata larga un ULP; la regressione del loft controlla estremi distinti, continuità G1 e topologia. Tre warning preesistenti `firstDimension` nei test tavole. Nessun commit/push; R3 e le rimozioni degli orfani restano aperti. Il formato 54 richiede il lettore aggiornato; i vecchi file rimangono leggibili.

## Difetti confermati

### R1 — P1: superfici diverse dichiarate coincidenti

**Sede:** `kernel/fk_surface_algo.cpp:776–780`, funzione `baseCurveOn`.

Il numero di campioni per campata scende a tre quando crescono i breakpoint. Una B-spline cubica ondulata di 40 campate e una piatta coincidono ai campioni scelti, pur differendo fra essi. Per le rispettive estrusioni `sameSurface` restituisce `true` con tolleranza `1e-9`, mentre al quarto di campata lo scarto è `0,28125`.

Il predicato è usato dalle booleane (`kernel/fk_boolean.cpp:374`) e dall'unificazione (`kernel/fk_unify.cpp:21`): possono essere trattati come equivalenti supporti geometricamente diversi. **Riprodotto il predicato errato, non una booleana completa con risultato errato.**

Proposta: certificare l'equivalenza delle pezze, oppure usare una suddivisione adattiva con limite d'errore. Aumentare soltanto un numero fisso di campioni non risolve il problema generale.

Sonda: `/private/tmp/kernel-review-same-surface.cpp`, collegata alla libreria kernel corrente.

### R2 — P1: le unità di un documento contaminano le altre finestre

**Sedi:** `cad_expression.cpp:16,56–65`; `forgeCad2026_viewport_document.cpp:43–47,62–63`.

L'unità di visualizzazione e l'elenco dei campi numerici sono globali. Caricare un secondo documento modifica anche i campi del primo, benché `CadViewport::lengthUnit()` del primo rimanga invariato. La riattivazione della finestra non sincronizza le unità; riselezionare la stessa unità esce anticipatamente da `setLengthUnit`.

Riproduzione con due viewport reali e un campo numerico:

```text
prima:  documento A=mm, globale=mm, testo=25,40 mm, input 10 -> 10 mm
dopo:   documento A=mm, documento B=in, globale=in,
        testo del campo A=1,00 in, input 10 -> 254 mm
riselezione mm in A: globale ancora in, input 10 -> 254 mm
```

Effetto: visualizzazione incoerente e dimensioni inserite in un'unità diversa da quella del documento. Non basta correggere la sola etichetta del campo.

Proposta: associare conversione e formattazione al documento proprietario dei widget; eliminare il contesto globale mutabile. La sola sincronizzazione al cambio focus è una mitigazione incompleta per finestre e pannelli simultanei.

Sonda: `/private/tmp/review_units.cpp`, log `/private/tmp/review_units.log`, oggetti del runner viewport corrente, Qt offscreen.

### R3 — P2: normali scritture del catalogo causano falsi errori di integrità

**Sedi:** `cad_revision_store.cpp:188–190,209–210`; conseguenza GUI in `forgeCad2026_gui.cpp:3925–3929`.

Il writer pubblica prima `head.json` e poi `catalog.json`. Il lettore legge separatamente catalogo e puntatore senza sincronizzarsi con la transazione. Durante un normale checkout/cancel nello stesso workspace può vedere due generazioni diverse e segnalarle come corruzione.

Riproduzione: 300 cicli checkout/cancel in un thread, `documents()` nell'altro; **1.745 falsi errori su 6.967 letture**, zero errori del writer, lettura finale corretta. Messaggio: «Il catalogo non corrisponde all'ultimo stato pubblicato».

La GUI tratta l'errore come accesso in sola lettura e rilascia `documentLock_`; quando il catalogo torna leggibile non riacquisisce automaticamente l'esclusiva. **Race riprodotta; perdita del lock verificata nel flusso sorgente, senza prova integrata con due finestre.**

Proposta: lettura coerente sotto lock o protocollo di ritentativo che distingua una pubblicazione in corso da una divergenza persistente; evitare di invalidare permanentemente la sessione per un errore transitorio.

Sonda: `/private/tmp/review_catalog_race.cpp`, compilata direttamente con `cad_revision_store.cpp` corrente e QtCore.

### R4 — P2: la soppressione non bypassa i riferimenti invalidi dello schizzo

**Sede:** `cad_feature_build.cpp:136–149`, prima del ramo `body.suppressed` a riga 155.

I controlli Pierce e appoggio dello schizzo precedono il bypass della feature soppressa. Se l'appoggio diventa invalido, una lavorazione soppressa restituisce un corpo nullo invece di trasferire il risultato precedente; può così interrompere anche le lavorazioni successive.

Riproduzione sul medesimo predecessore e sulla medesima estrusione soppressa: con `supportError` presente, `body=0` ed errore; togliendo soltanto `supportError`, `body=1` e nessun errore.

Proposta: eseguire il bypass della soppressione prima della validazione degli input della feature.

Sonda: `/private/tmp/review_suppression.cpp`, oggetti Release applicativi correnti. Non modificato un documento utente.

### R5 — P2: il codec perde i nomi topologici di appoggi e vincoli Pierce

**Sedi:** `cad_document_io.cpp:180,199,310–314`; enumerazione separata delle sole feature a riga 334.

`writeRefs` non serializza `GeometryRef::point.name`. Il passaggio aggiuntivo che conserva i nomi topologici riguarda le feature, ma non i riferimenti appartenenti agli schizzi. I riferimenti nominati sono realmente prodotti da `createFaceSketch` (`forgeCad2026_viewport_sketch.cpp:1280`) e usati per seguire l'appoggio (`forgeCad2026_viewport_document.cpp:1878`).

Round-trip riuscito di `encodeDocument`/`decodeDocument`: entrambi i nomi inizialmente `stable-face-name` diventano stringhe vuote; la distinta etichetta descrittiva Pierce resta conservata. La sonda usa segmento e riferimento di vincolo validi.

Dopo la riapertura la risoluzione deve affidarsi agli altri identificatori e alla geometria, perdendo una parte del riferimento persistente. **Perdita dei dati riprodotta; non riprodotto un modello reale che si agganci alla faccia sbagliata.**

Proposta: estensione compatibile/versionata per i nomi dei riferimenti degli schizzi, preservando l'ordine legacy dei nomi delle feature.

Sonda: `/private/tmp/review_sketch_ref_codec.cpp`, oggetti Release applicativi correnti.

### R6 — P2: il loft G1 con campata molto corta fallisce su Apple ARM

**Sede:** `kernel/fk_loft.cpp:351–365`.

La collocazione usa `long double` per distinguere ascisse di Greville che non sono rappresentabili distintamente in `double`. Sull'host Apple ARM entrambi i tipi hanno 53 bit di precisione: le ascisse coincidono e il sistema risulta singolare.

Riprodotto il fallimento di `forgecad_edge_loft_tests`, poi isolato nel kernel: stessa fixture con nodo interno `0.5` riuscita, zero problemi `checkBody`; con `nextafter(1,0)` fallisce con «sistema d'interpolazione singolare». Il registro contiene un caso reale analogo relativo a `mouse2`.

Proposta: calcolo delle basi in coordinate locali normalizzate della campata, indipendente dalla disponibilità di precisione aggiuntiva in `long double`.

Sonda: `/private/tmp/kernel-review-loft.cpp`. È un difetto geometrico/di portabilità, non un limite del backend offscreen.

## Integrazione dalla segnalazione dell'utente: pannelli di funzione

I due punti seguenti si aggiungono ai sei difetti riprodotti sopra. Sintomi segnalati dall'utente e riscontri statici nel codice; nessuna nuova prova grafica o correzione applicata in questa integrazione.

### R7 — P2: errori lunghi difficili o impossibili da leggere

**Sedi:** `forgeCad2026_dialogs_common.cpp:910–914,943–947,977–979`; contenitore a righe 305–311.

Gli helper comuni `runUntilApplied`, `runUntilAppliedModeless` e `PreviewScope` mostrano errori ed esiti dell'anteprima in `QLabel` con word wrap e larghezza massima di 360 pixel. Non hanno scorrimento proprio; il contenitore comune dispone di scorrimento verticale, ma disabilita esplicitamente quello orizzontale. Gli errori lunghi, multilinea o con identificatori estesi non hanno quindi un'area dedicata che consenta di consultarli agevolmente. Alcuni pannelli hanno anche etichette di stato locali da includere nella correzione.

**Requisito esplicito dell'utente:** scorrimento sia orizzontale sia verticale dei messaggi lunghi, conservando il testo completo.

Proposta: un componente diagnostico comune, in sola lettura, con testo selezionabile/copiabile, altezza contenuta e barre su entrambi gli assi quando necessarie. Per rendere effettivo lo scorrimento orizzontale, evitare l'a capo automatico imposto alla larghezza disponibile; preservare le interruzioni di riga del messaggio. L'arrivo dell'errore non deve allargare o allungare il pannello né rendere irraggiungibili Conferma/Annulla. Coprire errori alla conferma, anteprima e stato dei picker, senza sostituire indiscriminatamente le normali etichette dei campi.

Verifiche previste all'implementazione: testo breve, molte righe, riga lunghissima senza spazi, caratteri accentati, selezione/copia, entrambe le scrollbar e pulsanti accessibili a dimensione minima; riscontro grafico nel pannello reale.

### R8 — P2: dimensioni dei pannelli condivise o identificate dal titolo

**Sedi:** `forgeCad2026_dialogs_common.cpp:605–609,636–648`; `forgeCad2026_dialogs_surface_tools.cpp:138–142`.

La persistenza sceglie `objectName`, se diverso dal nome generico, altrimenti il titolo della finestra. Non tutte le funzioni condividono un'unica dimensione; esiste però una collisione concreta: Estendi fino a riferimento, Superficie da rete di curve e Modifica poli NURBS impostano tutti `objectName="surfaceToolDialog"`. Leggono e scrivono quindi `view/functionPanelSizes/surfaceToolDialog`, nonostante contenuti e larghezze minime differenti (440/580 pixel).

Il fallback sul titolo produce anche l'effetto opposto: Estrusione e Modifica estrusione possono usare preferenze distinte per la stessa funzione. Sono presenti inoltre adattamenti legittimi allo spazio del viewport (`maximumPanelSize`, `keepInsideViewport`), da distinguere dalla contaminazione fra funzioni. La condivisione di una classe C++ non impone di condividere le preferenze.

**Requisito:** dimensione memorizzata con un identificatore stabile per ciascuna funzione; riaprire la stessa funzione deve ripristinare la scelta dell'utente. Creazione/modifica della stessa funzione dovrebbero usare la medesima preferenza, salvo differenze intenzionali esplicite. Estensione, rete e poli devono avere chiavi separate, indipendenti da titolo e traduzione.

Proposta: chiave semantica esplicita separata da `objectName`, con migrazione controllata delle preferenze precedenti. Conservare la dimensione preferita anche quando un viewport più piccolo impone una riduzione temporanea; non farla sovrascrivere dai messaggi di errore o da variazioni del contenuto.

Verifiche previste: ridimensionare ciascuna delle tre funzioni superficie e riaprirle in ordine alternato; apertura creazione/modifica; riduzione e ripristino del viewport; errore lungo senza variazione della preferenza. La collisione delle chiavi è verificata staticamente, non è ancora riprodotta tramite trascinamenti reali.

## Funzioni senza chiamanti nel repository

Ricerca su sorgenti versionati, inclusi i test, e confronto con chiamate/riferimenti della mappa. Per questi nomi risultano soltanto dichiarazione e definizione; nessuna rimozione è stata applicata. Le API kernel potrebbero avere consumatori esterni al repository.

| Funzione | Definizione | Nota |
|---|---|---|
| `CadViewport::sectionEnabled` | `forgeCad2026_viewport.cpp:411` | Getter inutilizzato |
| `CadViewport::extrusionNames` | `forgeCad2026_viewport_document.cpp:782` | Elenco inutilizzato |
| `CadViewport::drawSketchDimensions` | `forgeCad2026_viewport_render.cpp:467` | Vecchio percorso di rendering senza chiamanti |
| `CadViewport::resumeEdgePick` | `forgeCad2026_viewport_selection.cpp:285` | Percorso di ripresa inutilizzato |
| `CadViewport::setEdgePickLinear` | `forgeCad2026_viewport_selection.cpp:202` | Setter inutilizzato |
| `CadViewport::setSelectedPointPosition` | `forgeCad2026_viewport_selection.cpp:669` | Mutazione privata inutilizzata |
| `CadViewport::constraintsVisible` | `forgeCad2026_viewport_sketch.cpp:1242` | Getter inutilizzato; conservare campo e setter usati |
| `StoryboardTree::firstFeature` | `forgeCad2026_storyboard.cpp:511` | Helper inutilizzato |
| `usesSketch` | `cad_feature_dependencies.cpp:182` | Helper inutilizzato |
| `modelBodyFeatures` | `cad_model_history.cpp:71` | Helper inutilizzato |
| `DrawingSheetEditor::setModelProvider` | `cad_drawing_editor.cpp:2896` | Provider sempre vuoto; relativo comando disabilitato e nascosto dal contenitore |
| `assignPolyhedralGeometry` | `kernel/fk_primitives.cpp:30` | API kernel senza utilizzi interni o test |
| `transformSurface` | `kernel/fk_transform.cpp:305` | Wrapper pubblico inutilizzato; `Transformer::surface` interno è usato |

Ulteriori piccoli residui: wrapper `CadViewport::basisMatrix` e `normalizedViewAngle` in `forgeCad2026_viewport_scene.cpp:21,56`, `CurvePicker::text` in `forgeCad2026_dialogs_common.cpp:1549`. Non eliminare le corrispondenti implementazioni della camera, che sono usate.

`dimensionOfSection`, diversi accessori delle tavole e metodi come `setSectionStyle`, `removeAxis` e `removeAnnotation` sono invece usati dai test: non sono classificati come orfani. L'assenza di chiamate AST da sola non basta per dichiarare morto un override Qt o un callback.

## Semplificazioni e costi evitabili

**Stato finale: implementate tutte e sei.** I paragrafi numerati sotto conservano il rilievo iniziale; gli esiti sono:

| Punto | Intervento e verifica |
|---|---|
| 1 | Nuovo modulo `cad_drawing_preparation`, senza QWidget; risultati preparati una sola volta e trasferiti all’editor dopo la validazione di tutti i fogli. Due fogli: **2 calcoli invece di 5**. Sonda locale:43ms contro108ms, non un benchmark generale. Test su annullamento/errore del foglio inattivo, stato atomico, presentazioni e storia. |
| 2 | Estensione e `trimSheet` condividono la selezione del pezzo; l’estensione riusa il primo taglio. SurfaceExtendUntil3/3 e Sheet40/40PASS, incluse due pareti in entrambi gli ordini. |
| 3 | Visitatori comuni per20famiglie; dipendenze, storia e topologia conservano le proprie regole. Codec binario separato. **4.640 casi** confrontati col codice precedente su29tipi di feature: stesso ordine, duplicati e contenuti. Test const e preservazione Undo per support/Pierce. |
| 4 | Helper unico `forgeBooleanWithHistory` per i tre wrapper, con operandi, opzioni, errori e genealogia conservati. Regressioni storia/booleane/estrusioni/rivoluzioni superate. |
| 5 | Libreria interna `forgecad_drawing_test_support` per **17 runner**: i dieci sorgenti comuni sono compilati una sola volta nel supporto. App, viewport e ribbon conservano i target propri; tutti i runner compilati e verificati. Nessuna misura del risparmio temporale dichiarata. |
| 6 | Impronta basata sulla chiusura conservativa degli include locali e delle implementazioni, dai punti di ingresso geometria/storia/codec; kernel completo e backend CPU/CUDA selezionato. Test: modifiche visuali indipendenti non invalidano, dipendenze geometriche sì; nuovi include e manifesto reale verificati. |

**Avviso Estrusione aggiuntivo:** messaggio informativo se non esistono schizzi, se manca uno schizzo attivo/selezionato o se il profilo non contiene geometria estrudibile. Riutilizzati i criteri del costruttore per costruzioni, punti e degeneri; uno schizzo selezionato valido apre normalmente il pannello. Nove scenari dell’azione reale verificati.

1. **Aggiornamento delle tavole: calcoli ripetuti verificati.** `cad_drawing_sheets_ui.cpp:139–141` costruisce ogni editor temporaneo sul vecchio modello, lo aggiorna al nuovo, poi aggiorna nuovamente l'editor attivo. Con due fogli e una vista per foglio, la sonda rileva **5 calcoli completi** per produrre i due nuovi risultati; 108 ms su un box semplice, non un benchmark di tavole pesanti. Separare preparazione geometrica da QWidget e trasferire i risultati già preparati, conservando l'atomicità. Sonda e log: `/private/tmp/review-drawing-update.cpp`, `/private/tmp/review-drawing-update.log`.
2. **Estensione delle superfici: intersezione ripetuta.** `kernel/fk_surface_tools.cpp:123` chiama `splitSheet`, poi `trimSheet`, che ripete `splitSheet` in `kernel/fk_sheet.cpp:276`. Riutilizzare i pezzi già prodotti. Duplicazione verificata nel sorgente, beneficio non misurato.
3. **Visitatori dei riferimenti geometrici duplicati.** `cad_model_history.cpp:115`, `cad_topology_ref.cpp:495`, dipendenze e grafo enumerano le stesse famiglie. Un visitatore comune ridurrebbe le omissioni all'introduzione di nuove feature. Il visitatore binario a ordine fisso del codec deve restare distinto come contratto del formato.
4. **Wrapper booleani duplicati, già segnalati dalla review precedente.** `cad_extrude.cpp:147`, `cad_forge.cpp:179`, `cad_pattern.cpp:86` ripetono operazione, costruzione dello shared pointer e registrazione della genealogia. Accorpamento possibile preservando esattamente operandi e storia; non era un nuovo rilievo; l’implementazione è stata successivamente autorizzata dall’utente.
5. **Compilazione ripetuta degli stessi sorgenti nei runner.** Molti target tavole in `CMakeLists.txt:540–690` ricompilano editor, proiezioni, fogli e icone. Una libreria interna condivisa dai test ridurrebbe duplicazione delle liste e lavoro di compilazione, verificando prima definizioni e dipendenze specifiche dei target. Nessun tempo di risparmio misurato.
6. **Impronta geometrica troppo ampia.** Il glob `cad_*.cpp/.h` in `CMakeLists.txt:245–248` include anche icone e UI delle tavole. Modifiche puramente visuali invalidano quindi l'impronta delle cache geometriche. Valutare una lista basata sulle dipendenze reali del calcolo, mantenendo la sicurezza dell'invalidazione; proposta, nessuna riduzione applicata.

## Verifiche e limiti della review iniziale

Le verifiche seguenti precedono le correzioni; per lo stato finale vedere sopra. Prima passata:

```sh
ctest --test-dir forgecad-release --output-on-failure -E '^forgekernel_' -j 2 --timeout 180
```

**102 test: 96 PASS, 5 FAIL, 1 SKIP, 58,38 s.** Log `/private/tmp/review-20261025-ctest.log`.

| Test non passato | Diagnosi |
|---|---|
| `forgecad_mouse_snapshot_tests` | `File_Esempio/Mouse.prt` assente; fixture esterna non disponibile |
| `forgecad_view_tests` | Fallisce alla lettura di `Applicatore.prt`, esempi assenti |
| `forgecad_spatial_sketch_tests` | Confronta il testo con `7.125`, mentre la formattazione segue il locale italiano e usa la virgola; test non indipendente dal locale |
| `forgecad_edge_loft_tests` | Difetto R6, ripetuto in seriale e isolato nel kernel |
| `forgecad_sketch_story_tests` | Salva in `/tmp/sketch-story/tree.png` senza creare la directory; **PASS dopo la sola creazione della directory temporanea**, senza modifiche al codice |
| `forgecad_overlay_uniform_tests` | SKIP: contesto OpenGL non disponibile nel backend offscreen |

Spatial, edge-loft e sketch-story inizialmente falliscono anche in seriale; non sono attribuiti alla parallelizzazione. I test dovrebbero usare `QTemporaryDir`, confrontare valori numerici o impostare esplicitamente il locale, e distinguere fixture mancanti da errori funzionali.

Verifica delle date sorgente/oggetto: app e runner viewport non presentano `.cpp` più recenti dei loro oggetti; vari runner standalone e ribbon risultano invece obsoleti. La passata eseguita misura i binari disponibili e **non certifica tutti i sorgenti correnti**. Non è stata eseguita una ricompilazione generale; sonde compilate temporaneamente o collegate agli oggetti correnti come indicato sopra.

Kernel: filtri **Transform 9/9 PASS**, **Surface 37/37 PASS**. Il filtro Step si è arrestato con exit 139 dopo un errore di scrittura della fixture fissa `/var/tmp/forgekernel_occt_import_test.step`, non scrivibile nella sandbox: nessuna validazione generale STEP/IGES dichiarata. Suite kernel completa, GPU CUDA e interazione manuale Cocoa non eseguite.

`python3 tools/generate_architecture.py --check`: **PASS, 456 file/6.433 relazioni**. `git diff --check`: PASS. La mappa non richiede rigenerazione perché questa review non cambia moduli, responsabilità o dipendenze; HTML/JSON restano locali e ignorati.

Documenti CAD dell'utente, sorgenti funzionali ed eseguibile installato invariati. Le sonde restano temporanee e non sono nuovi test permanenti. Le osservazioni preliminari non riprodotte sul lettore binario non sono incluse nei sei difetti confermati.

Ordine suggerito: R1/R2, poi R3/R5 per integrità e persistenza, R4/R6 per rigenerazione e portabilità; quindi costi ripetuti delle tavole/superfici e pulizia degli orfani in interventi separati.
