# K10 — contorni nell'appiattimento di una faccia piana, 15 novembre 2026

Prosecuzione autorizzata con tre agenti GPT-6.1 Sol: implementazione, regressioni e audit indipendente. Il blocco affronta un ulteriore punto di chiamata campionato, `SurfaceQuiltMap::flattenSheet`, dopo il [mapping obliquo](curve-fit-k10-diagonal-2026-11-15.md).

## Difetto riprodotto

La baseline congelata accetta una faccia piana con bordo polinomiale di grado 27, ma il contorno appiattito perde una variazione fra i campioni del fitter: errore geometrico minimo rispetto a tutti i bordi risultanti **5,742475e-5 mm**, deviazione dichiarata circa **2,476558e-10 mm**. Tolleranza pubblica 1e-6, budget locale 2e-8. La variante razionale a pesi positivi mostra **5,416219e-5 mm**, contro circa **2,894104e-10 mm** dichiarati. Sorgente e risultato superano il controllo topologico; la fonte resta immutata. L'esito positivo dei controlli campionati non garantiva la conservazione della geometria.

## Correzione e ambito

Il percorso nuovo riconosce strutturalmente **una sola faccia Plane**. La mappa affine viene condivisa fra appiattimento, inversione e calcolo delle metriche. I contorni Line/NURBS supportati e i relativi trim conservano il parametro originale; i coefficienti UV sono trasformati direttamente in una B-spline 3D esplicita, senza un fit campionato della forma. Il limite continuo include l'arrotondamento della trasformazione, senza dichiarare automaticamente errore zero.

Le rappresentazioni sul piano finale mantengono grado, nodi e pesi delle spline esplicite; i poli vengono proiettati e verificati nel piano finale. Sono attivi controlli su domini, dati finiti, budget, cancellazione, estremi e pubblicazione atomica di statistiche e genealogia. Le soglie di accettazione non sono aumentate.

La mappa usa le norme double degli assi originali, congelate come coefficienti della trasformazione; non assume assi matematicamente unitari. Flatten, fold, metriche e tagli della piegatura condividono gli stessi fattori. L'inversione mantiene i limiti U/V anche con `carrierOnly`, compreso il margine fisico e il clamp del percorso precedente.

Il nuovo helper `fk_planar_flatten` trasforma i poli originali con aritmetica outward. Fonte e risultato hanno gli stessi pesi positivi, quindi il massimo residuo dei poli limita la differenza continua fra le due curve. Le Line diventano B-spline di grado 1 sul loro intervallo originale. Dati non finiti, domini invalidi, grado oltre 128, oltre 100000 poli o budget inconclusivi vengono rifiutati nel ramo riconosciuto. Solo famiglie non supportate ritornano al percorso precedente. Helper e quilt sono compilati con FP rigoroso.

Dopo `computePCurves`, un ulteriore controllo continuo usa i coefficienti della SP-curve effettivamente memorizzata. Il ponte fra bordo e SP-curve viene sommato outward all'errore già noto e deve restare nel budget. Anche il riuso di un bordo conosciuto nella stessa faccia viene controllato attraverso i coefficienti. La genealogia è preparata localmente; un ultimo punto di cancellazione precede la pubblicazione di genealogia e statistiche.

**Limite dei vertici:** la discrepanza topologica sommata riguarda estremi e vertici numericamente valutati. Non costituisce un enclosure della valutazione NURBS esatta agli estremi. Il certificato continuo riguarda la curva rispetto ai coefficienti UV originali, con i fattori double della mappa interpretati esattamente; non certifica isometria ideale, iniettività globale o tutte le tolleranze preesistenti della geometria sorgente.

## Verifiche finali

- Build completa Release di applicazione, runner e nuovo target: PASS. 18 avvisi in file invariati in questo intervento; nessuno nei moduli qui modificati. Eseguibile `forgecad-release/forgecad` aggiornato: riaprire l'applicazione per usarlo.
- **6/6 nuovi gruppi, 27 scenari**: polinomiali e razionali, pose 3D, scale 0,01/1/1000, senso faccia invertito, trim annidati parziali, parametro non unitario, fori, genealogia, codec, errori/cancellazione atomici, limiti della mappa inversa e compatibilità della conica nel percorso precedente. La baseline congelata passa 3/6 gruppi: due gruppi falliscono anche geometricamente; il gruppo Line fallisce la conservazione della rappresentazione, senza attribuirgli un errore geometrico non dimostrato.
- Riproduttori finali polinomiale e razionale: **errore misurato 0**, deviazione dichiarata **8,526979e-14 mm**, controllo del corpo senza errori e fonte immutata. Lo zero misurato non è una dichiarazione di errore matematico nullo. La regolarità dei contorni sorgente è stata verificata indipendentemente usando i coefficienti IEEE come razionali esatti e suddivisione Bernstein; la distanza della baseline da tutti i bordi è confermata anche attraverso i rispettivi control net.
- **19/19 suite CTest PASS**, comprese mapping e piegatura precedenti, kernel, storia, pannelli, anteprime e ciclo di vita Qt offscreen. Non è una nuova prova visiva OpenGL.
- **ASan/UBSan/LSan: 6/6 gruppi PASS**, senza diagnostiche. Strumentati il nuovo test e le due unità di produzione `fk_planar_flatten` e `fk_quilt_mapping`; il resto dell'archivio kernel e il runner principale sono Release. Controllo mirato, non certificazione globale dell'applicazione.
- **118/118 controlli indipendenti PASS**, più il target mapping precedente completo. Ripetizione sugli stessi byte finali integrati: 48 immagini affini, 18 contratti estremi, 12 casi non-clamped/Line, 11 controlli SP-curve/vertici, 3 atomicità pubbliche, 24 controlli downstream, 2 riproduttori pubblici.
- Architettura: **755 file / 10888 archi / 375 unità C++**, diagnostica vuota, `--check`, uguaglianza HTML/JSON e audit entrante/uscente di chiamate, include e riferimenti PASS.
- Conservati byte per byte **2229 dei 2236 file preesistenti**; i 7 modificati appartengono allo scope dichiarato. Hash dei sorgenti/binari, FP rigoroso, link kernel senza Qt/OCCT, `git diff --check`, memoria entro 16 KiB e manifest delle prove verificati nell'audit finale.


## Stato e limiti

Questo blocco tratta una famiglia di un nuovo punto iniziale: **9/24 siti parzialmente trattati, 15 interamente da migrare**, oltre ai fallback dei siti già affrontati. K10 resta aperto. Appiattimenti di due facce, supporti curvi e altre famiglie non riconosciute mantengono il percorso precedente; le relative misure campionate non sono certificati globali. Restano aperti anche mapping tensoriali diagonali e offset normali significativi del blocco precedente.

Il totale dei **24 rilievi precedenti corretti** non aumenta. **K2 è in produzione**; restano **22 candidate orfane** e l'uniformazione completa di anteprime, pannelli e stili. Non si afferma assenza globale di leak o difetti; nessuna nuova prova OpenGL in questo intervento. Le [prove e il manifest](curve-fit-k10-flatten-prove-2026-11-15/README.md) distinguono baseline, risultati finali e verifiche mirate.


## Prosecuzione successiva

Il [blocco Spessore su facce piane](curve-fit-k10-thicken-2026-11-15.md) conserva le curve originali delle pareti tramite traslazione ed estrusione. Copre due ulteriori siti iniziali per questa famiglia: 11/24 parzialmente trattati, 13 interamente da migrare, oltre ai fallback. I risultati sopra descrivono la precedente consegna di appiattimento.
