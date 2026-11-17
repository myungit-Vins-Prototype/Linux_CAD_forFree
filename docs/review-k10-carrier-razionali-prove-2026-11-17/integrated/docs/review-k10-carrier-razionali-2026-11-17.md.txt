# K10: carrier NURBS razionali e pcurve nel dominio — 17 novembre 2026

Proseguita la review con tre agenti GPT-6.1 Sol, separando implementazione, test e audit indipendente. **Nessun commit o push**: HEAD e origin/main restano `47c7c19`. Le precedenti correzioni locali sono preservate. **33 bug funzionali corretti invariati**: questo intervento amplia la copertura del limite K10 già censito.

## Correzione

Il controllo continuo delle fin emesse da `extendSheet` comprende ora superfici BSpline native nonperiodiche con grado U e V pari a 2 oppure 3, anche misti, e pesi originali finiti strettamente positivi e variabili. Restano richiesti un bordo E BSpline cubico e una pcurve Q Line2 oppure BSpline2 grado1/3, con pesi E/Q assenti o esattamente costanti positivi. Pesi variabili E/Q, istanze, wrapper, superfici periodiche e altri gradi restano `Unsupported`.

Il provider usa coefficienti omogenei originali e intervalli arrotondati verso l'esterno. Limita il numeratore del residuo `H − (E−origine)·W` su ciascuna cella candidata e lo divide per un limite inferiore positivo di W. Sul sottoinsieme dove la cella è attiva, positività e partizione dell'unità delle basi danno `W ≥ min(pesi originali)`: l'estensione polinomiale fuori dalla cella può essere negativa e non viene usata per affermare positività. Si considerano tutte le celle nodali chiuse intersecate dall'immagine UV, incluse le adiacenti. Il limite indipendente dell'inviluppo di W può rafforzare quello dei pesi originali. Nessun campione è assunto come prova.

L'estensione del controllo ha individuato sei regressioni pubbliche in cui la Q costruita supera il dominio naturale della superficie per poche unità di arrotondamento: ImplicitPolynomial, Rational, SmallScale, LargeScale, FaceSense e CanonicalReversal. Nell'export attuale Rational il superamento V è esattamente `25/2^49 ≈ 4,44e-14` **in coordinate UV**, non in millimetri. In altri casi V arriva a `127/2^51`.

La costruzione privata del ledger prepara ora una Q candidata nel rettangolo UV naturale **prima della verifica continua**:

- Una Line viene rappresentata con una BSpline di grado1 sugli stessi estremi del range edge, usando origine e direzione effettive. Si limitano i due poli al dominio. La conversione avviene sempre nella famiglia ammessa: il solo endpoint arrotondato apparentemente interno non prova il contenimento dell'espressione affine esatta. Ricostruire una Line normalizzerebbe la velocità e cambierebbe la parametrizzazione.
- Una Q BSpline conserva grado, nodi e pesi; se occorre si costruisce una copia con poli limitati al dominio. La Q condivisa originale e le fin non previste dal ledger restano invariate.
- La candidata è accettata e assegnata alla fin soltanto se il provider certifica l'intero residuo `|E(t)−S(Q(t))|` entro il cap fissato prima del fit. Limitare i poli è costruzione, non prova. Il cap non viene aumentato; edge, superficie, range e sense non cambiano. Un risultato riconosciuto `Failed` è terminale e non passa a un controllo campionato. `Unsupported` conserva la Q originale e il percorso legacy.

Dopo l'unificazione resta il controllo già introdotto sulla genealogia esplicita, sull'identità delle curve e sui metadati, anche senza history richiesta dal chiamante. Non sono state modificate in questo intervento le integrazioni precedenti in `fk_sheet.cpp` e `fk_blend_model.h`.

## Verifiche finali

