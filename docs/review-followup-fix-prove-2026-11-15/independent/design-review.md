# Revisione indipendente del disegno K7/K8/K9

**Esito finale:** audit sul sorgente integrato SHA`08ac51e6fd3fd99d262fe8d0363c7437744b7714b34166dac7e74589bcf018e9`,60/60controlli indipendenti PASS. K10 confermato preesistente e aperto separatamente. Le sezioni e i log con SHA precedenti descrivono passaggi intermedi; `final-results.json`, `final-torus-cancel-k10.log` e `final-edges-extrusion.log` sono le prove finali autorevoli.

Sorgenti e grafo consultati senza modifiche. Probe compilato soltanto in questa directory temporanea, riusando il comando test_offset.cpp da compile_commands e l'archivio Release corrente; nessuna build condivisa. `baseline.log` fotografa gli esiti prima della patch.

## Contratto e chiamanti

`fk_offset.h` richiede O=S+dN nello stesso parametro e rifiuto di degenerazioni/cuspidi; `fitCurve` promette approssimazione parametrica entro tolleranza e deviazione misurata. Chiamanti esterni nel grafo conservati in `callers.json`: interfaccia offset, shell/thicken/open_offset/draft, mappa quilt, curve di schizzo/proiezione, blend, boundary, sheet, scambio e modifica poli.

`offsetFaces` ricava finestre UV dai loop e dalle SP-curve, ricompone gli intervalli sul cerchio, aggiunge margini e normalizza quasi-periodi completi. La finestra v può quindi essere negativa o traslata di più periodi. `offsetJoinedFaces` ricava finestre dal trim costruito e dalle SP-curve nuove e chiama `offsetSurfaceImpl` anche con `checkCurvature=false` per continuazioneC2: il nuovo controllo analitico del toro non deve essere accidentalmente escluso da quel flag, che attualmente riguarda solo BSpline.

## K8: criteri matematici

Per toro S=O+(R+r cos(v))e(u)+r sin(v)Z, porre rho(v)=R+r cos(v). Su×Sv=r*rho(v)*(cos(v)e(u)+sin(v)Z). Pertanto la normale unitaria è sigma*q con sigma=sign(rho), non sempre q.

Su un intervallo v con segno sorgente costante, il candidato esatto ha r_new=r+sigma*d. Richiedere r_new>budget e rho_new dello stesso segno della sorgente su tutto l'intervallo evita attraversamento dell'asse/collasso e conserva le due direzioni principali. Se la finestra sorgente contiene rho=0, la normale non è definita: rifiuto esplicito. Un semplice controllo r+d<R globale rifiuterebbe anche trim esterni perfettamente validi. Un semplice rho_new>0 sul trim rifiuterebbe il ramo negativo locale di un supporto spindle originale.

Il costruttore ToroidalSurface (`fk_surface.cpp:199`) richiede solo raggi positivi: ammette horn/spindle. makeTorus invece documenta r<R. I supporti originali possono quindi essere più generali delle primitive. La correzione può supportare il ramo negativo tramite r_new=r-d oppure rifiutarlo esplicitamente come limite; non deve restituire r+d come esatto su quel ramo.

Bounds continui del coseno: estremi dell'intervallo più min=-1 se contiene un multiplo dispari di pi e max=1 se contiene un multiplo pari; se lunghezza>=2pi usare [-1,1]. Gestire periodi traslati senza limitarsi al dominio [0,2pi]. Considerare valori molto grandi di v, intervalli non finiti e precisione degli estremi per evitare overflow di cast interi nei test del periodo.

### Controprove reali baseline

- R5/r1/d3, toro completo: regolare, errore~1.8e-15, normali concordi.
- d4 completo: accettato singolare; d5 completo: accettato con normale invertita.
- d5 con v[-.4,.4]: **deve restare accettabile**, errore~2e-15, normali concordi. Stesso risultato con v traslato di4pi.
- d5 con v[3pi-.2,3pi+.2]: oggi accettato ma normale invertita: finestra periodica avversa da rifiutare.
- Sorgente spindle R5/r6, trim esterno: offset .1 valido, errore~4e-15. Trim interno intorno a pi: oggi rifiutato dal controllo posizionale finale; non è un nuovo difetto accettato.
- Sorgente horn R5/r5, dominio completo, offset-.1: oggi accettata sebbene la normale originale a pi sia indefinita; candidato controllo sorgente. Trim esterno horn resta regolare.

Per i body, ripetere preserveSeams=false/true, normali faccia sense opposto e offset negativo. Trim fuori dalla regione critica non dovrebbe ereditare il rifiuto del supporto completo; margini UV possono tuttavia produrre rifiuti conservativi espliciti vicino alla soglia.

