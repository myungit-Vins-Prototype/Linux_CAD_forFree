# Review delle anteprime — 15 novembre 2026

**Aggiornamento successivo:** PRE1–3 corretti e verificati: attesa degli input per estrusione/loft e sorgenti mapping coerenti con il risultato. Le differenze di flusso fra famiglie restano distinte dai tre bug; uniformazione completa ancora proposta. [Implementazione, prove e limiti](../review-fix-next-2026-11-15.md). Il testo seguente conserva lo stato della review iniziale.

Stato: revisione statica e harness Qt mirato completati (exit 0), ripetuti dal coordinatore con lo stesso esito. Nessuna modifica ai sorgenti. Percorsi relativi a `/home/myungit/Linux_CAD_forFree`.

## Rilievi concreti

**PRE1 — P2: Estrusione conserva un risultato di un modo precedente quando manca il nuovo limite.** In `forgeCad2026_dialogs_solids.cpp:947`, passando da Fine=«A distanza» a una fine riferita a punto/curva/piano senza riferimento, `refresh()` esce prima di richiedere o cancellare l'anteprima. La forma precedente e il messaggio «Anteprima pronta» restano pur rappresentando parametri diversi; il pannello simultaneamente chiede di scegliere il riferimento. Anche un job precedente può completare durante questa attesa. Trigger: anteprima valida a distanza → Fine=fino a un piano → nessun piano scelto. Cancellare/invalidate la preview e mostrare attesa riferimento prima del return. Non implica promozione di forma sbagliata alla conferma: la chiave completa cambia e il riuso finale è protetto. Harness reale del dialogo: `ui_extent=3 preview_extent=0 preview_valid=1 old_key_unchanged=1 pending=0 label=pronta (in ambra nella vista)`.

**PRE2 — P2: Loft comunica un calcolo inesistente o un risultato pronto dopo averlo eliminato.** `forgeCad2026_dialogs_surfaces.cpp:619-620`: con meno di due sezioni si chiama soltanto `clearPreview()`. `PreviewScope` inizializza il testo a «Anteprima in calcolo...» e `clearPreview()` non invoca la callback; aprendo Loft con una sola sezione il testo resta in calcolo senza worker. Dopo un'anteprima valida, togliere una sezione conserva invece «pronta» con geometria cancellata. Mostrare lo stato «Seleziona almeno due sezioni» quando si cancella per input incompleti. Prova: `forgeCad2026_dialogs_common.cpp:1222-1244` e `forgeCad2026_viewport_preview.cpp:247-278`; harness reale del dialogo con una sezione: `preview_valid=0 pending=0 empty_key=1 label=in calcolo...`.

**PRE3 — P2: Mapping nasconde la sorgente anche quando sarà conservata nel risultato finale.** `forgeCad2026_viewport_preview.cpp:145`: ogni `SurfaceTools` eccetto network sostituisce visivamente `firstBody`, senza consultare `surfaceTool.mappingCopy` né tutte le sorgenti. `forgeCad2026_dialogs_mapping.cpp:176,210` offre «Conserva anche i corpi sorgenti», e unroll imposta sempre mappingCopy=true. `cad_feature_dependencies.cpp:251` nasconde sorgenti soltanto per wrap/mode4 con mappingCopy=false. Quindi l'anteprima di unroll e wrap con copia nasconde una sorgente che riappare dopo OK; wrap senza copia e più sorgenti nasconde solo la prima, lasciando sorgenti che spariranno dopo OK. Allineare le sorgenti sostituite al contratto `hiddenOperands` del mapping. Harness della policy di richiesta: `unroll mapping_copy=1 preview_replaces_source=1 final_hides_source=0`; non attende un risultato geometrico valido né misura pixel OpenGL. La conseguenza a preview valida è provata dal ramo `isBodyVisibleInScene` in `forgeCad2026_viewport_scene.cpp:13-20`. Distinguere dalla scelta intenzionale di colore e opacità.

## Matrice funzionale

