# Prosecuzione della review — 15 novembre 2026

**Aggiornamento successivo:** K7/K8/K9/PRE4/UI7 sono stati corretti e verificati nella [passata di correzione](review-followup-fix-2026-11-15.md). La [prosecuzione K10](curve-fit-k10-2026-11-15.md) corregge il percorso offset NURBS di estrusioni/rivoluzioni; restano da migrare i chiamanti generici del fit. Lo stato, le righe e le riproduzioni sotto fotografano la review precedente alle correzioni.

## Stato

Nuovo approfondimento del codice corrente con tre agenti **GPT-6.1 Sol**, dopo la chiusura di K2. **Cinque nuovi rilievi aperti: due P1 geometrici, due P2 di interfaccia/anteprima e un P3 di cancellazione latente.** I 19 difetti della prima review restano corretti; questi rilievi hanno riproduzioni e percorsi distinti. Le 22 funzioni orfane restano candidate manutentive, non altri 22 bug.

Questo intervento è una review: nessuna modifica al software, nessuna nuova build dell'applicazione e nessuna modifica ai CAD originali. I probe sono collegati alla libreria e agli oggetti Release correnti. I nuovi file persistenti sono rapporti, prove e memoria del progetto.

## Rilievi

| ID | Priorità | Trigger e conseguenza | Posizione primaria corrente |
| --- | --- | --- | --- |
| K7 | P1 | Offset di un'estrusione con una piccola variazione locale della curva base: la scorciatoia considera l'offset una traslazione esatta e restituisce una superficie fuori tolleranza, senza errore. | `kernel/fk_offset.cpp:624–638` |
| K8 | P1 | Toro completo R=5, r=1, offset positivo 4 o 5: viene accettata una superficie singolare o ripiegata; anche `offsetFaces` restituisce un corpo senza segnalazioni del checker. | `kernel/fk_offset.cpp:1263–1265` |
| PRE4 | P2 | Riempimento pattern nuovo, anteprima pronta, poi «Dalla vista»: la sorgente resta nascosta dall'anteprima ma selezionabile dal picker. La geometria mostrata e quella interrogata non coincidono. | `forgeCad2026_viewport_scene.cpp:9–11` |
| UI7 | P2 | Mapping, scelta superfici destinazione attiva, poi passaggio da un piano a «Mappa di …»: il pulsante «Termina selezione» viene disabilitato ma la selezione resta attiva; OK rifiuta il comando. | `forgeCad2026_dialogs_mapping.cpp:260–261` |
| K9 | P3 | `fitCurve` con token cancellato durante un callback privo di checkpoint: il raffinamento continua e restituisce successo. Esiste un chiamante prodotto di questo tipo nel prolungamento dei raccordi; non è stata misurata una pausa reale della GUI. | `kernel/fk_offset.cpp:1202–1208` |

### K7 — Traslazione riconosciuta solo su nove campioni

`exactOffsetCurve` controlla nove parametri uniformi e, se il vettore differenza coincide, trasforma tutta la curva. Questo criterio viene applicato anche a B-spline libere. Un'estrusione con curva di grado 4 e un piccolo rigonfiamento fra i nodi 0,1 e 0,15 passa il controllo: ai campioni la normale coincide con quella delle parti rettilinee, mentre all'interno della campata cambia. Anche il controllo finale di `offsetSurfaceImpl` usa campioni uniformi e non intercetta il tratto.

Riproduzione regolare e C1, non un offset oltre il limite di curvatura: x(u)=u e, nella piccola campata, z(u)=0,06·s²·(1−s)² con s=(u−0,1)/0,05. Il raggio di curvatura è almeno 1/48≈0,020833; la distanza richiesta è 0,001. A u=0,1125 la pendenza è 0,225 e lo scarto esatto dalla superficie richiesta è **0,00022086305215**, cioè **2208,63 volte** la tolleranza 1e−7. Il probe indipendente del coordinatore coincide con la formula analitica `d*sqrt(2-2/sqrt(1+slope*slope))`. Scalando la geometria di 1000, con distanza 1 mm, lo scarto confermato è **0,220863 mm** su una lunghezza di 1000 mm; il raggio minimo resta maggiore di 20 mm.

