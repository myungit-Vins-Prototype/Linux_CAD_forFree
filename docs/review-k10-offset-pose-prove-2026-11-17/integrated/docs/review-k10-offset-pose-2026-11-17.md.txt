# K10: svuotamento dopo scala e rotazione — 17 novembre 2026

Corretto il fallimento riproducibile dello svuotamento sulla fixture libera cucita `shell_stitched_tangent.body`, associata al limite già registrato di Superfici.prt. Tre agenti GPT-6.1 Sol hanno separato implementazione, test e audit indipendente. **Nessun commit o push**; HEAD e origin/main restano `47c7c19`. Il caso è stato riprodotto sulla fixture congelata, senza modificare CAD originali.

## Causa e cambiamento

La sola scala2,5, con spessore1,25, è sufficiente a provocare il fallimento precedente; la rotazione non è necessaria. Il controllo senza scala ma ruotato riesce già prima della correzione. Nel caso composto la rotazione è di0,42rad intorno all'asse(1;2;0,3), centro(12;−23;7).

Il bordo3D è costruito correttamente; fallisce il fit della relativa pcurve su un supporto di **estrusione**, non il nuovo certificatore dei carrier bicubici. La proiezione privata `projectLocal` interrompeva Gauss–Newton quando `norm(stepUV) ≤ 1e-12·(1+norm(UV))`, **prima di applicare quel passo**. Questo criterio dipende dalla grandezza e dalle unità delle coordinate parametriche, senza garantire l'errore necessario al fit. Con V≈200 consente passi omessi dell'ordine2e-10UV, contro un budget del fit di circa6,58e-11UV. Il fit raffina fino a una campata di circa1,52e-13 nel parametro del bordo senza eliminare il rumore e fallisce.

In `kernel/fk_offset.cpp` è cambiato soltanto quel criterio: arresto anticipato per due componenti del passo **esattamente nulle**; altrimenti prosegue la ricerca lineare esistente, che richiede una diminuzione stretta della distanza3D. Mancato miglioramento, limite60iterazioni, controlli di singolarità, non finiti e raggio di ricerca restano come prima. Non sono cambiati solver regolarizzato delle normali, `fitCurve`, superfici, genealogia o tolleranze. Il budget nominale dell'offset resta1e-7mm.

Un controesempio analitico indipendente con `Fraction` dimostra il difetto del vecchio predicato: sullo stesso piano, riancorare UV a200 fa scartare un passo esatto non nullo δ=2^-33, che con origineUV0 viene applicato. Il nuovo predicato applica il passo e in questo esempio porta la distanza3D a zero. È una prova del difetto del criterio, non un certificato dell'intero svuotamento.

La diagnostica privata sullo stesso gruppo di25parametri misura una diminuzione dello **spread** di U−4t da1,92415e-10 a4,44e-16 e di V−200 da5,82077e-11 a8,53e-14. Il riferimento(4t,200) è diagnostico: dopo la correzione rimane un offset quasi costante. Questi numeri descrivono la stabilità della callback, non la sua accuratezza assoluta o una prova continua. Nessun hook diagnostico è presente nella produzione.

## Verifiche finali

