# ForgeCAD — CAD parametrico 3D per Linux e macOS

**Lingua:** [English](README.md) · Italiano

[![CI Linux / macOS](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/workflows/ci.yml/badge.svg)](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/workflows/ci.yml)

Costruisci solidi parametrici e superfici a forma libera, modifica la storia delle operazioni e crea tavole tecniche 2D. ForgeCAD è un CAD gratuito e open source per Linux e macOS, con un kernel geometrico B-rep proprio.

**[Compila su Linux](#installazione-su-linux) · [Compila su macOS](#installazione-su-macos) · [Crea il primo modello](#primo-modello-e-comandi) · [Segnala un problema](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/issues)**

[![Modello di un mouse in ForgeCAD, con superfici, storyboard parametrico e pannello revisioni](docs/images/forgecad-3d.png)](docs/images/forgecad-3d.png)

*Modellazione di superfici, storia parametrica e revisioni nell’interfaccia di ForgeCAD.*

- **Disegna e vincola:** schizzi 2D, quote, vincoli geometrici e curve 3D.
- **Modella solidi e superfici:** estrusione, rivoluzione, Sweep, Loft, cucitura di superfici e modifica dei poli NURBS.
- **Rivedi il modello:** modifica i parametri delle funzioni e rigenera attraverso lo storyboard parametrico.
- **Crea tavole:** viste proiettate, sezioni, dettagli e quote.
- **Scambia geometria:** importa/esporta STEP e IGES; esporta STL e OBJ.

**Stato del progetto:** in sviluppo attivo, con interfaccia dell’applicazione in italiano. Il progetto è sviluppato con l’aiuto dell’intelligenza artificiale. Leggi i [limiti](#limiti-e-licenza) prima di affidarti al programma per un particolare flusso di lavoro.

Lo sviluppo è unificato su `main`. CUDA e Metal restano backend opzionali negli stessi sorgenti; gli esperimenti usano rami brevi e richiedono misure sui casi interessati prima dell’integrazione.

Stato delle funzioni e prossimi incrementi: [roadmap generale](docs/roadmap.md).

## Da dove iniziare

1. Installa le [dipendenze](#requisiti), quindi segui le istruzioni di compilazione dai sorgenti per [Linux](#installazione-su-linux) o [macOS](#installazione-su-macos).
2. Segui [Primo modello e comandi](#primo-modello-e-comandi) per disegnare uno schizzo vincolato ed estruderlo in un solido.
3. Salva il modello parametrico come documento ForgeCAD `.prt` oppure scegli un [formato di scambio](#formati-supportati) per altre applicazioni.

## Indice

- [Da dove iniziare](#da-dove-iniziare)
- [Galleria](#galleria)
- [Funzionalità](#funzionalità)
- [Formati supportati](#formati-supportati)
- [Requisiti](#requisiti)
- [Scaricare i sorgenti](#scaricare-i-sorgenti)
- [Installazione su Linux](#installazione-su-linux)
- [Installazione su macOS](#installazione-su-macos)
- [CUDA facoltativa](#cuda-facoltativa)
- [Primo modello e comandi](#primo-modello-e-comandi)
- [Esempi](#esempi)
- [Test e sviluppo](#test-e-sviluppo)
- [Risoluzione dei problemi](#risoluzione-dei-problemi)
- [Limiti e licenza](#limiti-e-licenza)

## Galleria

Schermate reali dell’applicazione. Fare clic sulle immagini per aprirle a piena risoluzione.

| Modellazione solida | Anteprima di una funzione |
|---|---|
| [![Modello solido di un mouse con storia delle feature e piani di riferimento](docs/images/forgecad-solid-modeling.png)](docs/images/forgecad-solid-modeling.png) | [![Anteprima dello svuotamento di un corpo con pareti e aperture](docs/images/forgecad-function-preview.png)](docs/images/forgecad-function-preview.png) |
| Costruzione del corpo con operazioni parametriche. | Controllo del risultato dello svuotamento prima della conferma. |

| Costruzione di superfici | Modifica dei poli NURBS |
|---|---|
| [![Superficie generata da una rete di curve U e V, con pannello dei parametri e anteprima](docs/images/forgecad-surface-network.png)](docs/images/forgecad-surface-network.png) | [![Gabbia dei poli NURBS con un punto selezionato e il manipolatore degli assi](docs/images/forgecad-nurbs-poles.png)](docs/images/forgecad-nurbs-poles.png) |
| Due famiglie di curve U/V definiscono la superficie. | Gabbia di controllo e spostamento dei poli lungo gli assi. |

[![Tavola 2D del mouse con viste proiettate, sezione, dettaglio quotato e vista isometrica](docs/images/forgecad-2d.png)](docs/images/forgecad-2d.png)

Tavola 2D con viste proiettate, sezione, dettaglio quotato e vista isometrica.

## Funzionalità

### Schizzi 2D e curve 3D

- Schizzi sui piani XY, XZ e YZ, su piani di costruzione e sulle facce piane dei corpi.
- Linee, polilinee, linee di costruzione, cerchi, ellissi e poligoni con numero di lati configurabile.
- Rettangoli per due angoli o dal centro, con possibilità di disegnare quadrati.
- Asola rettilinea: due centri e larghezza, con segmenti e archi tangenti vincolati; punti autonomi selezionabili, trascinabili e quotabili, esclusi dai profili. Vedi [strumenti dello schizzo](docs/sketch-tools.md).
- Archi da centro e estremi, per tre punti e tangenti a segmenti o archi.
- Spline per punti e NURBS; modifica di punti e maniglie, inserimento e rimozione dei punti di controllo.
- Opzioni per uniformare la curvatura delle spline, rilassare la curva e limitare le oscillazioni.
- Taglio, estensione, divisione, raccordo e smusso delle entità dello schizzo.
- Offset delle entità e delle catene; spostamento, rotazione e copia.
- Ripetizioni lineari e circolari e specchio delle entità dello schizzo.
- Snap alla geometria, alla griglia e all'origine; punti medi, quadranti e **Intersezione** fra curve esatte. L'intersezione resta associata a entrambe le sorgenti e viene seguita dal risolutore. I prolungamenti e le sovrapposizioni senza un incrocio isolato non creano uno snap.
- Riferimenti esterni ad assi, piani, spigoli, schizzi e curve, incluse quelle proiettate o estratte; conversione degli spigoli e sezioni dei corpi nel piano dello schizzo. Clic e passaggi del mouse restano temporanei: i riferimenti automatici sono salvati solo se usati, e ripuliti quando viene eliminato l'ultimo utilizzo, conservando vincoli, conversioni esplicite e Undo/Redo.
- **Visualizza → Mostra linee di costruzione** permette di mostrarle anche fuori dalla modifica dello schizzo; l'opzione viene conservata. Le linee di costruzione non entrano nei profili di modellazione.
- Uno schizzo nuovo chiuso senza entità completate viene scartato; punti e geometria di costruzione sono contenuti validi. Gli schizzi esistenti o usati da altre funzioni vengono conservati.
- **Schizzi 3D** con polilinee e spline definite da punti XYZ nello spazio.
- Estrazione associativa di bordi 3D, taglio delle curve 3D e proiezione di curve sulle superfici.
- Eliche e spirali utilizzabili come percorsi di modellazione.

### Quote e vincoli

- Quote di lunghezza, distanza orizzontale/verticale, angolo, raggio e diametro, anche rispetto a un asse.
- Coincidenza, orizzontalità, verticalità, parallelismo, perpendicolarità e collinearità.
- Tangenza, uguaglianza, concentricità, simmetria, punto medio, quadrante, punto sull'entità, *pierce* su superficie e geometria fissa.
- Vincoli associati a offset e ripetizioni.
- Pannello dei vincoli, gradi di libertà e indicazione dello stato completamente definito dello schizzo.
- Modifica delle quote tramite doppio clic e posizionamento grafico delle annotazioni.
- Campi numerici con espressioni: `25/2`, `3*(4+1.5)`, `sqrt(2)*10`, `360/7`. Sono disponibili operatori `+ - * / ^`, parentesi, `pi`, `sqrt`, `abs`, funzioni trigonometriche in gradi, `exp`, `ln` e `log`. Il separatore decimale può essere punto o virgola.
- Unità del documento: millimetri, centimetri, metri, pollici e piedi.

### Modellazione solida

| Operazione | Descrizione |
|---|---|
| Primitive | Parallelepipedo, cilindro, sfera, cono e toro |
| Estrusione | Costruzione da schizzo, estrusione in un verso, simmetrica o in due versi, fino al prossimo corpo e taglio del corpo |
| Rivoluzione | Geometria ottenuta ruotando un profilo attorno a un asse |
| Booleane | Unione, intersezione e differenza A − B |
| Sweep | Profilo lungo un percorso, con riferimenti a schizzi e curve/bordi 3D |
| Loft | Collegamento di sezioni con guide e controllo delle condizioni ai profili |
| Raccordo e smusso | Lavorazione degli spigoli, anche scegliendo i bordi di una faccia |
| Sformo | Inclinazione delle facce |
| Svuotamento | Creazione di un corpo cavo |
| Filettatura automatica | Catalogo ISO metrico M1–M68, passi grossi e fini comuni; passo manuale, lunghezza, mano e inversione |
| Ripetizioni e specchio | Copie lineari, circolari, speculari e riempimento automatico; variazioni dei parametri delle funzioni |
| Trasformazioni | Spostamento, rotazione e scala dei corpi |
| Piani di costruzione | Riferimenti per nuovi schizzi e operazioni |

In **Estrusione**, la fine **Fino al prossimo corpo** arresta il profilo sulle prime superfici esatte incontrate nel verso **Positivo** o **Invertito**, anche quando appartengono a più corpi. Richiede contorni chiusi solidi e un solo verso. Considera i solidi precedenti attivi e non consumati, anche nascosti manualmente; i corpi limite restano disponibili e si aggiornano con la storia. Se manca la copertura del profilo o la geometria presenta sottosquadri, ostacoli interni o superfici non certificabili, il comando segnala un errore. Non è disponibile con simmetrica, due versi o sola superficie.

La fine **Fino a una faccia o a un piano** accetta anche riferimenti a facce libere valide. Il risultato può usare la booleana di unione dell'estrusione quando geometricamente valida. Il caso Schizzo 3 / Corpo 1 di `Superfici.prt`, incluse entrambe le modalità e il verso Invertito, è documentato nel [rapporto di correzione](docs/superfici-estrusione-2026-11-02.md).

Le **Loft** solide e di superficie offrono elenchi ordinabili e riferimenti associativi:

- Sezioni e guide da schizzi, singole entità, bordi esatti e curve 3D proiettate o estratte. Le sezioni da schizzo intero escludono la costruzione. La scelta funziona nella vista e nella storyboard, anche per schizzi nascosti.
- Sezioni puntuali da entità Punto o vertici, solo al primo o all'ultimo profilo e con almeno una sezione curva. Uno schizzo con un solo punto può essere scelto interamente.
- Condizioni G0/G1/G2 ai profili, influenza e **Inverti normale** indipendente ai due estremi. **G1 — tangente a un piano** accetta piani principali, di costruzione o facce piane che contengano la sezione; le guide devono essere compatibili con la condizione.
- **Condizione trasversale** delle guide: libera, G1 normale al piano del profilo con inversione, oppure G1 tangente a una o più facce adiacenti. Le curve spaziali richiedono un piano esplicito per la modalità normale; gli appoggi devono essere incidenti e compatibili. Questi controlli sono distinti da G0/G1/G2 lungo la guida e non si applicano alla loft rigata.
- **Modifica corrispondenze fra sezioni**: punti trascinabili sui profili e posizioni percentuali lungo le curve per controllare cuciture e torsioni; aggiunta/rimozione dei collegamenti, inversione di una sezione e ripristino **Automatico**. Le corrispondenze incoerenti vengono segnalate.
- **Unisci facce tangenti** elimina i bordi fra le pezze compatibili che il kernel può concatenare senza nuovo fit. Non fonde qualsiasi coppia di facce G1; le cuciture necessarie alle superfici chiuse restano nel B-rep.
- Gli errori di attraversamento indicano guida e sezione con nome, freccia ed etichetta: guida rossa, sezione arancione.

Uso e limiti: [corrispondenze](docs/loft-corrispondenze-2026-11-04.md), [condizioni delle guide](docs/loft-controlli-guide-2026-11-04.md), [punti e piani](docs/loft-punti-piani-2026-11-05.md), [unione delle pezze](docs/loft-hook-2026-11-04.md#unione-delle-facce-tangenti).

Il catalogo della filettatura comprende 40 misure ISO a passo grosso e 22 combinazioni a passo fine. Il diametro geometrico resta quello della faccia scelta; il catalogo propone i parametri nominali.

Nei pannelli **Unione**, **Intersezione** e **Differenza**, la selezione degli strumenti parte già attiva usando il corpo A mostrato nel pannello: trascina un rettangolo per aggiungere più corpi alle spunte esistenti. Da sinistra a destra selezioni i corpi interamente inclusi; da destra a sinistra anche quelli attraversati. Un clic aggiunge/toglie un corpo; **Esc** o **Termina selezione** interrompe la selezione; **Strumenti dalla vista** la riattiva. Puoi cambiare A con il relativo selettore. I corpi scelti sono evidenziati.

**Taglia superfici → Taglia entrambi** permette di scegliere le porzioni da conservare di ciascun corpo e ricucirle in un nuovo risultato. Una lamina chiusa non viene promossa automaticamente a solido; per un volume usare la cucitura quando valida. Le booleane e i tagli lavorano sulle superfici esatte, anche NURBS, nei casi supportati dal kernel. [Esempi e limiti verificati](docs/knifehandle-taglio-reciproco-2026-11-05.md).

Il comando **Raggio** comprende quattro schede con illustrazioni: **Costante**, **Conico**, **Variabile** e **Tra facce**. I conici offrono sezioni ellittiche (ρ < 0,5), paraboliche (ρ = 0,5) e iperboliche (ρ > 0,5), controllabili tramite ρ oppure raggio di curvatura al centro della sezione. Il raggio variabile accetta più bordi: quelli tangenti formano un percorso unico con 2–64 punti equidistanti lungo la lunghezza totale. I percorsi disgiunti hanno tabelle indipendenti, visibili selezionando un bordo nell’elenco; ogni percorso ha interpolazione morbida o lineare e inversione del verso. I punti con etichetta R si trascinano nella vista 3D per cambiare i raggi e aggiornare l’anteprima; sui percorsi chiusi i raggi iniziale e finale restano uguali. Il raccordo tra facce richiede due facce adiacenti dello stesso corpo e raccorda i loro bordi condivisi. La propagazione tangente è disattivabile; nel modo variabile resta disattivata. Il pannello si può chiudere anche durante il calcolo dell'anteprima. Il supporto è stato introdotto nel formato nativo 59, mantenendo la lettura dei documenti precedenti.

I pannelli hanno un'intestazione compatta e un menu **⋮** per l'aggancio a sinistra/destra e **Usa tutta l'altezza disponibile**; restano sotto il ribbon. OK e Annulla deselezionano il comando. Nei piani di costruzione ogni riferimento, anche già definito, si rimuove con **×**. Le preferenze vengono conservate; trascinare il pannello permette di sganciarlo e riagganciarlo.

Durante una funzione, il riquadro sinistro del ribbon offre **Conferma/Annulla** o **Chiudi**; durante uno schizzo offre **Termina**. Uscendo da Estrusione o Rivoluzione mentre lo schizzo resta in modifica, il ribbon torna alla scheda **Schizzo**. Il pannello Loft parte da 440 px, con etichette vicine agli elenchi e frecce **↑/↓** compatte; rispetta le dimensioni salvate dall'utente. Le illustrazioni usano uno sfondo bianco nel tema chiaro. [Uso dei nuovi controlli](docs/ui-ribbon-loft-filettature-2026-11-05.md).

### Curva 3D da due schizzi

In **Curva proiettata** scegliere **Schizzo su schizzo (curva 3D)**, i due schizzi su piani non paralleli e le singole curve da usare. Gli elenchi e il pulsante **Scegli curva nella vista** permettono di escludere le altre entità. L’anteprima mostra la curva spaziale ottenuta dall’intersezione delle due proiezioni normali; la feature dipende da entrambi gli schizzi e si rigenera dopo le modifiche.

Il risultato usa NURBS esatte quando possibile e una curva implicita esatta negli altri casi, conservata tramite gli input nel documento nativo. Anche i rami impliciti si possono usare come riferimenti associativi in altri schizzi con **Converti spigoli**, snap, quote, PointOnCurve e Pierce, oppure come sezioni e guide di loft; sweep, conversione libera in NURBS e STEP/IGES restano indisponibili per questi rami; piani paralleli e degenerazioni non risolvibili danno errore. [Uso, geometria e verifiche](docs/curva-proiettata-due-schizzi-2026-11-02.md).

### Ripetizioni di funzione e riempimento automatico

Nelle ripetizioni lineari e circolari, **Ripeti: Una funzione** permette di variare più parametri numerici con incrementi costanti: `q(i) = q(0) + i * incremento`. Le quote degli schizzi usati direttamente dalla funzione appaiono nella viewport e si scelgono con un clic; l'elenco comprende solo i parametri della funzione e dei suoi schizzi diretti. La variazione è disabilitata per **Il corpo intero**. Le esclusioni mantengono l'indice delle istanze; non tutti i tipi di funzione o combinazioni di valori sono supportati. [Uso delle variazioni](docs/ripetizioni-variabili-2026-11-04.md).

**Funzioni → Ripetizione → Riempimento automatico…** dispone corpi o strumenti di funzioni additive/sottrattive dentro uno schizzo chiuso o una faccia piana:

- Schemi **rettangolare, sfalsato, esagonale e concentrico**, con icone di anteprima, passi e rotazione della disposizione.
- Esclusione dei fori e delle copie il cui ingombro rettangolare conservativo esce dal contorno; le copie mantengono l'orientamento della sorgente. La disposizione esagonale non cambia la forma del corpo ripetuto.
- Massimo 500 copie. Questa modalità non usa variazioni di quota o esclusioni numerate; le funzioni booleane con più strumenti non sono ammesse come sorgenti.

I risultati mantengono corpi e dipendenze nella storyboard, con modifica, rigenerazione e Undo/Redo. [Riempimento: dettagli e verifiche](docs/riempimento-sviluppo-adattamento-2026-11-05.md#riempimento-automatico).

### Modellazione di superfici

- Estrusione di profili aperti, Loft di superfici e Sweep di superfici.
- Superfici planari, rigate, tra curve e di riempimento.
- Selezione di bordi e curve per definire contorni, sezioni e guide. Attiva il campo di scelta del pannello, poi scegli nella vista o nella storyboard: clic sullo schizzo per il contorno intero, espansione per singole entità e bordi dei corpi. Si possono scegliere anche schizzi nascosti, senza mostrarli nel documento.
- Copia e rimozione di facce.
- Offset, estensione e taglio delle superfici.
- **Estendi superficie** apre subito il pannello: i bordi si aggiungono o rimuovono nella vista, con distanza, tipo, contorno completo e anteprima nello stesso pannello.
- **Estendi fino a riferimento**: un bordo libero prosegue fino a un piano o a una faccia, anche curva, entro una distanza massima di ricerca.
- **Superficie da rete di curve**: due famiglie ordinate U/V, con un solo incrocio 3D per coppia, generano celle di Coons cucite con continuità interna G0.
- Riempimenti con continuità **G0/G1/G2 per singola faccia adiacente**, ereditabile dall'impostazione comune; influenza e guida della forma restano associate alla faccia.
- **Modifica poli NURBS**: gabbia selezionabile e trascinabile nella vista, tabella XYZ e pesi; modifiche parametriche relative alla sorgente.
- Cucitura di più superfici; creazione di un solido quando chiudono un volume valido.
- Aggiunta di spessore per trasformare superfici in solidi.
- Ripetizione lineare, circolare e specchio delle superfici.

I comandi di superficie sono nel menu **Funzioni → Superfici**. Le reti accettano 2–16 curve aperte per famiglia; riordinare le curve con Su/Giù quando gli incroci non sono monotoni. Non è una superficie globale Gordon con G1/G2 fra le celle. L'estensione richiede che il riferimento attraversi tutto il prolungamento senza tagliare la superficie iniziale; aumentare la distanza di ricerca se necessario.

L'editor dei poli lavora su una o più facce NURBS dello stesso corpo, anche cucito o solido. Seleziona le facce nella lista o con **Scegli facce nella vista**: un secondo clic toglie la faccia. Le gabbie compaiono insieme; la colonna **Faccia** distingue i poli e consente spostamenti di gruppo tra griglie. Trascina sullo sfondo per ruotare, anche con più poli selezionati. Per il riquadro usa **Alt + trascinamento** oppure attiva **Selezione rettangolare**; Maiusc aggiunge, Ctrl/Cmd alterna ed Esc annulla il riquadro. Il rilascio fuori vista o la perdita di cattura chiudono il trascinamento; il cambio di approssimazione annulla l’interazione prima di ricostruire la griglia. Le facce escluse restano nel corpo. Nella modalità originale conserva gradi, nodi e rifilature UV, inclusi i fori. Con i vincoli automatici disattivati, i giunti condivisi devono restare coincidenti: sposta insieme i poli corrispondenti; una modifica che apre il giunto viene rifiutata. Il trascinamento avviene sul piano dello schermo; per il terzo asse usare la tabella o ruotare la vista. La tabella virtuale consente griglie grandi, con una gabbia di display alleggerita. **Semplifica la griglia** abilita spinner U/V comuni alle facce selezionate (4–256 poli per direzione): aggiornano automaticamente la superficie bicubica e mostrano lo scostamento campionato iniziale per faccia e il massimo delle facce selezionate. **Adatta i poli allo scostamento** aumenta automaticamente U/V in background; parte anche quando si attiva l’approssimazione o si cambia lo scostamento, se necessario. La conferma richiede il rispetto del limite scelto; cambiare U/V azzera gli spostamenti. Restano rifilature UV e fori; i bordi 3D seguono la superficie approssimata. Pesi positivi, fino a 4096 poli modificati per funzione. [Esempio Hook e limiti](docs/poli-nurbs-griglia-2026-11-09.md). Se la struttura della griglia cambia a monte, ricaricare la griglia e riselezionare i poli. I controlli di regolarità sono campionati e non certificano l'assenza globale di auto-intersezioni. Le nuove funzioni partecipano a rigenerazione, Undo/Redo, salvataggio e revisioni; il formato nativo 54 conserva i nomi topologici degli appoggi degli schizzi e dei vincoli Pierce e legge i formati precedenti.

Le **facce analitiche** (piani, cilindri, coni, sfere e tori) vengono convertite automaticamente in NURBS esatte prima della modifica. Un piano parte da una griglia 4×4; gli spinner consentono di cambiarne la densità. Restano i bordi, le rifilature e i fori, mentre il corpo sorgente rimane invariato. [Conversione e verifiche](docs/poli-nurbs-analitiche-2026-11-09.md).

**Trascina anche i poli vicini** distribuisce il trascinamento nella stessa griglia, con raggi U/V regolabili e vicini verdi. G1 conserva i giunti tangenti tra facce; su una faccia isolata non distribuisce da solo lo spostamento. Le coordinate in tabella agiscono sul singolo polo. [Uso e prove](docs/poli-nurbs-trascinamento-morbido-2026-11-10.md).


**Continuità opzionale dei poli:** attiva **Mantieni i bordi connessi (G0)** per propagare il movimento sui giunti; **Mantieni la tangenza esistente (G1)** include anche i poli della fascia tangente. Le facce NURBS adiacenti vengono incluse e i poli automatici sono verdi. **Verifica continuità** mostra apertura e scarto angolare campionati; gli spigoli netti vengono conservati. I giunti con griglie incompatibili sono protetti e segnalati. Si salvano la griglia risolta e le opzioni; i vincoli vengono ricalcolati a ogni nuovo gesto, anche sui poli già modificati. Esc ripristina la forma precedente e la riapertura conserva la stessa griglia. [Uso e limiti](docs/poli-nurbs-continuita-2026-11-09.md).

Il contorno può essere uno schizzo oppure più facce piane complanari. In **Scegli contorno nella vista** aggiungi le facce con clic successivi, poi **Termina selezione**; **Rimuovi** toglie le righe evidenziate. Le facce adiacenti formano un’area comune e le copie possono attraversare il bordo interno; fori e contorno esterno restano esclusi.

#### Taglio di solidi tra due superfici

**Funzioni → Superfici → Taglia superficie…**, con **Tipo di taglio → Tra due superfici (solidi)**, conserva tutti i volumi del corpo sorgente compresi tra due facce. Funziona anche con un corpo che contiene più solidi separati. Scegli il corpo e le due facce nella vista o nella storyboard; puoi invertire separatamente il lato mantenuto per ciascun limite. I tagli vengono richiusi sulle superfici esatte. Le facce finite devono attraversare completamente i volumi; estendile prima se necessario. La funzione conserva riferimenti associativi, modifica, anteprima e Undo/Redo (schema estensioni superficie9).

#### Distensione e appiattimento su un piano

**Funzioni → Superfici → Distendi superficie…** accetta più facce e un piano di destinazione principale o di costruzione. Scegliere le facce nella vista o nella storyboard; un secondo clic le rimuove, **Termina selezione** o **Esc** avvia l'anteprima. Posizione e rotazione sono modificabili.

Lo sviluppo mantiene lunghezze e forma intrinseca per i piani, i cilindri e i coni supportati; i coni richiedono contorni isoparametrici e le cuciture periodiche complesse possono richiedere una divisione preventiva. Le facce multiple diventano isole piane separate da 1 mm: non un unico sviluppo cucito. Le superfici a doppia curvatura vengono rifiutate; il comando non riconosce qualsiasi NURBS sviluppabile. La sorgente tridimensionale viene conservata.

In **Metodo → Appiattisci insieme di superfici** puoi appiattire anche una superficie a doppia curvatura oppure due facce tangenti dello stesso corpo con bordo comune U isoparametrico. Le facce formano una lamina piana collegata: il bordo comune resta condiviso. **Centra origine sulle superfici** suggerisce l'ancoraggio, modificabile con U/V; piano, posizione e rotazione restano selezionabili. **Esito** mostra variazioni locali campionate delle lunghezze e dell'area e lo scarto dei contorni. Questo metodo ammette deformazione e non sostituisce lo sviluppo esatto. Le superfici periodiche devono essere prima aperte. La nuova definizione richiede lo schema7; riferimenti, modifica, Undo/Redo e rigenerazione rimangono associativi.

#### Deformazione di corpi sulle superfici

**Funzioni → Superfici → Deforma corpo su superficie…** accetta più corpi solidi e/o singole facce. I corpi restano solidi separati e le facce diventano lamine; non vengono fusi automaticamente. Impostare piano sorgente, punto U/V sulla destinazione, posizione, rotazione e distanza normale. La conservazione dei corpi sorgenti è opzionale.

Per un riempimento, evidenziare uno dei risultati nell'elenco e premere **Copie**; scegliere la faccia destinazione e **Centra e allinea dalla sorgente**. Il comando recupera il piano dello schizzo quando disponibile e orienta la disposizione sulla faccia, lasciando modificabili i valori. Sul file KnifeHandle attuale sono state verificate 155 copie rigide su B15:F5 e B15:F6, con salvataggio, rigenerazione e Undo/Redo.

**Scegli superfici nella vista** consente di aggiungere più destinazioni: formano un unico appoggio e il corpo può attraversare giunzioni tangenti compatibili. Con più facce, il calcolo segue la normale all'appoggio della prima faccia e segnala vuoti, sovrapposizioni e giunzioni incompatibili; non è un avvolgimento universale di superfici ripiegate. Con una destinazione singola la mappa segue il carrier UV, senza ritaglio automatico al bordo della faccia. L'altezza locale segue la normale; può verificarsi stiramento tangenziale. Fit e controlli usano curve/superfici B-rep, senza mesh di calcolo.

Nel selettore **Metodo**, **Posiziona senza deformare** dispone ogni sorgente come corpo rigido: il centro della sua impronta si appoggia alla destinazione e l'asse normale segue la normale locale. Forma e dimensioni rimangono invariate; una base piana resta piana. **Centra e allinea dalla sorgente** usa la giunzione topologica comune quando si scelgono più facce dello stesso corpo. Il centraggio multifaccia verifica anche una piccola traslazione della disposizione, riportata nei campi X/Y; dimensioni e spaziatura delle copie restano invariate. I corpi spuntati e il piano di centraggio sono evidenziati nel viewport.

Il metodo **Piega da superfici appiattite** costruisce una corrispondenza comune fra piano e insieme di superfici, basata sulle lunghezze delle isocurve, e ne applica l'inversa alle sorgenti. Le basi vengono piegate sul carrier e lo spessore segue le normali locali. Supporta una faccia oppure due facce tangenti dello stesso corpo con giunzione U isoparametrica. Per nuove operazioni e' il metodo iniziale; le operazioni gia' salvate mantengono il precedente. Usa **Copie**, scegli le destinazioni e premi **Centra e allinea dalla sorgente**. La definizione associativa e' salvata nello schema6; il lettore aggiornato e' necessario. Non promette isometria sulle superfici a doppia curvatura. Lo sviluppo esatto resta un comando distinto. L'aderenza non implica proporzioni invariate: su KnifeHandle i bordi variano circa da -20,5% a +14,3%. Vedi il [rapporto della mappa piano/superfici](docs/confronto-flatten-quilt-creo-2026-11-06.md).

Se il riempimento usa il contorno di una funzione **Appiattisci insieme di superfici**, **Copie** riconosce la sua mappa. In **Piano / mappa sorgente** compare **Mappa di …**: piano, ordine delle facce, origine U/V, rotazione e spostamenti derivano dall’inversa dell’appiattimento e non vengono ricentrati. Vale anche per appiattimenti ruotati di 90° o traslati. Il collegamento è associativo e richiede lo schema8 delle estensioni superficie; i piani liberi e i metodi precedenti restano disponibili.

Queste operazioni conservano riferimenti associativi, modifica, anteprima, Undo/Redo e salvataggio nativo. [Uso, precisione e limiti](docs/riempimento-sviluppo-adattamento-2026-11-05.md).

### Storia parametrica e documenti

- Storyboard con schizzi, feature e corpi; selezione sincronizzata con la vista 3D.
- Modifica dei parametri delle operazioni e rigenerazione del modello.
- Spostamento delle feature nella storia nel rispetto delle dipendenze.
- Soppressione e riattivazione delle feature; cursore per lavorare in una posizione della storia.
- Selezione multipla, rinomina, visibilità e cancellazione di oggetti e feature.
- Evidenziazione delle facce di una feature o di un intero corpo.
- Annulla/ripeti per le modifiche al documento.
- Grafo delle dipendenze tramite **Visualizza → Debug history / storyboard**.
- Salvataggio delle definizioni parametriche e, facoltativamente, dei corpi già calcolati per accelerare la riapertura.
- Blocco esclusivo dei documenti aperti e salvataggio tramite file temporaneo per proteggere la scrittura.

### Tavole 2D

Viste proiettate, sezioni, dettagli e ausiliarie; viste interrotte orizzontali o verticali con anteprima e quote del modello completo. Vedi [viste interrotte](docs/drawing-breaks.md) per uso e limiti. I dettagli circolari, rettangolari o a polilinea possono essere collegati alla vista madre con una linea che segue gli spostamenti.

**Inserisci → Rugosità superficiale** aggiunge specifiche Ra/Rz in micrometri, con asportazione richiesta/vietata e processo opzionale. Simboli e richiami sono associati alla geometria e vettoriali nel PDF; proprietà, Undo e file `.drw` conservano i campi. [Uso e limiti](docs/drawing-revisions-study.md#rugosità-superficiale-strutturata--30-ottobre-2026).

Durante il primo posizionamento e il trascinamento delle quote, guide lampeggianti agganciano il centro del testo alla quota più vicina nel verso del movimento, in orizzontale e verticale. Vicino alle frecce ha priorità lo snap fra punte coincidenti, anche tra una quota angolare e una lineare con lato comune: un cerchio lampeggiante indica il punto condiviso. Alt esclude gli agganci, Esc annulla; il rilascio crea un solo Undo. [Allineamento grafico](docs/drawing-revisions-study.md#allineamento-grafico-delle-quote--30-ottobre-2026).

### Visualizzazione e analisi

ForgeCAD è scritto in **C++17** con **Qt 6** e una viewport **OpenGL 3.3 Core**. L’interfaccia usa lo stile comune Fusion e due palette dell’app, chiara e scura. In **Visualizza → Tema** si può scegliere **Chiaro**, **Scuro** oppure **Segui il desktop** (predefinito). La scelta viene salvata e aggiorna anche le finestre e i pannelli già aperti, mantenendo lo stesso disegno dei controlli tra le piattaforme. Il riconoscimento dello schema usa Qt 6.5+; sui backend precedenti dipende dalle notifiche della palette di sistema.

- Rotazione, pan, zoom e inquadratura della scena.
- Modalità filo di ferro, ombreggiata e ombreggiata con bordi.
- Visualizzazione e selezione dei bordi nascosti; identificativi topologici di facce e bordi.
- Vista in sezione con parametri modificabili.
- Griglia, piani e assi configurabili, anche in primo piano.
- Qualità della tassellazione bassa, media o alta; antialiasing MSAA.
- Illuminazione Studio, Morbida e Ispezione; sfondo, luce ambiente e colori personalizzabili.
- Trasparenza dei corpi nello schizzo e personalizzazione dei pannelli delle funzioni.
- Misura della geometria selezionata.
- Proprietà geometriche e di massa: volume, area, lunghezza, baricentro, tensore d'inerzia e momenti principali, secondo il tipo di geometria.
- Analisi di curvatura, zebra, continuità delle giunzioni e pettini di curvatura di curve e superfici.
- Analisi delle micro-geometrie.
- Tassellazione con CUDA quando compilata e disponibile; percorso CPU in assenza di CUDA.

## Formati supportati

| Formato | Apertura / importazione | Salvataggio / esportazione |
|---|---|---|
| `.prt` di ForgeCAD | Documento nativo con storia parametrica | Documento nativo |
| STEP (`.step`, `.stp`) | Importazione della geometria | AP242, AP214 e AP203 |
| IGES (`.igs`, `.iges`) | Importazione della geometria | IGES 5.3: solidi (186) e superfici (144) |
| STL | — | Mesh triangolare |
| OBJ | — | Mesh quadrangolare, per esempio per Blender |

Il formato nativo corrente è **66**: comprende variazioni dei parametri, corrispondenze delle loft, unione delle facce e condizioni G1 ai piani. Riempimento e mappature conservano definizioni e riferimenti associativi; le destinazioni multiple richiedono un lettore aggiornato. Dal formato 56 i salvataggi usano Zstandard con checksum separati per definizione e cache, senza perdere precisione geometrica o mesh. I file precedenti, inclusi quelli zlib (1–55), restano leggibili.

La cache dei corpi evita di ricostruire la storia all'apertura. In **Opzioni → Memoria per la cache all'apertura** il budget automatico usa al massimo un ottavo della RAM disponibile, fino a 4 GiB; si può scegliere un limite più basso. Una cache oltre il budget viene ignorata e i corpi vengono ricalcolati. **Nuova tavola 2D** riusa i corpi del modello aperto dopo il salvataggio, verificando che il file non sia cambiato; non ricostruisce la storia per creare il foglio vuoto.

I documenti `.prt` di ForgeCAD hanno una firma interna `FCAD`: l'estensione non indica compatibilità con i documenti di altri CAD. STEP e IGES trasferiscono la geometria senza ricreare lo storyboard parametrico originale. Le esportazioni STEP/IGES usano i corpi visibili.

## Requisiti

| Componente | Requisito |
|---|---|
| Sistema | Linux con sessione grafica oppure macOS |
| Compilatore | C++17, GCC o Clang; su macOS anche Objective-C++ e AppKit |
| CMake | 3.21 o successivo |
| Qt | Qt 6 con Widgets, OpenGL e OpenGLWidgets, inclusi i file di sviluppo |
| Grafica | Driver e contesto desktop OpenGL 3.3 Core |
| OpenCASCADE / OCCT | Facoltativo: serve solo a compilare `forgekernel_tests`, che confronta il kernel con OCCT; non serve all'applicazione |
| CUDA | Facoltativa, per la tassellazione su GPU NVIDIA |

L'applicazione e il kernel proprio non usano mai OpenCASCADE: l'eseguibile `forgecad` non lo collega e OCCT non viene distribuito con ForgeCAD. Serve solo come riferimento indipendente nei test di confronto del kernel (`kernel/occt_tests`). Se OCCT non è installato, CMake salta questi test con un avviso; per escluderli esplicitamente configurare con `-DFORGECAD_BUILD_OCCT_TESTS=OFF`.

Le istruzioni seguenti descrivono una compilazione dai sorgenti. Usare una cartella di build nuova su ogni macchina, evitando cache CMake e binari provenienti da un altro sistema.

## Scaricare i sorgenti

Da GitHub usare **Code → Download ZIP** ed estrarre il progetto, oppure clonare il repository. I comandi seguenti creano una directory locale chiamata `Linux_CAD_forFree`:

```sh
git clone https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree.git Linux_CAD_forFree
cd Linux_CAD_forFree
```

Se il repository viene rinominato o trasferito, usare l'URL del pulsante **Code** sulla sua pagina GitHub.

## Installazione su Linux

### 1. Dipendenze

**Ubuntu / Debian**, con Qt 6 disponibile nei repository:

```sh
sudo apt update
sudo apt install build-essential cmake git qt6-base-dev libqt6opengl6-dev zlib1g-dev libzstd-dev \
  libgl1-mesa-dev
# Facoltativo, solo per i test di confronto con OCCT (forgekernel_tests):
sudo apt install libocct-foundation-dev libocct-modeling-data-dev \
  libocct-modeling-algorithms-dev libocct-data-exchange-dev \
  libocct-visualization-dev
```

Su Ubuntu alcuni pacchetti si trovano nel repository **Universe**. Su Wayland aggiungere `qt6-wayland`, se disponibile per la distribuzione.

**Arch Linux / distribuzioni compatibili con pacman:**

```sh
sudo pacman -S --needed base-devel cmake git qt6-base mesa zlib zstd
# Facoltativo, solo per i test di confronto con OCCT:
sudo pacman -S --needed opencascade
```

Su Wayland aggiungere `qt6-wayland`. Per altre distribuzioni installare gli equivalenti di compilatore C++, CMake, Qt 6, OpenGL, zlib, Zstandard con i file di sviluppo (OCCT solo per i test di confronto). zlib serve alla decompressione incrementale dei documenti con controlli sui limiti di memoria.

Riferimenti: [Qt/OpenGL su Ubuntu](https://packages.ubuntu.com/jammy/libqt6opengl6-dev), [OCCT su Ubuntu](https://packages.ubuntu.com/search?keywords=libocct-data-exchange-dev&suite=noble), [Qt su Arch](https://archlinux.org/packages/extra/x86_64/qt6-base/) e [OCCT su Arch](https://archlinux.org/packages/extra/x86_64/opencascade/).

### 2. Compilazione e avvio

Dalla directory dei sorgenti:

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux --target forgecad --parallel 2
./build-linux/forgecad
```

`--parallel 2` limita il consumo di memoria; aumentare il numero su macchine con risorse sufficienti. Per aprire un documento esistente, sostituire il percorso seguente con quello del proprio file:

```sh
./build-linux/forgecad "/percorso/del/proprio/modello.prt"
```

### 3. Integrazione desktop o installazione di sistema

Lo script incluso aggiunge per l'utente corrente la voce del menu, le icone e l'associazione dei documenti ForgeCAD `.prt`, senza root:

```sh
sh packaging/linux/install-desktop-integration.sh "$PWD/build-linux/forgecad"
```

Il collegamento punta al binario compilato: dopo aver spostato la cartella ripetere l'integrazione con il nuovo percorso. Le utility `xdg-mime`, `update-mime-database` e `update-desktop-database` aggiornano le associazioni; installare i pacchetti corrispondenti della distribuzione se necessario.

In alternativa, installare binario e integrazione desktop a livello di sistema:

```sh
sudo cmake --install build-linux --prefix /usr/local
forgecad
```

Disinstallazione dell'integrazione dell'utente:

```sh
sh packaging/linux/uninstall-desktop-integration.sh
```

Disinstallazione dell'installazione di sistema con il prefisso precedente:

```sh
sudo sh packaging/linux/uninstall-desktop-integration.sh --system /usr/local
```

Ulteriori dettagli in [packaging/linux/README.md](packaging/linux/README.md).

## Installazione su macOS

### 1. Strumenti e dipendenze

Installare gli strumenti da riga di comando Apple:

```sh
xcode-select --install
```

Installare [Homebrew](https://brew.sh/) seguendo le istruzioni ufficiali, quindi:

```sh
brew install cmake qt zstd
# Facoltativo, solo per i test di confronto con OCCT:
brew install opencascade
```

Le formule ufficiali sono [Qt](https://formulae.brew.sh/formula/qt) e [OpenCASCADE](https://formulae.brew.sh/formula/opencascade). Usare compilatore e librerie della stessa architettura: arm64 su Apple Silicon oppure x86_64 su Intel.

CMake richiede anche zlib, disponibile nell'SDK installato con i Command Line Tools di Xcode.

### 2. Compilazione e avvio

Dalla directory dei sorgenti:

```sh
cmake -S . -B build-macos \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix opencascade)"
cmake --build build-macos --target forgecad --parallel 2
./build-macos/forgecad
```

`brew --prefix` risolve i percorsi delle dipendenze per Homebrew. La build macOS include l'integrazione nativa dei menu tramite AppKit. Metal accelera la valutazione B-spline/NURBS nei lotti di almeno 8.192 punti; rifilatura e triangolazione restano CPU. L’opzione «Tassellazione con Metal» abilita il percorso, con ritorno automatico alla CPU per lotti piccoli, GPU assente o dati mal condizionati.

Per aprire un documento esistente, sostituire il percorso seguente con quello del proprio file:

```sh
./build-macos/forgecad "/percorso/del/proprio/modello.prt"
```

La configurazione attuale genera un **eseguibile da avviare dal Terminale**. Non crea un bundle `.app` autonomo né un installer `.dmg`; le librerie Homebrew devono rimanere installate. Gli script di integrazione desktop della sezione Linux sono destinati a Linux.

## CUDA facoltativa

Su Linux CMake cerca automaticamente un compilatore CUDA e controlla anche `/opt/cuda/bin/nvcc`. Se lo trova compila il supporto CUDA, altrimenti usa il percorso CPU.

Servono un toolkit compatibile con il compilatore C++, il driver NVIDIA e una GPU supportata. L'opzione è in **Opzioni → Tassellazione con CUDA**. CUDA accelera la valutazione/tassellazione delle superfici, senza sostituire l'intera modellazione geometrica sulla CPU.

Su una macchina senza compilatore CUDA, i normali comandi producono una build CPU. Per escludere esplicitamente CUDA anche quando il toolkit è installato, configurare con `-DFORGECAD_ENABLE_CUDA=OFF`. Il valore predefinito `ON` rileva automaticamente il toolkit; disattivare CUDA nell’interfaccia cambia solo il percorso a runtime. Su macOS il backend Metal rimane disponibile indipendentemente da CUDA.

## Primo modello e comandi

1. Avviare ForgeCAD e scegliere **File → Nuovo**.
2. Impostare le unità da **Opzioni → Unità di misura del documento**.
3. Creare uno schizzo da **Schizzo → Nuovo schizzo**, scegliendo un piano.
4. Disegnare un rettangolo o un altro profilo chiuso, aggiungendo quote e vincoli.
5. Terminare la modalità schizzo e scegliere **Funzioni → Estrusione**.
6. Impostare la lunghezza e confermare: corpo e feature compaiono nello storyboard.
7. Riaprire lo schizzo o i parametri della feature per modificare il modello. Usare **Modifica → Rigenera tutto** quando necessario.
8. Salvare in `.prt` oppure esportare la geometria per altri programmi.

### Interazione e scorciatoie

- Clic per selezionare; `Ctrl` o `Maiusc` per aggiungere/togliere elementi alla selezione.
- Pulsante centrale trascinato, oppure `Spazio` + trascinamento sinistro, per il pan. Il tasto è personalizzabile in **Opzioni → Tasto per il pan**.
- Rotella per lo zoom; `+` e `−` per aumentarlo o ridurlo; `0` per inquadrare la scena.
- Doppio clic sullo schizzo per modificarlo; menu contestuale su feature e corpi per parametri e visibilità.
- `F2` per rinominare nell'albero, `Canc` per eliminare, `Esc` per interrompere lo strumento o chiudere il pannello attivo.

| Azione | Scorciatoia su Linux |
|---|---|
| Nuovo / Apri / Salva | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` |
| Annulla / Ripeti | `Ctrl+Z` / `Ctrl+Shift+Z` oppure `Ctrl+Y` |
| Importa STEP/IGES | `Ctrl+I` |
| Rigenera tutto | `Ctrl+B` |
| Estrusione / Rivoluzione | `Ctrl+E` / `Ctrl+R` |
| Piano di costruzione | `Ctrl+Shift+P` |
| Proprietà di massa / Misura | `Ctrl+M` / `Ctrl+Shift+M` |
| Mostra tutti gli oggetti | `Ctrl+Shift+H` |
| Vista in sezione | `Ctrl+Shift+X` |
| Quota nello schizzo | `D` |
| Taglia / Estendi / Spezza nello schizzo | `T` / `E` / `S` |
| Raccordo / Smusso nello schizzo | `R` / `M` |

Su macOS le scorciatoie standard seguono le convenzioni di Qt e del sistema. Consultare le combinazioni mostrate nei menu per ciascun comando.

## Esempi

Le schermate sopra mostrano modelli creati in ForgeCAD. I documenti CAD di esempio non sono inclusi nei sorgenti. Per provare l’applicazione senza file esterni, seguire [Primo modello e comandi](#primo-modello-e-comandi).

Se si dispone di una raccolta locale di modelli, aprire un documento con **File → Apri** e usare **Salva con nome** per sperimentare su una copia. I test che richiedono documenti reali possono usare una directory di esempi esterna; vedi [Test e sviluppo](#test-e-sviluppo).

## Test e sviluppo

La [CI GitHub Actions](docs/ci.md) compila l'applicazione completa ed esegue le
suite CTest autosufficienti su Ubuntu 26.04 e macOS 15 a ogni push e pull request.
I test viewport con documenti esterni e le prove GPU/rendering nativo restano
separati; il badge mostra il risultato complessivo del workflow.

Il target `forgecad` produce l'applicazione. Per compilare anche i test configurati ed eseguirli:

```sh
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
```

Su macOS sostituire `build-linux` con `build-macos`. Per aggiungere le prove delle interazioni del viewport:

```sh
cmake -S . -B build-linux -DFORGECAD_BUILD_VIEW_TESTS=ON
cmake --build build-linux --parallel 2
ctest --test-dir build-linux --output-on-failure
```

La stessa opzione può essere aggiunta alla configurazione macOS mantenendo i parametri Homebrew. I test del viewport registrati in CTest usano Qt offscreen; le verifiche OpenGL richiedono un ambiente grafico adatto. I test CUDA sono configurati solo quando è disponibile il compilatore CUDA.

Le prove su documenti reali richiedono i file di esempio. Se sono conservati fuori dal checkout, configurare `-DFORGECAD_EXAMPLE_DIR=/percorso/degli/esempi`: CTest usa questo percorso anche se la variabile d'ambiente omonima è diversa. Nei lanci diretti del runner si può invece impostare la variabile d'ambiente `FORGECAD_EXAMPLE_DIR`. I test salvano le copie nelle cartelle temporanee e conservano gli originali.

La mappa architetturale interattiva (`docs/architecture/index.html`, generata localmente), consultabile offline, raggruppa i file per responsabilità e mostra chiamate C++, include e riferimenti a funzione in entrambi i versi. [Guida a generazione e copertura](docs/architecture/README.md). Avviare `python3 tools/open_architecture.py` per **aggiornare la mappa a ogni apertura o ricaricamento** e consultarla nel browser. La build CMake esporta `compile_commands.json`, ma non avvia l’analisi della mappa; Python 3 e i binding libclang servono soltanto per aggiornarla. Rivedere le descrizioni curate dopo modifiche strutturali e verificare con `python3 tools/generate_architecture.py --check`, come richiesto in `AGENTS.md`.

| Percorso | Contenuto |
|---|---|
| `forgeCad2026.cpp` | Avvio e configurazione OpenGL |
| `forgeCad2026_gui.h/.cpp` | Finestra principale; sette fasi di inizializzazione per scena, menu, comandi, opzioni e toolbar |
| `forgeCad2026_viewport.h/.cpp`, `forgeCad2026_viewport_*.cpp` | Coordinatore GUI privato, con implementazioni per documento, interazione, selezione, sketch, anteprime, rendering e scena |
| `forgeCad2026_feature_preview.h/.cpp` | Chiave e calcolo anteprime su snapshot; ciclo asincrono GUI nel viewport |
| `forgeCad2026_sketch_curvature.h/.cpp` | Cache dei pettini di curvatura con stato e invalidazione propri |
| `forgeCad2026_camera.h/.cpp` | Stato camera e calcoli di vista, proiezione, raggi e orbita senza dipendenze dal widget |
| `forgeCad2026_glass_compositor.h/.cpp` | Compositore OpenGL dei fondali sfocati dei pannelli, con shader e framebuffer propri |
| `forgeCad2026_gpu_picker.h/.cpp` | Buffer del picking GPU: cache per frame, lettura degli identificativi e risorse proprie |
| `forgeCad2026_scene_renderer.h/.cpp` | Passata della scena OpenGL: fondale, framebuffer MSAA, risoluzione, opacita', linee di analisi, assi/griglia/piani di riferimento e passata datum, sezioni 3D, anteprime, corpi ed evidenziazioni di selezione/topologia |
| `cad_display_cache.h/.cpp` | Cache GPU e disegno di facce, spigoli e contorni dei display, comprese le istanze |
| `forgeCad2026_gui_common.h/.cpp` | Helper grafici condivisi da finestra e viewport |
| `forgeCad2026_storyboard.h/.cpp` | Albero storyboard, badge e cursore della storia |
| `forgeCad2026_dialogs.h`, `forgeCad2026_dialogs_*.cpp` | Dialoghi raggruppati per solidi, superfici, schizzi/curve e analisi |
| `forgeCad2026_dialogs_common.h/.cpp` | Pannelli, anteprime e selettori di riferimenti condivisi |
| `cad_*.cpp`, `cad_*.h` | Schizzi, vincoli, feature, storia, documenti, analisi e scambio file |
| `cad_fill_pattern.*`, `cad_surface_mapping.*`, `forgeCad2026_dialogs_mapping.cpp` | Riempimento, sviluppo esatto e deformazione associativa, con i relativi pannelli |
| `kernel/fk_surface_mapping.*`, `kernel/fk_body_collection.cpp` | Mappe B-rep e collezioni di solidi/lamine con genealogia esplicita |
| `kernel/` | Kernel geometrico, B-rep, curve/superfici e test numerici |
| `tests/` | Test dell'applicazione e delle interazioni |
| `packaging/` | Integrazione Linux e metadati macOS |
| `icons/` | Icone e risorse |
| `docs/` | Note tecniche e documentazione delle prestazioni |
| Directory di esempi esterna | Modelli CAD locali, non inclusi nel repository |

Le regressioni di riempimento e mappatura si eseguono con `ctest --test-dir build-linux --output-on-failure -R surface_mapping` (su macOS usare `build-macos`); il test applicativo richiede `FORGECAD_BUILD_VIEW_TESTS=ON`. Il [rapporto delle funzionalità](docs/riempimento-sviluppo-adattamento-2026-11-05.md) distingue le prove automatiche da quelle native Wayland sui documenti reali.

La [politica delle tolleranze](docs/tolerance-policy.md) definisce risoluzione, budget di connessione e approssimazione, tolleranze locali e controlli di validita'.

## Risoluzione dei problemi

**CMake non trova Qt 6 o OpenCASCADE.** Installare i pacchetti di sviluppo. Per percorsi personalizzati passare `-DCMAKE_PREFIX_PATH="/prefisso/qt;/prefisso/occt"`, oppure `Qt6_DIR` e `OpenCASCADE_DIR` con le directory contenenti i rispettivi `*Config.cmake`. Su macOS usare la configurazione Homebrew sopra.

**Cache CMake di un'altra macchina o compilatore.** Configurare una nuova cartella di build.

**Memoria insufficiente durante la compilazione.** Ridurre il parallelismo a `--parallel 1`.

**CUDA rilevata, ma configurazione o compilazione falliscono.** Verificare la compatibilità tra toolkit, compilatore e GPU; se necessario specificare un'architettura supportata con `-DCMAKE_CUDA_ARCHITECTURES=...`. Il percorso CPU viene selezionato automaticamente su una macchina senza compilatore CUDA; leggere la sezione CUDA per il comportamento del rilevamento attuale.

**Problemi grafici su Wayland.** Verificare il plugin Qt Wayland e i driver. Con XWayland disponibile, provare come diagnostica:

```sh
QT_QPA_PLATFORM=xcb ./build-linux/forgecad
```

**Problemi su un portatile Linux con GPU Intel + NVIDIA.** L'applicazione tenta di preferire NVIDIA quando rileva la configurazione EGL del driver. Per provare la GPU integrata:

```sh
FORGECAD_IGPU=1 ./build-linux/forgecad
```

**Nuovi comandi assenti dal ribbon personalizzato.** Aprirli dal menu **Funzioni → Ripetizione** o **Funzioni → Superfici**, oppure aggiungerli al proprio layout. Riaprire l'app dopo aver aggiornato l'eseguibile.

**Modello diverso dopo un aggiornamento.** Usare **Modifica → Rigenera tutto**. I documenti possono contenere corpi e mesh già calcolati; la rigenerazione aiuta a diagnosticare riferimenti mancanti e feature non valide.

**Documento già aperto.** ForgeCAD usa un file `.lock` per evitare scritture concorrenti. Chiudere l'altra istanza che lo sta usando prima di modificarlo.

Per segnalare un problema su GitHub indicare sistema operativo, architettura, versioni di Qt/CMake/OCCT, GPU, errore e passaggi per riprodurlo. Allegare un modello minimo condivisibile quando possibile.

## Limiti e licenza

Le operazioni geometriche e lo scambio STEP/IGES dipendono dalle entità supportate dal kernel proprio; non è garantita la compatibilità con ogni file o configurazione geometrica. Le istruzioni di installazione sono ricavate dalla configurazione del progetto e non certificano il funzionamento su tutte le versioni di Linux e macOS.

Il progetto è distribuito con licenza **GNU General Public License v3 (GPLv3)**. Il testo completo è nel file [LICENSE](LICENSE), identico a quello del branch `main`. Le dipendenze mantengono le rispettive licenze. OpenCASCADE Technology (OCCT), usato solo dai test di confronto del kernel e non collegato né distribuito con l'applicazione, è rilasciato con licenza GNU LGPL 2.1 con la Open CASCADE Exception.