Tutte le richieste comuni usano un primo avvio immediato e debounce 120 ms per aggiornamenti successivi, singolo worker con token di cancellazione e controllo generazione, cache delle ultime quattro anteprime entro 64 MiB di display. L'esito comune è «in calcolo / pronta / non riuscita», salvo gli stati specifici elencati. La cancellazione del pannello usa normalmente il distruttore PreviewScope (vedi problema UI2 dei loop annidati nel rapporto UI; non duplicato qui).

| Famiglia | Trigger e comando esplicito | Input incompleti / selezione | Conferma e riuso | Vista |
|---|---|---|---|---|
| Estrusione | Automatico; valori numerici applicati a editing concluso; maniglia aggiorna durante drag; nessuna checkbox Anteprima | Limite assente lascia risultato vecchio (PRE1) | Riuso B-rep se chiave coincide; ricerca automatica di unione/sottrazione viene ricalcolata | Ambra trasparente; aggiunta locale sopra base opaca; sottrazione mostra facce conservate più patch tagliente |
| Rivoluzione | Automatico; angolo applicato a editing concluso; nessuna checkbox | Richieste a ogni modifica significativa | Come estrusione | Come estrusione, patch locale |
| Sweep | Automatico; nessuna checkbox | Richiede anche input incompleti, con errori del builder | Riuso salvo merge automatico | Corpo risultante completo e opaco, anche nelle unioni/sottrazioni |
| Loft | Automatico; nessuna checkbox | Meno di due sezioni cancella la forma ma non il testo (PRE2) | Riuso B-rep se invariato | Ambra trasparente e curve di costruzione; sezioni di partenza conservate |
| Raccordo/smusso | Unica famiglia qui con «Anteprima automatica» (attiva inizialmente) e pulsante «Calcola»; disattivata: modifica cancella e chiede Calcola | Selezione/raggio invalidi cancellano e spiegano | Riuso della geometria completa; patch viene tessellata/promossa alla conferma | Patch ambra trasparente con X-ray e base opaca |
| Shell/spessore | Automatico; nessuna checkbox | Selezione in corso o input insufficienti cancellano | Riuso B-rep se invariato | Risultato completo opaco; offset semplice conserva sorgente |
| Sformo | Automatico; nessuna checkbox | Scelta facce/piano neutro cancella e mostra attesa | Riuso B-rep se invariato | Risultato completo opaco |
| Booleane | Automatico dopo chiusura scelta utensili; nessuna checkbox | Scelta in corso o senza utensili cancella con stato | Riuso B-rep se invariato | Risultato completo opaco; operandi sostituiti |
| Rigata, delimitata, planare, cucitura | Automatico; nessuna checkbox | Input insufficienti cancellano | Riuso B-rep se invariato | Risultato opaco; curve sorgenti conservate per superfici da curve |
| Riempimento superficie | Automatico; nessuna checkbox | Contorno insufficiente cancella; callback aggiunge avvisi del builder | Riuso B-rep se invariato | Opaco; riferimenti di contorno conservati |
| Superfici: estensione/rete/poli | Timer locale 60 ms oltre al debounce comune; rifit/ricerca 180 ms; drag poli calcolato al rilascio | Stati espliciti impediscono richiesta e OK; durante drag cancella | Riuso B-rep se invariato | Opaco; rete conserva sorgenti, altri modi sostituiscono base |
| Mapping unroll/wrap | Automatico; nessuna checkbox; modifiche sospese mentre si scelgono riferimenti | Durante scelta la preview precedente resta, senza stato di attesa nel messaggio preview | Riuso B-rep se invariato | Opaco; gestione sorgenti difforme dal risultato finale (PRE3) |
| Proiezione | Automatico; nessuna checkbox | Richiede anche input incompleti; errori e avvisi specifici del builder | Curve non promosse dal percorso takePreviewResult, vengono ricostruite | Curve/risultato, senza trasparenza della superficie |
| Pattern lineare/circolare/speculare | Automatico nel passo Parametri; nessuna checkbox | Scelta sorgente/direzione cancella e nasconde il messaggio preview; diverso dal riempimento | Pattern ricostruito alla conferma | Opaco, anche con istanze; originali secondo modalità |
| Pattern di riempimento | Automatico; nessuna checkbox; modifiche sospese durante scelta confine/componente | Mantiene la preview precedente durante scelta, come mapping | Pattern viene ricostruito alla conferma (escluso esplicitamente dal riuso) | Risultato opaco; può usare display istanziato |

