# K10 — pareti di Spessore su facce piane, 15 novembre 2026

Prosecuzione con tre agenti GPT-6.1 Sol, dopo l'[appiattimento piano](curve-fit-k10-flatten-2026-11-15.md). Il blocco riguarda i due siti iniziali `fitCurve` che ricostruiscono le pareti dello spessore lungo la normale. La costruzione diretta copre i bordi supportati appartenenti a una faccia strutturalmente `Plane`; K10 resta aperto per le altre famiglie.

## Difetto riprodotto

Una lamina piana regolare con bordo NURBS polinomiale di grado 27 viene ispessita con successo, ma la parete generata perde il rilievo tra i campioni: errore geometrico **5,742475e-5 mm**. Con pesi positivi variabili l'errore è **5,416219e-5 mm**. Il fit delle pareti usa inizialmente **1e-9 mm**; tolleranza pubblica **1e-6 mm**, tolleranza offset esplicita **1e-7 mm**. Il controllo del corpo non segnala errori e la sorgente resta immutata.

Il difetto riguarda la parete, anche quando i bordi dei due coperchi conservano il dettaglio. L'audit verifica la distanza dal testimone a tutti i supporti del risultato e prova tutti e tre i lati dello spessore. La medesima geometria con spessore lungo una direzione viene già conservata dalla costruzione esatta esistente. I riproduttori e i byte iniziali sono congelati prima dell'integrazione.

## Correzione e contratto

Per una faccia Plane, `faceNormalAt` restituisce una normale orientata costante. Le due curve sono quindi traslazioni dell'originale: `C(t) + s0*n` e `C(t) + s1*n`. Il nuovo ramo riusa `transformCurve` e `ExtrusionSurface`, già impiegati nello spessore lungo una direzione. Conserva la curva originale, il suo parametro, grado, nodi, pesi e wrapper supportati; non ricostruisce una cubica a partire da campioni. Il parametro V dell'estrusione finale è una coordinata fisica lungo la direzione normalizzata.

Coperchi ottenuti da `offsetFaces`, controlli delle pieghe, ordine della cucitura e trasporto della genealogia restano nel percorso normale. La costruzione avviene localmente per la parete piana: non instrada l'intera operazione sul percorso direzionale. Nessun aumento di tolleranza o modifica del formato persistente.

Il vecchio carrier tensoriale di grado 3 e senza pesi è coerente con le cubiche polinomiali prodotte dal fitter. Usarlo con i coefficienti NURBS originali avrebbe perso grado/pesi; il nuovo ramo evita questa ricostruzione e usa direttamente il supporto di estrusione. Non si conta tale rischio evitato come ulteriore difetto corretto.

Il riconoscimento riguarda la faccia di ciascun bordo libero, quindi vale anche nelle lamine con più facce piane che soddisfano i controlli preesistenti sulle giunzioni. Il ramo attraversa i wrapper Trimmed/Transformed per riconoscere le famiglie gestite; una base `Other` mantiene il comportamento precedente. Le coniche native usano la loro rappresentazione analitica e mantengono il parametro angolare. Sono presenti punti di cancellazione nell'ispezione dei wrapper. I normali arrotondamenti della trasformazione e della normalizzazione interna della direzione del carrier rimangono espliciti nei limiti sotto.

## Verifiche finali

