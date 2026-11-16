# Review kernel non-sweep — 15 novembre 2026

Sola lettura del repository `/home/myungit/Linux_CAD_forFree`; artefatti soltanto in questa directory. Nessuna patch di produzione. AGENTS.md/CLAUDE.md e descrizioni/chiamate del grafo 736 file/10570 archi consultati prima dell'indagine. Il parent gestisce l'aggiornamento della memoria condivisa.

## R1 — P1: un falso riconoscimento di traslazione restituisce una superficie offset fuori tolleranza

**Righe:** `kernel/fk_offset.cpp:629–635` (decisione errata); chiamante `:1279–1285`; verifica finale insufficiente `:1317–1328`.

`exactOffsetCurve` decide che l'offset sia una traslazione della curva originale usando soltanto nove parametri uniformi. Restituisce quindi la curva originale traslata e salta il fit adattivo. Una caratteristica locale fra quei parametri può cambiare la normale senza cambiare nessuno dei nove offset osservati; anche il controllo finale sui cinque parametri U fissi può non vederla.

**Trigger minimo, tutto finito e regolare:** B-spline grado 4, nodi `0×5, .1×4, .15×4, 1×5`, 13 poli. Le ascisse dei poli sono le medie di Greville (quindi x(t)=t); tutte le z sono zero salvo il polo 6, z=.01. La curva è geometricamente C1 ai giunti: sul tratto [.1,.15] la quota è `z=6*.01*s²*(1-s)²`, `s=(t-.1)/.05`. Si estrude lungo Y e si chiede `offsetSurface(...,.001,{0,1},{0,1},1e-7)`.

Il raggio minimo di curvatura è almeno 1/48=.0208333: d=.001 è molto inferiore, quindi non è un caso di fold o distanza irrealizzabile. A t=.1125, z'(t)=.225 e l'errore del risultato restituito è esattamente

`d*sqrt(2-2/sqrt(1+.225²)) = .00022086305215…`.

**Conseguenza:** successo con geometria distante circa 2208 volte dalla tolleranza richiesta. Scalando la geometria di 1000 e usando d=1, l'errore diventa **.220863 mm** su una lunghezza di 1000 mm, con la stessa tolleranza 1e-7. I punti del riconoscitore e quelli della verifica finale restano ciechi. Un controllo più fitto ma finito non corregge la causa generale: serve un riconoscimento geometrico continuo della traslazione o il percorso di fit generale.

**Prova:** `python3 build.py`; `./probe bump`; `./probe bump 1000`. Log `bump-scale1.log`, `bump-scale1000.log`. Chiamata alla libreria kernel corrente, senza sostituire oggetti del kernel. La coincidenza di nove normali e del controllo finale non è usata come certificato nella prova: il punto errato e la curva/piana sono valutati dalla geometria esatta e il valore è verificabile con la formula sopra.

## R2 — P1: il ramo toroidale accetta la perdita di regolarità e la superficie ripiegata

**Righe:** `kernel/fk_offset.cpp:1262–1266`. Il ramo controlla soltanto `minorRadius+d > tolerance`; manca il controllo sul fattore radiale `R+(r+d)cos(v)` nel dominio richiesto. Il controllo di curvatura `:1309` riguarda soltanto il ramo B-spline. La verifica posizionale finale non rileva il problema perché la parametrizzazione ripiegata continua a soddisfare formalmente S+dN.

**Trigger minimo:** toro regolare R=5, r=1, dominio U,V=[0,2π]. `offsetSurface(...,4,...)` restituisce il toro horn r=5, con Jacobiano nullo a V=π; `offsetSurface(...,5,...)` restituisce r=6, con normale interna invertita e cuspidi a `cos(v)=-5/6`. Nessuna eccezione.

**Conseguenza prodotto dimostrata:** `offsetFaces(makeTorus(Frame3(),5,1), allFaces, 5)` restituisce una lamina con una faccia e **checkBody=0 problemi**, non un errore esplicito. La faccia contiene il ramo ripiegato; i controlli topologici non certificano la regolarità della geometria. Il parent ha ripetuto indipendentemente d=3/4/5 con preserveSeams=false/true e confermato il difetto. Non occorre vietare globalmente ogni r+d>=R: una finestra limitata al lato esterno può essere regolare; la verifica va applicata al dominio effettivo.

