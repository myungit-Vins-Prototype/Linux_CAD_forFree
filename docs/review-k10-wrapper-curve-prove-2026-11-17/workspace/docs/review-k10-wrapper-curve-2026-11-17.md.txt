# K10: curve ritagliate e trasformate nel controllo continuo — 17 novembre 2026

Esteso il controllo continuo ai **wrapper Trim di E/Q e alle trasformazioni affini di E**, conservando coefficienti, matrici e parametro originali. Ogni intervallo richiesto deve rispettare esattamente tutti i trim incontrati. I budget geometrici restano invariati. **K10 globale resta parziale, con 33 rilievi funzionali invariati. Nessun commit o push.**

Implementazione, regressioni e audit con tre agenti GPT‑6.1 Sol; integrazione e build finale root. Solo verifiche kernel pertinenti: nessun test o avvio UI, suite generale483 o nuova CI. Nessun CAD utente modificato.

## Problema e soluzione

I wrapper validi erano esclusi dal provider con `Unsupported`; il ledger poteva quindi saltare il controllo continuo di fin la cui curva di base era già riconosciuta. Ora il provider attraversa i wrapper prima di chiamare accessori potenzialmente ricorsivi. Accetta fino a32 link complessivi per ciascuna curva, contando insieme trim e trasformazioni; il link33 produce `Failed`. Il percorso è cancellabile e controlla i coefficienti finiti di ogni matrice.

Per ogni `TrimmedCurve` controlla il range locale memorizzato senza copiare la lenienza1e-12 del costruttore. Dopo aver riconosciuto la famiglia di base controlla anche il dominio BSpline nativo. Una curva periodica di famiglia esclusa rimane `Unsupported` anche quando il trim legittimo attraversa il periodo; non viene scambiata per un intervallo BSpline invalido.

E ammette catene miste Trim/Transformed, con trasformazioni affini finite, anche non uniformi, con shear o singolari. La prova applica le matrici originali dall’interno all’esterno in aritmetica a intervalli outward; non compone nominalmente le matrici in double e non appiattisce i poli trasformati arrotondati. Un punto double può servire soltanto come origine comune per centrare i calcoli. La stessa origine è sottratta a E e S e non sostituisce la geometria originale. Nessuna inversione o richiesta di similarità è necessaria per questo certificato di posizione.

La costruzione privata della candidata Q valida prima tutti i wrapper originali e il dominio nativo: non può eliminare un trim fuori range prima del controllo. Se Q non cambia conserva il puntatore esterno; se una BSpline ritagliata viene legittimamente ricostruita la nuova candidata conserva il range richiesto. La conversione di una Line ritagliata in Bernstein conserva il range per analisi statica del ramo condiviso: il nuovo test TrimLine esercita direttamente il bound, non questa conversione della candidata. La conservazione positiva del puntatore della candidata è verificata runtime per QTrimBSpline invariata. Un `Failed` riconosciuto resta terminale, senza recupero campionato.

Non cambiano costruttori pubblici, codec, `fk_curve.cpp`, `fk_transform.cpp`, né le formule del residuo native già verificate. Questa estensione non introduce un nuovo errore pubblico attribuito a un documento utente.

## Verifiche

| Verifica | Esito e perimetro |
| --- | --- |
| App completa e sei target kernel | PASS, circa 32.67s; app SHA256 `3556beed956771614c78f2711947edb67e0aa982bae6b2f0a39846ef1ec72ca6`. |
| Sei suite CTest pertinenti | 6/6 PASS, zero skip, circa 44.93s; provider/ledger, mapping nonPlane, surface mapping, sheet join, istanze e tolleranze. |
| Regressioni Release | 67/67 PASS: otto nuovi casi più59 precedenti. La nuova TU contro la baseline soddisfa0/8 del nuovo contratto. |
| Sanitizer mirati | 67/67 PASS, circa5,169s: una TU produzione, otto TU test e main strumentati ASan/UBSan/LSan; resto kernel Release. LSan fuori ptrace calibrato con perdita reale19byte/exit23. Non certifica la memoria dell’intera app. |
| Compatibilità nativa | 48 fin pubbliche, sei probe E razionale/QLine e sette Q razionali: quattro confronti di export/stdout byte-identici baseline/overlay. |
| Audit indipendente sui coefficienti originali | Fraction indipendente: quattro fin effettive dopo build/unificazione, nove positivi esportati dai test e quattro negativi distinti PASS, sui coefficienti originali e sull’intero intervallo dei positivi. |
| Architettura e conservazione | PASS: 791 nodi, 11622 relazioni, 401 TU; hash sorgenti/configurazione/HTML e collegamenti nei due versi verificati. 31 pacchetti precedenti preservati. |

