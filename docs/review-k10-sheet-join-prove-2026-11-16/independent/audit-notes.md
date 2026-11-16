# Audit indipendente K10: giunzioni Estendi, 16 novembre 2026

Baseline HEAD `ce03a6d5941840886416aafe055763fb98209e94`; working tree già modificato e congelato nel manifest `baseline.json`. `kernel/fk_sheet.cpp` baseline SHA256 `fdf2810ae1777d376fd80719e9fa8210cdfda810e450c706f6e05963129f098c`. Il ramo audit scrive esclusivamente sotto `/tmp/sheet-join-review/audit`; root gestisce integrazione, app, mappa, documenti e CLAUDE.

Letti AGENTS, CLAUDE, politica tolleranze, rapporti K10 iniziale/Estendi-Fill/Mapping-UV, descrizione e archi bidirezionali del quartiere fk_sheet nel grafo locale. Copia del quartiere in `architecture-neighborhood.json`.

## Rilievi baseline (un unico K10 già aperto)

- `joinSplineExtensions` fitta la posizione prodotta da Newton con `fitCurve`, per cui l'esito campionato non prova un massimo continuo. La corrispondenza parametrica non è un certificato di esistenza/unicità dell'intersezione.
- Il recupero quasi tangente usa due isoparametriche, misura `seamGap` dai punti visitati dal fit più 65 punti uniformi e costruisce `joinTolerance=max(tolerance,2*seamGap)`. Il budget cresce dopo la misura, contro la politica del repository.
- Il dominio dei nuovi supporti viene ampliato con 65 punti UV. Un massimo UV fra essi può sfuggire. Per una mappa affine a pesi uguali i poli originali delimitano invece tutto il dominio per convessità.
- Il fit usa sa/sb già estese, poi crea newA/newB e ricalcola le SP-curve senza una prova continua che usi i coefficienti delle fin finali, dopo build e unifySameDomain. `checkBody` è campionato; la sola sua riuscita non chiude questa lacuna.
- L'API lavora su copie e pubblica history alla fine; la baseline azzera history in ingresso. `OperationCancelled` non deriva da `std::exception`, quindi i catch di geometria non la assorbono. Questo è un esame statico, non una prova di tutti gli stati di cancellazione.
- Geometria e provider usano double. L'audit non ha trovato float/QVector3D nel percorso sheet interessato; ciò non certifica la precisione assoluta a coordinate arbitrarie.

## Provider esistente: campo effettivo

`surfaceCompositionErrorBound` per B-spline supporta mappe isoparametriche originali: Line2 con direzione esattamente ±1, oppure SP-curve grado uno a pesi uguali con una sola coordinata variabile. Origini, ancore parametriche e nodi originali sono conservati. Gli input supportati non finiti/fuori dominio/non risolti falliscono. Non autorizza una mappa UV implicita Newton, obliqua su tensoriale, periodicamente traslata o cubica non affine.

## Verificatore Fraction indipendente

`verify_tensor_fraction.py` importa soltanto la libreria aritmetica Python standard e `fraction_common.py`, copiato con provenienza dal precedente audit di cucitura. Non legge i provider C++ né i coefficienti estratti da essi. Ricostruisce da nodi/poli/pesi IEEE la base omogenea Cox/de Boor di ogni campata della superficie originale e la compone con la SP-curve finale; confronta il numeratore razionale con la curva edge finale, nel medesimo parametro. Verifica tutti i polinomi dei rettangoli di nodo intersecati, perciò copre anche l'attraversamento di nodi senza scambiare una griglia per certificazione. I limiti Bernstein e le suddivisioni de Casteljau sono Fraction esatte; la radice superiore del modulo è razionale con controllo del quadrato.

Supporta curve Line/NURBS e wrapper trim/transform; superficie tensoriale originale a pesi positivi e istanze esportate come basis+matrice+traslazione. Non certifica denominatori non positivi, periodi impliciti, massimi di curvature, autointersezioni o topologia globale. Un testimone puntuale razionale può confutare una pretesa di massimo; un esito positivo richiede sempre la copertura continua completa.

Verifiche correnti del solo verificatore: bilineare con nodo interno attraversato PASS; superficie razionale positiva PASS; polo di output alterato rifiutato con testimone razionale esatto PASS. Non sono prove di produzione. Export delle API reali e audit candidato ancora da acquisire alla stesura di questa nota.

## Risultati correnti indipendenti su V2 congelata

Candidato V2 SHA256 `70efab1cde1559a528ff38377f4e2329512b9bc9edf36599fc3d82a251def868`; export JSON acquisiti in `exports-v2/` con manifest indipendente. La suite CPU di produzione è riferita dal collega tests e il log è stato letto, non eseguita dal ramo audit: 29/29. Sono invece state eseguite da questo ramo tutte le verifiche Python qui sotto.