**Prova:** `./probe torus`; `./probe torus-body`; log `torus-surface.log`, `torus-body.log`. Controllo valido d=3: Jacobiano interno 4; d=4: Jacobiano 0; d=5: Jacobiano modulo 6 ma verso invertito. Questo difetto è distinto da R1 e non dipende dal riconoscimento campionato.

## Osservazione C1 — P3 latente: fitCurve non osserva il token quando la callback non contiene checkpoint

**Righe:** `kernel/fk_offset.cpp:1199–1225`: nessun cancellationPoint nel raffinamento, che può visitare 32768 pezzi. Probe `./probe fitcancel`: callback geometrica liscia `(t,sin(100t),0)`, intervallo [0,1], tolleranza 1e-7; token attivato alla callback 20. Il fitter restituisce successo dopo **15807 callback e 5269 poli**, ignorando il token durante il resto del fit. Log `fitcancel.log`.

**Chiamante prodotto pertinente:** `kernel/fk_blend_surface.cpp:1631–1655`, `extendSplineBoundary`: callback `continuation` con Newton fino a 30 iterazioni che chiama `supports[i]->evaluate` direttamente, senza passare dai wrapper cancellabili `Surface::point/normal`, poi chiama fitCurve. È raggiungibile da `blendSurfaceChains` nel prolungamento dei bordi NURBS (`:2733`). Il worker anteprima installa CancellationScope in `forgeCad2026_viewport_preview.cpp:453`; clearPreview imposta il token a `:255`; un nuovo job resta rinviato finché previewRunning (`:387–389`).

**Limite esplicito della prova:** la cancellazione mancante è riprodotta nel fitter e il percorso chiamante è dimostrato dal sorgente/grafo; non è stata riprodotta una specifica operazione di raccordo UI con durata percepita misurata. Non vengono usati sleep sintetici per attribuire una latenza reale. Non è corretto estendere il rilievo a tutti gli offset: le callback di offsetSurfaceImpl chiamano Surface::point/normal e lì il token è già osservato. Per questo C1 resta P3 latente separata dai due difetti geometrici confermati.

## Copertura e ipotesi scartate

Consultati responsabilità/chiamanti/dipendenze nel grafo per fk_offset, fk_shell, fk_blend, fk_open_offset e i callsite fitCurve. Letti i percorsi pubblici offsetSurface/offsetFaces/offsetJoinedFaces/fitCurve, assemblaggio e storia shellBody/nestedShell, instradamento/fallback di blendEdges, prolungamento NURBS e alcuni loop/blocchi di errore dei blend; letti fk_parallel/fk_cancel e la catena preview pertinente.

* Non è stata dimostrata una non-terminazione: raffinamenti offset hanno limiti di giri/dimensioni, fitCurve un limite di visite, i loop angolari esaminati partono da atan2 e non dagli angoli enormi già corretti altrove.
* Non trovata una nuova mutazione parziale dell'ingresso: offset/shell assemblano copie locali e i blocchi esaminati dichiarano la storia al commit. La cancellazione della storia al principio è comportamento intenzionale e non è presentata come bug.
* Nessuna nuova perdita di memoria/use-after-free dimostrata. I precedenti M1/M2 sono esclusi dalla presente review.
* È stato osservato anche offset di un piano a distanza infinita accettato; non viene promosso qui a un ulteriore rilievo perché riguarda input non finito e non è necessario per provare R1/R2 su geometria valida.
* Questa è una review mirata, non lettura integrale del kernel, non build completa e non esecuzione della suite generale. Nessuna prova renderer/UI nuova.

## Ripetibilità e stato

`build.py` ricava i flag dal comando di compilazione test_offset.cpp in forgecad-release/compile_commands.json e le librerie dal link.txt del runner esistente, poi compila soltanto probe.cpp e lo collega alla **libforgekernel.a corrente**. L'uso delle librerie OCCT del runner di confronto è soltanto nel link del probe, non nella produzione né nel codice della prova. Tutti i file prodotti sono locali a questa directory.

HEAD di riferimento: ce03a6d5941840886416aafe055763fb98209e94, con modifiche locali intenzionali presenti. Hash sorgenti/libreria sono conservati in SHA256SUMS; non si attribuisce al solo HEAD l'intero stato verificato. Nessuna correzione implementata.
