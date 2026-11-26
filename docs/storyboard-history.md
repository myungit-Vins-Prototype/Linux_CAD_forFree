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
- La soppressione mostra e passa alle lavorazioni dipendenti il risultato
  dello stadio precedente. Le lavorazioni indipendenti conservano il risultato.
- Eliminare una feature ricollega la base implicita allo stadio precedente. Le
  feature successive non vengono cancellate; un riferimento esplicito a una
  faccia o a un bordo scomparso produce un errore visibile nella storyboard.
- Il riordino dal menu o con trascinamento rimappa tutti gli indici, ricostruisce
  le basi implicite e viene accettato solo se ogni dipendenza resta precedente
  alla feature che la usa. Gli schizzi si spostano come passi autonomi;
  le righe informative restano figlie della propria lavorazione. La feature
  iniziale di un corpo non puo' superare gli altri stadi del corpo. Dopo lo
  spostamento si invalidano gli stadi riordinati, quelli con operandi cambiati
  e i loro dipendenti. La maniglia resta al passo scelto e le lavorazioni
  coinvolte oltre la maniglia si aggiornano quando vengono raggiunte.
- Schizzi, datum e lavorazioni compaiono nella sequenza di calcolo. I corpi
  logici hanno un gruppo separato per consultare i risultati.

## Cache delle lavorazioni soppresse

La soppressione conserva l'ultimo B-rep valido per i reticoli, le booleane,
le primitive, la scala, i raccordi, il guscio, l'eliminazione di facce,
l'estensione, l'offset, la cucitura e lo spessore con direzione automatica.
La scena usa lo stadio di passaggio mentre la lavorazione è soppressa.

Con **Opzioni → Salva nel documento i corpi calcolati**, il `.prt` contiene
anche i risultati conservati che corrispondono ancora alla definizione.
La riattivazione riusa il B-rep se parametri, precisione e risultati
degli operandi corrispondono. Se un operando è stato rigenerato o la
definizione è cambiata, la lavorazione viene calcolata. Si aggiornano le
dipendenze entro il cursore. Gli stadi oltre il cursore vengono calcolati
quando diventano necessari.

La cache versione 19 aggiunge i risultati delle lavorazioni soppresse
alla definizione FCAD76. La versione 20 registra i raccordi e gli smussi
calcolati nell'ordine della storia. All'apertura di un documento di lavoro
con una cache precedente, ForgeCAD rigenera le lavorazioni che potevano
essere state invertite e i relativi dipendenti.
La versione 21 aggiorna anche i raccordi e gli smussi che potevano
lasciare materiale dove lo spigolo raggiunge un bordo cilindrico già
arrotondato. Al salvataggio successivo si conserva il rifilo corretto.
Il lettore riusa l'identità del B-rep condiviso con lo stadio precedente
quando legge due record di passaggio identici. Questo conserva la
genealogia codificata nel salvataggio.

Su una copia di Applicatore4 con 19 lavorazioni, la riapertura del file
salvato con il TPMS soppresso ha ripristinato la cache. La riattivazione
ha riusato lo stesso B-rep di 5.113 facce in entrambe le prove. I tempi
totali sono stati di 7,17 e 8,07 s, in gran parte per ricostruire
Rivoluzione 19 e Unione 20. Passano entrambi i test CTest mirati,
`forgecad_document_read_tests` e `forgecad_history_suppression_tests`.
Coprono anche gli operandi o parametri modificati, i rami indipendenti
e la lettura dei record condivisi delle cache precedenti.

## Schizzi come passi della storia

Ogni schizzo, anche non utilizzato, conserva la propria posizione e un ID
persistente. Creare uno schizzo lo inserisce al cursore; usarlo in una nuova
lavorazione non ne cambia la posizione. Il cursore conta schizzi e lavorazioni
separatamente. Durante la modifica si vedono gli stadi precedenti alla posizione
propria dello schizzo.

