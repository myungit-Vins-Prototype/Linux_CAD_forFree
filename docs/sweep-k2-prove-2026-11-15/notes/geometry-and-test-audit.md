> Nota di versione: questo audit descrive la copia preliminare indicata dagli hash al suo interno. Un audit successivo ha trovato un errore di riduzione della dimensione con coordinate 1e-200; è corretto nella **v2 conservata in prototype/**, verificata da cinque test diretti. I risultati definitivi sono nel rapporto principale e in logs/. I vecchi percorsi /tmp e log non inclusi sono riferimenti storici, non necessari alla riproduzione del pacchetto.

# K2 — regressioni, testimoni e audit (staging 15 novembre 2026)

Nessuna modifica software nel repository, nessuna build globale. Letti AGENTS/CLAUDE, analisi K2 e grafo (sweep, B-spline/cache, checker, Bernstein). Tutto questo lavoro è sperimentale, non una correzione K2 di produzione.

## Riproduzione corrente e inventario

`overlap.cpp`, compilato direttamente con libforgekernel.a corrente, restituisce `checks=0 volume=19.7417086214508 upper_bound=12.6 faces=4`. Sorgente/libreria identificati in baseline.sha256. Comando riproducibile dalla radice:

```
g++ -std=c++17 -O2 -ffp-contract=off -Ikernel /tmp/forgecad-k2-20261115/seam/overlap.cpp forgecad-release/kernel/libforgekernel.a -pthread -o /tmp/forgecad-k2-20261115/seam/overlap
/tmp/forgecad-k2-20261115/seam/overlap
```

Fixture valide già presenti: SweepMinimalTwistJoinedSpatialPathWithStraightSpans, SpatialInflection, ClosedSpatialSpline; SweepLineAndArcAreExact; ObliqueProfileAlongArcUsesFiniteMeridians; PlanarPathIsTube; AlongHelixIsSpring; ClosedPathAndSheet; MinimalTwistHelixAvoidsUniformGridAliasing; ClosedSpatialSplineExplicitSeamParameters; LocalFoldUsesSectionGeometry. Inoltre sei SweepSheet nella suite test_sweep_surfaces.cpp. Coprono solidi/lamine, curve analitiche/NURBS, caps, fori, giunti tangenti, seam, scale/pose e K1/K3/K4. Nessuna di queste certifica K2.

## Nuovi test sweep (sweep_regressions.inc, congelato)

Sei test da aggiungere a test_sweep.cpp, riusando gli helper già presenti:

1. SweepRejectsGlobalTurnOverlap: elica K2 originale; scale .01/1/10 e pose ruotata/traslata, percorso intero/diviso; anello con foro. Baseline accetta tutti i nove casi: nove assertion di rifiuto falliscono.
2. SweepRejectsSingleFaceAndCapSideOverlap: ribbon unica faccia autoattraversante e cap/lato di elica 1.125giri. Baseline accetta entrambi. Controlli positivi: segmento toroidale quasi chiuso, R3, gap angolare .1, raggi sezione .5/.1. Questi sono VALIDI: l'angolo toroidale è unico nonostante teste vicine. Un primo atteso di rifiuto per r.5 è stato corretto prima della consegna finale.
3. SweepChecksDifferentRegionsAcrossTurns: due dischi inizialmente disgiunti y±.25,r.1 su elica R4,pitch.5; ogni regione isolata è valida ma insieme collidono fra giri successivi. Due regioni radialmente separate x±.3 sono controllo valido. Baseline fallisce solo l'assertion di rifiuto combinato.
4. SweepGlobalCheckPreservesClosedSeamsAndRepresentations: toro con foro, cerchio analitico/NURBS e intero/diviso; baseline PASS.
5. SweepGlobalCheckPreservesSeparatedInterleavedRegions: y±.375,r.1, R4,pitch.5: controllo valido che il filtro su hull comune rifiuta indebitamente.
6. SweepGlobalCheckPreservesRankTwoSheet: parabola nel piano XZ estrusa lungo Z, controllo valido esatto che il filtro volumetrico sull'hull 2D rifiuta indebitamente.

I primi quattro test sono compilati e provati separatamente sulla baseline; log `Sweep*.baseline.log`. Gli ultimi due riproducono i controesempi già verificati dal parent. Snippet senza nuovi include dentro classi; vettore PathSegment costruito con if/else esplicito per evitare warning GCC16 sui temporanei.

## Testimoni sui carrier B-rep effettivi

`witness_probe.cpp` usa la libreria di produzione attuale e valuta le superfici effettive, non la sola mappa sweep ideale. Newton/bisezione trovano candidati; questi residui double non sono presentati come certificati interval-Newton.

- Ribbon unica faccia: S(.45000673101253946,.5) e S(.54999406827553365,6.7815941611279555), distanza 3.0404709722440588e-16. Parametri distinti interni allo stesso carrier.
- Cap/lato: elica R1,pitch.1,turns1.125,Frenet. Faccia laterale1, u=1,v=.78698930038803638, residuo rispetto al piano cap finale -9.6277152916712794e-17; distanza dal centro cap .4000126627550622 <r.5 (margine circa .1).
- Multiregioni y±.25: faccia0 u=.00033306708260958762,v=.5 e faccia3 u=1.9996677926600464,v=6.780760102389503; distanza 2.2204460492503131e-16. Parametri interni, regioni diverse.

Output in witness.log, comando analogo a overlap.cpp con witness_probe.cpp.

## Controesempio valido: due famiglie elicoidali interlacciate

Questa derivazione riguarda la mappa analitica nominale Frenet, non certifica automaticamente il B-rep quintico vicino: il trasferimento al B-rep richiede il ponte C1 discusso sotto.

R=4, pitch p=.5, centri sezione q_y=±a con a=.375, r=.1. Definire c=p/(2pi), L=sqrt(R²+c²). Per frame iniziale x radiale, la traiettoria del centro di un disco a quota q è

C_q(theta)=R e_r(theta)+(q c/L)e_theta(theta)+(c theta-q R/L)e_z.

Le due curve sono eliche coassiali con raggio rho=sqrt(R²+(a c/L)²), fasi ±phi, phi=atan(a c/(LR)). Alla stessa coordinata angolare differiscono verticalmente di D=2(aR/L+c phi). Per due parametri qualunque scrivere differenza angolare delta=2pi*m+epsilon con |epsilon|<=pi. La distanza soddisfa

d²=2rho²(1-cos epsilon)+(c epsilon+mp-D)² >= A epsilon²+(c epsilon+mp-D)²,
A=4rho²/pi².

Minimizzando il quadratico per epsilon reale (bound inferiore ancora valido sull'intervallo) si ottiene

d >= min_m |mp-D| sqrt(A/(A+c²)).

Valutazione double: rho=4.000006954415053, phi=.00186472584801557, D=.7501484046759123, bound=.2497296871796377. Per una stima matematica con margine largo bastano c<.08, rho>=4, .74985<D<.7503: ne segue d>.2495. Ogni punto di un disco è entro .1 dal suo centro. Le due famiglie hanno quindi distanza >.0495 e non collidono. L'hull convesso comune aggiunge materiale assente e sovrappone le spire: Unresolved sull'hull non dimostra un difetto del vero sweep.

## Controesempio valido: lamina rank2 esatta

Parabola Bezier2D con poli (-1,0),(0,.2),(1,0), frame normalY/asseX, estrusione Z da0a2. Il carrier esatto è

F(u,v)=(2u-1,0,v-.4u(1-u)), u in[0,1],v in[0,2].

È iniettivo: u=(X+1)/2, v=Z+.4u(1-u). Area4 perché |F_u cross F_v|=2. L'hull del profilo ha dimensione2; il suo sweep G(x,y,v)=(x,0,v-y) ha rank2 su dominio3D e non è iniettivo. Ciò non implica autoattraversamento della vera lamina 2D. Qui il carrier di estrusione è esatto, senza fit general.

## Audit del core map-only e prove dirette

Versione auditata/coperta nei file audited_core.cpp, fk_sweep_validation.h, audited_core.o e map_runner; hash tutti in core-audit.sha256. Source hash d7c7f5a66b355b5c02d6daf0a9f0e83baa08ba62bca7b083af689ab8c914ee34. Ho ricompilato la copia della sorgente, senza dipendere dall'object che l'altro agente aggiornava.

Quattro test directmap PASS (`map.log`, `map_regressions.inc`):
- Slab rettilinei adiacenti certificati; backtracking non certificato; budget0=Unresolved.
- Ring razionale chiuso in quattro quarti certificato, seam esatta.
- Pesi comuni1e-200/duration1e200 equivalente certificato; input NaN e overflow restano Unresolved; cancellation propagata.
- Loop piccolo R=.001 certificato, due percorrenze dello stesso loop non certificate.

Audit matematico mirato: prodotto Bernstein H'W-HW' corretto; coefficiente combinatorio e aritmetica interval outward conservativi; convex hull dei poli razionali con pesi positivi; positivo definito via complementi di Schur interval sufficiente; precondizionatore double è solo matrice costante arbitraria e non deve essere inversione esatta. Scaling temporale positivo per pezzo corrisponde a una riparametrizzazione continua piecewise; scaling delle righe fisso comune al chart. Le chart dirette/cicliche comprendono i pezzi intermedi, quindi nessuna fascia di adiacenza viene ignorata. Checkpoint cancellazione inizialmente assenti in costruzione/input segnalati e presenti nella copia auditata. Non ho trovato falsi Certified nel contratto limitato map-only; non è una prova formale dell'intero modulo.

Build della prova diretta dalla radice:

```
g++ -std=c++17 -O2 -ffp-contract=off -Ikernel -c /tmp/forgecad-k2-20261115/seam/audited_core.cpp -o /tmp/forgecad-k2-20261115/seam/audited_core.o
g++ -std=c++17 -O2 -ffp-contract=off -Ikernel -Ikernel/tests -I/tmp/forgecad-k2-20261115/seam /tmp/forgecad-k2-20261115/seam/test_map.cpp /tmp/forgecad-k2-20261115/seam/audited_core.o forgecad-release/kernel/CMakeFiles/forgekernel_tests.dir/tests/fk_test_main.cpp.o forgecad-release/kernel/libforgekernel.a -pthread -o /tmp/forgecad-k2-20261115/seam/map_runner
/tmp/forgecad-k2-20261115/seam/map_runner
```

## Relazione mappa→B-rep e conclusione

La via volumetrica è molto più circoscritta di un validatore universale delle superfici B-rep. Il suo certificato si riferisce solo ai coefficienti e all'hull forniti. Non è ancora un guard promuovibile su tutti gli sweep supportati.

`fitQuinticRows` risolve ogni riga separatamente: l'affinità ideale delle sezioni non è identità algebrica delle righe double memorizzate. Fit comune origine+assi riduce il disallineamento ma assemblare Pj+xiAj+yiBj introduce altri arrotondamenti. Un bound C0 fra superficie nominale ed effettiva non trasferisce iniettività: piccole oscillazioni possono piegare il carrier. Servono un bound C1 e un teorema quantitativo con ipotesi soddisfatte, oppure una rappresentazione comune esatta realmente usata dal B-rep. Inoltre edge trasversali analitici e carrier quintici sono cuciti entro tolleranza: bisogna distinguere il B-rep tollerante da una singola superficie matematica esattamente incollata.

L'hull comune crea falsi rifiuti su regioni disconnesse e lamine con profilo curvo (due controesempi sopra). Per proseguire: trattare il dominio effettivo con componenti/celle parametriche e membership dei fori, certificare lamine nella dimensione corretta e completare il ponte C1/representation. Nessun Unresolved può essere chiamato IntersectionWitness. Il core restituisce correttamente solo CertifiedInjective/Unresolved nella prima versione; il tipo ternario è predisposto ma il ramo witness non è simulato.

K2 resta aperto. Core/adapters/test rimangono sperimentali riproducibili; nessuna correzione completa né modifica dell'eseguibile viene dichiarata.
