# Audit indipendente K10 — mapping, 15 novembre 2026

**Esito finale: nessun blocker trovato nella famiglia implementata.** Il riproduttore reale della lamina piana che perdeva un rilievo NURBS alto0,001 è corretto conservando il carrier originale e certificando i bordi. **K10 resta aperto:** sono esplicitamente legacy le famiglie non supportate; due riproduttori pubblici di tali residui restano errati nelle prove finali, senza nuovo ID.

AGENTS, CLAUDE e grafo architetturale letti. Produzione, CMake, documentazione di progetto e build condivisa in sola lettura per questo agente. Tutti gli snapshot/oggetti/eseguibili qui presenti sono privati. Il coordinatore cura build app, runner generale, sanitizer, mappa e CLAUDE.

## Freeze effettivamente verificato

`manifest.json` contiene cinque percorsi core relativi al repository con SHA256 completi, più il test permanente finale revisionato. I byte di `final-sources/` sono identici ai cinque file finali integrati e sono stati ricompilati privatamente per tutti i replay finali:

- `kernel/fk_curve_fit_bounds.cpp`: `ee02df4e2d8e2f920afaf06835d9eb49ffb1ce659425d8e0517a163bad83303a`.
- `kernel/fk_curve_fit_bounds.h`: `44629c36150e39b1e48c962b0c1191ce0b37a905e0f3f33efd9477bf8cee1908`.
- `kernel/fk_mapping_carrier.cpp`: `a98eb1941657e929c9ad18c7c9bd9918f1b2b13e7fc77e35bb7242713c831b68`.
- `kernel/fk_mapping_carrier.h`: `02683cb1b48900c5a711c60a671f27a426d8526d96825c6a943cc5a86bf393a3`.
- `kernel/fk_surface_mapping.cpp`: `0c76d3b2b19c2c110712348f06f0a2680dd0d5f4ba8fa77f2c90c71ac07306a9`.

`baseline-manifest.json` e i file `.baseline` distinguono lo stato precedente. I log `*-staged0/2/4.log` documentano tentativi intermedi, **non** il risultato finale. Le prove finali sono esclusivamente `*-final.log`.

## Confine matematico del fix

Il vecchio percorso approssimava sia map(edge(t)) sia map(sourceSurface(u,v)). Entrambi potevano perdere lo stesso nodo nascosto del target, superando il controllo campionato di coerenza. Il nuovo helper interviene su lamine con carrier sorgenti Plane, quota nulla o entro bound esplicitamente addebitato, bordi e SP-curve sorgenti Line/Trimmed o B-spline polinomiali/equal-weight grado1 e immagini UV isoparametriche affini. Mantiene BSplineSurface o ExtrusionSurface ORIGINALI, senza conversione arrotondata a isocurva/NURBS.

La mappa resta quella pubblica:

```
p = sourcePlane.toLocal(P)
u = anchor.x + (cos(angle)*p.x - sin(angle)*p.y + shift.x)/scaleU
v = anchor.y + (sin(angle)*p.x + cos(angle)*p.y + shift.y)/scaleV
map(P) = target(u,v) + (p.z + normalOffset)*target.normal(u,v)
```

`scaleU=||Su(anchor)||`, `scaleV=||Sv(anchor)||` sono le metriche congelate all'anchor, come prima; non vengono sostituite da lunghezze d'arco o metriche locali. Il codice supporta una direzione d'estrusione obliqua e frame sorgenti posizionati, non assume assi globali XY. Il segno della faccia include l'orientamento del piano sorgente rispetto al frame della mappa.

Il carrier originale elimina l'errore del fit della superficie per questa famiglia. Per bordi/trim, siano epsU/epsV i limiti outward della differenza fra UV della mappa originale e UV effettivamente costruite, H il limite della quota scartata, MU/MV i limiti di ||Su||/||Sv|| sul rettangolo naturale che contiene l'intero corridoio UV. Allora:

```
||map_originale - curva_output|| <= H + MU*epsU + MV*epsV + errore_fit
```

Il termine H è verificato sul **materiale intero della faccia**, usando l'affinità della quota sulla Plane e il box convesso delle SP-curve originali; viene verificato anche sui bordi effettivi. I corridoi degli originali, delle UV costruite e delle SP-curve originali devono restare nel dominio naturale. Non si applicano clamp o margini arbitrari per nascondere sconfinamenti.

## Origine dei bound e rounding

`surfaceParameterBounds` ricava bounds globali dei quozienti omogenei originali: S=H/W, Su=(Hu*W-H*Wu)/W², analogamente Sv. I coefficienti derivati delle B-spline hanno fattore p/(U[i+p+1]-U[i+1]); indici U/V e normalizzazione dei pesi sono stati controllati. Il minimo positivo dei pesi normalizzati è un lower bound di W sull'intero rettangolo naturale, anche non clamped. Recenterare i poli prima delle coordinate omogenee riduce la dipendenza dalla traslazione; le sole parti lineari dei placement conservano la derivata. Nell'estrusione Su=C', Sv=D, usando il generatore originale, compresi trim e trasformazioni.

`surfaceCompositionErrorBound` estende la prova posizione al generatore originale d'estrusione. A V fisso aggiunge V*D*W ai coefficienti omogenei; a U fisso valuta C(U) outward e forma la retta V. Fixed-axis extraction su BSplineSurface resta sui dati tensoriali originali. I nodi nominali sono suggerimenti di suddivisione, non certificati.

Il cambiamento condiviso `affineBezier` da a*(1-t)+b*t ad a+(b-a)*t è algebricamente identico per ogni t reale, inclusi reverse ed extrapolation. Ogni operazione resta outward; l'enclosure è quindi valido senza assumere t in[0,1]. La forma delle differenze evita l'amplificazione artificiale della traslazione comune quando t è un intervallo derivato da un'inversa mal condizionata. I precedenti30+24 controlli del provider sono stati rieseguiti proprio per questo cambiamento condiviso.

L'aritmetica helper riconosce soltanto identità dimostrate: zero/±1, somme singleton esatte via TwoSum, divisione singleton a/a=1 con denominatore positivo, endpoint B-spline restituito dal polo originale. Non impiega clipping del dominio o una pretesa esattezza di prodotti arbitrari. Le assunzioni sono IEEE double senza fast-math/contrazione, input finiti e pesi positivi; gli script privati applicano gli stessi flag strict-FP richiesti anche ai moduli di produzione. Dati conosciuti ma inconclusivi devono lanciare, non diventare famiglia unsupported.

## Bordi, trim, vertici e transazione

`fk_mapping_carrier.cpp:318` ricertifica tutti i coefficienti cubici finali effettivi, inclusi i poli condivisi tra campate. Il gate delle SP-curve sorgenti, :340, verifica OGNI fin, comprese le facce adiacenti e i trim tolleranti: unione dei nodi, mappa affine originale della Plane(finUV(t)), confronto outward con la pcurve costruita e relativo corridoio di dominio. Non sostituisce un trim tollerante senza contabilizzarne la differenza.

I vertici costruiti dai bordi vengono confrontati con map(originalVertex), con limite conservativo della distanza e accumulo nel deviation finale. Il caso con normali del frame opposte conserva l'orientamento corretto. La topologia e la genealogia hanno corrispondenza per indice con origini Modified; non ho trovato modifiche della sorgente o del target né pubblicazioni anticipate.

Prima dell'helper restano eseguite le guardie Jacobiane preesistenti di mapBody. Storia e deviation vengono assegnate solo dopo costruzione del body, checkBody e cancellazione finale; i replay di errore/cancellazione conservano i sentinel. `affinePoint` usa ricerca binaria dello span e cancellationPoint: è stata eliminata la scansione O(N²) iniziale su molte campate.