Se si conferma un'estrusione o una rivoluzione mentre lo schizzo è aperto,
il risultato resta visibile con l'opacità dei solidi nello schizzo per la
sessione corrente. Un'unione sostituisce lo stadio di base, senza sovrapporlo
al risultato. Le altre lavorazioni future restano escluse; il risultato
appena creato non diventa un riferimento futuro per lo schizzo. Uscendo e
riaprendo lo schizzo si torna al suo contesto storico. Questa visibilità è
temporanea e non viene salvata nel documento.

Dal menu dello schizzo, **Cambia piano di riferimento...** permette di scegliere
un piano di origine, un datum o una faccia piana di una lavorazione precedente.
Geometria e quote conservano le coordinate locali sul nuovo piano; gli
utilizzatori vengono rigenerati. Datum e facce restano associativi: quando cambia
il produttore, lo schizzo segue il piano risolto. Un appoggio perso produce una
diagnostica e impedisce di rigenerare gli utilizzatori con una posizione obsoleta.
I vecchi schizzi creati su facce, privi di un riferimento persistente, conservano
il loro frame fisso finche' non si sceglie esplicitamente un nuovo appoggio.

Il riordino rifiuta uno schizzo dopo un suo utilizzatore o prima di un suo
appoggio/riferimento Pierce. Cambio piano e riordino sono operazioni Undo/Redo;
la posizione viene conservata nel file. Alla migrazione dei vecchi documenti gli
schizzi sono collocati davanti al primo utilizzatore; quelli liberi alla fine.

## Posizione di calcolo

Nell'albero **Storia delle feature**, la barra azzurra **Inserisci qui** separa
gli stadi attivi da quelli successivi, mostrati attenuati e in corsivo.
Trascinare la barra tra le feature per scegliere il punto di calcolo e
inserimento. Con la barra selezionata, le frecce su/giù spostano di un passo;
Home porta all'inizio, End alla fine. A posizione zero nessun corpo è mostrato;
alla fine sono mostrati i risultati completi.

La conferma di una modifica conserva la posizione scelta per **Inserisci qui**.
Si ricalcolano la funzione modificata e gli stadi dipendenti fino alla maniglia,
compresi gli schizzi associativi. Gli stadi indipendenti conservano la geometria
già calcolata. Con la maniglia subito dopo la funzione si vede subito la modifica;
con la maniglia in fondo si vede il risultato finale aggiornato.

Avanzando di un passo si calcolano gli stadi in attesa necessari per quel
passo, compresi gli schizzi associativi. Tornando indietro e avanzando di
nuovo si riusano i risultati. **End** raggiunge il risultato finale.
Aprire esplicitamente una funzione futura prepara i suoi operandi per l’editor.

La verifica del percorso di modifica dall’albero usa una base e tre scale
concatenate. Aprendo la prima scala dal doppio clic e confermando un fattore
diverso, la maniglia conserva rispettivamente le posizioni 4/4, 3/4 e 2/4.
In ciascun caso il volume dello stadio visibile corrisponde al valore atteso e
i successori oltre la maniglia restano in attesa.

Nella prova precedente alla rimozione dello spostamento automatico, il 21 novembre 2026, modificare la base di una catena di
30 scale ha richiesto 0,66 ms con arresto alla base e 4,10 ms con ricalcolo
fino in fondo (mediane di tre conferme in build Release con viewport offscreen).
Il primo avanzamento ha richiesto 0,48 ms. A fine storia i volumi coincidevano.
La verifica integrata ha superato 34 test mirati, inclusi i rami indipendenti,
i piani di appoggio, le estrusioni fino al prossimo corpo, Undo/Redo, le tavole
collegate e il recupero dopo salvataggio. Nel recupero una breve attesa sul
thread di lavoro permette a una scansione concorrente di rilasciare il lock
prima della pulizia del backup.