| Verifica | Esito e perimetro |
| --- | --- |
| Build completa | PASS, app aggiornata, circa166s; test target ricompilato dopo il congelamento della fixture finale. |
| CTest pertinenti | 16/16 PASS, zero skip; nomi esatti e JUnit nel pacchetto. |
| Kernel generale | 483/483 PASS, runner ricompilato. |
| Driver mirato | 67/67 PASS, senza rimuovere le 33 regressioni pubbliche precedenti. |
| ASan/UBSan/LSan | 67/67 PASS sui due TU modificati provider/ledger e test/main strumentati; resto kernel Release. Perdita intenzionale di19byte rilevata con exit23 fuori ptrace. Non certifica l'intera app. |
| Geometria pubblica | 6 casi,18 fin finali: runtime Certified, capedge=capfin=capPrefit; input immutato, Rational senza history byte-identico. |
| Audit indipendente Fraction | 18/18 PASS sugli originali IEEE. Rational massimo2,737789171e-11mm <1e-7; implicit2,059007839e-10mm <1e-7; LargeScale1,474654696e-7mm <2,331437325e-6 fissato prima del fit. |
| Avvio Wayland | PASS5s con profilo temporaneo ed eseguibile effettivo verificato; nessun documento/processo utente toccato, nessuna certificazione pixel/GL. |
| Architettura | 785 nodi /11461 relazioni /395 TU, zero errori AST; hash, HTML, configurazione e archi nei due versi verificati. |
| Conservazione | Baseline5796file,24pacchetti precedenti e nove sorgenti API locali preservati; regressioni SheetJoin e Offset originali immutate. Index vuoto; nessun commit/push. |


Il totale mirato comprende 13 test provider precedenti, 9 nuovi test razionali, 12 test ledger e le 33 regressioni pubbliche originali, incluse le quattro varianti OffsetLoft nonISO. I nuovi casi verificano gradi misti, pesi con fattori 2^±200, celle con estensione del denominatore negativa ma peso attivo positivo, trim/range/verso, picchi nascosti, risorse, cancellazione e rifiuto di gap reali. Il test Line a mezzo ULP copre la differenza fra contenimento arrotondato ed esatto. Nella prima fixture di quest'ultimo test il dominio nodale non coincideva con la mappa fisica ipotizzata: è stata corretta la fixture, senza modificare cap o alleggerire le regressioni originali.

L'audit Fraction usa i coefficienti IEEE appena esportati dai sei risultati pubblici, dopo unify: tre fin per caso, common ed extra. La selezione segue la storia dichiarata e i cap originali, senza riassociazioni per prossimità. Gli export diagnostici privati prima/dopo unify sono evidenze aggiuntive distinte dal driver API indipendente. Le prove su vecchi export sono indicate come storiche.

## Limiti e prossimo lavoro

**K10 resta parziale**: il certificato riguarda la posizione delle fin della famiglia riconosciuta. Non certifica convergenza o scelta del ramo Newton, trim globali, topologia, autointersezioni o preservazione globale del carrier. Le altre famiglie restano da estendere; non è stata introdotta una tolleranza UV permissiva. Unify che cambi parametrizzazione, ricostruisca Q o fonda range senza una mappa supportata può essere rifiutato conservativamente.

Risorse fisse: 100.000 poli E/Q, 2.000.000 scalari, 16.384 visite, 256 celle per nodo, profondità24, grado composto massimo21. La componente omogenea aggiuntiva è conteggiata prima dell'allocazione; cancellazione propagata. Questi limiti possono rifiutare geometrie valide e non sono una deadline temporale rigida. L'helper opera sul corpo candidato privato: non promette una transazione autonoma su un Body arbitrario dopo un errore tardivo.

Il limite separato **Superfici.prt scalato2,5/ruotato0,42** è stato riprodotto sulla fixture B-rep anonima già conservata, senza aprire o modificare il documento utente. Input valido, guscio chiuso1,25: fallimento `fitCurve: impossibile rispettare la tolleranza entro il limite di raffinamento`. Anche il vecchio prototipo locale non risolveva il caso; non è una correzione pronta da integrare. Rimane aperta inoltre la chiamata kernel inversa Flacone3, per cui l'app dispone già di un recupero validato.

Non sono nuove verifiche della sessione completa Wayland schede o della UI nativa macOS. La CI verde precedente non certifica questo delta locale: una nuova CI GitHub richiede il successivo commit/push autorizzato dall'utente.

[Prove riproducibili, log, snapshot inerti e manifest](review-k10-carrier-razionali-prove-2026-11-17/README.txt).