| Verifica | Esito e perimetro |
| --- | --- |
| Build completa | PASS, app e runner aggiornati, circa177s. |
| CTest pertinenti | 21/21 test distinti PASS, zero skip:17kernel più4UI sul runner finale. Dettaglio del primo avvio e della ripetizione sotto. |
| Kernel generale | 483/483 PASS, incluso il test storico dei quattro svuotamenti non trasformati e le regressioni giunti originali. |
| Nuovi test | 3/3 PASS; identico sorgente sul codice precedente:1/3PASS e2FAIL reali del fit. Controlli di spessore, genealogia, codec e input immutato. |
| Regressioni mirate precedenti | 16/16 CurveFitValidation/OffsetValidation PASS; sorgenti invariati, inclusi cancellazione, input invalidi, atomicità e singolarità. |
| ASan/UBSan/LSan | 19/19 PASS: un TU di produzione modificato (offset),3TUtest/main strumentati; resto kernel Release. Calibrazione19byte intenzionali rilevati, exit23 fuori ptrace. Non certifica l'intera app. |
| Audit indipendente API finale | Caso composto chiuso1,25:22facce/2shells,22origini di faccia dichiarate, input immutato, codec e checkBody PASS.16 misure interne per genealogia, massimo5,740538e-8mm; verifiche puntuali, non certificato continuo. |
| Caso aperto aggiuntivo | Scala+rotazione, apertura della faccia5 dichiarata, spessore1,25: prima FAIL, dopo PASS;21facce/1shell, input e storia preservati. Prova aggiuntiva staged, non un quarto test permanente. |
| Controesempio esatto | Fraction dimostra il difetto del vecchio arresto UV e l'applicazione del passo nel caso piano analitico. |
| Avvio app | Wayland PASS5s con configurazione temporanea, eseguibile effettivo aggiornato verificato; nessun documento o processo utente toccato. Non è verifica pixel/GL. |
| Architettura | 786nodi/11488relazioni/396TU, zero errori AST; hash/configurazione/HTML e call/include/reference nei due versi verificati. |
| Conservazione | Baseline5977file e25pacchetti precedenti preservati; API locali, moduli K10 precedenti, regressioni originali e fixture immutati. Index vuoto, nessun commit/push. |

Nel primo avvio CTest, sovrapposto al completamento della build, due test UI non avviano il runner (`BAD_COMMAND`, permission denied). A build conclusa sono stati rieseguiti tutti e quattro i test UI sul binario definitivo, tutti PASS: anche i due precedentemente riusciti sono esclusi dal conteggio finale perché precedevano il relink. I17test kernel già ricompilati sono conservati; log/JUnit iniziali, ripetizione e risultato aggregato restano distinti nel pacchetto. Non è stato modificato codice per recuperare questi errori di avvio.


Il nuovo `test_shell_pose.cpp` e il runner CTest autonomo non richiedono Qt/OCCT. Scala, scala+rotazione e controllo ruotato verificano input originali e trasformati immutati, due shell chiuse, assenza di bordi liberi, storia delle facce esplicita, codec e validità B-rep. Misurano inoltre lo spessore in almeno sei facce interne con la stessa soglia regressiva2e-5mm già usata dal test storico. Le misure puntuali, la quadratura di massa, `checkBody` e la mesh sono regressioni geometriche, non certificati continui globali. I test storici non sono stati modificati.

## Costo e limiti

La ricerca può eseguire più lavoro. In due confronti singoli su casi riusciti sia prima sia dopo, il tempo CPU totale cresce circa6%; il tempo reale circa50%: originale8,931→13,337s, controllo ruotato9,079→13,689s. Durante queste misure era in corso la build del root: non sono un benchmark statistico o una stima generale dell'app. La diagnostica a parametri fissi conferma più valutazioni della distanza3D; il costo non viene attribuito soltanto alla contesa. I limiti di iterazione restano finiti e invariati.

Il mancato miglioramento della distanza3D non dimostra accuratezza globale o convergenza matematica della proiezione. Il fit successivo conserva i suoi fallimenti terminali; callback generiche e superfici libere continuano ad avere controlli campionati preesistenti. **K10 resta parziale**: nessuna nuova certificazione globale di trim, topologia, Newton, autointersezioni o offset normale. L'esempio analitico, i campioni diagnostici e le operazioni pubbliche sono evidenze distinte.

È risolto il riproduttore congelato del limite Superfici scalato/ruotato nei casi indicati. Non viene dichiarata una soluzione universale per ogni scala, posa, spessore o documento aggiornato, né una nuova verifica interattiva del `.prt` originale. Il vecchio prototipo di base locale, che falliva anch'esso, non è stato integrato. Restano aperti gli altri carrier K10, il caso kernel inverso Flacone3 già recuperato dall'app, la verifica generale delle schede Wayland e quella nativa macOS. La CI verde precedente riguarda sorgenti precedenti; nessuna nuova CI GitHub senza il futuro commit/push autorizzato.

[Prove, ricette, snapshot inerti e manifest](review-k10-offset-pose-prove-2026-11-17/README.txt).

[Memoria precedente conservata](history/claude-prima-k10-offset-pose-2026-11-17.md).
