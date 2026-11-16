# K10 — pareti di Spessore su facce piane, 15 novembre 2026

Prosecuzione con tre agenti GPT-6.1 Sol, dopo l'[appiattimento piano](curve-fit-k10-flatten-2026-11-15.md). Il blocco riguarda i due siti iniziali `fitCurve` che ricostruiscono le pareti dello spessore lungo la normale. La costruzione diretta copre i bordi supportati appartenenti a una faccia strutturalmente `Plane`; K10 resta aperto per le altre famiglie.

## Difetto riprodotto

Una lamina piana regolare con bordo NURBS polinomiale di grado 27 viene ispessita con successo, ma la parete generata perde il rilievo tra i campioni: errore geometrico **5,742475e-5 mm**. Con pesi positivi variabili l'errore è **5,416219e-5 mm**. Il fit delle pareti usa inizialmente **1e-9 mm**; tolleranza pubblica **1e-6 mm**, tolleranza offset esplicita **1e-7 mm**. Il controllo del corpo non segnala errori e la sorgente resta immutata.

Il difetto riguarda la parete, anche quando i bordi dei due coperchi conservano il dettaglio. L'audit verifica la distanza dal testimone a tutti i supporti del risultato e prova tutti e tre i lati dello spessore. La medesima geometria con spessore lungo una direzione viene già conservata dalla costruzione esatta esistente. I riproduttori e i byte iniziali sono congelati prima dell'integrazione.

## Correzione e contratto

Per una faccia Plane, `faceNormalAt` restituisce una normale orientata costante. Le due curve sono quindi traslazioni dell'originale: `C(t) + s0*n` e `C(t) + s1*n`. Il nuovo ramo riusa `transformCurve` e `ExtrusionSurface`, già impiegati nello spessore lungo una direzione. Conserva la curva originale, il suo parametro, grado, nodi, pesi e wrapper supportati; non ricostruisce una cubica a partire da campioni. Il parametro V dell'estrusione finale è una coordinata fisica lungo la direzione normalizzata.

Coperchi ottenuti da `offsetFaces`, controlli delle pieghe, ordine della cucitura e trasporto della genealogia restano nel percorso normale. La costruzione avviene localmente per la parete piana: non instrada l'intera operazione sul percorso direzionale. Nessun aumento di tolleranza o modifica del formato persistente.

Il vecchio carrier tensoriale di grado 3 e senza pesi è coerente con le cubiche polinomiali prodotte dal fitter. Usarlo con i coefficienti NURBS originali avrebbe perso grado/pesi; il nuovo ramo evita questa ricostruzione e usa direttamente il supporto di estrusione. Non si conta tale rischio evitato come ulteriore difetto corretto.

__DETAILS__

## Verifiche finali

__VERIFICATION__

## Stato e limiti

**11/24 siti iniziali parzialmente trattati, 13 interamente da migrare**, oltre ai fallback nei siti già affrontati. Questo blocco copre due nuovi siti limitatamente alle pareti di facce Plane. Pareti con normale variabile e altre famiglie mantengono il percorso precedente campionato. I controlli locali sulla geometria e sulle pieghe non costituiscono certificazione globale di regolarità o assenza di autointersezioni.

Si tratta di conservazione della rappresentazione originale tramite trasformazioni analitiche, con i normali arrotondamenti in double. Non è stato aggiunto un nuovo certificatore outward per l'intera pipeline di trasformazione, proiezione e cucitura. Le misure dense e i confronti indipendenti documentano regressioni concrete; non diventano una prova globale dei calcoli a coordinate estreme.

L'API `thickenSheet` conserva il comportamento preesistente che azzera la genealogia all'ingresso. Non viene dichiarato un nuovo contratto di atomicità su errore/cancellazione. I **24 rilievi precedenti corretti** e le **22 funzioni candidate orfane** mantengono il conteggio; K2 resta in produzione. Uniformazione di anteprime, pannelli e stili ancora aperta. Nessuna nuova prova visiva OpenGL o certificazione globale dell'assenza di leak.

[Prove riproducibili e manifest](curve-fit-k10-thicken-prove-2026-11-15/README.md).
