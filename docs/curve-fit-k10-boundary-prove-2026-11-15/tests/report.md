# BoundarySheet K10 — regressioni permanenti

Ownership: solo nuovo kernel/tests/test_boundary_fit_validation.cpp. Staging /tmp/forgecad-k10-boundary-20261115/tests, nessuna produzione/CMake/docs/sharedbuild modificata da questo agente. AGENTS/CLAUDE letti; grafo Boundary→fitter/Curve/Body e chiamanti cad_forge/surface_tools/history consultati. Implementazione e CMake sono degli altri owner.

## Fixture e verifiche indipendenti

Fixture originali copiate dai riproduttori indipendenti, non sintetizzate da output della patch:
- grado6, nodi interni0.1 e0.101 ciascuno molteplicità6,19poli, picco geometrico0.001;
- stessa fixture razionale positiva con numeratore omogeneo che conserva x(t)=t, picco0.000952154;
- grado27 a campata unica,28poli double originali, lobi nascosti fra vecchi campioni, picco5.74247506e-5.

Il requisito di SUCCESSO è incondizionato nei validi: non esistono catch che promuovono il rifiuto di NURBS valida. Si confrontano punti della SUPERFICIE FINALE di Coons con le curve sorgente, entro l'intera operationtolerance richiesta, senza frazioni allargate. Non bastano tipo/splinecontrols. Non si usano le PCurve finali per il gatecarrier: possono avere budget preesistente distinto. I campioni sono controprove/regressioni indipendenti, non la certificazione continua fornita dal nuovo oracle di produzione.

## Otto gruppi

1. HiddenSpansAndDegree27FinalSurface:3riproduttori, confronto10001parametri C0(u)=S(u,0) ed hiddenpeak; doublecheckcomune della stessa riga finale.
2. CompoundSidesPoseScaleAndReversals:6varianti (scale.01/1/1000 × polynomial/rational), source trasformata da shear/scala/rotazione/traslazione, primo lato composto da2pezzi consplit.1005 (il nodo0.1 resta interno alpezzo). MappingC0 ricostruito pubblicamente dalle lunghezze geometriche e dall'affinità delparametro di ciascunpezzo. Connettori inordine diverso e versoinverso. Per scale.01 operationtol1e-9 rimane il gate, non portata a1e-7.
3. PartialNestedTrimAndAutomaticSubpieces: fonte in2Trimmed annidate, inputrangeparziale; mappingC0dalrangeinput. In più NURBScircle chiusa reale, suddivisioneautomaticain4lati; confronto4righe finali con circonferenzaanalitica (distanza geometrica esatta radial+z), senza attribuire parametroangolare alla NURBS.
4. ConicCompatibilityAndCancellation: Circleangolare legacy preservata,4righe rispettanocirconferenza; tokeniniziale provoca OperationCancelled.
5. ReversedKnownSide: veraNURBSreversedCurve conrangenegativo, stesso sourceparam sulla rigaC0.
6. KnownSideAllRowsAndTraversalDirections:12varianti(2pesi×3slot×2direzioni). NURBS originale NON modificata: in metàcasi si inverte solo la lineainiziale, così il chainLoop deve usare Piece.forward=false sulla NURBS. Rotazionelista mette source sui trealtribordi D1/C1/D0, con/senza reverseglobale del fit. Mappingdaorientamentotopologico dichiarato, non letto dallaimplementazione.
7. InvalidTolerance:0/negativa/NaN/Inf rifiutate.
8. FinalCoonsEndpointMismatchBudget: linee con δ agliestremi pur collegabili dalla jointolerance. δ0 e.25tol devono riuscire entro tol; δ2tol e5tol devono fallire, aliases sentinel svuotati alfallimento.

## Baseline ripetibile

`python3 /tmp/forgecad-k10-boundary-20261115/tests/build.py`
`timeout 90s /tmp/forgecad-k10-boundary-20261115/tests/tests-baseline`

Oggetto testprivato collegato all'archivioRelease corrente pre-fixBoundary, nessunacomplilazioneglobale. Snapshot oldsource fk_boundary.baseline.cpp e baseline-source.sha256. Esito **3/8 gruppi PASS**, baseline.log: gruppo1 tutti3falliscono geometricamente, gruppi3/5/6 falliscono sui relativipicchi, gruppo8 accetta δ2/5tol fuori budget e pubblica aliases.

