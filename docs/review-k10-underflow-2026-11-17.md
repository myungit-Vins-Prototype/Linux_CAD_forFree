# K10 — prodotti pesati e arresto adattivo, 17 novembre 2026

Dopo il commit/push `18d0adc`, ripresa la quadratura condivisa. Corretto il recupero dei prodotti pesati che diventano subnormali o zero prima di essere moltiplicati per l’ampiezza dell’intervallo. È una continuazione di K10; il conteggio resta **34 rilievi corretti**, K10 parziale.

## Riproduzione verificata prima della modifica

Sorgenti e libreria della baseline del checkpoint congelati mediante hash. Compilatore C++17 Release con `-fno-fast-math -ffp-contract=off`, binary64 e arrotondamento ordinario.

Con `d = 2^-1074` e `H = 2^1023`, la funzione costante `f(t)=d` ha integrale esatto `2^-51`. La regola GK della baseline restituisce valore e integrale assoluto zero, con 15 callback; l’errore stimato è `2^-51`. La versione vettoriale perde indipendentemente valore e magnitudine. Con `f(t)=4d`, la baseline restituisce `9*2^-52` anziché `8*2^-52`: **+12,5%**, ancora finito e diverso da zero.

Riprodotto anche tramite l’API pubblica `arcLength`: B-spline 3D di grado1, nodi `{0,0,H,H}`, poli `(0,0,0)` e `(2^-51,0,0)`. La derivata valutata è esattamente `d`, la lunghezza restituita dalla baseline è zero. Le dimensioni fisiche sono sotto la risoluzione del documento e sotto la tolleranza assoluta predefinita: **questa prova non dimostra un difetto nei normali documenti CAD né una violazione di quel budget predefinito**. Le nuove regressioni chiedono `1e-30` e verificano il risultato con una soglia relativa alla misura; il vecchio oracolo assoluto `1e-12` avrebbe accettato lo zero.

La baseline non è stata sottoposta alla nuova integrazione adattiva con tolleranza stretta: potrebbe espandere l’albero. La dimostrazione della perdita è sul pannello diretto e sulla chiamata pubblica con tolleranza predefinita, entrambe concluse in tempo limitato.

## Correzione integrata

In `fk_quadrature.h`, uno scan dei 15 campioni conserva le espressioni e l’ordine delle somme ordinarie. Controlla i termini centrali K/G, le coppie con segno K/G e le coppie assolute K: per ogni peso positivo `w`, `x != 0 && abs(x) < minNormal/w` segnala un prodotto che perde precisione prima della moltiplicazione per l’ampiezza. Il recupero già presente usa gli stessi campioni e prodotti `frexp/scalbn`, senza callback ripetute. Valori e magnitudini sono analizzati separatamente, anche nelle componenti passive; `controlled` continua a riguardare solamente il raffinamento.

Dopo la normalizzazione viene ripetuto il controllo. Un centro minuscolo accanto a coppie ordinarie che si cancellano può restare minuscolo quando il massimo è1: se il riscalamento non risana i prodotti, si solleva `domain_error`. È un **rifiuto conservativo aggiuntivo** su dinamiche miste, non una garanzia di recuperarle tutte. Il caso misto dei campioni dimostra una perdita della regola discreta, senza attribuirgli un nuovo errore di integrazione continua pubblica.

Prima versione: recuperava le costanti estreme ma mancava quest’ultima guardia; i suoi PASS preliminari non sono usati per dichiarare risolte le scale miste. Dopo audit in sola lettura Sol, aggiunta e verificata la guardia. Tolleranze, profondità30, campionamento, partizioni e contatori della cache restano conservati. Catalogo architetturale aggiornato per header e test.

## Verifiche

- **13/13 gruppi Release PASS**, inclusi i sei precedenti. Nuove regressioni: perdita completa/parziale, segni e inversione, 15 callback, assoluto, valore/magnitudine indipendenti, componenti passive anche con ricorsione effettiva, B-spline nativa e riuso cache, rifiuto delle scale miste non recuperabili; esaurimento della profondità scalare/vettoriale e segmento razionale nativo.
- **13/13 ASan/UBSan PASS**, con rilevamento leak abilitato. Strumentati unità `fk_curve_algo.cpp`, main e nuovi test; il resto della libreria rimane Release. Non è una verifica leak dell’intera app.
- **26 output GK e 8 lunghezze ordinarie byte-identici** fra baseline e candidato. Tre proprietà di massa pubbliche (scatola, cilindro, estrusione razionale: volume/area/baricentro/inerzia) byte-identiche alla baseline ordinaria conservata.
- **Build completa app e runner mirati PASS**, seriale,59,382s. **4/4 CTest PASS**: quadratura, lunghezza, estensione e derivata B-spline. Dieci casi storici in nove filtri kernel PASS: lunghezze, cache, rumore, massa/cancellazione e loft. Nessun test UI o avvio dell’app ripetuto.
- Mappa numerica PASS:804file/11741relazioni/408TU, zero errori,787,687s in serie; `--check` PASS. Hash, configurazione, HTML e archi tipizzati nei due versi verificati. Il legame template arcLength→integrate è visibile nel sorgente e nell’include, non risolto come arco di chiamata in questa configurazione AST; limite esplicitato.

## Profondità massima: errore pubblico riprodotto e corretto

