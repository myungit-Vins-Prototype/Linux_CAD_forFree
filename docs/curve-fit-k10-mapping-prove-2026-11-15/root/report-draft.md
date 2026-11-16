# K10 — Mapping su superficie, 15 novembre 2026

## Stato

Prosecuzione con tre agenti GPT-6.1 Sol dopo [Modifica poli](curve-fit-k10-composition-2026-11-15.md). Implementato un percorso che conserva il supporto originale per le lamine e le mappe riconosciute. **K10 resta aperto** per le famiglie non supportate e gli altri chiamanti campionati. Questa sottocorrezione non aumenta artificialmente il conteggio dei 24 rilievi precedenti corretti.

## Difetto e causa

La chiamata reale `wrapBodyOnSurface` applicava una lamina piana unitaria a un supporto di estrusione NURBS regolare, con rilievo di 0,001 mm fra U=0,1 e 0,101. Con tolleranza 1e-6 mm restituiva successo, scarto dichiarato circa 2,82e-11 mm e nessun errore del checker, ma tutti i poli del supporto risultante avevano Z=0: errore almeno 0,001 mm, **1000 volte la tolleranza**. La regolarità del target è verificata dai coefficienti originali con aritmetica razionale: non dipende da una cuspide trascurata. Il fit dei bordi e quello della superficie perdevano lo stesso dettaglio, risultando coerenti fra loro ma sbagliati rispetto al target.

La baseline è conservata e riprodotta anche variando scala parametrica, verso del piano, direzione di estrusione e supporto tensoriale. Una variante razionale già riusciva sulla baseline: rimane un controllo positivo. La variante di grado 27 era rifiutata dalla baseline e non viene conteggiata come falsa accettazione.

## Correzione integrata

`kernel/fk_mapping_carrier.cpp/.h` riconosce lamine con facce piane, bordi e SP-curve sorgenti Line oppure B-spline di grado uno a pesi uguali, inclusi i ritagli Trimmed. Il supporto target B-spline oppure Extrusion con generatrice Line/NURBS viene conservato nei suoi dati originali, comprese le trasformazioni supportate. Nessuna conversione approssimata del target diventa una nuova sorgente esatta. I nuovi parametri dei bordi sono affini a tratti e isoparametrici; piccoli residui trasversali sono ammessi soltanto addebitandone l'intero limite d'errore.

Il provider `surfaceParameterBounds` calcola limiti globali per le derivate U/V dai controlli omogenei originali, con pesi positivi, ricentramento e aritmetica verso l'esterno. Il limite continuo del trasferimento comprende arrotondamenti UV, quota rispetto al piano sorgente e scarto del fit. Il corridoio originale/risultante deve restare nel dominio naturale. La quota della regione piana intera è limitata tramite l'inviluppo delle SP-curve originali, non soltanto tramite i bordi 3D.

`surfaceCompositionErrorBound` ora tratta anche le estrusioni direttamente dalla generatrice originale. `fitCurveCertified` costruisce i bordi; i coefficienti cubici effettivamente prodotti vengono ricontrollati prima di pubblicare il risultato. Il budget resta **0,2 × min(tolleranza dell'operazione, risoluzione lineare)**, senza aumenti. La deviazione comprende anche il disallineamento dei vertici.

Si controlla ogni contorno di taglio originale rispetto alla mappa costruita. Questo evita di cancellare silenziosamente lo scostamento di una SP-curve tollerante dal proprio bordo 3D. Corpi sorgenti validi con vertici o trim spostati oltre il budget vengono rifiutati lasciando intatti genealogia e valori di uscita. Topologia, fori, verso delle facce e genealogia vengono conservati nei casi accettati.

Il punto d'integrazione in `fk_surface_mapping.cpp` esegue prima le guardie locali preesistenti sul Jacobiano. Il nuovo controllo riguarda la posizione e l'errore geometrico: non sostituisce quelle guardie con una dimostrazione globale di regolarità o iniettività. Un controllo riconosciuto ma numericamente inconclusivo solleva un errore, senza passare silenziosamente ai campioni. Lo stesso vale per il budget di 100.000 poli delle curve sorgenti di grado uno: il superamento produce un errore esplicito; il controllo dei pesi include punti di cancellazione. Una regressione a 100.001 poli distingue questo caso da una famiglia geometrica non supportata.

## Limiti espliciti

Le mappe diagonali generali, i bordi/trim di grado superiore, le famiglie di superficie non riconosciute, i solidi e gli offset normali fuori dal piccolo budget verificato mantengono il percorso precedente. In questi rami il difetto K10 può ancora presentarsi: una controprova con offset normale non nullo resta nei risultati dell'audit. Il fit inverso delle SP-curve e il fit generico delle superfici non sono dichiarati certificati.

Al bordo naturale, alcune pose o rotazioni valide possono produrre un intervallo numerico che oltrepassa il dominio: l'operazione viene rifiutata conservativamente. Sono conservate prove di questo limite e prove positive con la medesima trasformazione all'interno del dominio. Le identità esatte ai nodi e nelle divisioni di coefficienti uguali evitano rifiuti inutili nei casi razionali e con scala parametrica diversa da uno; non vengono applicati clamp o margini artificiali.

Le prove non certificano assenza globale di auto-intersezioni, renderer OpenGL, configurazioni CUDA/Metal/macOS/Windows o leak dell'intera applicazione. Le modifiche pregresse e i CAD originali sono preservati; nessun processo utente viene chiuso.

## Verifiche finali

__VERIFICATION__

## Stato complessivo

**8 dei 24 siti originari** dispongono di un percorso esatto/certificato per famiglie supportate; **16 restano interamente da migrare**, oltre ai fallback degli otto trattati. Questo blocco aggiunge un solo sito al conteggio: il fit dei bordi in `mapBody`. La conservazione del supporto evita anche il fit della superficie in questo percorso, ma quel fitter non era uno dei 24 siti `fitCurve`; l'inversione UV campionata altrove non è stata migrata.

Restano **24 rilievi precedenti corretti e 22 funzioni orfane candidate**. K2 è già in produzione. L'uniformazione completa degli stati delle anteprime e dei pannelli rimane distinta dalle correzioni funzionali già integrate. [Review complessiva](code-review-2026-11-15.md).
