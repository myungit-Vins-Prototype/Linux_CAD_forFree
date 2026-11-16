# K10 — giunti dei raccordi a mitra, 16 novembre 2026

Corretto e verificato il **ramo quasi coincidente isoparametrico nell’API interna** dei giunti a mitra, con tre agenti GPT-6.1 Sol per implementazione, test e audit indipendente. La build completa dell’app è aggiornata; riaprirla per usarla. **K10 passa a 23/24 siti iniziali parzialmente trattati, con uno interamente aperto; il totale resta31 rilievi corretti.** Questo non significa che23 siti siano completamente risolti.

Il riproduttore corretto è sintetico e usa la funzione interna `detail::traceBlendMitre`. Le16 regressioni pubbliche sono riuscite, ma una diagnostica privata non vi osserva il nuovo ramo ISO: non attribuiamo a questa consegna la soluzione di un errore su un documento pubblico. Diagonali e Newton restano campionati e parzialmente aperti.

## Difetto provato e campo della correzione

La baseline selezionava una cucitura quasi coincidente verificando65 punti, fittava la media dei due supporti e misurava poi il residuo in129 punti. Una variazione NURBS stretta fra i campioni poteva essere ignorata. Il chiamante perdeva le mappe UV originali e poteva aumentare la tolleranza del bordo dopo una misura campionata.

Il negativo **ISO** costruisce due superfici B-spline reali, con più campate V e un picco nel tratto1/512–3/512. La libreria iniziale restituisce un cut con `gap=4,259925745486726e-12 mm`. Root ha esportato i coefficienti autentici usando libreria e header vecchi congelati: a **t=1/256**, il residuo Z rispetto alla mappa affine originale del secondo supporto è **esattamente1/2 mm**, contro cap1e-7 mm. Il verificatore Fraction confuta il contratto; la nuova via rifiuta la costruzione invece di pubblicare quel cut.

La conversione a 17cifre round-trip/IEEE, il quadrato razionale e il bound sul primo supporto sono stati ricontrollati indipendentemente dall’agente audit. Non è una stima da griglia né una pretesa di minimo globale punto-superficie. L’export riguarda il cut interno e la sua mappa originale, non una fin pubblica.

**Limite sostanziale:** i carrier rolling-blend pubblici correnti hanno una sola campata quadratica razionale V; questo negativo ISO ne usa più d’una. Non è stato riprodotto attraverso `blendEdges` su un CAD. Un secondo negativo **diagonale**, U3/V2, ha testimone razionale di circa 0,13868557 mm contro gap circa 6e-12 mm: rimane intenzionalmente aperto nel percorso non certificato. Non lo contiamo come correzione ISO.

I cinque test diagnostici iniziali passano 4/5 sulla baseline: fallisce il negativo ISO. Un secondo gruppo passa 3/4: il budget di fit invalido non era rifiutato. Quest’ultimo è robustezza dell’input, distinto dalla prova geometrica; nessuna nuova asserzione su campi introdotti viene presentata come vecchio bug. La cancellazione diretta passava già: non è stato inventato un errore dei catch, e `OperationCancelled` non deriva da `std::exception`.

## Implementazione finale V2

I65 campioni restano un selettore algoritmico. Se entrambi i supporti sono B-spline esplicite e le mappe originali sono isoparametriche affini, il ramo richiede la verifica continua. Un input ISO riconosciuto che non si può certificare fallisce **senza recupero campionato**.

Il cap fisico è `linearResolution()`, fissato prima della costruzione e coerente con il profilo di precisione corrente. Il fit su S usa `min(fitTolerance,0.25*cap)` con `fitCurveCertified`; la curva cubica viene poi confrontata continuamente con **entrambi** i supporti originali al cap completo. In questo modo un disaccordo ammissibile di mezzo cap non viene erroneamente obbligato a rientrare nel budget più piccolo del fit. `gap` contiene un upper bound, mentre il cap rimane distinto e non cresce dopo la misura.

`BlendCut` conserva le due pcurve originali grado1 e il budget del certificato. `originalBlendMitreUV` annulla esplicitamente il verso del carrier attraverso il suo dominio V. Prima dell’assemblaggio `installBlendMitreCertificates` installa le mappe usando indice del bordo pianificato e identità del supporto, con esattamente una fin per obbligo. Nessuna riassociazione per prossimità.

