# Seguito review: orfane, callback e lifetime — 15 novembre 2026

## Esito

Nessun nuovo difetto funzionale confermato in questo campione. Nessuna modifica ai sorgenti. Le 22 candidate restano inventario P3, non 22 bug; M1/M2 non riesaminati come nuovi difetti.

## Classificazione delle 22 candidate

- **3 metodi di classi locali al .cpp** eliminabili senza cambio di API: `Corner::otherFace` (`kernel/fk_blend.cpp:369`), `ChainEdge::inPlane` (`kernel/fk_blend_loop.cpp:107`), `BandMatrix::band` (`kernel/fk_fill.cpp:239`). Sono normali metodi non virtuali, senza usi neppure nei test o nel grafo; gli omonimi esterni sono distinti.
- **12 metodi GUI interni** eliminabili insieme alle rispettive dichiarazioni, senza effetto osservabile nei percorsi del repository: 11 `CadViewport` e `StoryboardTree::firstFeature`. Elenco e righe in `orphans-current.json`. Non sono slot/Q_INVOKABLE/override, non esiste invocazione lessicale o riferimento di funzione. In particolare, i wrapper `basisMatrix` e `normalizedViewAngle` non autorizzano a cancellare gli omonimi di `ViewportCamera`, che sono usati.
- **6 funzioni esposte da header di namespace**: `forgeIntersectRay`, `modelBodyFeatures`, `usesSketch`, `commandIconNames`, `nearestBodyEdge`, `appendFixedReferenceLine`. Zero chiamanti nel repository, incluse le suite: rimozione tecnicamente compatibile con i consumatori attuali, ma questa review non dimostra l'assenza di consumatori esterni né documenta un intento pubblico. Scegliere esplicitamente tra deprecazione/rimozione e conservazione come API; non inventare un intento storico.
- **1 API con piccola catena obsoleta**: `DrawingSheetEditor::setModelProvider` (`cad_drawing_editor.cpp:3649`). Il membro `Impl::provider` (`:495`) non viene mai valorizzato nel repository. L'azione interna (`:2947`) è inizialmente disabilitata (`:2949`) e il contenitore la nasconde deliberatamente (`cad_drawing_window.cpp:148`); l'azione attiva del contenitore verifica percorso, identità e impronta. Non è quindi un comando utente accidentalmente rotto. Pulizia possibile di setter/membro/callback interno, tenendo conto dei riferimenti a `updateAction` nel setter read-only e della ricerca dell'azione per nome nel contenitore: cancellare solo il setter lascerebbe il resto morto; cancellare direttamente l'azione senza aggiornare il contenitore dereferenzierebbe un puntatore nullo.

### Altra piccola catena morta accertata

`setExtendPickCallback` (`forgeCad2026_viewport_selection.cpp:230`) è l'unico writer di `extendPickFinished_` (`forgeCad2026_viewport.h:1763`), ed è orfano. Il ramo legacy di `finishEdgePick` (`forgeCad2026_viewport_selection.cpp:1711–1712`) non può dunque consegnare il callback nel repository attuale. Non è una regressione funzionale: il pannello Estendi attuale usa `setEdgePickChangedCallback` e `setEdgePickPanelKeyCallbacks`, e pulisce entrambi (`forgeCad2026_dialogs_surfaces.cpp:2231–2257`). Eliminare soltanto l'orphan lascerebbe membro/ramo logicamente morti; non eliminare `beginExtendPick`, `edgePickExtend_`, `edgePickLinear_` o `requestExtendPreview`, che restano attivi nel pannello.

## Lifetime e callback verificati staticamente

