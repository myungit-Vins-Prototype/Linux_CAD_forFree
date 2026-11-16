# Audit indipendente del percorso Supporto — 16 novembre 2026

Sola lettura del repository. Nessuna compilazione, modifica produzione, prova UI, rigenerazione del grafico o scrittura del CAD originale. CLAUDE, AGENTS, politica tolleranze e descrizioni del grafico consultate. I risultati geometrici sottostanti sono letti dai log della sonda separata di core Sol, non prodotti da una seconda compilazione.

## Identità e risultato riprodotto

Copia `/tmp/supporto-review/Supporto.original.prt`: sei schizzi, sei funzioni, tre corpi logici. Precisione documento 1e-7 mm.

| Corpo del pannello | Funzione | Indice | featureId | modelBodyId |
| --- | --- | --- | --- | --- |
| Corpo 1 | Estrusione 1 | 0 | 1 | 1 |
| Corpo 2 | Estrusione 3 | 2 | 3 | 2 |
| Corpo 3 | Loft 6 | 5 | 6 | 3 |

Tutte le tre B-rep iniziali salvate superano checkBody. Sono cache, non ricostruzioni parametriche certificate da questo audit. Loft 6 ha due sezioni (schizzi 4/5, indici zero-based), due guide da bordo, condizioni G1/G1 e mergeTangentFaces disattivato.