Dopo assemblaggio `requireEmittedBlendMitres` verifica identità di curva/supporto/pcurve, range, budget, estremi e numero delle fin; ricostruisce il provider sui coefficienti effettivi. I bordi certificati sono esclusi dal loop che può assegnare `1.01*gap` agli altri bordi. Dopo `computePCurves` e i controlli finali vengono ricontrollati. Non c’è `unifySameDomain` dopo la mitra in questo percorso; l’unificazione tangente di preparazione è precedente.

Gli helper restano nel modulo dedicato `fk_blend_mitre`, mentre il coordinatore li richiama. Validati prima dei rami budget/scala/parametri finiti e degenerazione gX; controlli di cancellazione anche nei loop Newton e nel raffinamento. Modificati due TU e un header interno, CMake, catalogo e un test mirato. Nessun formato persistente, enum documento o comportamento UI modificato.

## Compatibilità e limiti conservati

V1 aveva eliminato il vecchio fastpath anche per diagonali non supportate: il positivo S=T con UV(f,f), prima riuscito, falliva su Newton singolare. **V2 conserva quel fastpath soltanto per famiglie non supportate**, con `certificateBudget=0` e commenti espliciti: fit della media e verifica129 punti restano campionati. Il positivo diagonale è una regressione permanente. Un certificato ISO fallito non può rientrare in questo ramo.

Newton/Hermite, zona iniziale singolare, raffinamento minimo, diagonali e altre curve del raccordo conservano i limiti precedenti. Un selettore che scelga Newton per un input ISO non quasi coincidente resta fuori dal certificato. Il testimone diagonale rimane un limite K10 noto. Il sito interamente aperto successivo è `extendSplineBoundary` in `fk_blend_surface.cpp`.

Il certificato non prova trim membership globale, topologia, iniettività, volumi, G1/G2 o assenza di autointersezioni. Controlli ordinari e tessellazione non sostituiscono il limite continuo. Metadati preesistenti oltre il cap, mappe/range modificati, incidenze perse o ambigue e risorse insufficienti possono causare un rifiuto conservativo. Un eventuale stadio successivo che modifichi la geometria non eredita automaticamente il certificato locale.

## Verifiche sui byte finali

| Prova | Risultato e campo |
| --- | --- |
| Baseline pubblica | 4/4 CTest e riferimento generale analitico1/1 PASS prima dell’integrazione |
| Build completa | PASS375,35 s: app e runner aggiornati |
| CTest selezionati | **27/27 PASS**,89,71 s: kernel, raccordi, storia, anteprime, pannelli, superfici, schede e tema |
| Suite permanente integrata | **13/13 PASS**: negativo ISO, polinomiali/razionali, posa/scala2,5/ordine, due budget distinti, oltre cap/input invalidi/cancellazione, coefficienti e fin reali |
| Regressioni pubbliche private V2 | **16/16 PASS**, sei cavità e dieci blend_features: box/tronco piramidale, sweep, loft e lamine; non sono prove pubbliche del ramo ISO |
| Riferimento analitico integrato | **1/1 PASS**, `BlendGeneralMatchesAnalytic` dopo la build |
| Regressioni core V2 | **69/69 PASS**, cinque runner privati; si sovrappongono ad altri controlli, non vanno sommati come casi unici |
| ASan/UBSan/LSan V2 | **19/19 PASS**, exit0 fuori ptrace:13 mirati+6 cavità. Strumentati mitre/surface e driver; resto kernel/provider Release |
| Coefficienti Release finali | Due JSON identici byte per byte al candidatoV2: cinque cut ISO e un Body sintetico con due fin effettive |
| Wayland | Raccordi avanzati con salvataggio/rigenerazione/Undo/cancellazione PASS; ricaricamento anteprima PASS; tema nelle schede **1.569 controlli PASS** |
| Avvio app | Processo isolato vivo dopo5 s, `/proc/pid/exe` corretto; terminato soltanto il processo di prova |
| Architettura | **777 file /11.312 archi /389 TU**, zero errori; `--check`, hash/compileDB, HTML e call/include/reference bidirezionali PASS |
| Oggetti kernel | 93 membri: soltanto mitre/surface modificati, **91 identici** alla baseline |

La prova V1 in sandbox aveva asserzioni riuscite ma LSan terminava per ptrace: è esclusa dal PASS leak. V2 ha leak detection attiva, senza soppressioni. Una calibrazione separata rileva una perdita intenzionale di 19 byte con exit23: serve a verificare lo strumento e non è un leak del kernel. La strumentazione mirata non certifica l’assenza di leak in tutto il programma.