- Anteprime: `startPreviewJob` (`forgeCad2026_viewport_preview.cpp:385–531`) copia ingressi e token; `computePreview` è statico. Il `this` catturato viene usato solo nella consegna GUI, protetta da `deliverResult` (`forgeCad2026_viewport_internal.h:142–148`) e QPointer. `clearPreview` (`forgeCad2026_viewport_preview.cpp:251–283`) ferma il timer, segnala cancellazione, incrementa generazione, svuota cache/definizioni/display. Lo stato running viene rilasciato dal completamento, evitando un secondo worker contemporaneo.
- Evidenziazioni: `setFeatureHighlights` (`forgeCad2026_viewport_selection.cpp:1917–2003`) conserva corpo/display/catena nei worker, e scarta generazioni superate sul thread GUI. Non è un leak permanente dimostrato. Non c'è cancellazione cooperativa del lavoro di evidenziazione: possibile ottimizzazione, senza impatto utente misurato qui.
- Cache picking: `warmPickCaches` (`forgeCad2026_viewport_selection.cpp:510–562`) mette in coda weak_ptr, con token cancellato su reset/distruzione; una coda in attesa non possiede l'intero documento.
- Caricamento file per preview: `DocumentFilePreview::startLoading` (`forgeCad2026_dialogs_common.cpp:160–197`) usa QPointer e generazione e consegna tramite applicazione; lo stato locale viene liberato anche se il widget è morto o il risultato è vecchio.
- Anteprima tavole: `PreviewTask` locale (`cad_drawing_editor.cpp:2103–2179`) possiede snapshot, cancellation e flag atomici. Il worker non cattura widget né stack del pannello. Alla chiusura timer fermi e cancellazione segnalata; il task rimane vivo soltanto fino all'uscita del worker.
- Refit poli: `SurfacePolesRefitSearch` (`cad_surface_poles_session.cpp:164–200`, distruttore `cad_surface_poles_session.h:51`) cancella, conserva sorgenti/token per valore e verifica QPointer/token/generazione sul thread GUI.
- Genealogia: registro con weak_ptr al risultato e operandi forti transitori; `compute` svuota gli operandi (`cad_element_names.cpp:179`) e le registrazioni successive eliminano i risultati scaduti (`:352`, `:429`). Nessuna prova di perdita permanente da attribuire a questo registro in questa sottoreview.
- Scope callback dei pannelli: `PreviewScope::~PreviewScope` (`forgeCad2026_dialogs_common.cpp:1268–1272`), pulizie Estendi/Raccordo/SurfaceTools, `BodyPicker` e `SurfaceEndControls` letti insieme ai setter; nessun callback rimasto sul viewport dopo i percorsi normali esaminati.

## Prove ripetibili

Eseguire `python3 /tmp/forgecad-review-followup-20261115/lifetime/inventory_orphans.py` e `python3 /tmp/forgecad-review-followup-20261115/lifetime/repeat_tests.py` dalla macchina del repository.

Il primo ricontrolla dichiarazioni/definizioni e zero call/reference entranti nel grafo; conserva tutte le occorrenze lessicali, inclusi omonimi, in `orphans-grep.txt`. `orphans-current.json` contiene la classificazione aggiornata.

Il secondo esegue gli eseguibili esistenti con `QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion`:

1. `forgecad_view_tests --async-lifetime`: worker preview/highlight in coda con viewport vivo/distrutto; cancel prima del dispatch; progresso chiuso, stato job rilasciato e successivo riavvio.
2. `forgecad_view_tests --document-lifetime`: weak_ptr a corpo/superficie precedenti scaduti dopo load; drag reale, pannelli, Undo/Redo, secondo viewport invariato, weak_ptr e cancellazione della coda picking.
3. `forgecad_drawing_view_edit_tests`: include chiusura durante calcolo, richieste obsolete, cambi sorgente/accesso, riuso geometria pronta e annullamento senza modifica documento/Undo.
4. `forgecad_display_cache_memory_tests`: rilascio storage al primo frame sostitutivo, riuso per 50 frame e cache vuota senza risorse.

Esiti e comandi in `test-results.json`, output nei quattro `.log`. Nessuna nuova build o modifica ai CAD utente. Primo tentativo senza svuotare QT_QPA_PLATFORMTHEME fallito prima dei test per GTK senza display: non attribuito al prodotto.

## Limiti

Analisi mirata di codice e regressioni esistenti, senza simulazione UI nativa OpenGL, ASan/LSan/TSan nuovi né prova sui backend opzionali CUDA/Metal. Nessuna rimozione sperimentale delle 22 funzioni e nessun nuovo relink con GC; la rimozione è una conclusione sul grafo/usi attuali, non una compilazione di una patch. Nessuna pretesa di analisi esaustiva di ogni funzione o callback del progetto.


## Approfondimento aggiuntivo: salvataggi, revisioni e clone fogli

Richiesto dal coordinatore dopo la sottoreview. **Nessun nuovo rilievo confermato**; non duplicati S1/S2 o difetti lock già corretti.

### Flussi riletti

