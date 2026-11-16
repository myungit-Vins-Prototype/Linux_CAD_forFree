# Ricontrollo delle 22 candidate orfane — 16 novembre 2026

**Aggiornamento successivo:** le cinque funzioni private/locali sono state eliminate dopo nuovo controllo di767file. Le17API pubbliche sono conservate. Build completa PASS;20/21CTest pertinenti,un fallimento preesistente riprodotto sul kernel precedente; mappa aggiornata. [Correzioni, prove e limiti](review-k10-helix-orphans-2026-11-16.md).

## Snapshot storico prima della rimozione

Scan in sola lettura: 763 file di codice/configurazione attivi non ignorati; tutti i test presenti, anche fuori CMake, backend opzionali e confronti OCCT inclusi. Snapshot grafo: 762 nodi, 11023 archi.

Esito: **22/22 ancora senza chiamanti o riferimenti a funzione osservati nel repository corrente**. Di queste, **5 private/locali sono candidate concrete alla rimozione** e **17 sono API pubbliche applicative da trattare separatamente**. Nessuna è diventata test-only o un falso positivo chiamato in produzione. Non sono 22 difetti funzionali.

| Funzione | Definizione corrente | Dichiarazione corrente | Visibilità / categoria | Evidenza |
|---|---|---|---|---|
| `CadViewport::setSelectedPointPosition` | `forgeCad2026_viewport_selection.cpp:746` | `forgeCad2026_viewport.h:1256` | privata | Metodo normale, accessibile al friend ViewportInteractionTest ma assente da tutti i test, anche fuori CMake. |
| `CadViewport::setEdgePickLinear` | `forgeCad2026_viewport_selection.cpp:232` | `forgeCad2026_viewport.h:458` | pubblica | Setter di stato del picker; nessuna chiamata o presa di indirizzo. Non è uno slot Qt. |
| `CadViewport::setExtendPickCallback` | `forgeCad2026_viewport_selection.cpp:230` | `forgeCad2026_viewport.h:456` | pubblica | API per installare callback del picker; il campo callback è letto altrove, ma il setter non è chiamato. |
| `CadViewport::resumeExtendPick` | `forgeCad2026_viewport_selection.cpp:237` | `forgeCad2026_viewport.h:460` | pubblica | Ripresa del picker di estensione; normale metodo pubblico non invocabile via metaoggetto. |
| `CadViewport::constraintsVisible` | `forgeCad2026_viewport_sketch.cpp:1259` | `forgeCad2026_viewport.h:799` | pubblica | Getter pubblico; setter e stato rimangono usati, il getter non ha chiamanti. |
| `CadViewport::sectionEnabled` | `forgeCad2026_viewport.cpp:456` | `forgeCad2026_viewport.h:750` | pubblica | Getter pubblico della sezione; nessun chiamante. |
| `CadViewport::geometrySnap` | `forgeCad2026_viewport.cpp:76` | `forgeCad2026_viewport.h:105` | pubblica | Tre occorrenze aggiuntive sono chiavi QSettings, non il getter; setter chiamato in produzione. |
| `CadViewport::gridSnap` | `forgeCad2026_viewport.cpp:80` | `forgeCad2026_viewport.h:107` | pubblica | Cinque occorrenze aggiuntive sono chiavi QSettings o nomi icone, non il getter; setter chiamato. |
| `CadViewport::basisMatrix` | `forgeCad2026_viewport_scene.cpp:48` | `forgeCad2026_viewport.h:823` | pubblica | Wrapper della camera non usato; ViewportCamera::basisMatrix ha chiamanti reali e va conservato. |
| `CadViewport::normalizedViewAngle` | `forgeCad2026_viewport_scene.cpp:83` | `forgeCad2026_viewport.h:844` | pubblica statica | Wrapper statico pubblico non usato; ViewportCamera::normalizedViewAngle ha chiamanti reali. |
| `CadViewport::extrusionNames` | `forgeCad2026_viewport_document.cpp:914` | `forgeCad2026_viewport.h:318` | pubblica | Accessor pubblico della lista nomi, senza chiamanti. |
| `StoryboardTree::firstFeature` | `forgeCad2026_storyboard.cpp:549` | `forgeCad2026_storyboard.h:94` | privata statica | Metodo statico privato non virtuale; nessuna occorrenza nei test o invocazione Qt. |
| `ForgeCad::DrawingSheetEditor::setModelProvider` | `cad_drawing_editor.cpp:3649` | `cad_drawing_editor.h:104` | pubblica | API callback per Aggiorna dal modello; la callback è consumata, ma setter non installato da alcun chiamante corrente. Non rimuovere automaticamente la funzionalità. |
| `ForgeCad::forgeIntersectRay` | `cad_forge.cpp:2407` | `cad_forge.h:228` | pubblica libera | Ponte Qt/kernel per ray picking; API dichiarata, nessun uso corrente. Le API kernel sottostanti sono distinte. |
| `ForgeCad::modelBodyFeatures` | `cad_model_history.cpp:75` | `cad_model_history.h:42` | pubblica libera | Utility lista feature di un corpo logico; dichiarazione e definizione soltanto. |
| `ForgeCad::usesSketch` | `cad_feature_dependencies.cpp:205` | `cad_feature_dependencies.h:21` | pubblica libera | Wrapper !sketchesOf(body).isEmpty(); sketchesOf è chiamata altrove, non eliminare la funzione sottostante. |
| `ForgeCad::commandIconNames` | `cad_icons.cpp:1634` | `cad_icons.h:15` | pubblica libera | API di enumerazione del registro icone richiamata da un commento del contratto; nessun chiamante runtime/test. |
| `ForgeCad::nearestBodyEdge` | `cad_sketch_refs.cpp:151` | `cad_sketch_refs.h:66` | pubblica libera | Utility geometrica documentata in header; nessun chiamante. Non promuovere il nearest a riassociazione genealogica. |
| `ForgeCad::appendFixedReferenceLine` | `cad_sketch_refs.cpp:576` | `cad_sketch_refs.h:57` | pubblica libera | Utility documentata per retta di riferimento fissa; nessun uso corrente. |
| `ForgeCad::Kernel::Corner::otherFace` | `kernel/fk_blend.cpp:369` | `kernel/fk_blend.cpp:369` | locale TU | Membro non virtuale di struct locale al namespace anonimo. Gli omonimi sono variabili in fk_boolean, non chiamanti. |
| `ForgeCad::Kernel::ChainEdge::inPlane` | `kernel/fk_blend_loop.cpp:107` | `kernel/fk_blend_loop.cpp:107` | locale TU | Membro non virtuale di classe senza base nel namespace anonimo; omonimi fuori classe sono lambda/variabili. |
| `ForgeCad::Kernel::BandMatrix::band` | `kernel/fk_fill.cpp:240` | `kernel/fk_fill.cpp:240` | locale TU | Getter non virtuale di classe senza base nel namespace anonimo; gli altri band sono variabili/campi/parametri. |

