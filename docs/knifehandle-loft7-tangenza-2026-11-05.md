# KnifeHandle: tangenza di Loft 7

## Diagnosi

La copia corrente contiene Loft 5 e Loft 7. Loft 7 è salvata con primo profilo normale (modo1), ultimo tangente a Piano2 (modo4); conserva due riferimenti a F0/F1 di Loft5. Attivando G1 alle facce sul primo profilo (modo3), il calcolo fallisce per tangenti incompatibili.

F0 di Loft5 conteneva un nodo U=0,99999999999996381 prima di 1. Nella campata residua la normale presenta componente laterale 0,003463 invece di circa zero. La suddivisione per lunghezza aveva trovato un parametro appena oltre un nodo esistente, entro il residuo già ammesso dal solver; il successivo ritaglio NURBS generava una pezza quasi nulla e jet instabili.

## Correzione

`parameterAtLength` in `fk_loft.cpp` usa un nodo già esistente quando lo scarto di lunghezza e il residuo del nodo rispettano entrambi il budget preesistente 1e-13×max(1,lunghezza). Verifica anche la distanza spaziale entro kLinearResolution. La curva originale non cambia; cambia solo la scelta del parametro di suddivisione entro l'accuratezza già richiesta. Nessuna tolleranza G1 allentata o faccia incompatibile ignorata.

## Verifiche concluse

- Applicazione `forgecad-release/forgecad` e runner dei test compilati con la correzione e cache13.
- Copia congelata `5b3a4f4e0ea4853d4e7dfa9cf491e45788da0839c7a747e6f314783167ba5f15`: la normale apertura scarta la vecchia cache e rigenera le dipendenze, senza forzare flag di ricalcolo nella sonda.
- Loft7 con primo modo3 (G1 alle due facce di Loft5), ultimo modo4 (G1 a Piano2) e influenza iniziale50% riesce. B-rep valido, zero facce fallite nella mesh; 258 punti sul bordo, massimo seno fra normali 3,99239e-5 entro la soglia G1 esistente 1e-4. Non si dichiara una certificazione analitica globale.
- F0 di Loft5 passa da 37 a 25 poli U: scompare il tratto numerico spurio. F1 conserva25; le due superfici restano con1201 poli V. Nessuna riduzione generale della complessità dichiarata.
- Salvataggio della copia G1 e riapertura con nuova cache13 PASS. Viewport Wayland separato verificato e schermata ispezionata.
- 9/9 CTest PASS: lettura/migrazione cache, revisioni native, punti/piani loft, corrispondenze, tangenza alle facce, guide e cucitura booleana. Verifica cache12 invalidata sulle copie di lavoro, check-in storici immutabili e loft soppressi conservati.
- 42/42 test del runner kernel filtrati Loft PASS. Regressione `LoftSectionLengthKeepsExistingKnots`: usa il nodo per scarti entro il residuo già ammesso; conserva un dettaglio distinto fuori da tale budget.
- Mappa finale: 597 file, 8323 relazioni, 287 unità analizzate, zero diagnostiche. Check della mappa e rendering di panoramica più cinque dettagli PASS. Collegamenti entranti/uscenti controllati, chiamate/include distinti; nessun arco reference per i file interessati. `git diff --check` PASS.

Le sonde non modificano il file originale. Tutti gli artefatti finali, log e sorgenti di prova sono in `forgecad-release/loft7-final-qa/` (ignorata da Git), inclusi `input.prt`, `KnifeHandle-loft7-G1.prt`, `probe.cpp`, `probe.log` e `viewport.png`. Una seconda copia del file aggiornato durante le prove è conservata come `input-user-latest.prt`; le misure sopra si riferiscono alla copia congelata identificata dall'hash.

L'istanza già aperta dell'applicazione deve essere riavviata per caricare il nuovo kernel; la riapertura delle normali copie con loft rigenera la vecchia cache. I check-in storici mantengono esattamente la propria geometria.