## Risultati finali indipendenti

**107 controlli supportati/di contratto PASS, più due controlli legacy residui espliciti.** Le106prove precedenti sono state rieseguite integralmente anche su V6: i sei log sono byte-identici a V5 (`v5-v6-replay-comparison.json`). Le seguenti prove usano il freeze sopra, non gli oggetti intermedi:

- `baseline-final.log`: **8/8 varianti pubbliche supportate** corrette: native Extrusion, generatore razionale, scala U0,4, direzione d'estrusione obliqua, frame sorgente 3D posizionato, frame normale opposta, quota0,2 compensata da normalOffset−0,2, target BSpline tensoriale. Errore bordo indipendente massimo da circa1,4e−11 a2,4e−9, sempre <=deviation<=2e−8. checkBody0 e genealogia presente. Il carrier originale è mantenuto; la variante a normale opposta restituisce sense=false. Il controllo comprende2.001 punti più valori forzati nel rilievo nascosto.
- `tolerant-final.log`: **4/4 controlli PASS**. Vertice spostato1e−5/tolleranza2e−5: rifiuto. Tutte SP-curve spostate X+1e−5/tolleranza2e−5: rifiuto. Entrambe sorgenti valide con checkBody0 e outputs invariati (deviation123, history2facce). Cancellazione preset: outputs invariati. Vertice spostato1,9e−8: accettato entro il budget, deviation1,9000000000000005e−8 copre lo spostamento1,8999999999999998e−8.
- `metric-final.log`: **16/16 PASS**. Superfici tensoriali razionali non clamped grado3×2 con istanze e pesi scalati1e−100/1/1e100; estrusioni con generatore polynomial/rational/Trimmed/Transformed/Line. I bound U/V dominano le derivate valutate indipendentemente su griglia65×33 più cinque coordinate nel dettaglio nascosto. Plane/Circle unsupported danno optional vuoto; pesi1e−300/1e300 riconosciuti ma inconclusivi producono throw.
- `extrusion-final.log`: **24/24 fit diretti PASS** su generatore originale poly/rational, Trimmed/Transformed, U/V, reverse/nonunit parameter, trim UV annidato e percorso UV a L. Ogni errore su2.001 punti <=deviation<=1e−7.
- `provider-final.log`: **30/30 PASS**, replay integrale del precedente audit composizione:24fit su tensor nonclamped razionale, pesi1e±100, U/V interni, reverse/nonunit, nestedtrim, L-shaped, placement; altri6controlli di unsupported/partial/dominio periodico. Ogni fit: errore denso<=deviation<=1e−7.
- `provider_queries-final.log`: **24/24 PASS**, replay di candidati cubici perturbati con query intera/dimezzata, estremo di nodo, ±1ULP e intervalli1e−9, diretto/inverso e cambio asse. Bound sempre finito e >=errore su2.001 punti.

- `budget-final.log`: **1/1 PASS** nuovo controllo V6: sorgente valida checkBody0 con una SP-curve B-spline polinomiale grado1 a100.001poli. La chiamata helper diretta prima restituiva optional vuoto (`budget-v5.log`), consentendo il fallback campionato; ora lancia errore di budget lasciando deviation123 e history2facce invariati. Nessun percorso generico pesante eseguito.

Il diff V5→V6 contiene esclusivamente la separazione del controllo type/degree dal limite100.000poli, il require sul budget e il cancellationPoint nel ciclo pesi. Il test permanente aggiunge soltanto il nono gruppo: tolta l’aggiunta e il newline separatore, il prefisso ha SHA `bb29d7ca7ce5dc9fae9cc15d9ef8e9b9a0b7f101db9b8e1fa0ee49093c6cb0c5`, identico al test V5. Test finale revisionato SHA `403a1ac55bf1d4d1b11308cbb8aaef5c2c09b4698e6c9d58ff06dca00752765d`. Il coordinatore/test agent esegue quel runner permanente; questo agente ha eseguito il proprio controllo risorse isolato e tutto il replay107.