## Distinzione fra assenza di uso e API intenzionale

Le 17 API pubbliche hanno un contratto leggibile (getters, callback, helper geometrici o di ispezione), ma il controllo non dimostra che debbano restare per un consumatore futuro. Non sono API test-only: i nomi non compaiono come chiamate neppure nei test. Essere pubbliche in C++ non le rende automaticamente SDK: CMake installa l’eseguibile, non un SDK con questi header. Questo fatto non autorizza comunque una cancellazione automatica. La decisione di conservarle, documentarle o rimuoverle resta distinta dal finding di assenza di uso.

`CadViewport::normalizedViewAngle` è statica ma pubblica, quindi non è stata inclusa nelle cinque sicure. I wrapper `CadViewport::basisMatrix` e `normalizedViewAngle` non hanno chiamanti; gli omonimi di `ViewportCamera` sono invece usati in produzione e devono restare. Le occorrenze QSettings/icone di geometrySnap/gridSnap sono stringhe, non invocazioni dei getter.

## Cinque candidate concrete, senza modifica effettuata

La proposta successiva è eliminare soltanto le cinque dichiarazioni/definizioni sotto, conservando classi, dati e tutti i metodi chiamati. Interessa sette file; nessun CMake o formato persistente cambierebbe.
- `CadViewport::setSelectedPointPosition`: forgeCad2026_viewport_selection.cpp:746; forgeCad2026_viewport.h:1256. Metodo normale, accessibile al friend ViewportInteractionTest ma assente da tutti i test, anche fuori CMake.
- `StoryboardTree::firstFeature`: forgeCad2026_storyboard.cpp:549; forgeCad2026_storyboard.h:94. Metodo statico privato non virtuale; nessuna occorrenza nei test o invocazione Qt.
- `ForgeCad::Kernel::Corner::otherFace`: kernel/fk_blend.cpp:369; kernel/fk_blend.cpp:369. Membro non virtuale di struct locale al namespace anonimo. Gli omonimi sono variabili in fk_boolean, non chiamanti.
- `ForgeCad::Kernel::ChainEdge::inPlane`: kernel/fk_blend_loop.cpp:107; kernel/fk_blend_loop.cpp:107. Membro non virtuale di classe senza base nel namespace anonimo; omonimi fuori classe sono lambda/variabili.
- `ForgeCad::Kernel::BandMatrix::band`: kernel/fk_fill.cpp:240; kernel/fk_fill.cpp:240. Getter non virtuale di classe senza base nel namespace anonimo; gli altri band sono variabili/campi/parametri.

