# Strumenti e riferimenti dello schizzo

## Polilinea, cerchio e arco per tre punti

**Polilinea** sostituisce **Linea** in menu, ribbon e toolbar: ogni clic dopo il primo
aggiunge un segmento collegato e prosegue dal suo estremo. Esc termina il disegno;
resta disponibile la linea di costruzione. I vecchi comandi personalizzati Linea
attivano la Polilinea. I valori dei tipi nei documenti esistenti non cambiano.

**Cerchio per tre punti**, accanto al cerchio da centro e raggio, usa tre punti
distinti sulla circonferenza. Dopo il secondo punto mostra l'anteprima; se il terzo
e' allineato ai primi due, resta in attesa di un punto valido. Il risultato e' un
cerchio analitico ordinario, quotabile ed estrudibile; Undo/Redo agiscono sull'intero
cerchio. Il centro calcolato non riceve vincoli automatici.

L'anteprima di **Arco per tre punti** non mostra il segmento di raggio ne' la misura R;
restano angolo e lunghezza dell'arco. Le quote di raggio aggiunte esplicitamente
alle geometrie restano disponibili.


I comandi **Asola** e **Punto** si trovano in **Schizzo → Strumento geometrico**,
nella scheda Schizzo della ribbon e nella toolbar di disegno.

## Asola rettilinea

1. Scegli il centro della prima estremità.
2. Scegli il centro della seconda: la loro distanza definisce l'interasse e la direzione.
3. Sposta il cursore lateralmente e clicca per scegliere la larghezza.

La larghezza è due volte la distanza perpendicolare del terzo punto dalla
retta dei centri; la lunghezza totale è interasse più larghezza. L'anteprima
mostra queste misure. Centri coincidenti e larghezza nulla non creano entità.
Esc annulla l'asola in corso.

La conferma crea due segmenti e due semicirconferenze esatte, con coincidenze,
tangenze e raggi uguali. Un solo Undo annulla tutta l'asola. La forma può
essere ruotata liberamente, usata come profilo di estrusione e modificata con
gli strumenti di quota e vincolo già presenti: una quota di raggio su un arco
regola metà della larghezza; i centri degli archi permettono di quotare
l'interasse. Le entità restano modificabili singolarmente.

## Punto autonomo

Ogni clic inserisce un punto nel piano dello schizzo, usando gli snap correnti.
Selezione permette di trascinarlo; i comandi di quota e vincolo agiscono sul
punto, anche rispetto all'origine o ad altre entità. Un punto libero ha due
gradi di libertà. Il punto viene mostrato come una piccola croce e non
partecipa ai contorni né alle estrusioni.

Il formato `.prt` 52 conserva i punti autonomi. I formati precedenti restano
leggibili; le versioni dell'app che leggono al massimo il formato 51
rifiutano i nuovi file, evitando di perdere i punti. L'asola è salvata come
segmenti e archi ordinari, senza un nuovo tipo di profilo nel kernel.


## Quote della geometria di costruzione

Per quotare la lunghezza di una linea di costruzione, attivare **Quota**,
cliccare sul tratto e poi nel vuoto per posizionare la quota. Restano
possibili le quote fra i due estremi e gli orientamenti orizzontale,
verticale o allineato. Una linea coincidente con un piano standard o un
asse, anche passante per l'origine, mantiene la precedenza sui riferimenti
automatici nascosti. Le geometrie convertite associative si modificano
invece dalla sorgente; possono essere riferimenti per le quote delle
entità locali.


## Normali dei piani

La freccia **N** mostra la normale durante la creazione o modifica del piano.
**Visualizza → Normali dei piani al passaggio del mouse** abilita o disabilita
la freccia in hover sui piani esistenti. La scelta viene ricordata; la normale
nell'anteprima di definizione resta disponibile anche con l'opzione disattivata.


## Riferimenti associativi al modello

Durante il disegno gli snap possono usare direttamente estremi, vertici, punti
medi, centri, quadranti e punti sulle curve delle parti e degli schizzi visibili
che precedono lo schizzo attivo nella storia. Sono disponibili anche le
inferenze di parallelo e perpendicolare ai bordi esterni. Non occorre avviare
Converti prima di disegnare: il collegamento si crea alla conferma del punto.
Il passaggio del mouse e la semplice selezione non modificano il documento. I riferimenti scelti restano temporanei finché non sono usati da geometria o vincoli confermati; Escape e annullamento non aggiungono Undo. Quando viene eliminato l’ultimo utilizzo, la cache automatica viene rimossa. Le conversioni esplicite e i riferimenti usati da altre funzioni vengono conservati.

Per una quota scegli il punto o il bordo esterno con lo strumento **Quota**,
insieme all'entità dello schizzo da quotare, quindi posiziona la quota. Per
coincidenza, tangenza, parallelismo e gli altri vincoli, seleziona le entità
normalmente; **Ctrl + clic** sceglie un punto. Piani e assi visibili sono
riferimenti utilizzabili anche dall'albero. L'origine del modello viene
proiettata correttamente anche su schizzi con sistema locale traslato.