- `saveDocumentFile` (`cad_document_io.cpp:1618–1644`): codifica prima della pubblicazione, `QSaveFile` con controllo short write, verifica ownership prima e dopo workspace e commit finale. Le uscite intermedie lasciano il temporaneo a QSaveFile senza pubblicare byte parziali. Non dimostrata una nuova gara o una perdita del file precedente.
- `saveDrawingFile` (`cad_drawing_file.cpp:112–123`): stesso schema di preparazione/validazione, file temporaneo e verifica lock prima del commit. `DrawingWindow::savePath` (`cad_drawing_window.cpp:307–333`) lavora su copia `prepared`, acquisisce un eventuale lock candidato e adotta percorso/stato solo dopo il salvataggio riuscito; Salva con nome e revisioni archiviate hanno controlli distinti.
- `RevisionStore::checkin` (`cad_revision_store.cpp:437–488`): ricevuta idempotente tramite UUID e fingerprint della richiesta; controllo della prenotazione e dipendenze esatte; oggetto content-addressed scritto atomicamente prima dell'aggiornamento del catalogo. La mancata pubblicazione del catalogo può lasciare un oggetto non referenziato, ma non un catalogo che punta a un oggetto scritto parzialmente. Nessuna nuova prova di leak permanente né nuova simulazione crash tra i due commit.
- `prepareNativeCheckin` / `publishNativeCheckin` (`cad_native_revisions.cpp:115–148`): snapshot preparato con UUID fissato; confronto della definizione prima della prima pubblicazione; riuso della ricevuta senza toccare un checkout successivo. Letto il supporto alla migrazione dei payload pendenti precedenti, conservando i byte originali.
- `cad_drawing_revisions.cpp:36–55`: completamento pendente conserva operazione e ricevuta fino al salvataggio locale riuscito, e distingue corrispondenza della copia corrente dal payload archiviato. Le cancellazioni/riprese usano sessione persistente e operazioni idempotenti.
- `cloneDrawingSheet` (`cad_drawing_file.cpp:60–77`): rinnova viste, quote, assi, annotazioni, centri e riserve di numerazione; remappa soltanto chiavi identitarie (`id`, `viewId`, `parent`, `axisId`, `datums`, `firstDimension`). Confrontati tali nomi col codec corrente in `cad_drawing.cpp` e `cad_drawing_sheet.cpp`; testo libero e versioni di modello/tavola restano intatti. Gli ID degli elementi del cartiglio hanno ambito locale al foglio e non sono parte delle identità globali controllate tra fogli. Nessun nuovo collegamento non rimappato individuato.

### Verifiche aggiuntive realmente eseguite

**5/5 exit 0**, tutti con `QT_QPA_PLATFORM=offscreen`, tema piattaforma vuoto e `QT_STYLE_OVERRIDE=Fusion`. Per ogni test `XDG_CONFIG_HOME`, `XDG_DATA_HOME`, `XDG_CACHE_HOME`, `XDG_RUNTIME_DIR` puntano a directory dedicate, mode0700, dentro `lifetime/isolated-revisions/<nome>/`.

1. `forgecad_revision_store_tests`: copie storiche/deduplicazione/ritentativi, revoca/copie obsolete, errori atomici/contenuti preparati/ripristino, dipendenze esatte, esclusione processi, 200 letture catalogo concorrenti, integrità e archivio spostato.
2. `forgecad_view_tests --native-revisions`: passaggio documenti nativi e conservazione dei payload pendenti storici.
3. `forgecad_view_tests --workspace-revisions`: copie di lavoro, sessioni/ripresa e GUI del workspace.
4. `forgecad_view_tests --drawing-revisions`: revisioni tavola, recupero e GUI storica.
5. `forgecad_view_tests --drawing-multisheet`: clone rinnova UUID, madre/derivata/asse/datum; riserve di numerazione rimappate e testo UUID libero conservato; isolamento tra fogli, Undo globale, codec atomico, PDF3/1, modello10→16, revisioniA/B e Salva con nome. Include migrazione cache e preparazione cancellabile atomica.

Ripetizione: `python3 /tmp/forgecad-review-followup-20261115/lifetime/repeat_revision_tests.py`. Comandi, ambienti effettivi, tempi ed exit in `revision-test-results.json`; cinque log omonimi. Il test multifoglio ha un percorso QA storico hardcoded `/tmp/forgecad-multisheet-native-qa` per PNG/PDF, esterno alle directory XDG isolate ma comunque in `/tmp`; nessuna scrittura ai CAD utente.

Limiti: regressioni esistenti eseguite con binari già presenti, senza ricompilare; nessuna nuova fault injection di disco pieno, power loss, rename/commit o rete. Le prove osservano errori e riprese previste dalle suite, non certificano la durabilità fisica su ogni filesystem. Nessuna modifica nel repository.
