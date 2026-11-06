# KnifeHandle — unione del manico con i rilievi

## Riproduttore e stato

Input congelato: `forgecad-release/knifehandle-union-pattern-qa/input-4b1c257e.prt`,
48.112.493 byte, SHA256
`4b1c257eca1ad9c43bdccd5cebb0974fe8979c35f5921a916e57ae90a0df668a`.
L’originale in Documenti non è stato modificato.

Il documento contiene 188 funzioni. Gli operandi confermati dall’utente sono:

- **Solido cucito 16**, indice 15: manico, 12 facce.
- **Deforma corpo su superficie**, indice 187: 168 solidi, 1.344 facce.

La prova kernel completa riesce in **130,907 s**, producendo **1.188 facce e
un solo solido connesso** (due regioni includendo l’esterno); `checkBody` PASS.
L’esecuzione riferita dall’utente era stata interrotta dopo oltre dieci minuti:
non è disponibile un suo tempo finale per calcolare un rapporto esatto.

L’eseguibile `forgecad-release/forgecad` è stato compilato con le correzioni.
La prova nativa Wayland dell’applicazione riesce in **155,432 s**, includendo
creazione della funzione, preparazione grafica e controllo B-rep/loop.
Caricamento iniziale: 6,156 s. Visibilità degli operandi consumati,
Undo/Redo, identità delle cache precedenti e salvataggio/rilettura PASS.
Copia verificata: `forgecad-release/knifehandle-union-pattern-qa/KnifeHandle-manico-e-rilievi-uniti.prt`,
63.915.355 byte, 189 funzioni; nessuna riscrittura del file originale.
SHA256 risultato: `91a3569d92352c669aa476b81626a1c39677a54dca82dcf152bd194fb0c18297`.
Riapertura della copia nell’app in 6,529 s; controllate tre viste native
Wayland e salvate le catture `viewport-unione-vista-0/1/2.png` nella QA.

## Cause e correzioni

### Inviluppi e costo delle proiezioni

Due facce del manico hanno ciascuna 208×1.615 poli NURBS. Anche le piccole
facce di appoggio dei rilievi possono condividere questi carrier. Il vecchio
box passava una finestra alla conversione NURBS, ma la conversione di una
B-spline già esistente restituiva l’intero carrier: molti confronti lontani
non venivano esclusi.

`fk_classify.cpp` costruisce il box del ritaglio con inviluppi delle SP-curve,
gerarchie Bezier e suddivisioni conservative delle pezze parziali.
Con curve UV generiche o mancanti conserva l’intero carrier; non deduce
esclusioni da un box campionato. `fk_intersect.cpp` limita esattamente i
box delle B-spline agli intervalli richiesti.

`fk_marching.cpp` riusa le pezze Bezier immutabili, i loro box e i parametri
noti dei semi. La verifica di coincidenza usa testimoni di distanza e
costruisce i tagli soltanto quando è dimostrata una zona coincidente di
area. Un contatto lungo una curva non basta. `fk_surface_algo.cpp` cerca
prima un buon minimo e verifica comunque i bordi, escludendo solo quelli
il cui inviluppo dimostra che non possono migliorare il candidato.
La memoria osservata nelle sonde passa da circa 5,2 GB a circa 1 GB;
non è una misura del consumo totale della GUI.

### Tracciamento e ritaglio degli archi

Un nodo corretto poteva convergere su una radice arretrata e alternare due
punti distanti circa 6e-5 mm fino al limite delle iterazioni. Il marcher
verifica l’avanzamento, riduce il passo e riprova. I nodi lisci C1 non
forzano arresti; spigoli reali e limiti del dominio mantengono i vincoli.
Un limite di dominio non viene più assorbito da un semplice nodo interno.
Il fit conserva la propria soglia originale.

In `curveOnSurface`, la media finale della bisezione poteva arrotondarsi
appena oltre la soglia: osservato 1,000000000936e-6 mm su 1e-6 mm. Si conserva
l’estremo interno già verificato e si ricontrollano gli agganci a nodi e
isocurve. I candidati lontani sono esclusi con un inviluppo conservativo.

Dopo la suddivisione globale, `collectCuts` riclassifica ogni sottoarco
nei due contorni: Outside è scartato, Boundary non crea un taglio interno,
Inside lo conserva. La classificazione del punto medio dell’arco intero
non era valida per tutti i sottointervalli; rimanevano rami esterni e
copie di bordi nel grafo. Le immagini UV note sono usate solo dopo la
verifica della distanza fisica; resta disponibile il classificatore 3D.

### Bordo condiviso e costo dei contorni

In assemblaggio, due rappresentazioni già abbinate dello stesso bordo
possono rispettare in modo diverso le facce incidenti. Se la prima curva
non soddisfa la nuova faccia, si considera la curva sorgente dell’altro
uso; si sostituisce soltanto se rispetta tutte le facce entro il budget
esistente. Versi e SP-curve vengono aggiornati. Non viene creato un nuovo fit.

Gli estremi dei tratti sono memorizzati per evitare milioni di valutazioni
NURBS ripetute. L’indice `fk_cycle_point_index.h` sostituisce i confronti
quadratici dei vertici dei cicli UV nella scelta del punto di prova dei
fori. Restituisce lo stesso minimo discreto, inclusi esclusione del ciclo
e traslazioni periodiche: nessuna nuova poligonizzazione geometrica.
Gli errori riportano ora le facce e gli operandi coinvolti.

## Verifiche

- Unione completa: 168 rilievi, 1.188 facce, una regione solida, `checkBody` PASS.
- Casi minimi estratti dalla copia: rilievo 2 (4,276 s), 14 (2,6 s),
  21 (4,152 s), 75 (3,729 s); ciascuno produce 19 facce valide.
- **38/38 Boolean**, **10/10 Marching**, **44/44 Surface** PASS.
- **5/5 CTest app**: genealogia delle booleane, cache della storia, mappature,
  riempimenti e sorgenti geometriche PASS.
- **4/4 CTest kernel**: box dei ritagli, cuciture booleane, semi del marcher,
  unioni disgiunte PASS.
- Il nuovo test copre box razionali/polinomiali, picco UV fra campioni,
  SP-curve generiche/mancanti, minimi sul dominio ristretto, 48 estremi
  entro la soglia originale e 840 minimi UV identici al confronto completo.
  La prova della bisezione fallisce con il codice precedente.
- Architettura aggiornata: 615 file, 8.679 relazioni, 297 unità analizzate,
  zero diagnostiche; `--check`, vista generale e otto dettagli HTML PASS.
  Controllati entranti/uscenti e distinzione fra chiamate/include/riferimenti.

I riproduttori e i risultati CAD sono nella directory QA ignorata da Git.
`result-all.fkb` contiene il risultato completo della sonda kernel;
le prove GUI salvano un nuovo documento, senza scrivere l’originale.

## Tolleranze e limiti

Nessun CSG su mesh, nessun incremento delle tolleranze geometriche.
Una proposta intermedia di sommare budget nel fit preliminare è stata
respinta dal controllo automatico e non applicata. La correzione effettiva
usa l’estremo interno della bisezione e le curve sorgenti compatibili.

La validazione attesta questo modello e i test eseguiti; non costituisce
una soluzione universale per qualsiasi intersezione NURBS. Il costo resta
significativo, dato il numero di rilievi e la complessità dei carrier.
