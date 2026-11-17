# K10: bordi NURBS razionali nel controllo continuo — 17 novembre 2026

Esteso il controllo continuo del residuo delle fin ai bordi BSpline cubici nativi con **pesi positivi variabili**. Il registro delle giunzioni ora riconosce anche questa famiglia: certifica prima di assegnare la pcurve e rifiuta i casi fuori budget, senza tornare al controllo campionato. **K10 globale resta parziale, il totale dei rilievi funzionali resta 33 e non sono stati eseguiti commit o push.**

È un ampliamento del provider e della famiglia verificata dal ledger interno. Non viene dichiarata una nuova correzione pubblica su un documento utente: i mapper attuali rifittano normalmente E in cubiche polinomiali. Nessun CAD utente aperto o modificato; soltanto test kernel pertinenti, senza avvio/test UI, suite generale 483 o nuova CI.

## Riproduttore e causa

Il provider riportava `Unsupported` per ogni E con pesi variabili, anche quando S e Q rientravano nella famiglia verificabile. Una cubica con pesi originali `{1,1,1e8,1e8}`, secondo polo Z=0,01 mm e gli altri a Z=0 ha un picco stretto vicino a t=1/√(3e8). La superficie usa la stessa parametrizzazione razionale U, con Z=0; Q è polinomiale. Ai 33 campioni k/32 il residuo è inferiore al cap prefissato 8e-8 mm, mentre il testimone interno lo supera di oltre dieci volte. La controparte senza picco viene prima certificata: il rifiuto non deriva semplicemente da un dominio UV inconclusivo.

La verifica indipendente Fraction sui coefficienti originali prova un residuo di 8,6594207606e-7 mm al parametro IEEE `0x1.e45106b6b3b93p-15`, pari a 10,8243 volte il cap. Il contenimento dell’intera Q nel dominio è provato separatamente. Il massimo dei soli 33 punti è 3,0670106158e-9 mm: documenta il punto cieco, senza costituire una prova continua.

Nel test del ledger la baseline non produceva alcun contratto per la singola fin E razionale. Il candidato certifica la variante corretta e rifiuta il picco prima dell’assegnazione: corpo serializzato, E e S restano identici dopo quel fallimento. **La prova riguarda una sola fin pianificata e la prima verifica fallita.** Non attribuisce transazionalità generale a `certifySheetJoins(Body&)` su più piani, che può aver già aggiornato fin precedenti.

## Modifica

La patch cambia una sola TU di produzione, `fk_surface_curve_bounds.cpp`, e il relativo contratto nell’header. E viene centrata sulla stessa origine di S prima di moltiplicare ciascun polo per il suo peso IEEE originale. Il numeratore omogeneo H_E e il denominatore W_E conservano nodi e parametro originali; l’estrazione Bernstein intervallare già esistente viene riutilizzata anche per W_E.

Per ciascuna cella attiva di S il residuo è verificato attraverso `H_S·W_E−H_E·W_S`, con denominatore positivo `W_E·W_S`. Il limite inferiore di W_E combina il minimo dei pesi originali, valido sul dominio naturale, e l’inviluppo locale calcolato con intervalli. Le due divisioni positive vengono arrotondate verso l’esterno separatamente: nessun prodotto nominale dei denominatori può diventare una prova ottimistica per underflow/overflow. Il grado massimo del numeratore resta 21.

Restano invariati cap geometrici e limiti di visite/profondità/celle/scalari. Lo spazio aggiuntivo del denominatore E viene conteggiato prima degli snapshot, solo per la nuova famiglia. I nuovi cicli sono cancellabili; dati inconclusivi, risorse esaurite o errore oltre budget danno `Failed` terminale. Non vengono normalizzati i pesi né sostituiti i coefficienti con una conversione arrotondata.

Il ramo E polinomiale o a pesi esattamente costanti conserva le espressioni e l’aritmetica precedenti. La costruzione privata di Q ora consente E razionale positiva, ma deve comunque precedere l’unica prova di accettazione. Mapping e ledger non cambiano: l’estensione del provider viene recepita dai contratti già presenti.

## Verifiche