Raw Corpo 2 / Corpo 3 fallisce con «booleanOperation: risultato non chiuso (Body::build: l'edge 3 non ha due fin opposte)». Ordine inverso: edge 2. Anche forgeBoolean fallisce in entrambi gli ordini, dopo il suo tentativo inverso. Riferimenti: raw-2-5.log, raw-5-2.log, app-2-5.log, app-5-2.log.

Il fallimento è nella prima costruzione topologica del risultato, fk_boolean.cpp:2675. Intersezioni, suddivisioni e classificazioni hanno restituito dati senza eccezioni. La costruzione non raggiunge computePCurves/unify/checkBody finali. Non si tratta del messaggio di mancato fit della loft. L'indice edge 2/3 appartiene al vettore assembly e non identifica lo spigolo sorgente.

## Percorso applicativo ed errore

- `forgeCad2026_dialogs_common.cpp:1490 resultBodyLabel` usa ModelBody.name per il pannello. Gli errori in buildGeometry usano ExtrusionObject.name, ossia nomi di funzione.
- `forgeCad2026_dialogs_solids.cpp` ordina i candidati per indice di storia; la prima spunta valida diventa secondBody e le altre booleanTools. L'ordine di clic non è l'ordine di calcolo.
- `cad_feature_build.cpp:211-248` per unione multistrumento prova prima forgePatternBodies({B,C,...}), poi forgeBoolean(A, strumenti). In caso di fallimento cancella quell'errore e riparte con A+B, poi +C. Il messaggio finale «Con ...» indica la funzione dello strumento del percorso sequenziale.
- `cad_pattern.cpp:93 uniteAll` è bilanciato e usa forgeBooleanWithHistory direttamente, senza retry inverso.
- `cad_forge.cpp:1161 forgeBoolean` prova ordine inverso una sola volta dopo std::domain_error; se fallisce conserva soltanto l'ultimo messaggio. OperationCancelled non viene trattata come domain_error. I budget restano gli stessi. Un eventuale errore finale fa riferimento all'ordine inverso.
- Errore della funzione: `ExtrusionObject.error`, azzerato da buildGeometry prima del calcolo. Errore dell'anteprima: `preview_.error` nel viewport, trasmesso al callback della preview. Nessun errore preview viene scritto da solo nel CAD.
- `cad_document_io.cpp:1222` salva body.error nella cache soltanto se viene scritto un B-rep. `1386` ripristina il testo solo per una geometria caricata. Una funzione fallita priva di B-rep non conserva quel messaggio nel payload parametrico: il messaggio viene rigenerato al calcolo.
- La cache può conservarsi al caricamento (`cad_feature_build.cpp:67-110`). `CadViewport::rebuildAllFeatures` azzera cachedGeometry prima di rigenerare; confrontare snapshot e ricostruzione è una prova distinta.

## Soglie e discriminanti diagnostici

Con il profilo rilevato: linearResolution 1e-7 mm; BooleanOptions.tolerance 1e-6 mm; marching fit 1e-9 mm. Angular resolution 1e-11 rad. Non modificati. L'assembler può usare anche tolleranze locali degli operandi, quindi stampare massimi vertex/edge è utile.

`fk_topology.cpp:255` lancia se forwardCount != 1 oppure backwardCount != 1: il messaggio non distingue zero usi, un uso, due usi dello stesso verso o più usi. Per distinguerli basta il dump diretto dei BuildFin, senza riconoscere entità per vicinanza.

`pairRadially` non opera con <=2 usi. `splitVertices` cambia gli estremi, non il numero o verso delle fin. Se due usi esistono ma hanno lo stesso verso, il punto diagnostico è l'assegnazione fin.sense (`fk_boolean.cpp:2648`), derivata da dot delle tangenti dopo proiezione sul Piece; confrontare parametro, branch e sourceFin. Se manca un uso, confrontare ogni source body/face in kept e ogni Piece prima del montaggio.

Matching di edge in assembler: stessi indici di vertice agli estremi più distanza al punto medio o sua proiezione <=10*tolerance, cioè 1e-5 mm. Il controllo geometrico fitsSurface usa invece max(linearResolution, existing.tolerance, piece.tolerance), spesso 1e-7 mm. Si tratta di ruoli distinti. L'audit non attribuisce il fallimento a questa differenza senza dump, né propone di allargare soglie.

Prova consigliata al core, già coordinata: dump prima di Body::build con numero e verso degli usi di ciascun edge e provenienza kept/Piece.source/sourceFin, oltre curve/range. Identificare l'uso perso o il verso incoerente per identità genealogica. Nessuna rimappatura per sola prossimità.

## Letteratura e regressioni pertinenti riutilizzabili

- `docs/knifehandle-booleana-2026-11-05.md`, `kernel/tests/test_boolean_seams.cpp`: loft chiusa e perno, conservazione dei due lati UV della cucitura, entrambi gli ordini/tre operazioni, validità loop/volume/mesh/genealogia/STEP e 1/4 thread. Causa storica «foro fuori da ogni contorno», diversa dal fallimento Supporto attuale; non assumerla uguale senza traccia.
- `docs/knifehandle-unione-rilievi-2026-11-06.md`: ritaglio sottoarchi e riclassificazione Inside/Boundary/Outside dopo split globale; scelta della rappresentazione già abbinata che rispetta tutte le facce entro budget; nessun nuovo fit. Contatti di sola curva non equivalgono ad area coincidente.
- `kernel/tests/test_boolean.cpp`: BooleanTangentContacts, BooleanPartialCoincidence, BooleanPartialSplineCoincidence, BooleanParallelMatchesSequential, BooleanTolerantVertices e BooleanThinSplineSlivers. Sono regressioni mirate, non una prova che Supporto debba riuscire.
- `docs/loft-tolleranza-incontri-2026-11-03.md`: guida entro connessione ma raccordo discontinuo prima del fit; raccordo C2 dei residui, sezioni esatte, fit separato. Fallimento storico «approssimazione della guida fuori tolleranza», non quello osservato qui.
- `docs/loft-sezioni-proiettate-2026-11-03.md`: fit intrinseco prima della posa, evita suddivisioni quasi coincidenti causate dal rumore. Le sorgenti razionali restano sul percorso originale.
- CLAUDE: Flacone3 Corpo 1/4 gestito dall'app con retry inverso; kernel grezzo nell'ordine inverso resta limitato. Il retry esiste già e non basta su Supporto.

Sonde riusabili: bodySectionCurvesWithOrigins conserva le coppie esatte di facce; checkBody(body,{true,true}) aggiunge loopCrossings; writeBodyBinary per congelare input/output; BooleanOptions.threads=1 e4 per escludere dipendenza dal parallelismo. Disattivare unify può isolare un fallimento finale, ma il fallimento presente precede già tale passo. Nessuna prova di superficie o volume continua è sostituita da campioni display.

## Conclusione delimitata

Riprodotto un risultato topologico non chiuso fra due input B-rep validi, indipendente dal retry dell'app. Il punto esatto del controllo è noto; manca ancora il dump del conteggio fin per affermare se sia uso mancante, verso errato o duplicazione. Nessuna patch giustificata prima di quella discriminazione.

## Aggiornamento dopo la trace numerica

`trace-2-5.log` e `trace-5-2.log` discriminano il difetto: tre edge con un solo uso, nessun verso doppio. I conteggi non cambiano dopo pairRadially/splitVertices. Il cilindro conserva una corda rettilinea lunga 2,5699211534e-5 mm; le facce laterali F0/F1 del loft conservano microarchi dei source edge E2/E5. Il cap finale F3 resta indiviso e viene classificato In, quindi scartato. La corda è Inside sul cilindro ma Boundary sul cap: collectCuts la inserisce soltanto come cut del cilindro, con partnerCrosses=false. La distanza fra centro della corda e vertice originale del cap, dalle coordinate round-trip della trace, è circa 1,97976e-11 mm. Non c'è perdita di una pcurve finale: il cap non viene diviso.

Il piano del cap NON è tangente al cilindro: il suo carrier è secante (distanza asse/piano circa 0,3888 mm contro raggio24). La quasi tangenza è fra curva d'intersezione e bordo ellittico del cap. Il codice pairArcs esclude per default tangentCurves dei carrier.

`cap-radial-fraction.py/json/log`: prova indipendente esatta dai getter originali, confermati dal dump `geometry-hex.log`. Poiché l'asse Y dell'ellisse è parallelo all'asse del cilindro, rho(u)^2-R^2 è una quadratica convessa in cos(u). Il massimo continuo è per cos=±1. Sqrt outward mediante isqrt su razionali: i due residui radiali sono strettamente positivi, 1,64960410962e-12 e 1,97910812234e-11 mm; il massimo continuo sull'intera ellisse è inferiore a 2e-11 mm, oltre 5000 volte sotto la risoluzione documento. Nessun campionamento o provider produttivo è usato per questa attestazione. Non si certifica l'intera superficie laterale del loft.

La rigenerazione parametrica eseguita dal core produce B-rep bit-identiche ai tre snapshot e replica il fallimento: la cache è esclusa come causa. Unione0/2 e0/5 riescono; coppia2/5 e unione complessiva falliscono (vedi regenerate.log/multi.log).

## Audit del possibile contratto Boundary, senza implementazione

Le API attuali di classificazione restituiscono solo PointLocation; non portano testimoni dei bordi. Nel ramo piano di pairArcs, l'intersezione esatta con ogni fin del cap già produce entrambi i parametri CurveCurvePoint.s/t, ma il codice conserva solo s. È possibile estendere privatamente Arc con eventi BoundaryAnchor{body,face,fin,edge,edgeParameter,arcParameter} ottenuti dall'intersezione, senza introdurre nearest remap.

Da due eventi si possono seguire loopFins e vertici originali, preservando fin/edge/range/verso e la connessione E2→vertice→E5. Un loop può offrire due percorsi: occorre accettare solo una catena univoca con bound continuo nel budget fissato, oppure dichiarare inconclusivo. Occorre dimostrare appartenenza della catena al carrier dell'altro operando e copertura monotona del tratto sostituito; il solo midpoint o la vicinanza degli endpoint non bastano. curveOnSurface è campioni+bisezione e non costituisce questa dimostrazione.

Piece.source/sourceFin/sourcePCurve attuali appartengono implicitamente a subFace.body. Riutilizzarli per il bordo di un altro corpo provocherebbe lookup nel corpo sbagliato in assemble2654 o un seed UV sulla superficie sbagliata in finishCycle. Un BoundarySource straniero deve avere identità separata; il cut cilindro non deve usare sourcePCurve del cap. I Piece originali dei lati loft conservano i loro source invariati.

Il solo scarto della corda non ripara il modello: rimarrebbero i due microarchi dei lati loft. Dividere entrambe le facce senza modificare la regola Boundary genererebbe uno sliver interamente entro il budget e non classificabile dai normali punti interni. Queste sono condizioni del contratto da risolvere, non una patch già giustificata.

## Review e contributo al prototipo privato successivo

Su richiesta esplicita del root, creato soltanto `/tmp/supporto-review/affine-pcurve-certificate.inc.txt`, incluso dal core nel suo nuovo TU privato. Nessun file software del repository modificato da questo agente.

Il certificatore affinePCurveError considera i coefficienti UV originali di Line2 o B-spline2 grado1 con due poli/pesi1/nodi clamped. Limita continuamente Ellipse(t)-Surface(affineUV(t)) mediante massimi dei gap originali ai due estremi più due errori di interpolazione Peano. Per il cilindro usa direttamente R*(||X||+||Y||)*DeltaU²/8, equivalente a Mcomposition*DeltaT²/8 ma senza dividere per intervalli di parametro minuscoli. Piano: composizione affine, secondo derivato zero. Frame, coefficienti, prodotti, radici e trigonometria sono racchiusi outward; trig Taylor esistente più Lipschitz per UV intervallari, nessun valore libm come prova. Famiglie/dati non supportati o irrisolti restituiscono infinito. Sintassi C++17 -Wall/-Wextra/-Wpedantic verificata senza errori o avvisi. Le prove runtime indipendenti sono assegnate all'agente test; questo audit non attribuisce loro esito anticipato.

Criticità segnalate e coperte nei successivi sorgenti privati del core: raggio del controllo Lipschitz calcolato outward prima delle differenze; invalid face guard prima di boundaryChain per imprintCurves; tutte le quantità non finite propagano errore; mapping della genealogia dalla POSIZIONE fin realmente montata, non da shared_ptr/range; unione di origini locali e witness; mancato discendente dopo unify lancia errore invece di saltare ricertificazione.

Il preesistente exactPCurve può accettare un'immagine UV affine con verifica campionata e marcarla con pcurveTolerance zero; il fallback fit lineare può accettare fino a50 volte il budget richiesto. Per questo il nuovo percorso installa una propria immagine UV solo sul nuovo uso straniero e dichiara un limite continuo positivo, prima/dopo unify. Le pcurve originali dei lati vengono preservate. Il bound va ricontrollato sul range effettivo finale, senza considerare il solo checkBody una certificazione.

Stato letto dai log privati aggiornati alle10:02: raw2/5 e5/2 riescono con6facce/10edge/8vertici/1shell e checkBody senza errori. Questi esiti non indicano integrazione o build dell'app di produzione, e non sostituiscono freeze/test regressioni finali.
