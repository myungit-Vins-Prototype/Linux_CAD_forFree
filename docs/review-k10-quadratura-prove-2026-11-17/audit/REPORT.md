# K10 quadratura scalar/vector: audit indipendente

Stato: finding statici con aritmetica Python riprodotta; nessun C++ compilato o eseguito, nessuna ricorsione patologica avviata, nessuna modifica al repository. Gli oracoli non costituiscono certificazione della quadratura adattiva.

## Fonte e confini

`kernel/fk_quadrature.h`, SHA256 `30964c986ea45e022115223d60ae6277373d844ecced2d75b97a3ecd9b345310` (copia inerte allegata). Consultati AGENTS/CLAUDE e descrizione/relazioni call/include/reference entranti e uscenti del grafo; dettagli e hash in graph-consult.json. I template non espongono necessariamente tutte le chiamate risolte: fk_curve_algo.cpp compare come include, la cache privata come call+include. Chiamanti reali comprendono lunghezze/cache, area di profilo, massa su curve/facce e prove. Entrambi i percorsi scalar e vector condividono i rischi sotto descritti.

## Finding concreti

1. Integrando **finito** costante F=2^1023 su [0,2^-1023]: integrale esatto Fraction=1. `left+right` trabocca a Inf; K e G diventano Inf e l'errore `(K-G)*half` diventa NaN. Anche sostituire solo la coppia con `w*left+w*right` non basta: la somma non scalata dei pesi tende a 2*F e trabocca comunque. L'oracolo Python riproduce entrambe le strade e una regola scalata che dà 1.

2. I confronti adattivi con errore NaN non accettano il pannello. Nel vettoriale NaN rende anche nearRoundoff=false; a depth30 accepted diventa true senza controllo di finitezza. Nel caso costante sopra ogni pannello continua a traboccare: possibile albero completo di 2^31-1 pannelli, 15 valutazioni ciascuno. Questo è un conteggio statico potenziale, non un runtime misurato da questo audit.

3. Estremi finiti a=1e308,b=1.1e308: `.5*(a+b)` è Inf, mentre `.5*a+.5*b` è finito e interno. Estremi a=-2^1023,b=2^1023: `.5*(b-a)` è Inf, mentre `.5*b-.5*a` è finito. Il primo caso può valutare il callback a un parametro non finito pur avendo estremi validi; il secondo rompe la mappa del pannello prima di qualsiasi errore dell'integrando.

4. Finitezza del valore finale non basta: absolute/magnitude/error non finiti possono indebolire il criterio adattivo o creare l'albero NaN; vanno controllati prima dei confronti, del ritorno a profondità massima e dell'uscita noise. Le somme dei figli possono traboccare dopo pannelli individualmente finiti.

## Fix minimo proposto, non implementato

- Conservare le formule ordinarie e l'ordine di valutazione/somma quando tutti gli intermedi sono rappresentabili; nessun nuovo contratto su tolerance/relativeTolerance/roundoff (inclusi 0/negativi/NaN/Inf), nessun cap/tolleranza aggiunta.
- Calcolare center/half con la formula attuale e usare `.5*a+.5*b` / `.5*b-.5*a` solo quando l'intermedio ordinario trabocca. Validare parametri campionati finiti e nell'intervallo chiuso orientato. Non vietare automaticamente intervalli invertiti: il half con segno rappresenta l'integrale orientato.
- Controllare finitezza dei valori callback; per vector anche magnitude. Controllare K/G, risultato, errore, absolute/magnitude prima dei confronti adattivi e controllare somme dei figli. Se il pannello non è valutabile, lanciare domain_error anziché ricorrere/accettare NaN o Inf, anche con tolerance infinita. È il più piccolo rimedio conservativo; può rifiutare il caso con integrale finito 1.
- Se si vuole recuperare proprio il caso F=2^1023, serve un fallback dell'**intero pannello**, non la sola pair-sum. Memorizzare i 15 campioni evita richiamare un callback stateful; il percorso ordinario può mantenere le proprie operazioni. In fallback scalare valori e grandezze con una potenza di due, accumulare pesi nello spazio scalato e ripristinare insieme alla scala di half tramite frexp/scalbn, evitando il prodotto intermedio troppo grande. Lo stesso trattamento serve a K-G e alle grandezze.
- Una normalizzazione cieca con max|f| può cancellare campioni molto piccoli ma non nulli, rilevanti dopo cancellazioni. Verificare la reversibilità della scalatura dei campioni (o rifiutare conservativamente il pannello inconcluso); non trasformare la perdita di rappresentabilità in zero riuscito.
- Se adaptive richiede bisezione ma middle non è strettamente interno, terminare con errore conservativo; nessuna ricorsione sui medesimi estremi. Distinguere questo da un pannello già accettato. Per estremi distinti separati da 2^-1074, half può arrotondare a zero: non dichiarare per questo integrale esattamente zero; rifiuto o mappa scalata separatamente progettata.

## Limiti e attribuzioni

Python verifica identità Fraction e singoli pannelli floating point soltanto. Nessun finding CAD ordinario o contratto globale su picchi non sondati, cancellazioni, stima d'errore o convergenza. La correzione precedente locale della norma in arcLength/cache non protegge gli accumuli della quadratura: il nuovo esempio usa una velocità già finita. Un rifiuto conservativo risolve sicurezza/terminazione; un fallback scalato ben verificato recupera più integrali rappresentabili ma non rende certificata la stima Gauss-Kronrod.

## Revisione candidata finale

Peer review indipendente del candidato SHA256 `048c8007849e807c2721a20e90387c855a2076ba3e56bcc90cf55320e3a804b7`: **nessun blocker statico nel perimetro autorizzato**. Letta anche TU test SHA256 `88f9718de56f1488f24e0aa637c03f563d67868894437fc6af9f18c4f6f60e29`, senza compilarla o eseguirla.

Il codice mantiene le operazioni ordinarie e memorizza i 15 campioni per il recupero. Parametri e campioni non finiti sono rifiutati; nel vettoriale magnitude è validata e recuperata separatamente. Tutti gli output del pannello risultano finiti prima delle decisioni adattive; adaptivePanel controlla anche un Panel ricevuto direttamente prima del caso depth30. Somma errori nel ramo noise e somme value/magnitude dei figli sono controllate prima delle uscite. Sono presenti il fallback di center/half, il rifiuto half=0 per estremi distinti e il rifiuto della bisezione stagnante soltanto quando il pannello non è già accettato. Tolleranze e roundoff conservano la semantica precedente.

Differenza rispetto alla proposta preliminare: il fallback effettivo divide per il massimo finito dei campioni, non necessariamente potenza di due, e rifiuta la perdita completa di un campione normalizzato non nullo. Non dichiariamo quindi una scalatura esatta o un enclosure intervallare. Il successivo frexp/scalbn evita overflow nei prodotti intermedi di ripristino. L'underflow finito dei prodotti pesati rimane un limite esplicito, non risolto né nascosto dal presente delta. Nessuna garanzia su tutte le scale o sull'errore globale.

Runtime C++, risultati e confronti bitwise sono di ROOT/tester e non vengono inferiti da questa lettura. Il dettaglio della checklist è in candidate-static-review.json; la fonte candidata è allegata come testo inerte. Nessun manifest pesante richiesto o prodotto.