## Differenze di comportamento e limiti

Il costo può giustificare il comando manuale del raccordo, il rilascio prima del calcolo dei poli e il debounce; non giustifica messaggi che descrivono risultati vecchi o un calcolo assente. La differenza patch locale estrusione/rivoluzione rispetto a corpo completo sweep è dichiarata nel sorgente (`forgeCad2026_viewport_preview.cpp:148-162`), quindi non viene presentata come errore geometrico. La scelta corrente di trasparenza comprende soltanto Loft/Blend/Extrusion/Revolution (`forgeCad2026_viewport_render.cpp:1474-1502`); il messaggio generico «in ambra» non spiega questa differenza. Una checkbox Anteprima generale non esiste nelle altre famiglie; unificarla è una scelta di prodotto, non un bug dimostrato.

Anche l’editing invariato può apparire diverso dalla stessa feature appena modificata: `sceneResult` riusa il corpo esistente e disabilita trasparenza/X-ray (`forgeCad2026_viewport_render.cpp:1485-1487`). Non è un errore geometrico, ma contribuisce alla percezione che ogni anteprima segua regole diverse.

Il riuso alla conferma non è uniforme per ragioni verificabili: `takePreviewResult()` rifiuta mergeProbe e trasferisce solo geometria B-rep (`forgeCad2026_viewport_preview.cpp:290-339`); Pattern è escluso nei percorsi create/update (`forgeCad2026_viewport_document.cpp:1086-1090,1506-1517`). Quindi una preview pronta non garantisce conferma immediata; questa differenza merita un indicatore coerente quando il ricalcolo dura.

Copertura: lettura dei dialoghi e del ciclo request/cache/cancel/worker/promozione/render; non verifica pixel OpenGL, performance su grandi modelli, tutte le combinazioni di selezione o interruzioni del kernel. Il problema UI2 sui pannelli modeless ritarda anche la distruzione di PreviewScope ed è già nel rapporto UI. Nessuna nuova prova sanitizer né modifica ai sorgenti.


## Riproduzione eseguita

`repro.cpp` usa i veri dialoghi estrusione/Loft, il vero worker per l’estrusione e ispeziona stato/visibilità tramite la classe friend di test già prevista dal viewport. QSettings è isolato in una directory temporanea. `build.py` riprende la compile command di `tests/test_viewport.cpp`, sostituisce soltanto il main e collega gli oggetti esistenti di `forgecad_view_tests`; nessuna scrittura nel repository. Gli oggetti solids/surfaces/viewport_preview hanno timestamp successivi ai sorgenti.

```sh
python3 /tmp/forgecad-review-20261115/preview/build.py
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic QT_STYLE_OVERRIDE=Fusion timeout 30s /tmp/forgecad-review-20261115/preview/repro
```

Output acquisito in `repro.log` (exit 0):

```text
PRE1 ui_extent=3 preview_extent=0 preview_valid=1 old_key_unchanged=1 pending=0 label=pronta (in ambra nella vista)
PRE2 one_section preview_valid=0 pending=0 empty_key=1 label=in calcolo...
PRE3 unroll mapping_copy=1 preview_replaces_source=1 final_hides_source=0
```

L’avvio preliminare con il solo QT_QPA_PLATFORM falliva prima del test per il tema GTK senza display; l’esecuzione sopra con tema generic e Fusion è quella valida. Nessuna sessione grafica o processo dell’utente è stato modificato.

## Copie conservate

I file originali del probe sono conservati in `prove/preview-repro.cpp.txt`, `prove/preview-build.py.txt` e `prove/preview-repro.log`. Per ripetere il comando precedente, ripristinare le estensioni e i percorsi temporanei indicati, oppure adattare le variabili `root` e `tmp` nello script alla propria build.
