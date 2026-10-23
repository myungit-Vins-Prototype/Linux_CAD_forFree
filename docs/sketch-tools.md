# Asola e punto nello schizzo

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