`CadViewport` dichiara `friend class ViewportInteractionTest` a riga 82: il friend è stato considerato, non ignorato. Tutti gli header e TU di test presenti sotto tests, kernel/tests e kernel/occt_tests, anche non elencati nei target CMake, sono stati cercati. Nessuno chiama setSelectedPointPosition o firstFeature. Le altre tre funzioni sono membri ordinari di classi senza base nel namespace anonimo del proprio TU; non sono virtuali e non hanno un header esterno. Non si trovano include diretti dei tre .cpp in test o strumenti. Gli omonimi band/inPlane/otherFace nei test e negli altri moduli sono variabili, campi, lambda o nomi diversi.

Prima di un’eventuale rimozione: confronto del delta ristretto, build completa e runner pertinenti già esistenti; rigenerazione della mappa poiché spariscono simboli e archi uscenti. Non sono stati scritti test che ripetano questi getter inutilizzati e non è stata eseguita alcuna rimozione.

## Metodo, prove e limiti

- Per ciascun simbolo: ricerca lessicale completa con rg e percorso esplicito `.`, poi lettura di definizione, dichiarazione e visibilità. Le ricerche non dipendono dalla sola compile_commands della configurazione corrente.
- Grafo: simbolo presente nel modulo previsto e zero archi entranti call/reference per tutti i 22; contesto di chiamate, include e riferimenti conservato separatamente in graph-module-context.json.
- Nessuno dei tre header GUI coinvolti contiene Q_OBJECT, slot, signal, Q_INVOKABLE o Q_PROPERTY che renda questi normali metodi callback del metaoggetto. Le sole invokeMethod locali rilevate chiamano una lambda o quit, senza questi nomi.
- Ricerca di puntatori a funzione e macro: gli indirizzi espliciti conterrebbero il nome e non sono presenti; non risultano token pasting o include diretti dei .cpp che espongano i cinque helper a un test nascosto nel tree esaminato.
- Nessuna build nuova, prova linker/garbage collection, certificazione Qt runtime o scansione esaustiva di tutte le funzioni è dichiarata. Questa è analisi statica delle 22 candidate.
- Non sono esaminati archivi locali ignorati, build generate, repository esterni, chiamanti futuri o caricamenti dinamici arbitrari. Non si pretende assenza globale di consumatori fuori dal repository.

Prove: table.json, 22 file *.rg.txt, graph-snapshot.json, graph-module-context.json, manifest sourcehash-primary.json e sourcehash-all-scanned.json, elenco dei file cercati, qt-friend-context.txt e private-five-test-occurrences.txt. Lo script refresh.py rigenera i dati senza modificare il repository.

Prove conservate nel repository: [inventario e riproduttore](review-preview-unification-prove-2026-11-16/orphans/README.md).