Numeri: grado6polynomial gap0.001=10000tol, grado6rational0.000952154248988336=9521.54tol, grado27 gap5.74247506017861e-5=574.2475tol. Compoundcases conbreakvisibile e coniccompat erano già validi e restano controllipositivi. Gli inputinvalidi erano già rifiutati dalfitter; non li dichiariamo nuovi difetti.

Esito patch e SHA oggetti da congelare dopo disponibilità implementazione staged; nessun esito positivo anticipato.

## Versione corretta e integrazione

`python3 build.py /tmp/forgecad-k10-boundary-20261115/implementation/fk_boundary.o /tmp/forgecad-k10-boundary-20261115/implementation/fk_curve_fit_bounds.o`
`/usr/bin/time -f 'elapsed=%e maxrss_kb=%M' timeout 120s ./tests-fixed`

**8/8PASS**, fixed.log,0.38s e maxRSS8340KB. Nessuna tolleranza o fixture indebolita rispetto baseline. Testcopiato nel solo fileassegnato kernel/tests/test_boundary_fit_validation.cpp dopo esito normale; parent notificato. SHAidentità staged/integrato e sorgenti/oggetti usati in manifest.json e fixed-source.sha256.

Sanitizerstruttura: build-sanitizer.py compila con ASan/UBSan, -fno-fast-math e -ffp-contract=off i soli test+fk_boundary+fk_curve_fit_bounds. Frameworkmain e resto dell'archivioRelease rimangonononstrumentati; non è una prova sanitizer dell'intero kernel/app. Esito runtime separato dopo run.

## Sanitizer finale

**8/8PASS**, sanitizer.log, exit0 nessun diagnosticASan/UBSan/LSan. Esecuzionecon ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1, timeout120, escalationnecessaria perlimiteptraceLSandel sandbox. Manifest aggiornato con hashbinary/oggetti/logsanitizer e headercorefinali. Nessun sanitizer integraleapp/kernel dichiarato.

## Nono gruppo — query chiusa alla giunzione

BoundaryFitOracleClosedJunctionEndpoints:2sorgenti Line o BSpline1 con fitrange[0,1]/[1,2] e valori alla giunzione1/3; entrambequery[0,1] e[1,2], metadata reversedfalse/true. Ogni candidato segueesattamente il proprio span, ma il bound deve contenere anche il valore delvicino all'estremo chiuso (gap2 o4). Ottoasserzioni dirette sull'API oracle; nessunanuovaipotesisullacontinuitàinternadiBSpline.

Controprova prima del fix: riusato l'oggettoASan fk_curve_fit_bounds della precedente run8test e conservato come bounds-before-junction-asan.o; hash combacia con manifest-initial-eight-tests.json. Linkprivato junction-previous col nuovo testnormal, filtro OracleClosedJunction:0/1,8asserzionifallite, log junction-previous.log. ASan detect_leaks0 solo perquesta controprova storica; noncontata come sanitizerfinale.

Attenzione: bounds-before-junction.o e junction-before.log sono uno snapshotRelease preliminare giàaggiornato durante la consegnaagenteframe (9/9PASS): NON costituiscono la controprova pre-fix. La controprova autorevole è solo l'oggettostrumentatostorico appena indicato.

Nuovo freeze test SHA908f631340da6b03baf9ceda2b91b965bbd54f8fa293d85cd290f9b2a5632872, integrato nel solo file test assegnato dopo9/9PASS normale;0.37s maxRSS8576KB. Corebounds sourcefeed286dba124919fe1dc986d94bc76c4621952a53c2e7bee867086e98ee0e06. Sanitizer9test da aggiornare dopo runfinale.

Sanitizerfreeze2 finale: **9/9PASS**, ASan/UBSan/LSan detect_leaks1:halt_on_error1 e UBSAN halt_on_error1, exit0nessunadiagnostic; treTUtest+boundary+bounds strumentate, main/restolibRelease no. sanitizer.log e manifest.json aggiornati con9test e sorgenti nuovi. Nessunaulterioremodifica software dopo questo esito.
