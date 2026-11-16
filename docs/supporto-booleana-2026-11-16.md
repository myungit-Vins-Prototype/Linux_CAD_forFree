# Supporto.prt: unione dei tre corpi — 16 novembre 2026

**Corretto nel kernel di produzione e verificato sull’app Release ricompilata.** L’unione Corpo 2/Corpo 3 riesce in entrambi gli ordini; riesce anche l’unione dei tre corpi nelle due sequenze provate. La copia `output/Supporto_unito.prt` contiene l’unione come settima funzione della storia, con identità nativa distinta.

## Versione analizzata e diagnosi

| Corpo | Funzione | Indice nella storia |
| --- | --- | --- |
| Corpo 1 | Estrusione 1 | 0 |
| Corpo 2 | Estrusione 3 | 2 |
| Corpo 3 | Loft 6 | 5 |

Le prove riguardano l’input congelato di 177161 byte, SHA256 `79887e91d75feb748af19fcdb14cd469cc40e74abab03e3cbeaa12e2d3271f7f`. I tre B-rep salvati superano il checker. La rigenerazione senza cache ricostruisce nuovi oggetti senza errori e produce gli stessi byte B-rep. Prima della correzione la coppia 2/3 fallisce nei due ordini e anche il comando applicativo sui tre fallisce. Il retry inverso dell’app non basta. Corpo 1+2 riesce come due shell disgiunte; Corpo 1+3 come una sola shell valida.

La corda di intersezione fra cilindro e piano del cap finale è quasi tangente al **bordo ellittico della faccia ritagliata**. I carrier piano e cilindro si intersecano trasversalmente. Dai coefficienti double originali esportati in esadecimale, una prova continua indipendente con Fraction e radice intera arrotondata verso l’esterno limita la sporgenza massima dell’intera ellisse a **1,97910812234e-11 mm**. La risoluzione del documento è 1e-7 mm; la connessione booleana è 1e-6 mm. Entrambe restano invariate.

Il clipping precedente classifica la corda, lunga circa 2,57e-5 mm, interna al cilindro e sul bordo del cap. Divide soltanto il cilindro e rimuove il cap rimasto indiviso. Le facce laterali della loft conservano due microarchi ellittici: l’assemblaggio presenta tre edge con un solo uso. Il pairing radiale e lo split dei vertici non cambiano quei conteggi. `Body::build` rifiuta correttamente il risultato aperto; la proiezione finale delle pcurve non è ancora eseguita. Eliminare soltanto la corda lascerebbe due microarchi orfani.

## Correzione generale

Il nuovo modulo `kernel/fk_boolean_boundary.{h,cpp}` certifica una catena del bordo originale attraverso gli eventi reali di intersezione e le incidenze di face/fin/edge/vertice. Cerca i due versi del loop e accetta una sola catena monotona, senza riassociare elementi per prossimità. La provenienza dell’altro operando resta separata dagli indici locali di `Piece`.

Il percorso riguarda intersezioni rettilinee e bordi ellittici originali di una faccia piana, con carrier piano/cilindro certificabili. Aritmetica intervallare arrotondata verso l’esterno e limiti di derivata provano scarto e monotonia sull’intero intervallo. Il budget nuovo è `min(BooleanOptions.tolerance, linearResolution())`. La geometria realmente montata viene ricontrollata dopo l’assemblaggio e dopo `unifySameDomain`, usando la genealogia esplicita. Le nuove immagini UV affini mantengono il parametro originale dell’ellisse e ricevono un limite continuo positivo; il massimo osservato è **4,3398801253300545e-11 mm**.

Il nuovo percorso è attivo soltanto in `booleanOperation`. Catene ambigue, famiglie non supportate, dati non finiti o certificati inconclusivi conservano il percorso precedente. Limiti di lavoro finiti e punti di cancellazione impediscono traversamenti indefiniti. Se una sostituzione già usata fallisce con `domain_error`, si ricostruisce una sola volta sul percorso precedente, con nuove copie, identiche opzioni e storia azzerata. Cancellazione, `logic_error` diretto e altre eccezioni propagano. Nessuna modifica al formato nativo, alle enum persistite o alla geometria sorgente.

La risoluzione e il budget del nuovo contatto sono distinti dalle tolleranze locali del B-rep. Il risultato riporta un massimo di circa 1,893e-5 mm su edge/vertici; il codice preesistente `tolerantEnds` è byte-identico. Non si afferma che ogni tolleranza locale dell’intero risultato sia inferiore alla risoluzione. [Controllo puntuale](supporto-booleana-prove-2026-11-16/root/local-tolerance-audit.json).

## Verifiche effettive

