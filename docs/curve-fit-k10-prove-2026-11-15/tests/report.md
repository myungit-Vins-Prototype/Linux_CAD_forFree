# K10 — proposta regressioni e inventario chiamanti

Stato: sola indagine/proposta. Nessuna produzione, CMake, doc condivisa o buildglobale modificata. Letti AGENTS/CLAUDE, rapporto followup e prove independent; grafo consultato con descrizioni e siti call distinti da include. `callers-graph.json`: 24 callsite produzione, test esclusi.

## Ripetizione e baseline

`python3 /tmp/forgecad-k10-20261115/tests/build.py`
`timeout 60s /tmp/forgecad-k10-20261115/tests/probe > /tmp/forgecad-k10-20261115/tests/baseline.log 2>&1`

Il build recupera il comando test_offset e collega archivio Release esistente; solo oggetto/binario privati. Non prova un eseguibile app aggiornato. Probe usa 28 poli originali, non rigenera coefficienti approssimati. Campionamento 20001 punti è una controprova della falsa accettazione, NON un certificato di correttezza del risultato.

24 casi geometria: fit diretto e offset ExtrusionSurface della spline27; scale .001/1/1000 × identità/rotazione .71rad asse(1,2,3)+traslazione(11,-7,3) scalata × polinomiale/razionale pesi1+.1sin(.4i).
- 12 casi polinomiali accettati fuori tolleranza: scarto circa574.25×tol sia fit che offset, persiste sotto pose/scala. Nessuna ipotesi su fit perfettamente covariante richiesta: solo bound geometrico nel sistema globale e relativo budget appropriato.
- 12 casi razionali accettati entro tol: fit circa .9612tol; offset circa .9463tol. Sono validi pertinenti, non riproduttori negativi.
- callback polinomiale avversa:4poli/12callback, deviazione dichiarata1.195e-11, misurata .000445999>4459tol.
- tol0/negativa/NaN: rifiuto dopo circa147kcallback; tolInf accettata. Proposta nuovo contratto: tolleranza strettamente positiva e finita, rifiuto prima callback.
- callback NaN/Inf: throw dal costruttore della spline, ma deviation -123 diventa0 PRIMA throw. Proposta atomicità su OGNI fallimento: pubblicare output dopo costruzione e controlli.
- cancellazione iniziale/primo/ultimo(12°)callback: OperationCancelled, deviation -123 conservata, rispettivamente0/1/12 callback.
- discontinuo e transizione stretta con breaks: rifiuto esplicito con sentinella conservata. Quest'ultimo non è un test positivo: il derivative helper vicino al break attraversa il cambio di regime; serve jet unilaterale/oracolo geometrico per il futuro test positivo.

## 24 chiamanti: quali bounds sono realmente disponibili

“Disponibili” significa oggetti matematici sorgente accessibili localmente; NON significa che curveBox campionato sia automaticamente una prova, né che tutti i CurveType abbiano già un evaluatore intervallare. Per razionali richiedere peso positivo con bound e divisione outward. Per normalizzazioni richiedere bound denominatore lontano da0. Un solve numerico senza certificazione del ramo resta generico anche quando le superfici originali sono note.

| File:riga | Classe | Informazione e lavoro necessario |
|---|---|---|
| cad_sketch_offset.cpp:401 | G | OffsetPiece.f conserva curva2D e offset normale; recuperare struttura da offsetOf invece di type erasure std::function; bound derivata/norma |
| cad_sketch_offset.cpp:602 | G | medesimo offset di curva2D; identica provenienza, trim dopo fit |
| cad_sketch_refs.cpp:218 | G | curva procedurale esplicita, spesso IsoCurve; supporto e parametro disponibili via type; fallback Other richiede provider o rifiuto |
| fk_blend_mitre.cpp:129 | G | media di due superfici valutate su segmenti UV affini; bounds tensorBézier/composizione |
| fk_blend_surface.cpp:1655 | S | continuazione intersezione due superfici con Newton4D/30iter; serve tubo di ramo+Jacobian invertibile certificato, non solo bounds supporti |
| fk_boundary.cpp:216 | G | concatenazione curve note con affine piecewise, cuts noti; Piece.parameter è affine, lunghezze sono costanti di normalizzazione |
| fk_exchange.cpp:740 | S | parameter(t) proiezione target→source, scelta fra candidati nearestbranch; inversione parametrica non affine da certificare |
| fk_fill.cpp:1117 | G | proiezione affine curva nota su piano; può spesso trasformare direttamente poli e pesi senza fit |
| fk_offset.cpp:679 | S/G | wrapper perde provenienza offset.f: può essere offset geometricamente noto oppure solve giunzioni/correzioni; separare rami |
| fk_offset.cpp:1311 | G | sezione di ExtrusionSurface+normalizzazione normale, curva sorgente e direzione note; caso K10 primario |
| fk_offset.cpp:1330 | G | meridiano RevolutionSurface+normale; curva/asse note, escludere singolarità asse |
| fk_offset.cpp:1855 | S | projectLocal del bordo su supporto offset, seed pcurve; Newton/inversione UV |
| fk_offset.cpp:2348 | S | projectLocal con seamJet/seed interpolato, medesimo problema implicito |
| fk_project.cpp:449 | S | proiezione su facce con pointOn/runs e possibili cambi faccia; richiede ramo intersezione/proiezione certificato |
| fk_quilt_mapping.cpp:411 | S | flatPoint: metrica quilt/inversione chart; derivazione bounds provider interno non immediata |
| fk_quilt_mapping.cpp:592 | S | image.parameters→contact quilt, selezione chart e solve contatto |
| fk_sheet.cpp:213 | G | parallela complanare curva nota con normale costante; bounds derivata e normalizzazione |
| fk_sheet.cpp:402 | S | sample(t) intersezione superfici estese con Newton, scelta ramo |
| fk_sheet.cpp:590 | G | parallela complanare identica al213 |
| fk_surface_mapping.cpp:152 | S/G | map è callback generico; alcuni chiamanti map hanno formule note, provider deve attraversare mapBody; impossibile garantire callback arbitrario da pochi campioni |
| fk_surface_mapping.cpp:182 | S/G | image.parameters è callback generico, spesso inversione; conservare eventuale provenienza analitica |
| fk_surface_poles.cpp:107 | G | composizione supporto B-spline modificato con PCurve nota; bounds razionali bidimensionali |
| fk_thicken.cpp:312 | S/G | curve nota+faceNormalAt che proietta sulla faccia: s0=0 è curva esatta, altri casi solve UV/normalizzazione |
| fk_thicken.cpp:313 | S/G | idem per seconda quota s1; evitare fit quando s=0 |