Completata anche la seconda parte del punto numerico1. Il primo probe, concluso con watchdog3s, usa B-spline razionali 3D grado1 con nodi `{0,0,1,1}`, poli `(0,0,0)`/`(1,0,0)` e pesi `{1,w}`. Tracciano monotonamente lo stesso segmento di **1mm**: `x(t)=wt/(1-t+wt)`, derivata positiva e finita. Tutte le chiamate usano la normale API pubblica e tolleranza1e-13, senza imporre artificialmente profondità30.

| Peso finale | Lunghezza baseline e cache | Diagnostica privata sulla stessa aritmetica | Esito nuovo |
| --- | --- | --- | --- |
| 2^-20 | 0,99999999999999079mm | 3937 pannelli, zero esauriti | Successo entro1e-12 |
| 2^-40 | 0,47130065712704883mm | 1987 pannelli,543 esauriti; errore locale massimo0,3681753 | Rifiuto esplicito |
| 2^-48 | 0,0026606066743453654mm | 531 pannelli,136 esauriti; errore locale massimo0,00222966 | Rifiuto esplicito |

Esiti pubblici byte-identici fra libreria appena compilata prima del fix e oggetto arcLength ricostruito dall’header del checkpoint18d0adc: è un problema preesistente. La traccia privata aggiunge solo contatori dopo la valutazione del pannello e riproduce gli stessi risultati byte per byte; non è entrata nel software di produzione. Nei due casi difficili il vecchio ritorno ammette errori di52,9% e99,7%, non soltanto un limite sotto la risoluzione del documento.

Lo scalare ora restituisce solo dopo il criterio di tolleranza o arrotondamento; se entrambi falliscono a profondità30, solleva `domain_error`. Il vettoriale valuta il prefisso controllato anche alla profondità massima e applica lo stesso rifiuto se non è accettato. `controlled=0`, componenti passive, finitezza di tutte le componenti e criterio legacy sul rumore restano conservati. Le regressioni native verificano anche che una richiesta fallita nella cache continui a fallire alla ripetizione. Due regressioni scalare/vettoriale con un salto a1/3 raggiungono il limite in meno di2000 callback, senza un albero esponenziale.

L’esito è un **fallimento trasparente per mancata convergenza**, non un nuovo algoritmo capace di calcolare tutte queste parametrizzazioni valide. Anche lunghezze/loft, aree e massa possono ora rifiutare geometrie valide a quel limite; il report massa applicativo intercetta l’eccezione. Non aumentati profondità o tolleranze e non aggiunte regole per singoli documenti.

## Limiti e prosecuzione

Restano stime di errore, criterio legacy di accettazione del rumore, arrotondamenti della normalizzazione, dinamiche miste e underflow del risultato finale non rappresentabile. La guardia non certifica ogni operazione in floating point. Il check delle soglie dell’agente riguarda round-to-nearest; modalità FTZ/DAZ e altri arrotondamenti non sono certificati. L’analisi aggiunge lavoro costante sui 15 campioni: non si rivendica un guadagno di prestazioni.

La fase successiva ha riparato anche la regressione del profilo proiettato e il budget cumulativo dei test pose emersi nella CI del checkpoint. Dopo quelle due modifiche, app completa ricompilata e7/7 CTest pertinenti PASS, inclusi di nuovo tutti i13 gruppi numerici; suite proiezione5/5 e mappa aggiornata PASS. Queste verifiche sono locali, separate dall’esito remoto. Nessun CAD originale modificato né processo utente chiuso. Solo le riparazioni CI sono state pubblicate in78d7268; header/test/catalogo numerici restano locali. Tre nuovi gruppi di profondità verificati sulla vera baseline18d0adc:0/3PASS attesi prima,13/13PASS complessivi dopo. Un primo driver aveva incluso per errore l’header candidato dalla directory del sorgente, ed è stato corretto prima di attribuire il risultato alla baseline. I controlli interni del verificatore sono stati adeguati allo schema AST reale e al limite sul template, senza modificare software o risultati numerici. [Elenco dei punti aperti](review-punti-aperti-2026-11-17.md), [checkpoint e CI](review-checkpoint-push-2026-11-17.md).

[Prove inerti e checksum](review-k10-underflow-prove-2026-11-17/SHA256SUMS).

Pulizia finale: rimossi84622928byte di ELF/oggetti di questo staging numerico e123176752byte nello staging CI (198.2MiB complessivi). Conservati sorgenti, log, CAD e prove. Eseguibile finale app SHA256 `adcf65dee896c8c65bb61b60a8b249b4e5e8366cb5994d3733a4793930396e5d`; nessun processo utente riavviato.

Controllo finale del catalogo: copie CMake archiviate rinominate come testo inerte, con contenuti e sigilli originali conservati e verificati tramite `REPACK.json`. Non cambiano software, log o risultati delle prove.

## Prossimo punto: carrier globale, proposta non eseguita

Un agente Sol ha delimitato un probe pubblico `wrapBodyOnSurface`: box1×1×0,001, anchor(0,0), tolerance1e-6; target BSplineSurface cubica3/3,9×9poli, pesi1, nodiU=V `{0×4,46/256,47/256,48/256,49/256,50/256,1×4}`. XY dei poli sono le coordinate Greville; Z zero salvo polo(4,4), alto `(9/4)*1e-4`. La proposta produce un rilievo C2 molto localizzato vicino a(3/16,3/16). Il percorso solido esclude la conservazione diretta del carrier; le griglie di fit derivano dai breakpoint sorgente e il wrap passa cuts vuoti. Verificare se l’interno del rilievo viene perso pur rispettando le sonde: nessun runtime di questo nuovo probe né nuovo bug accertato in questo intervento. Non aumentare semplicemente il numero di sonde come sostituto di una garanzia continua.
