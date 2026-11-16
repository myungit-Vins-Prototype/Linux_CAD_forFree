# Inventario manutentivo: funzioni inutilizzate — 15 novembre 2026

Repository esaminato: `/home/myungit/Linux_CAD_forFree`. Nessuna modifica ai sorgenti.

## Metodo e risultato

Sono confermate **22 funzioni senza chiamanti nel repository esaminato**. Non sono 22 bug funzionali: sono candidati di pulizia, priorità **P3**, da rimuovere o documentare come API intenzionali. Il controllo combina `docs/architecture/graph.json` (nessuna chiamata C++ né riferimento a funzione per i simboli qualificati), ricerca lessicale `rg` nell'intero albero non ignorato, incluse suite e strumenti, e lettura di dichiarazioni/definizioni. I membri GUI elencati sono normali metodi non virtuali: non sono slot, segnali, `Q_INVOKABLE` o override Qt.

| Funzione | Definizione | Dichiarazione | Evidenza / distinzione |
|---|---|---|---|
| `CadViewport::setSelectedPointPosition` | `forgeCad2026_viewport_selection.cpp:746` | `forgeCad2026_viewport.h:1256` | Solo dichiarazione e definizione. |
| `CadViewport::setEdgePickLinear` | `forgeCad2026_viewport_selection.cpp:232` | `forgeCad2026_viewport.h:458` | Solo dichiarazione e definizione. |
| `CadViewport::setExtendPickCallback` | `forgeCad2026_viewport_selection.cpp:230` | `forgeCad2026_viewport.h:456` | Solo dichiarazione e definizione. |
| `CadViewport::resumeExtendPick` | `forgeCad2026_viewport_selection.cpp:237` | `forgeCad2026_viewport.h:460` | Solo dichiarazione e definizione. |
| `CadViewport::constraintsVisible` | `forgeCad2026_viewport_sketch.cpp:1256` | `forgeCad2026_viewport.h:799` | Solo dichiarazione e definizione. |
| `CadViewport::sectionEnabled` | `forgeCad2026_viewport.cpp:456` | `forgeCad2026_viewport.h:750` | Solo dichiarazione e definizione. |
| `CadViewport::geometrySnap` | `forgeCad2026_viewport.cpp:76` | `forgeCad2026_viewport.h:105` | Le altre occorrenze sono stringhe di impostazioni/icone, non chiamate al getter. |
| `CadViewport::gridSnap` | `forgeCad2026_viewport.cpp:80` | `forgeCad2026_viewport.h:107` | Le altre occorrenze sono stringhe di impostazioni/icone, non chiamate al getter. |
| `CadViewport::basisMatrix` | `forgeCad2026_viewport_scene.cpp:47` | `forgeCad2026_viewport.h:823` | Il wrapper CadViewport è inutilizzato; gli omonimi ViewportCamera sono chiamati e vanno conservati. |
| `CadViewport::normalizedViewAngle` | `forgeCad2026_viewport_scene.cpp:82` | `forgeCad2026_viewport.h:844` | Il wrapper CadViewport è inutilizzato; gli omonimi ViewportCamera sono chiamati e vanno conservati. |
| `CadViewport::extrusionNames` | `forgeCad2026_viewport_document.cpp:914` | `forgeCad2026_viewport.h:318` | Solo dichiarazione e definizione. |
| `StoryboardTree::firstFeature` | `forgeCad2026_storyboard.cpp:549` | `forgeCad2026_storyboard.h:94` | Solo dichiarazione e definizione. |
| `ForgeCad::DrawingSheetEditor::setModelProvider` | `cad_drawing_editor.cpp:3649` | `cad_drawing_editor.h:104` | Solo dichiarazione e definizione. |
| `ForgeCad::forgeIntersectRay` | `cad_forge.cpp:2405` | `cad_forge.h:228` | Solo dichiarazione e definizione. |
| `ForgeCad::modelBodyFeatures` | `cad_model_history.cpp:75` | `cad_model_history.h:42` | Solo dichiarazione e definizione. |
| `ForgeCad::usesSketch` | `cad_feature_dependencies.cpp:205` | `cad_feature_dependencies.h:21` | Solo dichiarazione e definizione. |
| `ForgeCad::commandIconNames` | `cad_icons.cpp:1624` | `cad_icons.h:15` | Una terza occorrenza è un commento di documentazione nell’header. |
| `ForgeCad::nearestBodyEdge` | `cad_sketch_refs.cpp:151` | `cad_sketch_refs.h:66` | Solo dichiarazione e definizione. |
| `ForgeCad::appendFixedReferenceLine` | `cad_sketch_refs.cpp:576` | `cad_sketch_refs.h:57` | Solo dichiarazione e definizione. |
| `ForgeCad::Kernel::Corner::otherFace` | `kernel/fk_blend.cpp:369` | Definito nella classe locale al `.cpp` | Solo dichiarazione e definizione. |
| `ForgeCad::Kernel::ChainEdge::inPlane` | `kernel/fk_blend_loop.cpp:107` | Definito nella classe locale al `.cpp` | Gli omonimi fuori da questa classe sono variabili/lambda/metodi diversi; nessuna chiamata al membro indicato. |
| `ForgeCad::Kernel::BandMatrix::band` | `kernel/fk_fill.cpp:239` | Definito nella classe locale al `.cpp` | Gli omonimi fuori da questa classe sono variabili/lambda/metodi diversi; nessuna chiamata al membro indicato. |