Una nuova funzione creata durante il rollback viene inserita alla posizione
scelta. Le basi implicite dello stesso corpo vengono ricollegate; gli stadi
successivi interessati vengono calcolati quando si raggiunge la loro posizione. I riferimenti
espliciti che non sono più risolvibili producono il normale errore della
feature. Per vedere il risultato completo dopo l'inserimento, avanzare alla
fine. Anche quando una funzione gia' inserita diventa una fusione o una
sottrazione, il suo risultato entra nella catena del corpo e diventa la base
implicita delle fusioni successive. Le preferenze di visibilita' dei corpi
restano indipendenti dai tip futuri nascosti durante il rollback.
All'apertura, una fusione salvata su una base che scavalca uno stadio dello
stesso corpo viene ricongiunta e le cache dipendenti sono rigenerate, anche
attraverso gli schizzi associativi. Bersagli di altri corpi e riferimenti
espliciti a facce, bordi o assi conservano il proprio produttore; riferimenti
mancanti restano errori espliciti. Il file originale cambia solo al salvataggio.
Il salvataggio conserva tutte le definizioni e il documento si riapre a fine
storia. Salvare i corpi calcolati, creare una tavola o effettuare un check-in
richiede il modello completo e mantiene la maniglia nella posizione scelta.
Il salvataggio delle sole definizioni aggiorna gli eventuali schizzi associativi
in attesa. Undo/Redo ripristinano anche la posizione dello stadio registrato.

Dal menu **Visualizza → Barra di posizione storyboard** si sceglie tra
**Orizzontale nell'albero**, **Verticale a sinistra** e **Verticale a destra**.
La preferenza viene conservata per le aperture successive. Cambiare modalità
non sposta la posizione di calcolo. Nelle modalità verticali la maniglia occupa
una corsia accanto alle feature; il suo colore segue la palette del tema.
La dicitura *Inserisci qui* resta nel punto della storia selezionato.
La maniglia è compatta; un pallino sulla guida identifica ogni feature visibile.
Cliccando sul pallino si porta il calcolo subito dopo quella feature. La guida e tutti i pallini
usano lo stesso azzurro della scritta *Inserisci qui*, così restano visibili
anche con palette poco contrastate. I pallini attivi sono pieni, quelli
successivi sono vuoti; la maniglia continua a seguire il colore del tema.
La guida è interrotta con un piccolo spazio prima e dopo ogni pallino, per
separarlo visivamente; i bordi dei segmenti sfumano nell'azzurro trasparente.



## File e compatibilita'

Il formato `.prt` 51 aggiunge identita' e posizione autonome degli schizzi e
l'appoggio associativo su faccia. Il formato 20 salva gli identificatori, la soppressione, i corpi logici
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

## Rigenerazione incrementale

Eliminazione, modifica dei parametri e soppressione di gruppi conservano i
risultati precedenti validi e i rami indipendenti. Si aggiornano i consumatori
coinvolti e la loro cascata; modificare un datum aggiorna solo gli schizzi e
le lavorazioni che ne dipendono. Eliminando uno stadio, la catena del corpo
si ricollega al precedente prima di propagare l'invalidazione per ID persistenti.
Riferimenti geometrici rimasti senza sorgente continuano a segnalare l'errore.
Undo/Redo conservano le istantanee. Il comando esplicito di rigenerazione
completa mantiene il proprio comportamento.
[Verifiche, misure e limiti](rigenerazione-incrementale-2026-11-06.md).

## Lavoro successivo

Aggiornamento del 30 ottobre 2026: la proposta originaria di un porting separato
è superata dall'unificazione su `main`. Compilazione e test Linux/macOS sono
verificati in CI, con backend CPU e acceleratori CUDA/Metal opzionali nello
stesso albero dei sorgenti. Stato e prove: [CI](ci.md) e
[rapporto di unificazione](unificazione-main-2026-10-30.json).

Restano successivi il porting Windows, la preparazione dei relativi pacchetti
e le verifiche native del backend grafico su quella piattaforma. Le altre
priorità funzionali e prestazionali sono raccolte nella [roadmap generale](roadmap.md).
