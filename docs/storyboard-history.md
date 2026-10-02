# Storyboard delle feature

Il modello distingue ora i corpi logici dalle feature che ne producono gli
stati successivi. `ExtrusionObject` resta l'unita' di calcolo e conserva il
B-rep intermedio, ma possiede un `featureId` persistente e un `modelBodyId`.
`ModelBody` contiene nome, visibilita' e feature finale (`tipFeatureId`).

## Regole della cronologia

- Soltanto il tip non soppresso di ogni corpo viene disegnato. I B-rep degli
  stadi precedenti restano disponibili per modifica, rollback e rigenerazione.
- Una nuova feature che modifica un corpo eredita il suo `modelBodyId`.
  Primitive, importazioni, rivoluzioni, loft e operazioni senza fusione iniziano
  invece un nuovo corpo. Datum ed eliche restano geometria di riferimento.
- Le booleane appartengono al corpo dell'operando A. I corpi strumento rimangono
  corpi logici distinti e vengono soltanto nascosti mentre sono consumati.
- La soppressione rende la feature un passaggio trasparente: il suo risultato e'
  quello dello stadio precedente e le feature successive vengono rigenerate.
- Eliminare una feature ricollega la base implicita allo stadio precedente. Le
  feature successive non vengono cancellate; un riferimento esplicito a una
  faccia o a un bordo scomparso produce un errore visibile nella storyboard.
- Il riordino dal menu o con trascinamento rimappa tutti gli indici, ricostruisce
  le basi implicite e viene accettato solo se ogni dipendenza resta precedente
  alla feature che la usa. Nel trascinamento anche schizzi e righe informative
  vengono ricondotti alla feature piu' vicina dello stesso corpo. La feature
  iniziale non puo' essere spostata.
- La geometria di riferimento precede i corpi nell'albero; datum ed eliche non
  partecipano al riordino delle feature dei corpi.

## File e compatibilita'

Il formato `.prt` 20 salva gli identificatori, la soppressione, i corpi logici
e i riferimenti persistenti alle sotto-entita' B-rep. Il formato 19 introduceva
la storyboard. I documenti fino al formato 19 vengono migrati automaticamente seguendo gli
operandi gia' presenti: raccordi, smussi, scale, trasformazioni, booleane e
fusioni ereditano il corpo della loro base. La migrazione non modifica il file
finche' l'utente non lo salva.

I riferimenti a vertici, spigoli e facce conservano l'ID persistente della
feature proprietaria, l'ID della sotto-entita', il tipo geometrico e una firma
del suo contesto topologico. L'ID e' il percorso normale; se una ricostruzione
rinumera il B-rep, tipo, facce adiacenti o numero dei bordi e punto selezionato
permettono di ritrovare l'entita'. I file precedenti mantengono il punto come
fallback e acquistano gli ID al salvataggio successivo.

## Diagnostica grafica

**Visualizza -> Debug history / storyboard...** apre una finestra non modale e
ridimensionabile con il grafo parametrico del documento corrente. Ogni corsia
rappresenta un corpo logico; geometria di riferimento e schizzi hanno corsie
distinte. Le frecce mostrano profili, operandi booleani, basi dei raccordi,
percorsi sweep, sezioni e guide loft, fusioni e riferimenti di datum, pattern e
trasformazioni.

I riferimenti mancanti, rivolti a una feature successiva o non piu' risolvibili
sono rossi e tratteggiati. Le feature fallite sono rosse, quelle soppresse
grigie e il risultato corrente di ogni corpo verde. Selezionando un blocco si
ottengono gli ID persistenti, lo stato, il messaggio esatto del kernel, il
conteggio topologico B-rep, le sotto-entita' selezionate e tutte le dipendenze.
Per uno schizzo il dettaglio elenca segmenti, curve e ogni vincolo con i suoi
riferimenti e il residuo numerico. La selezione nel grafo seleziona anche
l'elemento corrispondente nel viewport. Ogni blocco riporta esplicitamente il
nome della feature e il tipo di lavorazione. Una legenda spiega i colori di
risultati, stadi intermedi, riferimenti, schizzi, errori e feature soppresse.
La rotella e i pulsanti `−`/`+` cambiano lo zoom, mentre *Adatta leggibile*
adatta il grafo senza scendere sotto la dimensione minima che rende leggibili
i testi. Nelle storyboard larghe la vista si apre sulla parte iniziale e il
resto si raggiunge trascinando o con la barra orizzontale. La finestra e'
ridimensionabile e conserva
dimensione e posizione. Il grafo si aggiorna quando cambia il documento senza
azzerare lo zoom; schizzi ed etichette delle frecce si possono nascondere per
leggere modelli grandi.

I blocchi possono essere trascinati liberamente; i collegamenti, le frecce e le
etichette seguono lo spostamento. Le etichette delle dipendenze sono nascoste
all'apertura e compaiono passando sulla relativa freccia, oppure tutte insieme
con *Mostra tutte le etichette*. *Ripristina disposizione* elimina gli
spostamenti manuali. Il contenuto testuale resta ritagliato nel proprio blocco,
cosi' nomi lunghi o errori non coprono i nodi adiacenti.
La ricostruzione richiesta da una modifica del documento viene rimandata fino
al rilascio del mouse: il nodo che Qt sta trascinando non viene eliminato nel
mezzo dell'evento. La scena elimina inoltre prima i collegamenti e poi i nodi,
evitando puntatori residui durante aggiornamenti e chiusura.

## Limiti attuali

- Se una modifica elimina davvero la faccia o il bordo e non esiste una
  sotto-entita' con la stessa firma topologica, la feature rimane in errore e
  deve essere ridefinita. Il riferimento non viene assegnato a una feature
  diversa soltanto perche' ne ha riutilizzato l'indice.
- Lo spostamento tra due corpi diversi non e' implicito. Richiedera' un comando
  separato che scelga il nuovo corpo bersaglio e rimappi i riferimenti.
- La cronologia Undo/Redo continua a usare istantanee complete del documento;
  ogni soppressione, eliminazione o riordino e' un singolo passo.

## Lavoro successivo

Dopo la stabilizzazione della storyboard, il porting va gestito in un branch
separato. Le prime attivita' sono una toolchain CMake senza percorsi Linux,
backend grafico Qt/OpenGL verificato su Windows e macOS ARM64, sostituzione o
isolamento delle dipendenze specifiche di sistema, pacchetti Qt/OpenCASCADE per
x64 e arm64, CI multipiattaforma e mantenimento del backend CPU quando CUDA non
e' disponibile.
