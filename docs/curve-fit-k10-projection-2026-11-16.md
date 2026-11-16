# K10 — proiezione di curve su Plane, 16 novembre 2026

Prosecuzione con tre agenti GPT-6.1 Sol dopo gli [offset dello schizzo](curve-fit-k10-sketch-offset-2026-11-16.md). Trattato un ulteriore sito iniziale, `Projector::finish`: ora usa un limite continuo per rette e NURBS proiettate su una singola faccia esplicitamente Plane. K10 rimane aperto.

## Difetto riprodotto

Una NURBS regolare di grado 27 contiene una variazione fra i parametri controllati dal fitter. Proiettata su un piano, viene accettata con quattro poli e scarto dichiarato di circa 1,13e-10 mm, ma perde un dettaglio di circa **5,74e-5 mm** a fronte di una tolleranza di **1e-7 mm**: circa 574 volte il limite.

Il nuovo audit indipendente riproduce il difetto su 16 varianti polinomiali/razionali, sopra e sotto il piano, con direzione normale/obliqua e piano ruotato/traslato. I dati IEEE originali vengono trattati come razionali esatti. Un punto della proiezione esatta viene separato dall’involucro globale dei poli dell’output: il limite inferiore di errore geometrico è almeno **5,4156407e-5 mm** nelle sedici varianti. Il testimone usa un parametro razionale esatto; non dipende dalla ricerca numerica della proiezione più vicina.

Il confronto ricostruito usa `fk_project.cpp/.h` da HEAD precedente alla correzione, collegato al resto del kernel Release corrente; il vecchio modulo non chiama il nuovo provider. Questa baseline di modulo è dichiarata separatamente dall’archivio kernel iniziale, non più disponibile dopo l’interruzione.

## Correzione e contratto

Il nuovo provider `planeProjectionErrorBound` parte dalla curva e dal piano originali. Se O è l'origine, X/Y gli assi parametrici del piano, n=X×Y e d la direzione originale, la proiezione è `P=C-d*(n·(C-O))/(n·d)`. La normale è calcolata dagli assi che definiscono la superficie: non si assume che lo zDir memorizzato sia una normale matematicamente esatta. Anche la normalizzazione arrotondata usata dal percorso numerico non diventa una fonte esatta del certificato.

Per C=H/W e candidato cubico Q, il residuo omogeneo è `(H-QW)*(n·d) - d*(n·H-(n·O)*W)`. Estrazione, trasformazioni e operazioni sui coefficienti usano intervalli arrotondati verso l'esterno; il limite viene diviso per limiti inferiori positivi di W e |n·d|. Dati non finiti, pesi non positivi, domini fuori dai trim, denominatore irrisolto e budget esauriti producono un rifiuto per le famiglie riconosciute. Nessun certificato viene ricavato da poli o trasformazioni intermedi già arrotondati.

`fitCurveCertified` conserva il parametro originale e produce cubiche non razionali. I gruppi su più facce, i supporti diversi da Plane e le sorgenti non supportate mantengono il fit campionato precedente. La certificazione è posizionale sui tratti mantenuti; non dimostra che la classificazione campionata abbia individuato ogni cambio di faccia o il primo impatto globale.

## Giunti e scarto mostrato dall'applicazione

La concatenazione precedente media gli estremi e trasla i nodi. Il nuovo certificato opaco conserva per ogni cubica il dominio sorgente originale e il limite richiesto. Dopo la media, anche nella fusione della prima e ultima catena, vengono verificati i **poli effettivamente pubblicati**. Ogni campata finale deve essere isolabile come cubica Bernstein; il suo parametro normalizzato corrisponde in modo affine al dominio originale. La verifica non assume che la traslazione dei nodi sia esatta.

Una provenienza associata a una curva o a un intervallo sostituito viene rifiutata. I dati necessari al limite sono copiati e posseduti dal certificato; curva, piano e corpo sorgenti possono essere distrutti prima della giunzione. La provenienza è piatta e non trattiene le catene figlie.

`ProjectedChain::deviation` riporta lo scarto dopo la giunzione. `forgeProjectedCurves` usa questo valore nel riepilogo, anziché il massimo dei pezzi prima delle modifiche agli estremi. Per le parti legacy rimane una metrica campionata, distinta dal controllo certificato; lo spostamento degli estremi entra nella sua stima. Non viene promessa una continuità C1 generale.

La tolleranza di connessione dei giunti non aumenta il budget del fit: un gap ammesso per collegare gli estremi può ancora produrre un rifiuto se la loro media supera il limite posizionale originale. È un rifiuto conservativo intenzionale; non viene dichiarata riuscita ogni catena geometricamente valida entro la sola tolleranza di connessione.

## Verifiche finali e ripresa dopo l’interruzione

La prima fase ha implementato e verificato la correzione con tre agenti GPT-6.1 Sol. L’interruzione ha conservato sorgenti, eseguibili e mappa, ma la cartella temporanea non ancora archiviata è andata persa. Due agenti GPT-6.1 Sol e il coordinatore hanno quindi ripetuto i controlli seguenti e salvato direttamente le prove nel repository. I cinque hash di sorgenti kernel/test coincidono con quelli registrati prima dell’interruzione.