Gli otto nuovi gruppi coprono range correlati non unitari e QLine ritagliata, catene miste e carrier istanziato, trasformazioni affini singolari, perdita da arrotondamento dei poli, picco Q fra campioni, prima fin pianificata prima dell’assegnazione, tutti i trim interni e candidata Q, limiti32/33, valori non finiti, famiglie escluse e cancellazione. Nei quattro test precedenti cambiano soltanto sei aspettative di wrapper validi da `Unsupported` a `Certified` e un nome descrittivo; gli scarti reali già verificati restano rifiutati. I67 casi si sovrappongono alle sei suite e non vanno sommati.

Il test contro l’appiattimento usa un polo X=2^-36, traslazione X=2^18 e cap2^-40 mm: il polo nominale trasformato perde il piccolo scarto, mentre la geometria originale dista16cap dal carrier. La versione arrotondata sarebbe certificabile, quella originale viene rifiutata. È una protezione contro una futura implementazione scorretta: **la vecchia produzione escludeva il wrapper, non produceva questo falso certificato**.

La Q cubica con pesi1,1,1e8,1e8 e poloY0,01mm mantiene il picco oltre10cap fra i33 campioni della vecchia griglia anche dentro i wrapper. La controparte piatta viene certificata. Il test del ledger riguarda la prima e unica fin pianificata: il rifiuto precede l’assegnazione e conserva corpo/E/S. Non dimostra transazionalità generale per più fin già modificate.

L’osservatore finale usa `Body::buildSheet`, `certifySheetJoins`, `unifySameDomain` e `requireUnifiedSheetJoins`: quattro fin effettive, ciascuna con E avvolta in cinque wrapper misti Trim/Transformed e Q in due trim. La baseline produce zero contratti `Unsupported`, la libreria finale quattro `Certified`; Fraction conserva gli originali e verifica l’intero parametro. I campi E/S/Q, range, cap edge, verso e genealogia restano invariati; finTolerance riceve il cap già pianificato1e-7mm. Non è un aumento del budget di accettazione né una nuova giunzione pubblica `extendSheet` dimostrata. L’input rimane immutato; con history nulla l’unificazione resta coerente con i controlli di genealogia.

La libreria root finale è `2bd551b222b2c07cbfaa02635ebf999562ef90ec8f00b0ba7ba398c1b2028f1d`,181672362byte, verificata dall’auditor prima/dopo il link diretto. La baseline condivisa congelata è `6f0a76dfdde164289e0b646843120c1efdcaabcede8eb38446f5aa3459987356`. L’audit Fraction separato prova il residuo anti-arrotondamento esatto −2^-36 sull’intero range e conferma il picco Q8,6594207606e-7mm=10,8243cap. Nei due negativi di trim, solo il range interno è violato di2^-54: i domini nativi E/Q/S restano validi e, rimuovendo il solo trim, il residuo è esattamente zero. Non sono fallimenti confusi con il dominio del carrier.

Per lo spazio temporaneo il tester ha eliminato solo tre propri duplicati ELF ricreabili dopo averne conservato hash, log ed export, liberando166714200byte. Nessun CAD, sorgente, prova o binario di produzione eliminato.

## Raggiungibilità e limiti

Codec B-rep e importazione IGES conservano o costruiscono wrapper di curva; `transformBody` conserva wrapper trasformati già presenti. Gli accessor ISO che emettono `TransformedCurve` sono quelli di estrusione/rivoluzione, famiglie S tuttora escluse da questo provider. Gli ISO di `BSplineSurface`, anche istanziata, restituiscono curve BSpline native: non sono la fonte di wrapper qui. Il mapping generico può conservare Q quando manca un’immagine esplicita della faccia. Sono percorsi verificati staticamente, non una nuova prova runtime su un CAD pubblico.

Il perimetro riconosciuto resta: base E BSpline cubica, QLine o BSpline1/3, pesi finiti positivi anche variabili, S NURBS nonperiodica2/3 per direzione nativa o istanziata; si aggiungono Trim E/Q e Transformed E fino al limite32. Altre famiglie/gradi, superfici periodiche e altre rappresentazioni di S restano escluse. Wrapper non validi, non finiti o oltre limite vengono rifiutati conservativamente.

Il certificato riguarda E(t)−S(Q(t)) nel parametro correlato e le restrizioni dei wrapper di curva. **Non certifica l’appartenenza al trim B-rep della faccia, la topologia, l’intento globale della mappa, Newton, iniettività o assenza globale di autointersezioni.** K10 resta parziale; la prossima priorità rimane la validazione globale dei percorsi ancora campionati e delle famiglie non riconosciute. I limiti di lavoro non sono una deadline temporale rigida.

[Prove, sorgenti inerti e manifest](review-k10-wrapper-curve-prove-2026-11-17/README.txt). [Memoria precedente conservata](history/claude-prima-k10-wrapper-curve-2026-11-17.md). La CI storica non certifica queste modifiche non committate.
