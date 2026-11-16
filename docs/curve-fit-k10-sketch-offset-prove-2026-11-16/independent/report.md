# Audit indipendente K10 — offset dello schizzo, 16 novembre 2026

Audit isolato dei due siti Free in `cad_sketch_offset.cpp`. Nessuna modifica a produzione, CMake, documentazione condivisa o build condivisa da questo agente. Letti `AGENTS.md`, `CLAUDE.md` e responsabilità/archi in `docs/architecture/graph.json`; la selezione consultata è in `graph-consult.json`. Gli aggiornamenti condivisi della memoria e della mappa spettano al coordinatore.

## Esito statico

Nessun rilievo bloccante sul candidato `62bc7824975f38656f0bbba2279e9bc1f47ca57380c6b5468d1170eba18ad69f`, poi integrato identico. Il provider parte dai coefficienti originali della B-spline 2D, includendo pesi e dominio dei wrapper Trimmed. L'embed con `Frame3()` esegue soltanto moltiplicazioni per 0/1 e somme di zero: conserva le coordinate IEEE finite (a parte il segno dello zero, irrilevante geometricamente), i nodi e i pesi. Non certifica una ricostruzione campionata della sorgente. Il normale del provider è `T × Z`, verso destra: la distanza negativa di `(forward ? 1 : -1)*s` coincide con `f` che usa la sinistra del verso di catena.

I dati NURBS riconosciuti ma inconclusivi, i pesi non positivi, i budget esauriti e la cancellazione propagano l'errore: nessun ritorno al fit storico per questi casi. Il fit senza loop restituisce il medesimo oggetto certificato; `fitCurveCertified` inserisce direttamente i quattro poli effettivamente controllati. La conversione a 2D e la pubblicazione Converted copiano x/y/nodi double; `curveGeometry` e il ramo Converted di ricalcolo non rifittano i coefficienti.

Quando `trimOffsetLoops` rimuove intervalli, i domini originali delle Bezier retained sono mantenuti in ordine. `joinBezierPieces` riporta il pezzo k su [k,k+1] e può sostituire il suo primo polo con l'ultimo del precedente. Il nuovo controllo usa proprio i quattro poli finali stride 3, contro il dominio originale del pezzo corrispondente. La mappa normalizzata tra i domini è affine; non aggiunge aritmetica ai poli. Ogni eventuale errore di estrazione, di taglio o del giunto è quindi incluso nel controllo sull'offset originale. La soglia finale resta esattamente `1e-7`, con rifiuto per risultato non finito, negativo o eccessivo.

L'estremo superiore di ogni Piece continua a usare `evaluateLeft` rispetto al suo dominio originario. Il provider sui coefficienti esclude lo span successivo quando la query termina esattamente sul nodo. Il comportamento è stato provato su entrambi i lati di un nodo C0 e in entrambi i versi.

## Riproduttore pubblico e prova geometrica

`public.cpp` chiama `offsetSketchEntities` senza intercettazioni, costruendo una Converted BSP27 dai coefficienti hidden di `fixtures.h`. La variante razionale usa pesi non costanti `1+.5*i/27`, ascisse omogenee di `t+.5*t²` e ordinate `.01*z27`; i poli sono quelli divisi per il peso. Ampiezze 1, .1, .01 e .001, entrambi i versi: sedici scenari finali, tutti obbligati a riuscire; il runner restituisce errore se una nuova esecuzione finale rifiuta o supera `1e-7` nelle query diagnostiche.

Nella baseline l'ampiezza .01 è accettata con soli quattro poli, ma la geometria perde oltre `5.74247e-7` nella variante polinomiale e oltre `5.41621e-7` nella razionale, per entrambi i versi. Le ampiezze 1/.1 erano rifiutate dal controllo storico di collasso; .001 è un controllo sotto soglia.

`proof.py` legge i coefficienti e i pesi ORIGINALI realmente esportati dal runner, interpretati come razionali esatti dei rispettivi double IEEE, e non si affida a `projectPoint`. La positività dei coefficienti Bernstein di `Hx' W - Hx W'` dimostra x' > 0 su tutto il dominio: lower ~1 per la polinomiale e > .4444 per la razionale. Sessantaquattro suddivisioni de Casteljau esatte limitano le derivate razionali e la curvatura: per ampiezza .01, |d κ| < 7.16e-6 e < .00014086 rispettivamente. Anche i due offset sono quindi regolari e monotoni in x: il controesempio non deriva da cuspidi, collassi o auto-intersezioni.

Per un punto dell'offset matematico, l'ordinata è racchiusa con Fraction e radice quadrata intervallata tramite radice intera a scala 10^60. L'intera B-spline finale appartiene all'involucro globale delle ordinate dei suoi poli. La separazione lungo y fornisce dunque un lower bound della distanza dal punto a OGNI punto del risultato: non una semplice differenza nello stesso parametro, né una proiezione numerica, né un upper bound ricavato da soli campioni. I lower bound esatti e i dati dei witness sono in `proof.json`.

## Verifiche dirette

`direct.cpp` include il modulo selezionato a compilazione, senza alterarlo, e controlla dodici condizioni: estremi sui due lati del nodo C0 e nei due versi (quattro), oracle positivo, polo iniziale alterato respinto, verso opposto respinto, campata sorgente di ~1e-11, Trimmed con dominio originale, peso negativo riconosciuto con errore, provenienza/poli effettivi dopo loop e rifiuto postjoin oltre la soglia.

Il loop a papillon conserva i domini originali [0,.5], [2.5,3], [3,4] e li pubblica su [0,3]. Il probe verifica ciascuno dei dodici poli ricevuti dal callback contro quelli effettivi del risultato; un callback che segnala `2e-7` deve far rifiutare la ricomposizione.

Replay finale sui sorgenti integrati: sedici scenari pubblici e dodici controlli diretti, con `-fno-fast-math -ffp-contract=off`. Risultati identici al candidato. La massima distanza delle query diagnostiche è ~3.02e-8; la garanzia continua viene dai provider e dalla struttura controllata, non da questa misura campionata. Il kernel statico usato è identico alla copia baseline congelata (hash nel manifest); nessuna nuova build condivisa eseguita dall'audit.

## Limiti espliciti

Il certificato riguarda la distanza parametrica dell'approssimazione sulle famiglie NURBS supportate e, dopo rimozione dei loop, sui soli intervalli conservati con la loro provenienza. Non dimostra che il classificatore dei loop trovi tutte le auto-intersezioni, che i tratti rimossi fossero globalmente quelli desiderati, o che il controllo storico di collasso con 33 proiezioni sia globale. Ellissi e famiglie non supportate conservano il percorso storico: nessuna nuova certificazione continua attribuita a loro. L'inventario complessivo K10 resta parziale: 13/24 siti coperti parzialmente, 11 restanti più fallback. Nessuna verifica UI o renderer svolta da questo audit.

## Ripetizione

Dalla radice del repository, usare `python3 <stage>/build.py before`, quindi eseguire `public-before <stage>` per esportare input/output baseline e lanciare `python3 <stage>/proof.py`. Usare `python3 <stage>/build.py final`, eseguire `public-final`, poi `python3 <stage>/build-direct.py final` ed eseguire `direct`. Le build producono esclusivamente file nello stage. `FORGECAD_REPO` può indicare una diversa radice del repository. Manifest e hash usano percorsi dei sorgenti relativi al repository.
