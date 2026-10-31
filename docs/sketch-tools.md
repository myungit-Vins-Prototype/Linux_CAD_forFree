# Strumenti e riferimenti dello schizzo

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


## Riferimenti associativi al modello

Durante il disegno gli snap possono usare direttamente estremi, vertici, punti
medi, centri, quadranti e punti sulle curve delle parti e degli schizzi visibili
che precedono lo schizzo attivo nella storia. Sono disponibili anche le
inferenze di parallelo e perpendicolare ai bordi esterni. Non occorre avviare
Converti prima di disegnare: il collegamento si crea alla conferma del punto.
Il semplice passaggio del mouse non modifica il documento.

Per una quota scegli il punto o il bordo esterno con lo strumento **Quota**,
insieme all'entità dello schizzo da quotare, quindi posiziona la quota. Per
coincidenza, tangenza, parallelismo e gli altri vincoli, seleziona le entità
normalmente; **Ctrl + clic** sceglie un punto. Piani e assi visibili sono
riferimenti utilizzabili anche dall'albero. L'origine del modello viene
proiettata correttamente anche su schizzi con sistema locale traslato.

Gli attraversamenti degli spigoli con il piano dello schizzo forniscono punti
**Pierce** esatti. Resta disponibile **Pierce su superficie** per vincolare un
punto alla sezione di una faccia. **Converti** e **Sezione** conservano ora
il collegamento rispettivamente alla geometria scelta e al corpo sezionato.

I riferimenti usati restano interni: non aggiungono tratti, curve o punti
ausiliari visibili nello schizzo, anche quando riguardano piani, assi e origine.
La geometria originale resta visibile secondo le sue impostazioni. I riferimenti
seguono le modifiche della sorgente e del piano dello schizzo, rimangono costanti
per il risolutore e conservano quote e vincoli locali, senza un vincolo **Fisso**.
La costruzione disegnata dall'utente e la geometria convertita esplicitamente
in un profilo restano visibili. Si modificano dalla sorgente. Salvataggio, riapertura e Undo/Redo
conservano il collegamento. Un vertice coincidente inizialmente con l'origine
conserva il collegamento alla parte, evitando un secondo blocco sull'origine.

Un riferimento deve precedere lo schizzo nella storia. La cancellazione
della sorgente, una sorgente non disponibile o un cambio di topologia che
non permette di conservare le entità vincolate producono un errore esplicito
e impediscono di rigenerare le lavorazioni dipendenti usando una copia vecchia.
Il grafo della storia mostra anche le dipendenze dei riferimenti associativi.

Il formato `.prt` 57 conserva sorgenti persistenti e cache geometriche; i
formati precedenti restano leggibili. Le vecchie copie già salvate con
**Fisso** non contengono la loro sorgente: per renderle associative occorre
ricrearle scegliendo nuovamente la geometria nel modello.
