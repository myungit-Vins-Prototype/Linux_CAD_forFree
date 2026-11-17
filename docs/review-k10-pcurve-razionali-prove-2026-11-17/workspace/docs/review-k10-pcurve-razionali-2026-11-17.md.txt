# K10: pcurve NURBS razionali nel controllo continuo — 17 novembre 2026

Esteso il controllo continuo alle **pcurve BSpline native di grado 1 o 3 con pesi positivi variabili**. La verifica conserva i coefficienti omogenei originali di E/Q/S e il parametro comune, senza aumentare i budget. I casi riconosciuti fuori budget danno `Failed` terminale. **K10 globale resta parziale; il totale dei rilievi funzionali resta 33. Nessun commit o push.**

Nessun CAD utente aperto o modificato. Solo test kernel pertinenti: nessun test/avvio UI, suite generale 483 o nuova CI. Implementazione, regressioni e audit svolti con gli stessi tre agenti GPT‑6.1 Sol, con integrazione e build root.

## Problema e percorsi interessati

Prima il provider riportava `Unsupported` per ogni Q a pesi variabili: anche una fin su superficie polinomiale poteva rimanere fuori dalla verifica continua. Il ledger interno saltava queste fin e i chiamanti mantenevano il percorso legacy previsto per le famiglie escluse.

Non tutti i mapper rifittano Q: nel ramo con immagine di faccia assente il mapping conserva la pcurve sorgente. Questo percorso è raggiungibile staticamente da `wrapBodyOnSurface` quando non ritorna il percorso rapido del carrier e da `wrapBodyOnFaces`. Con un’immagine esplicita i parametri vengono invece rifittati; alcuni percorsi Quilt usano superfici quintiche, ancora escluse da questo provider. La raggiungibilità è verificata sui sorgenti e sulla mappa; **non viene dichiarato un nuovo documento utente o un nuovo fallimento pubblico risolto**.

Il test negativo usa Q cubica con pesi `{1,1,1e8,1e8}`, un secondo polo Y=0,01 mm e gli altri a Y=0. E conserva la corrispondente curva piatta e S è un carrier polinomiale identità. Ai 33 punti k/32 il residuo rimane sotto il cap 8e-8 mm; vicino a t=1/√(3e8) lo supera di oltre dieci volte. La Q senza picco è prima certificata e l’intero dominio della Q è dimostrato contenuto nella superficie.

La prova indipendente Fraction sui coefficienti originali dà residuo Y **8,6594207606e-7 mm**, pari a **10,8243 volte il cap**. Il massimo della sola griglia è 3,0670106158e-9 mm: spiega il punto cieco ma non è un certificato. Il test del ledger verifica il rifiuto della prima e unica fin pianificata prima dell’assegnazione, con corpo serializzato/E/S invariati. Non implica transazionalità generale su più fin già modificate.

## Composizione omogenea

Il nuovo ramo rappresenta Q come `(U/D,V/D)` usando poli, nodi e pesi IEEE originali. La ricorrenza Cox omogenea conserva `U−k·D` e `k·D−U` nei fattori di ogni asse. Le due direzioni della superficie condividono quindi la potenza `D^(pu+pv)`.

Questo denominatore deve essere presente **anche quando S è polinomiale**. Il numeratore omogeneo composto H_S e il denominatore composto W_S vengono confrontati con H_E/W_E tramite `H_S·W_E−H_E·W_S`. Il grado massimo resta 21. Per S con pesi variabili la positività sulla porzione attiva della cella combina minimo originale S e limite positivo di D; quando il vecchio snapshot ha eliminato un peso S comune, il minimo effettivo è 1, non quel peso comune.

Le divisioni per i limiti positivi avvengono separatamente con arrotondamento verso l’esterno. Il minimo fra questo upper e quello ottenuto dall’inviluppo positivo del denominatore composto usa due limiti indipendenti validi; non forma prodotti double nominali dei lower. Underflow, overflow o condizioni inconclusive possono causare un rifiuto conservativo, mai una certificazione ottimistica.

L’immagine UV si limita dividendo l’inviluppo dei numeratori per quello positivo di D e intersecando con l’inviluppo euclideo dei poli Q originali. È un secondo limite indipendente, non un taglio al dominio della superficie. Si verificano tutte le celle chiuse interessate. La candidata Q può essere costruita privatamente nel dominio prima dell’unica prova, mantenendo i pesi originali; non c’è recupero campionato dopo un `Failed`.

I limiti di scalari, visite, profondità e celle rimangono invariati. Lo spazio aggiuntivo del denominatore Q viene conteggiato prima degli snapshot e i nuovi cicli sono cancellabili. Le vecchie funzioni `basis`, `snapshot`, `onRange` e `residual` mantengono il corpo precedente; Q polinomiale/a pesi esattamente costanti segue quel ramo. L’helper dei soli pesi costanti non è più necessario ed è stato rimosso, sostituito dalla validazione comune dei pesi positivi.

## Verifiche