I confronti densi sono controprove indipendenti e regressioni. **Non** sono la fonte della certificazione continua, che discende dai coefficienti originali e dai bounds outward. Sanitizer/app/runner generale sono verificati dal coordinatore e dagli altri agenti, non attribuiti a questo agente.

Ripetizione: `python3 build_final.py`. Compila solo oggetti privati dai cinque snapshot finali usando i compile_commands reali e l'archivio esistente per le dipendenze; non invoca build condivisa.

## Problemi trovati e risolti durante lo staging

Questi non sono nuovi bug di produzione né nuovi ID:

1. Metric Extrusion confondeva inf conosciuto con unsupported: ora throw sul riconosciuto inconclusivo.
2. Corridoi artificialmente fuori dominio su endpoint0/1, razionale e scala0,4: risolti con endpoint/identità esatte, senza clamp. Le due false rejection baseline-success sono ora successi nel replay finale.
3. FinUV tolleranti: prima bozza accettava una sorgente valida il cui footprint originale[1e−5,1+1e−5] usciva dal target[0,1], sostituendo i trim dalle edge. Riprodotto in `tolerant-staged0.log`, ora rifiutato dal gate di OGNI trim originale.
4. Vertice tollerante: mantenuto il gate verso la mappa del vertice originale; la distanza accettata viene addebitata al deviation.
5. Ricontrollo dei cubici finali e ricerca O(logN)/cancellazione aggiunti prima del freeze.
6. Ultimo controllo V6: il superamento100.000poli su una BSP1 riconosciuta non deve diventare unsupported; separato il budget con throw e cancellazione nel ciclo pesi. Nessun nuovo campo geometrico supportato e nessuna modifica alle formule. Lo stato audit V5 è conservato in `v5/`.

## Riproduttore iniziale e limiti ancora aperti

Il repro originario ha target S(u,v)=(x(u),v,z(u)), profilo polinomiale grado6, nodi0×7,0,1×6,0,101×6,1×7, GrevilleX e unico polo9Z=0,0032. Il target è regolare: `regularity.py/json` dimostrano esattamente con Fraction sui double originali x'>=0,9999999999999721 per convex hull delle derivate Bernstein; ||Su×Sv||>=x'>0. Zmax0,001. La vecchia superficie restituita aveva TUTTI i poliZ0 e quindi una distanza geometrica dal picco almeno0,001, indipendente dal parametro. Log baseline e fixture/input/regularità sono conservati in questa cartella e nel rapporto precedente.

Due controlli ancora errati, **dichiaratamente fuori scope**, in `baseline-final.log`:

- `native_rotation_shift`, angle0,29/shift(0,03;0,02)/anchor(0,05;0,1): crea UV diagonali; resta legacy e perde0,001.
- `native_normaloffset`, offset normale0,01: richiede un offset carrier e non la quota nulla; resta legacy e lo scarto osservato raggiunge0,011718353468606611. Qui non è dimostrata la regolarità/iniettività della mappa offset: è un controllo fuori scope, non un ulteriore finding di geometria offset valida.

K10 resta dunque aperto. Non sono stati certificati mapping generali, inverse UV, quilt/arc-length, bordi sorgenti di grado superiore o coniche riparametrizzate. Domini periodici fuori dal naturale e dati conosciuti inconclusivi possono fallire esplicitamente; non si promette successo per tutta geometria valida. La regolarità interna, l'iniettività globale e le autointersezioni restano fuori dalla prova, con guardie campionate preesistenti conservate.

Conteggio concordato: **8/24 siti fitCurve parzialmente trattati**, non24chiusi. Il nuovo helper tratta una famiglia del sito edge fit di mapBody; il surface fit bypassato non era nell'inventario24fitCurve, e il ramo inverse UV generico non era attivo in wrapBodyOnSurface e non va contato come secondo sito migrato.