Le 22 voci erano già presenti o riconducibili all'inventario aperto del 2 novembre; qui sono ricontrollate sul codice corrente e con righe aggiornate. Non sono presentate come regressioni introdotte dopo quella review.

## Esclusioni: funzioni raggiunte solo dai test

Queste API risultano esercitate dai test, quindi **non sono orfane** nel repository. L'eventuale isolamento in un'interfaccia di test è una scelta manutentiva distinta.

| Funzione | Definizione | Chiamante verificato |
|---|---|---|
| `CadViewport::drawSketchDimensions` | `forgeCad2026_viewport_render.cpp:499` | `tests/test_sketch_linked_refs.h:381` |
| `CadViewport::hasPendingSketchChanges` | `forgeCad2026_viewport_document.cpp:26` | `tests/test_sketch_interaction.h:35`, altri 6 siti nel grafo |
| `CadViewport::advanceWheelZoom` | `forgeCad2026_viewport_scene.cpp:367` | `tests/test_viewport.cpp:8429`, altri 5 siti nel grafo |
| `CadViewport::previewError` | `forgeCad2026_viewport_preview.cpp:283` | `tests/test_extrusion_next_body_gui.h:50`, altri 12 siti nel grafo |
| `ForgeCad::gpuBoxPairsProcessed` | `cad_gpu_broad_phase.cpp:58` | `tests/test_gpu_broad_phase.cpp:105`, `tests/test_lazy_mesh.h:248` |
| `ForgeCad::HistoryGraphDialog::zoomFactor` | `cad_history_graph.cpp:796` | `tests/test_viewport.cpp:9103` |
| `FunctionDialogPanel::backdropBlur` | `forgeCad2026_dialogs_common.cpp:380` | `tests/test_viewport.cpp:9153` |

## Esclusioni: callback Qt e funzioni tornate in uso

- `CadViewport::initializeGL` (`forgeCad2026_viewport_render.cpp:120`) e `resizeGL` (`:143`) non hanno chiamanti C++ diretti nel grafo: sono override di `QOpenGLWidget`, quindi sono chiamati dal framework.
- Lo stesso vale per `CadViewport::event` (`forgeCad2026_viewport_interaction.cpp:1190`), `keyReleaseEvent` (`:1222`), `leaveEvent` (`:1373`) e `mouseDoubleClickEvent` (`:634`); le dichiarazioni `override` sono in `forgeCad2026_viewport.h:993–1011`. Costruttori, distruttori e virtuali delle curve/superfici non si classificano per sola assenza di archi diretti.
- **`CadViewport::ensureBodyDisplay` non è più orfana**, diversamente dall'inventario del 2 novembre: produzione in `forgeCad2026_viewport_body_box.cpp:17` e `forgeCad2026_viewport_storyboard.cpp:154`, più 7 siti di test nel grafo.
- **`Kernel::transformSurface` non è più orfana**: produzione in `kernel/fk_blend_polar.cpp:305`, test in `kernel/tests/test_polar_blend.cpp:31`.
- `CadViewport::resumeEdgePick` è usata in produzione in `forgeCad2026_dialogs_fillet.cpp:295` e `:631`, oltre ai test; resta esclusa come già chiarito il 2 novembre.

## Prove ripetibili e limiti

- `orphans-grep.txt` contiene il comando `rg` e tutte le occorrenze per ciascun simbolo; i nomi omonimi sono conservati per mostrare la distinzione.
- `orphans-callgraph.json` contiene il controllo degli archi entranti per i 22 simboli qualificati.
- `inventory_orphans.py` rigenera questo inventario e verifica che le righe indicate contengano il simbolo atteso e che il grafo non abbia chiamate/riferimenti entranti. Eseguire `python3 /tmp/forgecad-review-20261115/memory/inventory_orphans.py` dalla macchina che contiene il repository.
- Analisi del grafo locale disponibile, senza rigenerazione né build/link dedicato con garbage collection in questa sottoreview. Non si rivendica quindi un nuovo risultato di eliminazione del linker; tale verifica apparteneva al rapporto storico.
- L'inventario è un campione verificato, non una dimostrazione esaustiva su tutti i metodi del progetto. Backend opzionali, dispatch virtuale, template e usi in repository esterni non si escludono con un solo call graph. Nessuna API dei backend CUDA/Metal è stata proposta per la rimozione solo perché inattiva nella build Linux CPU.