| Verifica | Esito e perimetro |
| --- | --- |
| App completa e sei target kernel | PASS, circa 30,22 s; app SHA256 `40514d86d35368fa2380fde3bf1e5b8fef6b452fd706a6af135d93d4a48a19c7`. |
| Sei suite CTest pertinenti | 6/6 PASS, zero skip, circa 43.33 s; provider/ledger, mapping nonPlane, surface mapping, sheet join, istanze e tolleranze. |
| Driver Release | 59/59 PASS, circa 0,440 s: sette nuovi casi più i 52 precedenti. Baseline nuova TU: 0/7 al nuovo contratto. |
| ASan/UBSan/LSan mirati | 59/59 PASS, circa 4,877 s. Una TU produzione, sette TU test e main strumentati; resto kernel Release. LSan fuori ptrace calibrato con perdita reale di 19 byte/exit 23. Nessuna certificazione della memoria dell’intera app. |
| Compatibilità Q costante | 48 fin pubbliche e sei probe E razionale/QLine: tre confronti di export/stdout byte-identici fra baseline e overlay. |
| Osservatore finale indipendente | Quattro fin effettive E3/Q3 genuinamente razionali, dopo assemblaggio/unificazione, tutte runtime `Certified` e Fraction PASS con residuo esatto zero. |
| Coefficienti diretti esportati dai test | Otto positivi Fraction PASS, massimo upper circa 3,2051e-14 mm; due negativi distinti: picco Q e denominatore omesso. |
| Architettura e conservazione | PASS: 790 nodi, 11599 relazioni, 400 TU; hash sorgenti/configurazione/HTML e collegamenti nei due versi verificati. 30 pacchetti precedenti preservati. |

Le sette regressioni coprono Q lineare/cubica realmente razionale, carrier polinomiale nonlineare, denominatori su S razionale e istanziata con posa/gauge, trim/versi e attraversamento celle, picco nascosto, prima fin pianificata, dominio/wrapper/cancellazione. Nei tre test precedenti cambia soltanto un’aspettativa Q variabile da `Unsupported` a `Failed` per uno scarto reale; un nome viene aggiornato. I 59 casi e le sei suite si sovrappongono e non vanno sommati.

Il controllo contro l’omissione del denominatore è distinto dal picco: Q_u=1/(2−t), S identità 2/2 e E erronea=(2−t)³. Il numeratore tensoriale X è D³ ma il denominatore è D⁴: eliminarlo produrrebbe un residuo erroneo identicamente nullo, mentre il residuo reale a t=0 è 7,5 mm. Il candidato corretto rifiuta il caso e certifica la controparte coerente. È un guard contro un’implementazione sbagliata, **non un’omissione attribuita alla produzione precedente**, che riportava `Unsupported`; gli estremi del caso artificiale sono già errati e non viene presentato come CAD utente.

L’osservatore costruisce un corpo con `Body::buildSheet`, poi esegue `certifySheetJoins`, `unifySameDomain` e `requireUnifiedSheetJoins`. Sono quattro fin di bordo su S quadratica polinomiale con pesi S comuni uguali a 7. Il calcolo Fraction conserva quel gauge originale e dimostra simbolicamente `W_S_original=7·D⁴`; la produzione usa correttamente il gauge normalizzato D⁴. Il MCD esatto delle coordinate Q mostra quattro denominatori ridotti di grado 3: non sono solo pcurve polinomiali memorizzate con pesi.

La baseline produce zero contratti per queste Q, il risultato finale quattro. E/S/Q/range, edgeTolerance, versi e genealogia restano identici; finTolerance passa da zero al cap già pianificato 1e-7 mm. Non è un aumento del budget di accettazione e non si dichiara identico il corpo serializzato alla baseline. L’input resta immutato; l’unificazione con history nulla è byte-identica. Sono prove del ledger interno su un corpo reale, non una nuova giunzione dimostrata nell’API pubblica `extendSheet`.

La libreria finale root è `6f0a76dfdde164289e0b646843120c1efdcaabcede8eb38446f5aa3459987356`, con hash verificato dall’auditor prima/dopo il link. L’unica baseline congelata corrente è `8641c0a3c65dccd9906a935139669eb64c8e829fdb6bcf20aefcd7c97408c40e`. Fraction è indipendente dal provider di produzione.

## Limiti e roadmap

Il perimetro ora include E BSpline cubiche native, Q Line o BSpline native grado 1/3, pesi E/Q finiti positivi anche variabili e S NURBS nonperiodiche grado 2/3 per direzione, native o istanziate. **Wrapper E/Q, altri gradi/famiglie e superfici periodiche restano esclusi.**

La verifica E(t)−S(Q(t)) non prova il carrier rispetto all’intera mappa richiesta, il ramo/convergenza Newton, trim/topologia, iniettività o assenza globale di autointersezioni. K10 resta parziale; dopo le pcurve razionali rimangono i wrapper e la validazione globale dei percorsi ancora campionati. I limiti di lavoro non sono una deadline temporale rigida.

Per liberare lo spazio temporaneo sono stati eliminati soltanto due archivi congelati di staging già completati e 63 ELF/oggetti dei due interventi precedenti, ricreabili dai sorgenti. Il tester ha rimosso anche tre propri duplicati ELF correnti dopo hash ed export. Piani, hash e risultati sono conservati; nessun sorgente, log, coefficiente, CAD, binario di produzione o pacchetto di prove nel repository è stato eliminato. La baseline condivisa di questo intervento è conservata durante le verifiche.

[Prove, sorgenti inerti e manifest](review-k10-pcurve-razionali-prove-2026-11-17/README.txt). [Memoria precedente conservata](history/claude-prima-k10-pcurve-razionali-2026-11-17.md). La CI storica non certifica queste modifiche non committate.
