# Apertura 2D: calcolo parallelo delle viste — 29 ottobre 2026

Studio eseguito su `solver-mesh-gpu`, sopra `4b05855`. Il parallelismo è ora verificato anche su `main`; commit e push richiesti esplicitamente. Le misure seguenti restano quelle dello studio sullo sviluppo. Installazione desktop invariata.

## Risultato misurato

CT0578 corrente, **11 viste**, Linux, Xeon W-10885M con 8 core fisici e 16 thread logici. Tre coppie di processi nuovi, ordine alternato, senza app CAD o compilazioni concorrenti; cache filesystem calda.

| Preparazione del foglio | Sequenziale precedente | Parallela automatica |
|---|---:|---:|
| Mediana di tre processi | 16,485 s | 10,981 s |
| Memoria massima residente, mediana | 1,106 GiB | 1,580 GiB |

**5,504 secondi recuperati, 33,39% in meno, 1,501×.** Stessa impronta di curve campionate, contorni, tracciati visibili/nascosti, campiture, immagini e risultati delle quote in tutti i confronti. Il confronto usa il precedente oggetto applicativo di preparazione, conservato prima delle modifiche; non un semplice interruttore interno al nuovo algoritmo.

Una coppia separata che costruisce davvero `DrawingSheetEditor`, comprese le entità grafiche, passa da **17,206 a 11,369 s**. Lettura e preparazione del modello richiedono circa 0,27 s e sono misurate separatamente. Non è una misura dell’avvio completo del programma. Un pilota con due worker richiede 12,801 s e 1,318 GiB: meno memoria, minor recupero di tempo. I valori grezzi sono nel rapporto JSON affiancato.

Il primo profilo seriale divide circa 16,79 s fra visibilità 14,36 s, curve 1,50 s, contorni 0,91 s e tratteggio 0,013 s. Il lavoro dominante è CPU nella classificazione della visibilità. La lettura del file non è il collo di bottiglia di questo campione.

## Come viene distribuito il lavoro

`prepareDrawingSheet` eseguiva già il calcolo fuori dal thread GUI, ma preparava le viste una dopo l’altra. La nuova pianificazione mantiene una barriera quando una vista deve creare la mesh di un corpo o richiederne una più fine; poi raggruppa le viste consecutive che possono completarsi usando soltanto le mesh disponibili.

Ogni worker riceve la propria mappa di cache. Corpi, geometria e mesh immutabili possono essere condivisi; risultati, indici di occlusione e tracciati sono locali. Il coordinatore unisce i risultati nell’ordine originale solo dopo il completamento del gruppo. Le viste derivate hanno già il proprio piano risolto nel foglio: i worker non modificano le relazioni fra viste. La GUI e la scena restano nel thread dell’interfaccia.

Le barriere conservano la precisione che ogni vista avrebbe osservato nella vecchia sequenza. Preparare subito la mesh più fine per tutte le viste avrebbe invece potuto cambiare il campionamento dei contorni precedenti. La deflessione necessaria è calcolata dalla stessa funzione usata dall’occlusione; la query della cache è conservativa.

Automaticamente si usano al massimo **quattro worker**, limitati dalle CPU disponibili e dalle viste compatibili. L’executor del kernel impedisce la moltiplicazione di pool annidati. L’overload interno con numero di thread permette confronti seriali (`1`) ed espliciti fino a otto; non è una nuova impostazione dell’interfaccia. Le callback di avanzamento sono serializzate e monotone. Annullamento o eccezione raggiungono tutti i worker prima di ritornare e non installano risultati parziali nell’editor.

## Correttezza e riproduzione

Il `.drw` corrente segnala un’impronta modello differente. Per misurare le viste effettivamente ricalcolate è stata creata soltanto una copia temporanea `CT0578-benchmark.drw` con l’impronta del modello congelato corrente. Geometria e originali restano preservati. Hash, normalizzazione, sorgenti delle sonde, log e misure sono archiviati localmente in `forgecad-linux-solver/benchmarks/drawing-opening-2026-10-29` (ignorato da Git).

La nuova regressione `forgecad_drawing_preparation_tests` verifica identità seriale/automatica/2/4/8 worker, fonti e scale multiple, barriere di raffinamento, sezioni e ombreggiatura, quota analitica di raggio 3 mm, cache, progressi, cancellazione prima/durante, errori, richieste concorrenti e parallelismo annidato. **Verifiche concluse:** applicazione completa e altri 25 target compilati; 30 test pertinenti superati. La prima esecuzione estesa ne supera 29/30 in 62,29 s: il test multifoglio rimasto contava ogni emissione della vecchia fase 5 come una vista. Aggiornato all’avanzamento aggregato monotono, con verifica dei risultati e degli ID di entrambe le viste, passa alla ripetizione in 1,21 s. Le altre 29 prove restano valide perché la correzione riguarda soltanto quell’oracolo di test; nessuna ulteriore modifica produttiva.

Mappa finale: 503 file, 6899 relazioni, 238 unità di compilazione, zero diagnostiche; `--check` superato, cinque simboli con chiamate risolte verificate nei due versi e distinte dagli include, 16279 siti 2D con righe valide. Blocco interessato ispezionato visivamente; HTML/JSON generati restano ignorati.

Conferma finale sul CT contro gli oggetti della build finale: impronta ampliata a tutti i campi dei risultati delle quote identica al riferimento (`24526793025eba0c26e0d3b1b1bb280844ae4f7f3c88782059a2fba965a2be0e`) e costruzione dell’editor riuscita. ForgeCAD di sviluppo risulta riaperto dopo le misure comparative; il controllo preventivo ha quindi fermato l’ultima richiesta di benchmark. Si è proseguito con verifiche funzionali, senza usare i loro tempi. Nessuna nuova chiusura dell’app richiesta e nessun processo utente terminato.

Nessuna modifica a tolleranze, formati persistenti, algoritmi geometrici o percorso CUDA. Beneficio misurato su questo campione; una sola vista o molte barriere possono offrire meno parallelismo. Nessuna nuova verifica macOS/Windows o ThreadSanitizer.

## Possibili passi successivi, non implementati

- Ridurre la classificazione della visibilità dei dettagli alla zona ritagliata, con esclusioni conservative: oggi il ritaglio grafico avviene dopo il calcolo dell’intera vista.
- Riutilizzare le sezioni geometriche identiche fra dettagli quando piano, sorgenti e precisione coincidono.

Richiedono nuove misure e confronti geometrici separati. Il cambiamento attuale si limita a distribuire su più CPU i gruppi di viste già indipendenti.

I file della cartella esempi CT0578.drw risultano aggiornati durante l’intervento dopo il congelamento; conservati senza ripristino. Tutti i confronti usano le copie congelate con SHA iniziali invariati.

## Verifica del porting su main

Applicazione completa e 26 target compilati; **30/30 CTest superati** in 63.50 s. Stessa impronta completa sul CT congelato a 11 viste e costruzione dell’editor riuscita; tempi di queste prove esclusi dai benchmark. Mappa main: 476 file, 6743 relazioni, 233 unità, zero diagnostiche; `--check` superato, chiamate dei cinque simboli verificate nei due versi, include distinti e blocco ispezionato. Differenze del kernel e dei cataloghi dei due branch conservate. Il prototipo del ritaglio dei dettagli resta esterno all’applicazione.