- Build completa Release di applicazione, runner e nuovo target: PASS. 22 avvisi in file invariati in questo intervento; nessuno nel modulo o nel test nuovi/modificati qui. Eseguibile `forgecad-release/forgecad` aggiornato: riaprire l'applicazione per usarlo.
- **6/6 nuovi gruppi, 31 scenari PASS**: 6 polinomiali/razionali nei tre lati, 12 pose/scale/sensi, 4 trim annidati/inversi/istanze/fori con codec, 2 controlli direzionali, 5 input invalidi/cancellazione col reset storico della genealogia, 2 curve chiuse NURBS/conica. Geometria della parete associata al bordo testato e SP-curve effettive di ogni suo fin verificate densamente entro 1e-9 mm, oltre alla conservazione di grado/nodi/pesi. La baseline passa 2/6 gruppi: quattro si fermano sul requisito di conservazione della rappresentazione; la prova geometrica del difetto è nei due riproduttori separati. I confronti densi sono prove di regressione.
- Riproduttori finali: errore nello stesso parametro **0**; distanza geometrica dalla parete **2,361894e-16 mm** (polinomiale) e **1,481673e-17 mm** (razionale). Sorgente immutata, risultato solido, controllo del corpo senza errori, origine genealogica del bordo preservata. Prima: circa 5,74e-5/5,42e-5 mm accettati. Nessuna dichiarazione di errore matematico nullo.
- Test precedenti ricompilati privatamente sugli header correnti: **6/6 Spessore e 38/38 genealogia PASS**. Il report dell'implementatore distingue questi eseguibili privati dagli oggetti generici precedenti con ABI datata.
- **18/18 suite CTest PASS**, comprese Spessore e strumenti superfici Qt, storia, pannelli, ciclo di vita, offset e controlli kernel. Impostazioni isolate e Qt offscreen; nessuna nuova prova visiva OpenGL.
- **ASan/UBSan/LSan: 6/6 gruppi PASS**, senza diagnostiche. Strumentati `fk_thicken.cpp` e il nuovo test; main e resto dell'archivio kernel sono Release. Verifica mirata, non copertura integrale dell'applicazione.
- **37 controlli indipendenti finali PASS**, con replay sugli stessi byte integrati e hash nel manifest. Dettagli, esclusioni e confronto delle famiglie procedurali sono nell'[audit](curve-fit-k10-thicken-prove-2026-11-15/independent/audit.md).
- Architettura: **756 file / 10923 archi / 376 unità C++**, diagnostica vuota, `--check`, uguaglianza HTML/JSON e audit bidirezionale di chiamate/include/riferimenti PASS.
- Conservati byte per byte **2365 dei 2372 file preesistenti**; i 7 modificati appartengono allo scope dichiarato. Hash sorgenti/binari, link kernel senza Qt/OCCT, `git diff --check`, memoria entro 16 KiB e manifest delle prove verificati nell'audit finale.

## Stato e limiti

**11/24 siti iniziali parzialmente trattati, 13 interamente da migrare**, oltre ai fallback nei siti già affrontati. Questo blocco copre due nuovi siti limitatamente alle pareti di facce Plane. Pareti con normale variabile e altre famiglie mantengono il percorso precedente campionato. I controlli locali sulla geometria e sulle pieghe non costituiscono certificazione globale di regolarità o assenza di autointersezioni.

Si tratta di conservazione della rappresentazione originale tramite trasformazioni analitiche, con i normali arrotondamenti in double. Non è stato aggiunto un nuovo certificatore outward per l'intera pipeline di trasformazione, proiezione e cucitura. Le misure dense e i confronti indipendenti documentano regressioni concrete; non diventano una prova globale dei calcoli a coordinate estreme.

L'API `thickenSheet` conserva il comportamento preesistente che azzera la genealogia all'ingresso. Non viene dichiarato un nuovo contratto di atomicità su errore/cancellazione. I **24 rilievi precedenti corretti** e le **22 funzioni candidate orfane** mantengono il conteggio; K2 resta in produzione. Uniformazione di anteprime, pannelli e stili ancora aperta. Nessuna nuova prova visiva OpenGL o certificazione globale dell'assenza di leak.

[Prove riproducibili e manifest](curve-fit-k10-thicken-prove-2026-11-15/README.md).


## Prosecuzione successiva

Il [blocco offset dello schizzo](curve-fit-k10-sketch-offset-2026-11-16.md) tratta due ulteriori siti per le NURBS riconosciute, con verifica continua prima e dopo il rifilo. Conteggio aggiornato: 13/24 siti parzialmente trattati, 11 interamente da migrare, oltre ai fallback. I risultati sopra descrivono la consegna precedente di Spessore.