La diagnostica privata V1 sui16casi pubblici conta121 invocazioni del tracciatore, tutte Newton/coincident=false; nessuna ISO osservata. V2 passa gli stessi16casi dopo la scelta di compatibilità. Include header OCCT del framework storico, ma quel runner è collegato al kernel proprio e non esegue confronti OCCT. Nessuna espansione della certificazione viene attribuita al solo successo di questi casi.

Le prove Wayland verificano percorsi e asserzioni reali; l’avvio isolato non certifica pixel OpenGL o nuovi CAD. Riletti i collegamenti desktop, entrambi puntano alla Release aggiornata. Nessun processo utente terminato.

## Audit continuo e fin effettive

Audit indipendente Fraction su coefficienti IEEE originali, composizione tensoriale omogenea e limiti Bernstein/de Casteljau, non una griglia:

- **10 mappe /736 campate**, cinque cut ISO reali: tutte sotto cap1e-7 mm, upper massimo **5,000003784e-8 mm**, incluso supporto spostato di mezzo cap.
- **2 fin effettive /2 campate**, versi opposti, dopo helper di produzione e `Body::buildSheet`: upper massimo **6,725652855e-11 mm**. È un Body sintetico, distinto da un risultato pubblico del comando raccordo.
- Un exportatore indipendente conferma8 mappe/734 campate sovrapposte: non sommare questi numeri ai precedenti.
- Tre controlli negativi alterano polo edge, supporto e UV: il verificatore li rifiuta.
- La riflessione V è provata separatamente sull’helper; non è un test di Body con carrier riflesso. Nessun nuovo round-trip codec di questo Body è dichiarato.

Root ha rieseguito il verificatore sui due export della libreria finale e ottenuto10/10 mappe e2/2 fin PASS. L’audit ha eseguito personalmente anche il runner miratoV2,13/13PASS. Il negativo ISO baseline aggiunto da root è stato confermato indipendentemente: residuoZ=1/2 esatto; il negativo diagonale resta aperto. Le copie del candidato e i confronti distinguono V1 storico da V2 finale.

## Conservazione e riproducibilità

Baseline **4.548 file**, undici sigilli precedenti ricontrollati e intatti. **4.541 file iniziali invariati**; sette esistenti modificati come previsto: due TU/header mitre, CMake kernel, generatore, memoria e registro review. Nuovi: test, rapporto, memoria precedente e prove. Nessun CAD originale modificato, nessun branch/commit/PR/worktree.

Prima del codice consultati descrizioni e892relazioni incidenti del grafo776/11282/388. Dopo la modifica controllate568relazioni nel focus finale, distinte call/include/reference, incluse superficie→installazione/ricontrollo mitre, mitre→fit/provider e test→tracciatore. La modifica dell’header ha richiesto l’analisi delle389 TU; successivo refresh del solo catalogo da cache. HTML/JSON locali restano ignorati da Git.

Compile DB807→809 record: cambiano soltanto i flag delle due TU, con `-fno-fast-math -ffp-contract=off`; si aggiungono test e main del runner. I coefficienti finali sono esportati con 17cifre round-trip e ricostruiti come IEEE esatti dal verificatore. I confronti sono sui JSON, non su file Body binari.

[Prove e sigillo](review-k10-mitre-prove-2026-11-16/README.txt); [memoria precedente completa](history/claude-prima-k10-mitre-2026-11-16.md). Solo testo/JSON e sorgenti/script inerti, niente ELF/oggetti/librerie/CAD; il sigillo alla radice descrive il pacchetto curato, quelli upstream documentano provenienza.

Restano il sito iniziale `extendSplineBoundary`, i fallback parziali di K10,17API pubbliche candidate orfane e l’uniformazione dei colori letterali dell’interfaccia.

App finale SHA256 `5e24ac6fab52d7ed2256af643b86d488dfbd2fafcc61169b2a237bde1742f270`; mitre `a 36cf93ed20bb44c5cc7127ea4b0ce1d35f37c4047b53d5875a330f8eda68104`; surface `0fd41f22575881c8317a2a41696f0645e6f73a1ccecf687adaa84f35dce833c5`; test `73811bf286bd01217092d662cbc34a98e614bdba453d6dff05ebac8d1e1235ba`.