| Prova | Esito e perimetro |
| --- | --- |
| Baseline sul CAD | 176 controlli PASS: 24 tentativi, 8 riusciti e 16 fallimenti attesi; input e transazione del comando fallito preservati |
| Replay finale Release | **424/424** controlli; **24/24 unioni riuscite**, 12 worker e 12 tramite `CadViewport::createBoolean`, snapshot e rigenerato; nessun oggetto kernel privato sovrapposto |
| Consegna nativa | **65/65**: nuovo UUID, Save atomico, riapertura, ricalcolo senza cache, Annulla/Ripristina, scena con il solo risultato disponibile, definizioni e B-rep preservati |
| Nuovo test permanente | **51 asserzioni**: budget, monotonia, versi, input non finiti/overflow, casi non supportati, UV nel parametro originale, intervalli minimi, scale/pose; incluso nei 24 CTest |
| Build completa | App, runner e kernel ricompilati; **24/24 suite CTest** e **38/38 regressioni Boolean** PASS |
| ASan/UBSan/LSan mirati | **78/78** controlli senza diagnostica, due TU kernel nuove/modificate e driver strumentati; resto del kernel Release, nessuna soppressione |
| Retry iniettato privato | **15/15**: singolo ripiego dopo sostituzione, reset storia, stesse opzioni, propagazione di cancellazione/logic_error, input immutati |
| Architettura | **765 file, 11045 archi, 382 TU**, nessuna diagnostica; `--check`, hash, HTML e 107 archi incidenti ai quattro file del blocco verificati nei due versi |

Il risultato consegnato ha 11 facce, 24 edge, 17 vertici, una shell chiusa e una regione solida; volume circa **696887,830657 mm³**. Tutte le facce conservano provenienza dagli operandi, nomi persistenti e carrier originali. Il controllo campionato della frontiera non trova parti perse o aggiunte; non è una certificazione continua globale dell’intera booleana. La prova continua riguarda i contatti e le immagini UV introdotti. Nessuna nuova verifica grafica OpenGL è attribuita a questa correzione.

Il test permanente è autonomo e non distribuisce il CAD dell’utente. La regressione negativa completa usa la copia congelata di Supporto.prt nei programmi privati archiviati. Alcune varianti sintetiche erano già accettate dalla baseline e non sono presentate come regressioni negative. Il primo tentativo LSan nel sandbox termina per il limite ptrace dopo le asserzioni: è conservato e distinto dall’esito unrestricted positivo dello stesso binario. Il fixture di consegna è stato corretto per verificare la visibilità effettiva, anziché imporre flag storici che `currentDocument()` normalizza; tentativi iniziali conservati, nessuna modifica al prodotto per adattarlo al fixture.

## Consegna, conservazione e ripresa

Copia consegnata: `output/Supporto_unito.prt`, SHA256 `95bdb2d2d27f4db6f8ef6461b39c2a59394024c7fc15238af706394c7e8bb3b6`, nuovo UUID `{3a1ae096-e6b7-454f-aa58-9acf9cf84323}`. La storia conserva i sei passi originari e aggiunge `Unione 3 corpi`; il ricalcolo completo produce gli stessi byte B-rep. Riaprire l’app per usare il kernel ricompilato.

Questa revisione non scrive nell’originale `/home/myungit/Documenti/Cad_Example/Supporto.prt`. Alla ripresa dopo l’interruzione, il file corrente risulta di 589514 byte con SHA256 `70e4ec5c218d590d85ebb44a45bd02546155135564d5be3b73847ba9573f1759`, diverso dall’input congelato. Il file attuale è preservato: non viene sovrascritto né dichiarato coperto dalle prove eseguite sulla precedente versione. Le directory temporanee della precedente esecuzione non sono più disponibili; sorgenti, log, coefficienti e metadati erano già archiviati nel repository. [Osservazione alla ripresa](supporto-booleana-prove-2026-11-16/root/input-observation-after-resume.json).

Confronto con la baseline congelata: **3110/3115 file preesistenti invariati**. I cinque cambiamenti attesi sono `CLAUDE.md`, il registro della review, CMake kernel, `fk_boolean.cpp` e il catalogo architetturale. Tre nuovi sorgenti/test, questo rapporto, l’archivio della memoria e le prove sono inventariati separatamente. I tre blocchi precedenti restano sigillati e verificati: 95, 146 e 172 artefatti.

[Indice delle prove](supporto-booleana-prove-2026-11-16/README.md), [audit finale](supporto-booleana-prove-2026-11-16/root/final-audit.json), [manifest copia](supporto-booleana-prove-2026-11-16/root/output-manifest.json), [memoria precedente](history/claude-prima-supporto-booleana-2026-11-16.md). K11 è corretto; K10 e l’uniformazione completa delle anteprime restano aperti secondo la [review](code-review-2026-11-15.md).
