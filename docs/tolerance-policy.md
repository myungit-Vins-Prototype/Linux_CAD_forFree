# Politica delle tolleranze di ForgeCAD

Stato: regole di sviluppo e default esistenti formalizzati il 15 ottobre 2026.
La migrazione delle soglie locali e' progressiva; questo documento non certifica
che tutti gli algoritmi siano gia' conformi. Fonte dei default numerici del kernel:
`kernel/fk_precision.h`; connessione schizzo: `cad_kernel.h`.

## Unita', rappresentazione e campo di validita'

Coordinate e distanze in millimetri, angoli in radianti, geometria in `double`.
Il campo nominale e' il cubo centrato nell'origine di lato `kSizeBox = 1e6` mm.
`insideSizeBox` verifica una coordinata: non e' un controllo automatico di tutto
il modello. Fuori campo non si promette la stessa precisione assoluta.

Curve analitiche e NURBS sono la rappresentazione primaria. Valutazione,
intersezione e integrazione hanno errori numerici: rappresentazione esatta non
significa calcolo senza errore. Mesh e `float` sono riservati alla visualizzazione
e al picking; non determinano coincidenza, volumi o validita' del B-rep.

## Budget distinti

| Quantita' | Default | Significato e proprietario |
| --- | --- | --- |
| Risoluzione lineare | `kLinearResolution = 1e-7` mm | Soglia geometrica minima di riferimento; kernel |
| Risoluzione angolare | `kAngularResolution = 1e-11` rad | Confronti di direzioni; kernel |
| Connessione schizzo | `ForgeCad::kSketchConnectionTolerance = 1e-6` mm | Unione estremi per costruire contorni; applicazione |
| Connessione booleane | `kBooleanConnectionTolerance = 1e-6` mm | Default di `BooleanOptions::tolerance` |
| Connessione marching | `kMarchingConnectionTolerance = 1e-6` mm | Default di `SurfaceIntersectionOptions::tolerance` |
| Fit marching | `kMarchingFitTolerance = 1e-9` mm | Default di `SurfaceIntersectionOptions::fitTolerance`; errore della curva approssimata |
| Fit SP-curve | `kPCurveTolerance = kLinearResolution` | Scarto 3D richiesto al fit delle SP-curve |
| Arrotondamento | `kMachineEpsilon` | Grandezza adimensionale del `double`, da rapportare alla scala numerica |

Le costanti con lo stesso valore restano semanticamente distinte. Un fit a 1e-9
non autorizza a fondere vertici con quella soglia; la connessione a 1e-6 non
rende accettabile qualsiasi errore di costruzione inferiore a 1e-6.
Gli override delle opzioni sono scelte esplicite del chiamante, da motivare.

## Tolleranze locali del B-rep

`Vertex::tolerance` e `Edge::tolerance` sono distanze 3D finite e non negative.
Zero significa default, non precisione infinita. Anche valori positivi inferiori
alla risoluzione non restringono i controlli sotto `kLinearResolution`.
`Fin::pcurveTolerance` e' uno scarto 3D finito e non negativo; zero indica una
SP-curve dichiarata esatta, non un'approssimazione col budget di default.

I confronti di `checkBody` usano:

- estremo curva/vertice: `max(kLinearResolution, vertex.tolerance, edge.tolerance)`;
- curva edge/superficie: `max(kLinearResolution, edge.tolerance)`;
- SP-curve/edge: `max(kLinearResolution, edge.tolerance) + fin.pcurveTolerance`.

Il massimo combina tolleranze alternative della stessa connessione; la somma
combina l'errore dell'edge e quello della sua rappresentazione sulla superficie.
Non sommare indiscriminatamente tutte le tolleranze di un corpo.

Importazione, split, fusione e trasformazioni devono conservare i budget delle
entita' coinvolte e includere gli scarti introdotti, con provenienza spiegabile.
Una similitudine di fattore s richiede budget lineari scalati di |s|; una
trasformazione generale richiede un limite conservativo dell'amplificazione.
La conformita' di ogni percorso di trasformazione resta da verificare.

Non aumentare una tolleranza dopo un fallimento al solo scopo di far passare il
controllo. Uno scarto misurato non e' automaticamente un budget accettabile.
Per geometria generata, fissare il budget prima della costruzione e rifiutare
il risultato se non viene rispettato. Per geometria importata, preservare la
precisione dichiarata e distinguere eventuali stime dagli errori certificati.
Non e' introdotto un tetto globale arbitrario: gli edge importati possono essere
legittimamente piu' tolleranti. Limiti di qualita' specifici dell'operazione
vanno dichiarati e verificati separatamente.

## Parametri, angoli e soglie numeriche

Una distanza UV o un intervallo del parametro non sono millimetri. Per convertirli
in errore spaziale usare le derivate della curva o il Jacobiano della superficie,
con un limite adeguato sull'intervallo. Nei poli o vicino a derivate nulle non
basta dividere per la velocita' locale: occorrono raffinamento, limiti conservativi
o un fallimento esplicito. Non fondere nodi distinti della stessa B-spline solo
perche' distano meno di una soglia parametrica.

Confrontare distanze al quadrato con il quadrato del budget. I confronti di
normali richiedono normalizzazione; soglie sul seno, sul coseno e su un angolo
non sono intercambiabili. Convergenza Newton, pivot di sistemi lineari e residui
relativi richiedono scala e unita' proprie: non sostituirli meccanicamente con
`kLinearResolution`. I predicati esatti `orient2d`/`orient3d` restano tali: non
aggiungere una fascia di zero geometrico al loro segno.

## Controlli implementati e loro limiti

`checkBody` rifiuta `InvalidTolerance` su vertici, edge e fin con valore negativo,
NaN o infinito, anche con `CheckOptions::geometry = false`. Ferma i controlli
geometrici prima che budget invalidi possano nascondere errori. Non modifica il
corpo e non ripara automaticamente i metadati.

I controlli esistenti `VertexOffEdge`, `EdgeOffFace` e `PCurveOffEdge` verificano
le relazioni sopra. I controlli edge/superficie e SP-curve sono campionati:
un esito positivo non certifica un massimo globale. La tolleranza non dimostra
assenza di autointersezioni, continuita' G1/G2 o correttezza dei nomi topologici.
`loopCrossings` e' un controllo aggiuntivo opzionale.

La regressione `forgekernel_tolerance_tests` copre default, metadati invalidi,
zero, tolleranze importate, soglie vertice/edge e composizione SP-curve/edge.
Esecuzione dalla directory di build: `ctest -R '^forgekernel_tolerance_tests$' --output-on-failure`.
Non richiede OCCT nel proprio codice ne' nel collegamento; la configurazione
principale del progetto richiede ancora Qt e OCCT per gli altri target.

## Regola per ogni nuova soglia o modifica

Prima di introdurre una costante, identificarne ruolo, unita', scala e budget
proprietario. Riutilizzare il default nominato pertinente; se la soglia e'
algoritmica, darle un nome locale e documentare formula e motivazione.
Ogni cambiamento di valore deve includere un caso sotto e sopra soglia, un caso
a scala diversa quando rilevante e una verifica di conservazione del budget
nei risultati. Una regressione deve controllare anche lo scarto geometrico:
il solo successo dell'operazione o `checkBody` non basta.

Migrazione ancora aperta: classificare le soglie anonime nei singoli moduli,
verificare propagazione nelle trasformazioni e nelle operazioni, validare gli
override alle API, aggiungere misure conservative dove oggi ci sono campioni.
Non vietare con una regex tutti gli `1e-9`: possono descrivere grandezze diverse.
