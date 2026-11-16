# K10 — contorni nell'appiattimento di una faccia piana, 15 novembre 2026

Prosecuzione autorizzata con tre agenti GPT-6.1 Sol: implementazione, regressioni e audit indipendente. Il blocco affronta un ulteriore punto di chiamata campionato, `SurfaceQuiltMap::flattenSheet`, dopo il [mapping obliquo](curve-fit-k10-diagonal-2026-11-15.md).

## Difetto riprodotto

La baseline congelata accetta una faccia piana con bordo polinomiale di grado 27, ma il contorno appiattito perde una variazione fra i campioni del fitter: errore geometrico minimo rispetto a tutti i bordi risultanti **5,742475e-5 mm**, deviazione dichiarata circa **2,476558e-10 mm**. Tolleranza pubblica 1e-6, budget locale 2e-8. La variante razionale a pesi positivi mostra **5,416219e-5 mm**, contro circa **2,894104e-10 mm** dichiarati. Sorgente e risultato superano il controllo topologico; la fonte resta immutata. L'esito positivo dei controlli campionati non garantiva la conservazione della geometria.

## Correzione e ambito

Il percorso nuovo riconosce strutturalmente **una sola faccia Plane**. La mappa affine viene condivisa fra appiattimento, inversione e calcolo delle metriche. I contorni Line/NURBS supportati e i relativi trim conservano il parametro originale; i coefficienti UV sono trasformati direttamente in una B-spline 3D esplicita, senza un fit campionato della forma. Il limite continuo include l'arrotondamento della trasformazione, senza dichiarare automaticamente errore zero.

Le rappresentazioni sul piano finale mantengono grado, nodi e pesi delle spline esplicite; i poli vengono proiettati e verificati nel piano finale. Sono attivi controlli su domini, dati finiti, budget, cancellazione, estremi e pubblicazione atomica di statistiche e genealogia. Le soglie di accettazione non sono aumentate.

__IMPLEMENTATION__

__VERIFICATION__

## Stato e limiti

Questo blocco tratta una famiglia di un nuovo punto iniziale: **9/24 siti parzialmente trattati, 15 interamente da migrare**, oltre ai fallback dei siti già affrontati. K10 resta aperto. Appiattimenti di due facce, supporti curvi e altre famiglie non riconosciute mantengono il percorso precedente; le relative misure campionate non sono certificati globali. Restano aperti anche mapping tensoriali diagonali e offset normali significativi del blocco precedente.

Il totale dei **24 rilievi precedenti corretti** non aumenta. **K2 è in produzione**; restano **22 candidate orfane** e l'uniformazione completa di anteprime, pannelli e stili. Non si afferma assenza globale di leak o difetti; nessuna nuova prova OpenGL in questo intervento. Le [prove e il manifest](curve-fit-k10-flatten-prove-2026-11-15/README.md) distinguono baseline, risultati finali e verifiche mirate.