La funzione è condivisa da offset di estrusioni, rivoluzioni e curve di bordo. Il difetto dimostrato riguarda l'estrusione; non si estende la certificazione a ogni chiamante. Per correggerlo, la scorciatoia deve avere una giustificazione geometrica sul dominio continuo, altrimenti occorre il fit con controllo delle campate. Aumentare soltanto il numero fisso dei campioni lascerebbe lo stesso problema.

### K8 — Mancato controllo della singolarità interna del toro

Il ramo toroidale verifica solo `r+d > tolerance`. Per un toro completo, a r+d=R il cerchio interno collassa; oltre R il supporto attraversa l'asse e si ripiega. Il controllo successivo verifica la posizione `S+dN`, non la regolarità o l'inversione della normale, quindi non rifiuta questi risultati.

Verifica indipendente su `offsetFaces`, sia con `preserveSeams=false` sia con **`preserveSeams=true`**, modalità usata dal comando interattivo:

| Distanza | Risultato | Norma del Jacobiano a v=π | Errori `checkBody` |
| --- | --- | --- | --- |
| 3 | Accettato, regolare | 4 | 0 |
| 4 | Accettato, singolare | 0 | 0 |
| 5 | Accettato, ripiegato | 6, con orientazione invertita nel lato interno | 0 |

La correzione deve verificare degenerazioni e cambio di ramo nel dominio interessato. Va distinto il toro completo del riproduttore da una faccia toroidale rifilata che potrebbe escludere la zona critica. Non basta affidarsi al checker topologico.

### PRE4 — Geometria invisibile selezionabile durante il picker

Il pattern di riempimento conserva l'anteprima mentre si sceglie la sorgente o il contorno. Per una nuova funzione `refPickOwner_ == -1`: il ripristino della scena d'ingresso in `isBodyVisibleInScene` è condizionato a `refPickOwner_ >= 0` e non interviene. Il picker interroga comunque il corpo originale tramite `referenceBodyEligible`.

Harness con pannello reale, worker reale e sorgente fuori dal contorno di riempimento: `preview.valid=1`, `source_visible=0`, `source_eligible=1`, `ray_picked_body=0`. Il raggio nella posizione della sorgente invisibile restituisce proprio il corpo originale. È un problema funzionale aggiuntivo alla differenza di flusso già documentata; PRE3 riguardava invece quali operandi Mapping nascondeva rispetto al risultato finale.

La scelta dei riferimenti deve mostrare la stessa scena che il picker interroga, anche durante la creazione di una feature. Verificare sia ingresso/uscita dal picker sia creazione/modifica, sorgenti consumate e contorni, senza reintrodurre i problemi di selezione sugli stadi successivi della storia.

### UI7 — Modalità Mapping cambia senza terminare la scelta destinazione

Durante `choosing==2`, selezionare «Mappa di …» imposta le destinazioni dalla funzione di appiattimento e disabilita i relativi controlli. Non cancella però il picker e non azzera `choosing`. L'harness crea prima una funzione reale di appiattimento, apre Mapping, avvia la scelta destinazione e cambia piano: `pick=1`, pulsante disabilitato con testo «Termina selezione». Premendo OK, `apply` non viene chiamato e appare «Termina la scelta dei riferimenti».

**Esc nella vista recupera lo stato**, quindi non è un blocco irreversibile. È un ramo distinto da UI4, che riguardava G1/G0 nei controlli di estremità delle superfici. Prima di disabilitare i controlli o sostituire le destinazioni occorre terminare la scelta non più applicabile.

### K9 — Raffinamento senza checkpoint proprio

Il probe attiva il token alla ventesima valutazione di `f`; `fitCurve` completa ugualmente con **15807 valutazioni totali e 5269 poli**. Il budget di 32768 nodi impedisce una crescita illimitata: non è dimostrato un ciclo infinito.