I suggerimenti associativi compaiono soltanto durante un comando attivo:
Polilinea, Spline, Cerchio e gli altri strumenti mostrano la sorgente dello
snap; **Quota** mostra la proiezione del bordo o la sezione della faccia con
il piano dello schizzo. In **Selezione** il solo passaggio del mouse non
propone né evidenzia riferimenti associativi. Se hai già scelto un punto o un elemento
locale, elenca i vincoli compatibili. Il clic mantiene la selezione locale e
sceglie un solo riferimento; applica poi il vincolo dal pannello **Vincoli**.
**Ctrl + clic** permette anche di scegliere il centro di un cerchio. Le entità
locali visibili hanno priorità quando sono sovrapposte alla geometria esterna.
Il riferimento scelto viene evidenziato; il passaggio del mouse non crea
vincoli. Se la faccia non produce una sezione utilizzabile, il badge ne indica
la ragione. La scelta esplicita è disponibile anche con gli snap disattivati.

Sulle parti raggiate puoi usare anche il **contorno apparente**: il bordo estremo
visibile della superficie curva, anche dove non esiste uno spigolo reale. Si
proietta sul piano dello schizzo lungo la normale del piano, anche quando
il piano è esterno al corpo. Non richiede un'intersezione tra piano e faccia.
In Polilinea e negli altri comandi di disegno passa sul bordo estremo per
agganciarlo; in **Quota** scegli l'entità locale e il contorno proiettato.
Il badge riporta **Contorno apparente della faccia F… di …**, con la sorgente.
Il collegamento segue le variazioni della faccia, compreso il raggio, e usa
un solo riferimento per la stessa faccia e lo stesso ramo. Il formato `.prt`
**69** conserva questa distinzione; le versioni precedenti restano leggibili.
Sono supportati cilindri, coni, sfere e i meridiani dei tori in vista laterale;
le superfici libere e i contorni toroidali obliqui richiedono un'estensione.

Gli attraversamenti degli spigoli con il piano dello schizzo forniscono punti
**Pierce** esatti. Resta disponibile **Pierce su superficie** per vincolare un
punto alla sezione di una faccia. **Converti** e **Sezione** conservano ora
il collegamento rispettivamente alla geometria scelta e al corpo sezionato.

I riferimenti usati restano interni: non aggiungono tratti, curve o punti
ausiliari visibili nello schizzo, anche quando riguardano piani, assi e origine.
La geometria originale resta visibile secondo le sue impostazioni. I riferimenti
seguono le modifiche della sorgente e del piano dello schizzo, rimangono costanti
per il risolutore e conservano quote e vincoli locali, senza un vincolo **Fisso**.
La costruzione disegnata dall'utente e le geometrie ottenute con **Converti
spigoli e curve** e **Sezione** restano visibili, anche con l'opzione **Di
costruzione** attiva, mentre lo schizzo è in modifica. Fuori dalla modifica puoi mostrarla o nasconderla con **Visualizza → Mostra linee di costruzione**.
La scelta viene ricordata tra le sessioni e riguarda gli schizzi già visibili. Con l'opzione spenta
la costruzione è esclusa anche da selezione e inquadratura; durante la modifica resta visibile.
Le cache associative automatiche rimangono nascoste. Il comando cambia soltanto la visualizzazione,
senza modificare geometria, vincoli, Undo o il documento salvato. La costruzione non fa parte delle sezioni loft: selezionando lo schizzo intero viene usato soltanto il profilo. La visibilità delle conversioni è distinta dai riferimenti
automatici ed è salvata nel formato nativo 60. Si modificano dalla sorgente;
salvataggio, riapertura e Undo/Redo conservano collegamento e visibilità.
Per recuperare una conversione di costruzione nascosta salvata nei formati
57–59, selezionare nuovamente la stessa sorgente con **Converti**: il riferimento
esistente torna visibile senza duplicare la geometria o perdere i vincoli. Un vertice coincidente inizialmente con l'origine
segue la scelta del badge: con snap Origine attivo usa soltanto l'origine locale;
con Origine disattivato conserva il collegamento alla parte.

Il badge indica la sorgente e il ruolo del riferimento (per esempio centro,
quadrante o punto medio), con nome della funzione/schizzo e identificatore
dell'entita'. Alla conferma si assegna un solo aggancio al punto mostrato:
l'origine locale ha precedenza su centri ed estremi sovrapposti. Per scegliere
esplicitamente una sorgente sovrapposta all'origine si puo' disattivare lo snap
Origine. Uno snap **Intersezione** conserva invece le due curve necessarie.

Se una sorgente viene eliminata o diventa irrisolvibile, l'errore elenca il nome
conservato della sorgente e i numeri/tipi dei vincoli coinvolti. La storyboard
segnala lo schizzo e mostra il dettaglio nel suggerimento; nel pannello Vincoli
le voci interessate sono segnate **ROTTO**, distinguibili da quelle sane. Si
possono selezionare ed eliminare soltanto quei vincoli per ricrearli. Il formato
`.prt` **68** conserva i nomi anche dopo salvataggio e riapertura; legge i
formati precedenti. Per riferimenti gia' eliminati in file precedenti senza
nome conservato, resta disponibile l'identita' persistente della sorgente.

Un riferimento deve precedere lo schizzo nella storia. La cancellazione
della sorgente, una sorgente non disponibile o un cambio di topologia che
non permette di conservare le entità vincolate producono un errore esplicito
e impediscono di rigenerare le lavorazioni dipendenti usando una copia vecchia.
Il grafo della storia mostra anche le dipendenze dei riferimenti associativi.

Il formato `.prt` 57 conserva sorgenti persistenti e cache geometriche; i
formati precedenti restano leggibili. Le vecchie copie già salvate con
**Fisso** non contengono la loro sorgente: per renderle associative occorre
ricrearle scegliendo nuovamente la geometria nel modello.
