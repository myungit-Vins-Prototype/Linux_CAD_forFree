# K10: composizione superficie/SP-curve nella modifica poli

Ho letto AGENTS/CLAUDE e consultato descrizioni e chiamate del grafo per `fk_surface_poles.cpp`. La prima fase è rimasta in sola lettura. Dopo l'assegnazione del coordinatore ho integrato esclusivamente il nuovo `kernel/tests/test_surface_curve_fit_validation.cpp`, verificato privatamente. Core, CMake, documentazione condivisa e build globale sono degli altri owner.

## Riproduttore reale

Il supporto è una `BSplineSurface` di gradi 6×1 e 19×2 poli. I nodi U interni sono `.1` e `.101`, entrambi con molteplicità 6. La superficie nominale è il grafico regolare `x=u, y=v, z=.0016 B9,6(u)(1-v)`. È presente anche una variante razionale con pesi positivi, denominatore nominale `1+.5u` e coordinate omogenee che conservano `x=u`.

Il body è costruito attraverso `Body::buildSheet`: faccia, edge, fin e SP-curve sono espliciti. Il bordo inferiore è la NURBS isoparametrica esatta del supporto; la sua SP-curve è affine e ha soltanto i due estremi nei breakpoints. Il probe iniziale usa `Line<2>`; le regressioni usano anche `BSplineCurve<2>` di grado 1.

L'input ha zero problemi da `checkBody({true,true})` e uno scarto denso edge→superficie inferiore a `7.11e-16`. Una chiamata reale a `editBodyFacesPoles`, con incremento Z di `.0016` al polo `(9,0)` e tolleranza `1e-7`, sulla baseline restituisce successo e ancora zero problemi dal checker. Tuttavia lo scarto del bordo finale arriva a **`.001`, ossia 10.000 volte la tolleranza**. Il supporto al picco è effettivamente cambiato di `.0005`; i byte del source rimangono invariati. Una seconda variante, inizialmente piana, restituisce uno scarto di `.0005`, pari a 5.000 tolleranze.

Il fitter usava soltanto i breakpoints della SP-curve. La composizione `S(uv(t))` attraversa invece i nodi interni del supporto. Dopo il fit, l'edge cubico perde anche i nodi della curva originale: i controlli campionati successivi su edge e SP-curve non vedono più quel tratto.

## Bordo condiviso

Due facce hanno coordinate rispettivamente `y=v` e `y=-v`, stessa Z sul bordo comune `v=0`, fin opposte e SP-curve affini. Gli input polinomiale e razionale hanno zero problemi dal checker e scarto inferiore a `3.6e-16`.

Sulla baseline, sia la modifica di una sola faccia sia quella coerente di entrambe restituiscono successo con scarti `.001` e `.000976077124494`, rispettivamente per pesi unitari e razionali. Le nuove aspettative sono distinte: modificare solo una faccia deve essere rifiutato perché apre il giunto; modificare entrambe coerentemente deve riuscire e rispettare `1e-7` rispetto a ogni adiacente. In entrambi i casi il source deve rimanere immutato.

## Cinque gruppi permanenti

1. **PolesHiddenSupportPoseScaleAndParameters:** 18 input validi, combinando tre scale (`.01/1/1000`), due famiglie di pesi e tre configurazioni: intero dominio, trim V interno, parametro dell'edge non unitario con inversione topologica. Include rototraslazione e delta trasformato nello stesso frame. Successo obbligatorio, checker senza problemi, scarto entro `1e-7*scale` e source immutato. Alla scala `.01` il limite resta `1e-9`.
2. **SharedHiddenNeighborRejectedAndCoherentAccepted:** rifiuto del giunto incoerente e successo di quello coerente, per entrambi i tipi di pesi; controllo geometrico finale e immutabilità dell'input.
3. **DirectOracleKnotCrossingsAndUnsupportedUV:** parametro `[-2,3]`, anche con due `TrimmedCurve<2>` annidate; verifica dei breakpoints derivati dal supporto (`-1.5/-1.495`) e bound finito almeno pari al picco contro un candidato piano. La UV diagonale resta esplicitamente non supportata.
4. **InstancedSurfaceAndMaterializedAffine:** istanze NURBS annidate con rotoscala e traslazione, più shear materializzata nei poli seguita da un'istanza esterna. Il bound deve coprire lo scarto dal candidato piano. Il costruttore di istanze accetta soltanto similitudini: il primo tentativo di usarvi una shear è stato corretto come fixture invalida, senza attribuirlo a un difetto del kernel.
5. **InvalidInputAndCancellation:** source o SP-curve nulli, intervalli vuoti, invertiti o non finiti; query fuori dominio inconclusa; cancellazione esplicita durante costruzione del provider e query.

I campioni densi sono verifiche indipendenti dell'output finale, non una certificazione del continuo. Tale garanzia proviene dall'oracolo di produzione. Non si usano controlli basati soltanto sul tipo di curva o sul numero dei poli. Nessun catch promuove il rifiuto di una geometria valida.

## Ripetizione e risultati

- Probe originale: `python3 build.py`, poi `./poles-probe`.
- Bordo condiviso: `python3 build-shared.py`, poi `./shared-probe`.
- Correzione: `python3 build-tests.py`, poi `./tests-fixed`.
- Baseline: `python3 build-tests.py --baseline`, poi `./tests-baseline`.

Entrambi i runner permanenti collegano il nuovo oggetto dell'oracolo; solo l'oggetto della modifica poli cambia. La controprova isola quindi il vecchio percorso di ricostruzione dei bordi.

**Correzione: 5/5 PASS**, 0,58 secondi e RSS massimo 6056 KiB. **Baseline: 3/5**, con 22 controlli falliti nei due gruppi geometrici reali; i test diretti del provider passano in entrambi. Log: `test-fixed.log` e `test-baseline.log`. SHA completo di sorgenti, oggetti, binari e log in `manifest.json`.

Test integrato: `3050fd5a3ff043b9ae48565b8b9c725fb38d2a406fbaf390a961e15585d453ad`. Core usati: bounds `f1aeae2d...`, modifica poli `8ef57b10...`.

La verifica sanitizer strumenta soltanto test, modifica poli e bounds, con ASan/UBSan e aritmetica senza fast-math/contrazione FMA. Framework main e resto dell'archivio Release non sono strumentati: non è una verifica sanitizer dell'intera applicazione o del kernel. Esito runtime aggiunto separatamente dopo l'esecuzione.

Lo scope certificato rimane quello approvato: UV affine isoparametrica strutturale su supporto B-spline noto e relativi trim. UV diagonali o non lineari restano sul percorso legacy esplicito; questi programmi non certificano callback generici o inversioni implicite.

## Sanitizer finale

**5/5 PASS**, exit 0, nessun diagnostic ASan/UBSan/LSan. Esecuzione con `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` e `UBSAN_OPTIONS=halt_on_error=1`, fuori dal sandbox per il limite ptrace di LSan. Log `sanitizer.log`; manifest aggiornato con binario e oggetti strumentati. I limiti di strumentazione descritti sopra rimangono invariati. Nessuna ulteriore modifica al test dopo il freeze.