## K7: esattezza e fit

Il callback f è generico: nove valori coincidenti non dimostrano una traslazione sul continuo. Per una B-spline arbitraria la scorciatoia va protetta da una premessa geometrica, oppure evitata. Una certificazione di traslazione è possibile se tutti i supporti coinvolti sono piani con normali costanti; quando trimSeams introduce correzioni dagli estremi o Newton bisogna verificare anche che la correzione sia costante, non riusare la premessa alla cieca. I percorsi offsetCurve per bordi sono distinti dalle curve base di estrusione/rivoluzione; non basta correggere uno solo.

Casi da conservare: curve planari NURBS razionali esatte traslate, anche con knot stretti e TrimmedCurve annidati; cerchi completi e archi; Line/Circle sotto TransformedCurve; estrusione obliqua del cerchio (non sempre una circonferenza offset); rivoluzione del meridiano con assi differenti e trim che esclude l'asse. Il cerchio completo passa campioni0/pi/2pi quasi allineati: non basare il centro su un prodotto vettoriale quasi nullo senza ripiego.

Passare al fit risolve il riproduttore grado4 multispan perché i breakpoints fanno emergere il piccolo tratto. Non costituisce una certificazione globale per callback arbitrari o spline di grado elevato.

**Controprova reale del fit esistente**, non rilievo separato promosso: f(t)=(t,0,1e4*[t(t-.25)(t-.5)(t-.75)(t-1)]^3), range[0,1], tolerance1e-7. fitCurve restituisce4poli e deviazione dichiarata1.195e-11, ma su10001valutazioni indipendenti lo scarto è0.000445999063689424 (4459.99×tol). I campioni di accettazione e le derivate agli estremi non vedono la variazione. Non dichiarare che il solo fallback Hermite certifica ogni spline; eventuali difese ulteriori dovrebbero usare informazioni geometriche/limiti per campata, non solo un diverso numero fisso di campioni.

## K9: checkpoint dopo callback

Caso avverso semplice: f(t)=(t,0,0), imposta token=true dentro f(.75), ultimo campione del primo segmento già accettabile. Baseline torna successo dopo12callback e scrive deviation. Checkpoint solo a ingresso/inizio ricorsione o prima dei callback non basta: serve controllo dopo valutazioni oppure almeno dopo ultimo segmento/prima di assegnare output e ritornare.

Verificare: token già attivo produce OperationCancelled prima di invocare f; cancellazione nel primo callback, nelle derivate, nell'ultimo campione e con più break; deviation sentinella resta invariata su cancel. OperationCancelled non deriva da std::exception: i percorsi di recupero geometrico non devono intercettarlo come errore fit.

La callback di continuazione in fk_blend_surface.cpp:1631–1655 usa evaluate diretto/Newton e non eredita i checkpoint Surface::point/normal: è un chiamante pertinente al fix. La sonda non misura latenza GUI né durata di un singolo callback Newton.

## Ripetizione

`python3 /tmp/forgecad-followup-fix-20261115/independent/build.py` e poi `.../probe`. Il build.py legge compile_commands e il link.txt correnti, non rigenera CMake e non riscrive oggetti del repository. Per la revisione patch successiva collegare prima dell'archivio un oggetto fk_offset.cpp compilato privatamente con il relativo comando reale.


## Audit patch staged e risultati indipendenti

Letto lo staged `/tmp/forgecad-followup-fix-20261115/kernel/fk_offset.cpp`, copiato in `fk_offset.staged.cpp` e compilato privatamente con il comando reale da compile_commands. Oggetto privato anteposto all'archivio Release esistente. Identità sorgente in `staged-source.sha256`; risultati macchina in `independent-results.json`; output in `patched.log` e `baseline.log`.

**12/12 controlli indipendenti PASS** sulla patch esaminata: dieci combinazioni toro (incluse finestre traslate, source horn/spindle, entrambi i rami regolari) più due momenti di cancellazione. Nessun nuovo problema rilevato nelle formule exactExtrusionOffset/planeTranslation e nei bounds continui del coseno. `checked` protegge ogni callback prima/dopo e la pubblicazione della deviazione.

Non posso estendere questa conclusione a tutti i chiamanti con il solo probe; regressioni shell/loft/estensioni e app appartengono alla verifica integrata del coordinatore.

### Residuo del fitter confermato con geometria reale

Richiesta esplicita del coordinatore: non soltanto callback sintetica. Costruita una **BSplineCurve<3> di grado27**, clamped su[0,1],28poli e pesi unitari, con coordinate x(u)=u, y(u)=0 e

z(u)=1e10·[u(u−1)(u−.25)(u−.5)(u−.75)(u−.05)(u−.275)(u−.725)(u−.95)]³.