| Verifica ripetuta | Esito corrente archiviato |
|---|---|
| Build Release | Build completa precedente riuscita; replay incrementale di app e runner PASS, eseguibili successivi ai sorgenti |
| Regressioni permanenti | **33/33 PASS**; confronto del modulo precedente **3 PASS / 30 FAIL** |
| CTest pertinenti | **24/24 PASS**, kernel, proiezioni, offset, storia, durata documenti/worker, pannelli e anteprime |
| Runner kernel generale, filtro `Projected` | **5/5 PASS** |
| Prove applicative private | **3/3 PASS**, geometria e riepilogo confrontati con la pipeline kernel |
| ASan/UBSan/LSan mirati | **33/33 PASS**, leak check attivo, nessuna soppressione |
| Audit geometrico indipendente finale | **16 varianti, 688 campate finali**; controllo separato degli stessi pezzi, senza raddoppiare il numero delle geometrie |
| Giunti indipendenti aggiuntivi | Due scenari, **264 campate**, compresa fusione della prima e ultima catena |
| Limite superiore continuo finale | Massimo **9,908761802e-8 mm**, entro 1e-7 mm |
| Prova esatta del difetto precedente | **16/16 testimoni** separati dall’involucro globale dei poli |
| Architettura | **758 file, 10958 archi, 378 TU**; zero diagnostica, `--check`, HTML/JSON e collegamenti entranti/uscenti PASS |
| Conservazione dalla ripresa | **2601/2603 file invariati**, aggiornati solo questo rapporto e CLAUDE; nessuna ulteriore modifica software |

Le 30 asserzioni fallite nel confronto comprendono requisiti della nuova metrica e della provenienza: non sono 30 bug indipendenti. Le regressioni coprono dettagli nascosti, razionali, versi, pose, scale 0,001/1/1000, trim/istanze, nodi C0, parallelismo, sorgenti distrutte, giunti anche primo/ultimo, provenienza sostituita e rifiuto degli spostamenti fuori budget.

Il probe applicativo usa gli oggetti finali della build e un main privato con QApplication offscreen. Nel caso linee con gap 5e-9, il limite prima della giunzione è **6,443778892e-11 mm**, quello finale **2,500008534e-9 mm**, circa 38,8 volte maggiore. Il riepilogo mostra **2,5e-9 mm** e l’asserzione distingue il comportamento precedente. Gli altri due casi verificano NURBS con dettaglio nascosto, con e senza piccolo gap. Struttura e poli coincidono con la pipeline kernel; non è una prova del renderer.

Il sanitizer strumenta soltanto `test_projection_fit_validation.cpp`, `fk_project.cpp` e `fk_curve_fit_bounds.cpp`; il resto del kernel proviene dall’archivio Release corrente non strumentato. Il replay viene eseguito fuori dal sandbox per consentire il leak check. Non certifica ogni istruzione del kernel né l’intera app. Un primo tentativo del probe Qt è fallito prima dei casi per il tema GTK ereditato: il log è preservato, i replay finali usano esplicitamente offscreen e Fusion.

I nuovi certificati indipendenti usano frazioni esatte e coefficienti originali, senza riutilizzare il provider di produzione. Verificano ogni campata anche rispetto alla `deviation` pubblicata. Il massimo dei due scenari di giunzione aggiuntivi è **9,763302787e-8 mm**. Varianti, dati originali, output e script sono archiviati separatamente dalle prove normali.

Il nuovo runner è C++ puro senza Qt/OCCT; anche il link dell’app resta senza OCCT. Il runner generale di confronto conserva le proprie dipendenze opzionali. Il replay incrementale non ricompila i moduli: non viene usato per attribuire nuovi conteggi di warning. Il controllo finale verifica flag floating-point rigorosi, hash dei binari e tempi successivi ai sorgenti.

Il manifest iniziale di 2600 file e i vecchi 67 controlli grezzi non sono recuperabili dalla cartella temporanea. **Il precedente conteggio preliminare 2590/2600 è sostituito dall’audit verificabile della ripresa**, non ricostruito come se fosse ancora disponibile. Sono conservati un nuovo snapshot di 2603 file, hash delle sorgenti identici a prima dell’interruzione, 249 archi incidenti della mappa corrente, `git diff --check` e un manifest dell’archivio. Gli esiti numerici di questa sezione appartengono ai nuovi replay.

## Lavoro ancora aperto

**14/24 siti iniziali parzialmente trattati, 10 interamente da migrare**, oltre ai fallback nei quattordici affrontati. Questo blocco aggiunge un solo sito al conteggio. Le verifiche numeriche non aumentano il numero dei rilievi: restano **24 rilievi precedenti corretti**, **22 candidate orfane** e l'uniformazione completa di anteprime, pannelli e stili. K2 rimane in produzione.

| Siti ancora interamente da migrare | Numero | Motivo |
|---|---:|---|
| Offset generico e due ricostruzioni UV in `fk_offset.cpp` | 3 | Provenienza geometrica e solve impliciti da limitare |
| Giunti e continuazione dei raccordi | 2 | Composizione di supporti e controllo del ramo implicito |
| Trasferimento UV in mapping/quilt | 2 | Inversione parametrica e scelta del supporto |
| Rimappatura della SP-curve nella cucitura | 1 | Corrispondenza non affine fra curve |
| Giunzione delle superfici estese | 1 | Intersezione implicita |
| Curve procedurali proiettate nello schizzo | 1 | Provenienza del supporto da conservare |

[Inventario dei dieci siti e origine dei conteggi](curve-fit-k10-projection-prove-2026-11-16/independent-replay/inventory.md). La selezione delle facce, le transizioni, onEdge/partialEdge e i supporti/famiglie non riconosciuti restano campionati. Budget finiti e limiti numerici conservativi possono rifiutare dati validi. Nessuna nuova certificazione del renderer/OpenGL né assenza globale di leak o difetti. CAD originali e modifiche pregresse preservati.

[Prove, sorgenti dei riproduttori e manifest](curve-fit-k10-projection-prove-2026-11-16/README.md).