Molti chiamanti passano attraverso `point`/`normal`, che controllano già il token: non è corretto descrivere tutti gli offset come non cancellabili. In `kernel/fk_blend_surface.cpp:1631–1655`, invece, il callback di continuazione usa direttamente `evaluate` e Newton, poi viene passato a `fitCurve`. La preview segnala la cancellazione alla chiusura/nuova richiesta, ma avvia il lavoro successivo soltanto quando quello corrente esce.

Mancanza confermata a livello della funzione e chiamante prodotto verificato staticamente; **nessuna durata percepita o riproduzione end-to-end di un raccordo lento certificata**. Priorità P3 per questa limitazione della prova. Aggiungere checkpoint all'ingresso e durante il raffinamento protegge anche i callback che non ne hanno.

## Orfane e durata delle risorse

Le 22 candidate precedenti sono state ricontrollate contro sorgenti, test e archi call/reference:

- 3 metodi di classi locali e 12 metodi GUI interni non hanno consumatori nel repository; la rimozione resta proposta, non compilata.
- 6 funzioni esposte da header di namespace richiedono una decisione sul contratto esterno; l'assenza di chiamanti locali non dimostra l'assenza di consumatori esterni.
- `DrawingSheetEditor::setModelProvider` appartiene a un percorso sostituito intenzionalmente dall'azione del contenitore. Non è un comando utente rotto.

Due piccole catene morte sono ora esplicitate: callback legacy `extendPickFinished_` e provider interno delle tavole. Eliminarle richiede pulizia coordinata: il contenitore cerca ancora l'azione interna per nome. Nessuna cancellazione automatica eseguita.

Nessun nuovo leak o use-after-free confermato nei percorsi esaminati. I worker conservano snapshot e proteggono la consegna con generazione/QPointer; i test verificano cancellazione, distruzione dei viewport e rilascio delle cache. Dettagli e limiti in [lifetime e persistenza](review-followup-prove-2026-11-15/lifetime/report.md).

## Verifiche e limiti

**13/13 test esistenti distinti PASS** sulla build corrente:

- 4 kernel: `OffsetAnalyticSurfaces`, `OffsetFreeformSurfaces`, `OffsetFitCurveSmallSmoothFeature`, `OffsetInwardBeyondCurvatureIsExplained`.
- 4 lifetime/cache: `--async-lifetime`, `--document-lifetime`, `forgecad_drawing_view_edit_tests`, `forgecad_display_cache_memory_tests`.
- 5 persistenza: `forgecad_revision_store_tests`, `--native-revisions`, `--workspace-revisions`, `--drawing-revisions`, `--drawing-multisheet`.

I test esistenti passano, ma non coprono i nuovi casi dimostrati dai probe. Riletti anche salvataggio atomico `.prt/.drw`, adozione del documento dopo successo, revisioni pendenti/idempotenti e rimappatura degli ID nella copia dei fogli: nessun nuovo difetto confermato nel campione, senza nuova simulazione di crash o guasti del filesystem.

Prove nuove: kernel geometrico/cancellazione e harness Qt sui due flussi difettosi; controverifica geometrica indipendente del coordinatore. Qt offscreen/Fusion verifica oggetti, stati ed eventi, **non i pixel del renderer OpenGL**. Impostazioni del nuovo harness e revisioni isolate in `/tmp`; le prove usano geometrie sintetiche e file temporanei. Nessun nuovo ASan/LSan/TSan, nessuna certificazione dei backend opzionali o dell'intera suite.

Mappa architetturale consultata prima dell'approfondimento; `--check` PASS, **736 file / 10570 relazioni**, nessun cambiamento strutturale. Controllo SHA256 dei file iniziali conservato nelle prove, per distinguere i soli aggiornamenti documentali dalle modifiche software preesistenti.

[Riproduttori, rapporti e istruzioni](review-followup-prove-2026-11-15/README.md). La review resta mirata: non dimostra l'assenza di difetti nelle restanti combinazioni di blend/offset/booleane, pannelli e backend.