I coefficienti Bernstein sono ottenuti esattamente con Fraction dalla forma in potenze e poi convertiti in double; tutti i28poli sono nel probe ripetibile. ExtrusionSurface reale lungoY, offset .001, tol1e−7, finestra[0,1]×[0,1]. La patch accetta ma10001valutazioni indipendenti misurano errore **5.7424750615e−5**, cioè574.2475×tol. Picco originale z=5.7424751728e−5; raggio minimo di curvatura campionato14.5781 (quest'ultimo è campionato, non un bound globale dimostrato).

È un residuo distinto della convalida campionata del fit, **non una regressione introdotta dalla rimozione del fastpath K7**: il probe baseline viene rieseguito contro lo stesso archivio senza oggetto patch. La curva ha una sola campata e i tre campioni del fit più cinque campioni finali di superficie cadono sulle radici. Non usare il successo dei nuovi test K7 per affermare certificazione globale degli offset liberi; chiudere il difetto del riconoscitore esatto con il limite del fit esplicitamente conservato. Non proposto qui un redesign globale del fitter.


### Validazione della geometria e classificazione K10

**K10 confermato e preesistente**, distinto dal riconoscitore esatto K7. Il probe con identici28poli produce il medesimo errore e successo su baseline e staged. Non è soltanto un limite teorico per callback impossibili: il costruttore BSplineCurve supporta il grado27, la curva è una singola campata polinomiale reale, il parametro x è strettamente crescente e tutte le coordinate sono finite. Max|polo_z|104.07526656293439, maxnorma polo 104.076558223261; nessun peso razionale estremo.

Confronto indipendente su10001parametri fra valutazione kernel e prodotto fattorizzato in longdouble (derivate tramite prodotto): errore massimo posizione8.506e−15, derivata prima3.507e−13, seconda2.073e−11. Lo scarto dell'offset5.742e−5 è dunque nettamente separato dagli errori numerici della sorgente.

**Bound continuo non campionato**: `source_bounds.py` interpreta come Fraction esatta ciascun coefficiente double effettivamente passato al kernel, deriva i coefficienti Bernstein e suddivide7volte al punto medio con aritmetica razionale esatta. La proprietà convex-hull fornisce:

- x'≥0.9999999999999984; |x''|≤7.793765632868599e−14;
- |z'|≤0.0014723991627257414; |z''|≤0.0686309870442156;
- k≤(max|x'|max|z''|+max|z'|max|x''|)/min(x')³≤0.06863098704421612;
- raggio di curvatura≥14.570677809948167; d·k≤6.863098704421612e−5<1.

Questi sono bounds globali sull'intervallo[0,1] della curva rappresentata dai double, non una deduzione da campioni. Per l'estrusione lungoY la seconda curvatura principale è0; pertanto l'offset richiesto .001 rimane regolare. `source-bounds.json` conserva dati e metodo ripetibili. I valori decimali stampati sono arrotondamenti dei rapporti razionali calcolati: il confronto esatto d*k<1 è un assert nello script.

K10 è quindi un risultato geometrico fuori tolleranza accettato dal fitter e dalla verifica finale, esistente già prima di questa passata. La correzione del fastpath K7 non lo introduce e non lo risolve. La decisione sullo scope spetta al coordinatore; non eseguito qui un redesign del fitter e non ampliata la patch5.


## Audit helper rawEdgeOf — SHA640a610ec977fe86673a7568094988d5bc1001464696b3fa0e233b32406c5471

Riletto anche l'aggiornamento con `constantNormal` (piani o generatrici rettilinee di cilindri con direzione parallela esatta all'asse) e cerchi di sezione dei cilindri coordinate-axis. Geometrie valide rispettano le premesse; il segno del raggio per il cerchio segue `Face::sense`, non il verso della curva. Per l'estrusione circolare il verso deriva da dot(D,cross(x,y)). Nessun nuovo errore individuato in questi rami.

**30/30 prove aggiuntive PASS**, eseguibile privato `edge_probe`, log `edges-patched.log`: tre frame (asse+Z,−Z e(.2,.3,1)); offset±.1; cilindro con sole facce laterali, soli cappucci o tutte; lamina ottenuta da arco circolare con due TrimmedCurve annidate e versi di percorrenza opposti (normale faccia sense±). Ogni fin verificata su101parametri nel proprio parametro mantenuto; `checkBody` zero issue. Nei casi coordinati curve circolari/rette restano analitiche. Nei casi ruotati questa versione passa al fit: errore bordo/superficie1e−8…9.4e−8, sotto tol1e−7 ma senza garanzia di area al1e−9. Il coordinatore ha separatamente osservato perdita di area2.89e−7 su prisma arrotondato ruotato e autorizzato ulteriore fix di covarianza: queste30prove non certificano quell'aggiornamento successivo.

### Formule di covarianza discusse con implementatore

Per cerchio C+r e(t) su supporto cilindrico di asse(z,O), porre Qz=I−zz^T e delta=|Qz(C−O)|+r(|z·x|+|z·y|)+roundoff. Allora |Qz(C−O)+rQz e−r e|≤delta su tutto il parametro. Per delta<r il bound della differenza fra normali unitarie è≤2delta/(r−delta), con normalizzazione/roundoff inclusi. Errore di costruzione del cerchio offset≤|d| volte tale bound. Il raggio del cilindro non deve essere assunto identico al raggio della curva per questa specifica prova delle normali.

Per estrusione circolare con direzioneD, separare a=dot(D,n) dalle componenti tangenziali. eps=|D·x|+|D·y| maggiora la perturbazione di cross(T,D) rispetto ad a e(t). Se |a|>eps, errore normalizzato≤2eps/(|a|−eps) con segno sign(a). Autorizzazione dell'analiticità entro una frazione esplicita della tolleranza e budget roundoff separato è coerente e non dipende da un numero fisso di campioni.

Caveat comunicato: la derivazione cilindrica confronta unitradial(curve(t)), mentre i callback di produzione usano la normale in pcurve(t). Con SP-curve tolleranti l'eventuale discrepanza di fase va assorbita nel budget ereditato o resa precondizione esplicita; le prove generate hanno PCurve coerenti e controllano comunque lo scarto sul bordo. Non individuato un nuovo caso concreto di regressione accettata.


## Esito finale sulla versione integrata

Verificata identità byte per byte fra `kernel/fk_offset.cpp` nel repository, staged privato copiato in `fk_offset.staged.cpp` e oggetto privato impiegato dai probe: sorgenteSHA`08ac51e6fd3fd99d262fe8d0363c7437744b7714b34166dac7e74589bcf018e9`. Il test di produzione `kernel/tests/test_offset_validation.cpp` risultaSHA`7d70490b069ca9fb23f14089181baa809367611bd6a20d36282ee668b9bc8f30`. I probe indipendenti non usano i test di produzione per le proprie asserzioni.

Replay dopo integrazione mediante `python3 final_verify.py`:

- **12/12** controlli toro/cancellazione: collassi/ripiegamenti completi e traslati rifiutati; trim esterno e spindle interno/esterno regolari conservati; horn sorgente singolare rifiutato; cancellazione nell'ultimo callback e prima del primo rispettata senza scrittura di deviation.
- **48/48** controlli su bordi/estrusioni:30casi lato/cappucci/tutto cilindro, archi Trimmed annidati, sense±, offset± e3frame;18casi direzione estrusione±,3frame e deviazione da parallelismo0/1e−10/1e−3. Bordo e superficie confrontati su101parametri; estrusioni su1001parametri; checker zero issue per i body costruiti.
- I cerchi cilindrici ruotati rimangono **Circle**, con residui circa1e−15. Le estrusioni quasi parallele1e−10 mantengono Circle con errore~9.8e−12, entro il budget analitico dichiarato; quelle oblique1e−3 producono BSpline e scarto≤9.474e−8, sotto tol1e−7.
- **K10 resta riprodotto** dalla stessa versione finale: vera spline27, successo e scarto5.7424750615e−5. Conferma baseline identica, bound razionale continuo raggio≥14.5706778099, valutazioni kernel accurate rispetto alla formula indipendente. È aperto e distinto dai tre fix esaminati.

Audit formula finale: `delta` cilindrico include lo scarto massimo3D `Fin::pcurveTolerance`, come da contratto `fk_topology.h`, quindi la prova confronta anche N(pcurve(t)); il percorso della generatrice rettilinea richiede PCurve esatta. Il bound dell'estrusione mantiene il segno dell'asse e richiede denominatore positivo. Budget costruzione/normalizzazione e errore geometrico cumulati prima di autorizzare il cerchio entro0.01tol. Nessuna obiezione residua sul disegno, nessuna regressione concreta emersa in questi percorsi.

`final-results.json` conserva SHA di sorgente/test/binari, comandi dei replay ed esiti; `SHA256SUMS` registra le prove indipendenti. I file `patched.log`, `edges-patched.log` e `staged-source.sha256` descrivono l'ultima compilazione privata; il vecchio `independent-results.json` era il primo audit intermedio e non va usato come esito finale. Nessuna compilazione condivisa, modifica produzione/CMake/CLAUDE o cancellazione delle22orfane effettuata da questo agente. Build completa, renderer e suite integrate sono responsabilità del coordinatore e non vengono qui dichiarati verificati.