| Verifica | Esito e perimetro |
| --- | --- |
| Build completa dell’app | PASS; app SHA256 `92b6337fd287d56318fe5f4d3c7b177cf589f06e6e08ff4b32b282ef806ba782`. |
| Sei suite CTest pertinenti | 6/6 PASS, zero skip, circa 46.59 s; provider/ledger, mapping nonPlane, surface mapping, sheet join, istanze e tolleranze. |
| Driver Release mirato | 52/52 PASS, circa 0,457 s: sei nuovi casi e 46 regressioni precedenti. |
| ASan/UBSan/LSan mirati | 52/52 PASS, circa 5,086 s. Una TU di produzione, sei TU di test e main strumentati; resto kernel Release. LSan attivo fuori ptrace, calibrato con perdita reale di 19 byte ed exit 23. Copertura parziale, non certificazione della memoria dell’intera app. |
| Confronto ramo polinomiale | 48 fin pubbliche, export IEEE e stdout identici fra baseline e overlay candidato. |
| Audit indipendente sulla libreria finale | Quattro fin effettive dopo assemblaggio/unificazione: due E realmente razionali e due polinomiali, tutte certificate e verificate con Fraction. |
| Altri coefficienti esportati dai test | Cinque casi positivi Fraction PASS e un testimone negativo distinto; conteggi separati dalle fin dell’osservatore. |
| Mappa e conservazione | PASS: 789 nodi, 11578 relazioni, 399 TU; hash sorgenti/configurazione/HTML e collegamenti verificati in entrambi i versi. 29 pacchetti precedenti preservati. |

Le sei nuove regressioni coprono E razionale realmente non polinomiale, dominio naturale anche non clamped, trim e verso correlato, pose e scale indipendenti dei pesi, picco fra campioni, prima fin del ledger, dominio/aspettative di esclusione e cancellazione distinta. La baseline finale passa soltanto il caso di cancellazione, 1/6; il candidato finale passa 6/6 e 52/52 includendo le suite correlate. I log iniziali e finali sono distinti; nessun fallimento è eliminato dalle prove.

Nei due file di test preesistenti cambia una sola aspettativa E a pesi variabili da `Unsupported` a `Failed`, perché quei casi hanno un residuo reale fuori budget. I nomi dei due test vengono resi coerenti con il nuovo perimetro. Le aspettative Q razionale e wrapper restano invariate. I 52 casi e le sei suite non vanno sommati: si sovrappongono.

L’osservatore indipendente usa un corpo costruito con `Body::buildSheet`, poi `certifySheetJoins`, `unifySameDomain` e `requireUnifiedSheetJoins`. Le quattro fin sono di bordo: non vengono presentate come una nuova giunzione creata dall’API pubblica `extendSheet`. La baseline produceva due contratti polinomiali; il risultato finale ne produce quattro. Il calcolo Fraction sull’intero intervallo dà residuo esattamente nullo per questi coefficienti. Il denominatore ridotto di due coordinate E ha grado 3 e MCD costante: non si tratta soltanto di curve polinomiali memorizzate con pesi.

E/S/range, edgeTolerance, versi e genealogia restano identici alla baseline. Per le due nuove E razionali QLine diventa BSpline1 prima della prova e finTolerance passa da zero al cap già pianificato 1e-7 mm. Non cambia il budget di accettazione; non si dichiara identico il corpo serializzato alla baseline. L’input dell’osservatore resta immutato e l’unificazione con history nulla produce lo stesso risultato serializzato.

La libreria finale root è `8641c0a3c65dccd9906a935139669eb64c8e829fdb6bcf20aefcd7c97408c40e`; l’auditor ne verifica l’hash prima/dopo il collegamento. Le prove in overlay usano una sola copia congelata della baseline `57c11033779f33893b07d166c682a0c5e8e3d24c14c98abfee43c7253ca62fa7`. Le verifiche indipendenti non chiamano il provider di produzione.

## Punti aperti

E resta una BSpline cubica nativa; Q resta Line/BSpline grado 1 o 3 con pesi assenti o costanti positivi. **Q a pesi variabili, wrapper E/Q, altri gradi/famiglie e superfici periodiche restano esclusi.** S mantiene il supporto NURBS nonperiodico 2/3 per direzione, con pesi positivi, nativo o istanziato.

Il residuo E(t)−S(Q(t)) non certifica il carrier rispetto all’intera mappa richiesta, convergenza o ramo Newton, trim/topologia, iniettività o assenza di autointersezioni. Il provider può rifiutare conservativamente geometrie valide per condizionamento o limiti di lavoro. Non si dichiara chiuso tutto K10 né completata l’intera roadmap.

Implementazione, regressioni e audit svolti con agenti GPT‑6.1 Sol e integrazione/verifiche root. [Prove, sorgenti inerti e manifest](review-k10-edge-razionali-prove-2026-11-17/README.txt). La CI storica non certifica queste modifiche non committate.

[Memoria precedente conservata](history/claude-prima-k10-edge-razionali-2026-11-17.md).
