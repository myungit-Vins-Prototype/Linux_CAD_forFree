# CI — classificazione delle giunzioni estese, 16 novembre 2026

Dopo il push richiesto del commit c44975e, controllate entrambe le CI complete: Linux e macOS compilano e K12/WholeRim passa; l'unico nuovo fallimento è OffsetLoftExtendedSeamsTrimBoth nella suite generale,482/483casi. Le altre109/110 e112/113suite passano. Artifact e metadati del run43917116955 conservati. La riproduzione locale immutata fallisce anche con GCC16 e Clang23; CI Linux usaGCC15.2. Non si tratta di un problema specifico del compilatore.

Il ramo quasi tangente in fk_sheet sceglieva una giunzione ISO dal solo seno delle normali, circa1,49e-11. Le pcurve originali della cucitura sono invece B-splinegrado3 con262/409poli e coordinataU variabile. Il limite continuo rifiuta correttamente la mappa ISO sbagliata: residuo nello stessoUV circa0,03455mm contro cap1e-7. Questo non è un limite inferiore della distanza minima da tutta la superficie. Un testimone Fraction su coefficienti effettivi conferma il residuo della mappa al parametro scelto; nessun aumento di tolleranze giustificato.

Correzione integrata e verificata con tre agenti GPT-6.1 Sol: scegliere la famiglia dalla provenienza e dai coefficienti delle due fin sorgenti prima di tentare qualsiasi certificato. Le sorgenti nonISO conservano la via Newton storica, esplicitamente campionata; un certificato ISO riconosciuto che fallisce rimane terminale. Le ancore devono derivare dagli endpoint delle pcurve originali tramite incidenza e genealogia, non da una nuova proiezione con errore di qualcheULP. La prima bozza troppo restrittiva regrediva positivi esistenti ed è esclusa dalla versione finale.

La versione finale V4 usa le ancore delle fin sorgenti originali e un limite fisico continuo della variazione nella coordinata costante: hull dei poli NURBS a pesi positivi, oppure endpoint Line con aritmetica verso l'esterno, moltiplicato per il limite derivativo sul rettangolo del carrier effettivo. Il budget è prefissato dal minimo fra cap dell'operazione e contratto della fin sorgente; nessuna tolleranza viene aumentata. La provenienza è determinata da incidenza edge/face/vertex e univocità, senza ricerca per prossimità. La qualificazione della famiglia non certifica il bordo sorgente né l'algoritmo Newton. Ogni ISO selezionata conserva i controlli continui sui due supporti e sulle fin effettive, con fallimento terminale.

L'audit ha corretto anche l'enclosure di prodotti minuscoli: un residuo FMA può arrotondare a zero. La guardia conserva 106 bit di intervallo esponenziale per il prodotto dei due significandi, con percorsi esatti zero/unità. Un testimone Fraction dimostra il problema della bozza intermedia. Le bozze che regredivano positivi esistenti non sono state integrate.

## Verifiche locali sul codice finale

- Build completa dell'applicazione e di tutti i target configurati: PASS; eseguibile aggiornato.
- Suite generale del kernel: 483/483 PASS, incluso il test storico CI immutato e il positivo OffsetLoftSeamExtensionFollowsTangentSeam.
- 28/28 CTest pertinenti PASS; il target giunzioni contiene 33/33 casi, i 29 precedenti e quattro varianti pubbliche del loft: originale, posa rigida, scala0,25 e scala2,5 con posa. Estensione dei due anelli, taglio reciproco, genealogia, codec, tessellazione e input immutabile.
- GCC e Clang23:33/33PASS. ASan/UBSan/LSan:33/33PASS, fuori ptrace e detect_leaks=1; strumentati fk_sheet e driver/test, resto kernel/provider Release. Calibrazione separata: perdita intenzionale19byte rilevata,exit23. Nessuna dichiarazione generale di assenza di leak.
- Fraction sulle due fin Newton effettive selezionate per provenienza: 120campate, limite massimo1,543930915e-8mm sotto budget1,15e-7mm. Questa verifica offline riguarda le fin esportate; non rende continuo il metodo Newton runtime.
- Avvio dell'app finale per5secondi su Wayland con profilo isolato, senza CAD utente; terminato soltanto il processo di prova. Nessuna nuova certificazione pixel/GL.

Baseline5004file e16sigilli precedenti conservati; prima del codice consultati grafo778nodi/11361relazioni/390TU e538relazioni incidenti. Catalogo e vista aggiornati e verificati in entrambi i versi; hash, compileDB, HTML e --check PASS. Test_offset.cpp, fixture e CAD originali invariati. Le prime asserzioni nuove che pretendevano dichiarazioni genealogiche per ogni entità legacy sono state corrette al contratto pubblico effettivo prima del congelamento; nessun test preesistente indebolito.

K10 rimane parzialmente trattato nei24/24 siti iniziali; totale31rilievi invariato. Newton e famiglie non supportate restano campionati;17 API candidate orfane e colori UI letterali ancora aperti.

Nuova CI GitHub da verificare dopo il push: non si attribuisce il verde al run precedente fallito.

[Prove e sigillo](ci-sheet-join-prove-2026-11-16/README.txt); [memoria precedente](history/claude-prima-ci-sheet-join-2026-11-16.md).

SHA256: sorgente `b99a70318f2cf7dcd6b24e646d8976bd287f6622ed9cf4accab968403380afbc`; test `451b30b1488b94f3e817449a27779bef85ad0d28abf2896cabd4e22e4b5ab763`; app `3c251c23faf99bf16d47a90a8702ee466fcb63a7f35bc12ec6759e6aac448992`.
