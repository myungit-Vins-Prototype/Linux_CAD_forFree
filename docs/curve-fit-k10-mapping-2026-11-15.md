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

- **Build completa dell'app e dei runner PASS**. `forgecad-release/forgecad` aggiornato; riaprire l'app per usarlo. Warning in file preesistenti, nessun warning nei sorgenti modificati in questo blocco.
- **9/9 gruppi nuovi PASS, 33 scenari**: 18 successi nel nuovo percorso, due controlli del fallback e tredici rifiuti/cancellazioni. Coperti polynomial/rational × Extrusion/BSpline × scale 0,01/1/1000, pose, quarto di giro, fori, trim, grado 27 senza nodi interni, genealogia, deviazione e roundtrip B-rep. Le forme finali e gli originali vengono controllati separatamente.
- **Confronto baseline finale 5/9**: con il vecchio modulo pubblico compilato dallo snapshot iniziale e collegato prima dell’archivio corrente, mantenendo helper/provider finali, falliscono tre gruppi geometrici e il controllo della deviazione del vertice. Le distanze a uguale parametro verificano anche il nuovo requisito di preservare il carrier; l'errore geometrico della baseline è dimostrato separatamente dal picco proiettato e dal riproduttore completamente piano.
- Il nono gruppo dimostra il caso limite delle risorse: la prima implementazione restituiva famiglia non supportata per una sorgente valida con 100.001 poli; la versione finale rifiuta esplicitamente e preserva gli output. I risultati dei primi otto gruppi e delle batterie prima di questa guardia sono conservati separatamente; i conteggi finali si riferiscono alla build con la guardia integrata.
- Riproduttore originale corretto: errore sul carrier **0,001 → 0 mm**, scarto dichiarato circa **1,6233e-8 mm**, checker senza problemi. Anche il caso di grado 27 ora riesce, con scarto dichiarato circa 1,6634e-8 mm; prima veniva rifiutato.
- **129 casi kernel distinti / 145 esecuzioni PASS**: Offset, Shell, Sheet, BoundarySheet, Fill, Projected, Sew, Thicken e sette gruppi Blend.
- **37/37 suite CTest PASS**, eseguite dopo il completamento dell'intera build. Comprendono i nuovi test, mapping preesistente, provider certificati e test Qt offscreen di pannelli, anteprime, storia e modifica poli.
- **9/9 regressioni ASan/UBSan/LSan PASS**, senza diagnostiche: strumentati nuovo test, `fk_mapping_carrier.cpp`, `fk_surface_mapping.cpp` e `fk_curve_fit_bounds.cpp`; main e restante archivio Release non strumentati. Nessuna certificazione globale dei leak dell'app.
- **107 controlli indipendenti PASS**: otto mapping supportati, quattro casi tolleranti/atomici, sedici metriche, ventiquattro fit di estrusione, trenta prove del provider precedente e ventiquattro query avverse precedenti e un controllo del budget a 100.001 poli. Due controlli legacy residui sono separati: UV diagonali perdono ancora 0,001 mm; offset normale presenta uno scarto ma la regolarità della mappa offset non è dimostrata, quindi non è contato come nuovo finding di offset valido. I campioni indipendenti verificano il provider, non costituiscono la sua prova continua.
- **Mappa 749 file / 10800 relazioni / 371 unità PASS**: diagnostica vuota, controllo aggiornamento, HTML/JSON identici, descrizioni e collegamenti in ingresso/uscita verificati distinguendo chiamate, include e riferimenti.
- Hash dei sorgenti verificati contro il freeze indipendente, flag floating point stretti su entrambi i moduli intervallari, target senza Qt/OCCT, modifiche pregresse preservate e `git diff --check` PASS. [Prove, script e manifest](curve-fit-k10-mapping-prove-2026-11-15/README.md).


## Stato complessivo

**8 dei 24 siti originari** dispongono di un percorso esatto/certificato per famiglie supportate; **16 restano interamente da migrare**, oltre ai fallback degli otto trattati. Questo blocco aggiunge un solo sito al conteggio: il fit dei bordi in `mapBody`. La conservazione del supporto evita anche il fit della superficie in questo percorso, ma quel fitter non era uno dei 24 siti `fitCurve`; l'inversione UV campionata altrove non è stata migrata.

Restano **24 rilievi precedenti corretti e 22 funzioni orfane candidate**. K2 è già in produzione. L'uniformazione completa degli stati delle anteprime e dei pannelli rimane distinta dalle correzioni funzionali già integrate. [Review complessiva](code-review-2026-11-15.md).


## Prosecuzione: mapping obliquo

Il [blocco successivo](curve-fit-k10-diagonal-2026-11-15.md) estende il certificato alle UV affini diagonali sulle estrusioni originali e corregge la riserva del budget dei trim prima del fit. Il riproduttore `native_rotation_shift` ora conserva il rilievo. I mapping diagonali su NURBS tensoriali generali e gli offset restano esclusi. Il conteggio rimane 8/24 siti parziali, 16 interamente da migrare; numeri e prove sopra descrivono la consegna isoparametrica precedente.