- Due negativi baseline rifiutati da Fraction: fin11, spike a campata stretta, testimone circa 9,848267876e-4 mm; fin11, divaricazione tangente, testimone circa 1,352082213e-6 mm contro cap prefissato 1e-7 mm. Parametri e quadrati esatti sono in `baseline-witness-summary.json`. Il primo supera anche il budget dichiarato del B-rep baseline, il secondo dimostra perché un budget ricavato dopo la misura non è una prova di qualità.
- 30/30 fin nuove, 10 varianti tangenti, 36 celle parametriche: residuo continuo S(Q)-E sotto il cap proprio dell'edge, senza usare la somma permissiva di checkBody. Rapporto massimo upper/cap 0,721995584824; il massimo upper assoluto è 1,794439706e-7 mm nel caso scalato, con cap anch'esso scalato. Nel trim originale sono compresi tutti i nodi stretti, non eliminati come rumore.
- 38/38 supporti finali, 19 varianti, 46 rettangoli parametrici: conservazione continua sul dominio originale provata con residuo razionale tensoriale bivariato, max upper 2,278602663e-13 mm. Il collegamento originale/finale usa un unico antenato Face dichiarato in faceOrigins, operand0; niente associazione per vicinanza o numero di faccia supposto. Tolleranza di questa prova 1e-7 mm, senza dichiarare identità bitwise dopo estensione numerica.
- Esame più ampio di tutte le fin: 214/247 actual e125/152 source certificati sul dominio naturale;33/27 restano non certificati perché il relativo inviluppo razionale UV supera il dominio di quantità piccole. Un inviluppo che supera il dominio non dimostra da solo che tutta la curva ne esca; qui determina un esito non risolto. Non è stato trovato in questi restanti un testimone geometrico fuori budget. Il valutatore C++ prosegue il polinomio al bordo anche fuori dominio, ma il verificatore strict non assimila quel prolungamento alla copertura del dominio naturale. Questa distinzione è registrata e non è un nuovo rilievo contato.
- Controlli negativi: polo dell'edge actual alterato di1e-3mm rifiutato con testimone razionale; polo del supporto actual alterato rifiutato dalla prova bivariata. I selftest aritmetici continuano a passare.

## Esame statico candidato e limiti

V3 definitiva SHA256 `8e9bd9d6c1d61bf9e8a189e783d6803510c3c61f07ed28c8e5b82f2efa457e2f`; rispettoV2 aggiunge solo il numero atteso di fin a ogni piano e richiede uses==expectedUses dopo build. Diff letto dall'audit. La versione certificata forza la via affine per giunzioni inizialmente quasi tangenti, senza recupero campionato dopo un fallimento del certificato. La soglia angolare iniziale seleziona il percorso, non costituisce un certificato di tangenza globale.

La via affine delimita il dominio usando tutti i poli UV originali grado1 a pesi uguali; costruisce i supporti finali prima dei provider, assegna il cap prima del fit e controlla separatamente i residui A/B. Confronta i coefficienti cubici originali con i provider dopo assemble e dopo unify, seguendo i discendenti Face+Edge. Identità, range, SP-curve e budget differenti vengono rifiutati conservativamente. Non pubblica history prima dell'ultima cancellationPoint.

La via trasversale Newton conserva fit, inviluppoUV e controlli finali campionati. Fuori ambito: certificato di esistenza/unicità dell'intersezione implicita, giunzioni multiway complesse, rimappature unification e periodicità non supportate. Le risorse limitate o il condizionamento possono rifiutare input geometricamente validi. L'assenza di autointersezioni, la correttezza G1/G2 e le topologie globali non seguono dal solo certificato S(Q)-E. L'audit non ha compilato app, eseguito UI, CMake o sanitizers; root/tests forniscono e distinguono queste prove.

K10 rimane un unico rilievo aperto: questa famiglia porta eventualmente il conteggio a22/24 siti parzialmente trattati, con2 siti interamente aperti, solo dopo integrazione e verifica root. Nessun nuovo difetto contato per varianti dello stesso caso.

## Sigillo finale V3

L'audit ha confrontato direttamente i19 JSON V3 con i19 V2: tutti identici byte per byte; il confronto con entrambi gli SHA è in `v2-final-byte-comparison.json`. Dopo il confronto sono state **rieseguite** le verifiche Fraction sui JSON finali acquisiti in `exports-final/`:30/30 fin target (36celle),38/38 supporti (46celle),214/247 actual e125/152 source per l'esame più ampio. Dati finali in `fraction-final-*.json` e `summary-final.json`. Il codice V3 analizzato è conservato come `candidate-fk_sheet.cpp.txt`. L'audit ha letto il log della suite ReleaseV3 con29/29; quella esecuzione resta riferita al ramo tests.

Per riprodurre dal pacchetto audit: `python3 check_exports.py final-strict fraction-rerun-strict.json`; `python3 verify_surface_preservation.py exports-final fraction-rerun-support.json`; `python3 check_exports.py exports-final fraction-rerun-all.json`. I due negativi completi sono in `baseline-negatives/`; le copie `*-strict-seam.json` isolano le fin target imponendo il cap originario. Nessun binario ELF/libreria/oggetto/CAD viene incluso nel pacchetto audit.
