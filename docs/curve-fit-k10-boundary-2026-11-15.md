# K10 — Superficie tra curve, 15 novembre 2026

## Perimetro

Prosecuzione con tre agenti GPT-6.1 Sol, dopo [Estendi e Riempimento](curve-fit-k10-estendi-fill-2026-11-15.md). Questo blocco riguarda il fit dei lati di `boundarySheet` e i bordi effettivi della patch di Coons prodotta. Implementazione integrata e verificata per le famiglie supportate. [Prove, riproduttori e manifest](curve-fit-k10-boundary-prove-2026-11-15/README.md).

## Difetti riprodotti sulla versione precedente

- Una spline regolare di grado 6 con variazione concentrata fra i nodi 0,1 e 0,101 viene sostituita da un bordo piano: scarto 0,001 mm, **10000 volte** la tolleranza 1e-7 mm. La variante razionale arriva a 9521 volte il budget.
- La sorgente regolare di grado 27, senza nodi interni, conserva uno scarto di circa 574 volte la tolleranza. Aggiungere i soli nodi non risolve quindi K10.
- Quattro rette con un piccolo disallineamento a un angolo passano il controllo di connessione; la costruzione Coons sposta il bordo di 2 o 5 volte la tolleranza, pur avendo fit dei lati praticamente esatti. `checkBody` non segnala questi difetti.

I primi riproduttori e la prova continua di regolarità delle sorgenti sono conservati nel rapporto precedente; nuove regressioni confrontano anche curve ritagliate, lati composti, versi e trasformazioni.

## Correzione integrata

Il provider di posizione descrive ogni tratto con curva originale, intervallo sorgente, intervallo del lato e verso. Per NURBS e rette sotto wrapper supportati, limita il residuo continuo mediante coefficienti Bernstein con aritmetica verso l'esterno. Il parametro della curva viene legato esplicitamente a quello del lato; nessuna conversione delle coniche che cambi parametro.

I nodi interni e le giunzioni dei tratti vengono riportati nel parametro del lato. Il fit iniziale usa metà del budget; dopo compatibilizzazione dei nodi e costruzione Coons, i quattro bordi vengono ricontrollati sui poli effettivi della superficie, rispetto alle curve originali e al budget richiesto. Un limite non conclusivo comporta errore, senza ritorno al fit campionato per le famiglie riconosciute.

Un lato contenente tipi non supportati conserva esplicitamente il percorso campionato. Il controllo continuo riguarda i lati interamente riconosciuti. Il limite di un milione di poli della superficie impedisce allocazioni incontrollate senza aumentare la tolleranza. Le SP-curve dei lati riconosciuti sono affini sul parametro originale: evitano una seconda approssimazione per inversione della superficie.

L'audit indipendente ha inoltre individuato un caso limite del nuovo provider: una query chiusa che iniziava esattamente alla giunzione poteva omettere il valore dell'altro tratto. Il controllo include ora entrambi i valori agli estremi della query con aritmetica verso l'esterno. La regressione diretta copre rette/NURBS, entrambi i versi e le due query adiacenti; prima della correzione fallivano tutti gli otto controlli. Non è stata osservata una falsa accettazione di un corpo causata da questo caso.

Aggiunti punti di cancellazione nell'allineamento dei nodi e nella costruzione della griglia Coons. Il certificato riguarda la posizione; non promette continuità C1 nei nodi delle sorgenti.

## Esiti finali

- **Build completa Release app e runner: PASS**. Eseguibile su disco aggiornato; un'app già aperta deve essere riavviata per caricarlo.
- **9/9 nuovi gruppi permanenti PASS**. Gli otto gruppi di integrazione erano 3/8 sulla versione precedente; il nono verifica direttamente la nuova API alle giunzioni. Coprono tre curve avverse, sorgenti razionali, ritagli annidati/parziali, lati composti, trasformazioni e scale, tutti i quattro lati e versi, coniche compatibili, cancellazione, input invalidi e angoli Coons. Non viene usata una SP-curve ottenuta per fit come riferimento per il confronto.
- Sui tre riproduttori, gli scarti densi passano da 0,001 / 0,000952154249 / 0,000057424751 mm a circa **1,17e-8 / 3,29e-8 / 3,12e-8 mm**, con budget invariato 1e-7 mm. Queste misure verificano i casi di regressione; la garanzia continua deriva dal provider Bernstein, non dal campionamento.
- Disallineamenti angolari 0 e 0,25 volte la tolleranza restano ammessi; 2 e 5 volte vengono rifiutati con alias vuoti. Un divario di 20 volte resta rifiutato già come contorno aperto.
- **129 casi kernel distinti / 145 esecuzioni PASS**, includendo Offset, Shell, Sheet, BoundarySheet, Fill, Projected, Sew, Thicken e sette gruppi Blend. La riesecuzione copre anche il refactoring condiviso del provider offset precedente.
- **29/29 gruppi CTest PASS**, inclusi i nuovi nove casi e gli undici del fitter precedente. Test Qt con impostazioni isolate e piattaforma offscreen.
- **Audit indipendente: 27 controlli oracle e 9 sulle giunzioni PASS**, oltre ai tre riproduttori e ai cinque casi angolari. Verificate anche superficie e SP-curve risultanti; risultati e hash delle versioni verificate nel rapporto `independent/audit.md` delle prove.
- **ASan/UBSan/LSan mirati: 9/9 PASS**, senza segnalazioni. Strumentati test, boundary e bounds; framework main e resto dell'archivio non strumentati. Dettagli nel rapporto `tests/report.md` delle prove; nessuna certificazione sanitizer completa dell'app.
- **Mappa 745 file / 10708 relazioni / 368 unità PASS**, zero diagnostica; controllo aggiornamento, HTML/JSON identici e archi in ingresso/uscita verificati separando call/include/reference.
- Confronto degli hash con lo snapshot iniziale di 1630 file e `git diff --check`: esiti nel controllo finale delle prove. Modifiche preesistenti preservate fuori dal perimetro dichiarato.

## Limiti e prosecuzione

Dei 24 siti originari, sei hanno un percorso esatto/certificato per famiglie supportate; **18 siti restano interamente da migrare**, oltre ai fallback dei sei trattati. K10 resta pertanto aperto. I lati misti con coniche/procedurali non hanno un certificato continuo. Budget finiti, pesi non validi o limiti numerici non conclusivi possono produrre un rifiuto esplicito anche per geometrie valide. Non viene dichiarata una certificazione globale dell'interno della patch o dell'assenza di auto-intersezioni. La prosecuzione proposta riguarda i bordi ricostruiti da superficie e SP-curve in modifica poli e mapping: richiedono un limite della composizione, oltre al provider di posizione introdotto qui. Le 22 orfane restano candidate manutentive; i 24 rilievi precedenti corretti mantengono il loro conteggio. Nessuna modifica ai CAD originali, nessun commit/push richiesto. Le prove desktop/OpenGL restano distinte dai test Qt offscreen.


## Prosecuzione

Il blocco successivo [composizione superficie/SP-curve](curve-fit-k10-composition-2026-11-15.md) aggiunge il percorso certificato in Modifica poli per UV affini isoparametriche NURBS, incluso il controllo delle facce adiacenti. Il conteggio aggiornato è 7/24 siti parzialmente trattati e 17 interamente da migrare, oltre ai fallback. Il rapporto conserva anche un difetto distinto del mapping ancora aperto; prove e numeri sopra si riferiscono alla consegna BoundarySheet.