G: composizione geometrica esplicita; S: solve/callback non certificato. 11 G,8 S,5 misti S/G (conteggio righe). Nessun caller dispone oggi di un bound certificato attraverso l'attuale sola std::function. I provider G sono il primo insieme migrabile; non promettere soluzione automatica degli8 solve impliciti con un semplice bounding box.

## Matrice test dopo API concordata

1. Testunitario fitter con provider geometrico: spline27 originale e12 varianti sopra; i casi polinomiali e razionali validi devono avere SUCCESSO, bound di errore <=tol e deviazione finita coerente. Un rifiuto con budget standard è fallimento del test; limiti artificialmente bassi sono testati separatamente. I casi razionali moderati devono riuscire; nessun “fix” che rigetta tutto. Confronto indipendente 20001punti come ulteriore controllo, bound certificato viene dall'oracolo e non dai campioni.
2. Offset end-to-end su12 varianti: S(u,v)+dN(u,v) confrontato nello stesso parametro; garantire successo almeno originalepolinomiale e pose/scale ordinarie, errore<=tol (con roundoff esplicito se concordato). Boundglobale curvatura della fixture già dimostrato da independent/source_bounds.py: raggio>=14.57067, d*k<1; non è legittimo rigettarla come singolare.
3. Callback senza provider avversa (grado15 e bump isolato fra tutti i campioni): esito esplicito Uncertified/Unresolved o eccezione nel percorso che richiede garanzia; mai certificazione automatica. Un numero fisso maggiore di campioni non chiude K10.
4. Provider avverso: intervalli non finiti/rovesciati, bounds troppo grossi, denominatore che include0, risposta “non supportato”, vettori NaN/Inf: failclosed, sentinella intatta. Non si può validare magicamente un provider bugiardo che dichiara bound falso; documentare trustedcontract.
5. Limiti: maxCells/maxDepth0/1 e budget medio: rifiuto/Unresolved senza successo terminale depth>=40/spanmin se errore non provato; risultato/diagnostic coerenti. Ripetere geometria valida semplice che si certifica entro budgetminimo.
6. Intervalli: range invertito/vuoto/NaN/Inf, breaks fuori range/duplicati/NaN/Inf; rifiuto deterministico o filtro documentato, mai loop infinito. Rangetraslato1e6..1e6+1 e stretto1e-9 con coordinate moderate per identificare roundoffderivate.
7. Atomicità: deviation e altrioutparameters sentinella su NaN, eccezione callback, esaurimento, cancel durante jet/provider/ultimo segmento. Callbackiniziale non chiamata se token giàattivo. OperationCancelled propagata non convertita in domain_error.
8. Integrazioni mirate nonOCCT: offsetExtrusion+revolution, offsetFaces preserveSeams±, shell/thicken, planarSheet, boundary/fill, surfacePoles, projection, map/quilt, exchange sewing, sketchcurveoffset. Parent sceglie migrazione API per rami S; test legacy non deve chiamare “Certified” un fit solo campionato.
9. App mirata: preview offset/pattern deformazione pertinente, annulla durante raffinamento/callback, conferma riusa BReppronto e errore kernel coerente; nessun test raster necessario per K10. CAD originali e settings invariati.

API suggerita ai test: separare modalità sampled/legacy da richiesta certificata; geometria/callback devono fornire bound locale del residuo o enclosure/jet con contratto esplicito. Result status Certified/Unresolved + diagnostic/budgetconsumato, oppure throw esplicito nei wrapper che richiedono garanzia. Nessun nuovo codice testproduction prima di accordo parent sull'API.
