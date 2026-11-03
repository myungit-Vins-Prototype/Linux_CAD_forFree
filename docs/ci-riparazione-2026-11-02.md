# Riparazione CI Linux/macOS — 2 novembre 2026

## Diagnosi dai log GitHub

L'esecuzione [41503734941](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/41503734941), sul commit `cc26a27`, compila correttamente sia Linux sia macOS. Falliscono i test `forgekernel_tests` e `forgecad_revision_store_tests` su entrambe le piattaforme. Anche le esecuzioni 41485859689 e 41476394741 confermano il difetto geometrico e la contesa intermittente del catalogo. Non è stato necessario cambiare il workflow.

### Sformo: conservare le sezioni circolari esatte

Il secondo sformo di un cilindro ricostruiva numericamente il bordo tra cono e piano come spline, perdendo la sezione circolare analitica. Il volume risultava circa `253.5270716475676`, contro `253.5270734420086`: lo scarto `1.79444e-6` supera la tolleranza esistente `1e-6`.

`kernel/fk_offset.cpp` ricava adesso direttamente la sezione circolare dei supporti sostitutivi piano/conico o piano/cilindrico quando gli assi sono compatibili. Conserva il parametro angolare originale e usa la stessa sezione per il vertice della cucitura chiusa con due soli appoggi. Negli altri casi resta il percorso generale; compatibilità, budget locali e controlli geometrici restano applicati.

Il riproduttore dà ora `253.52707344200866`, con scarto circa `5.7e-14`. La nuova regressione `DraftCircularSectionsStayAnalytic` verifica sei sformi successivi di entrambi i segni, tre scale, asse ruotato e origine traslata, volume analitico, bordi circolari, validità B-rep, persistenza e tassellazione. La soglia del test preesistente non cambia.

### Revisioni: il lettore non deve prenotare il lock del writer

La precedente rilettura prendeva `catalog.lock` quando `head.json` e `catalog.json` erano momentaneamente disallineati. Un writer concorrente usa intenzionalmente `tryLock(0)`: la prenotazione del lettore faceva fallire checkout/cancel validi. Riproduzione locale prima della correzione: 2 fallimenti su 8 esecuzioni concorrenti; gli errori erano nel writer.

`cad_revision_store.cpp` legge head/catalogo/head, valida digest e identità della fotografia e ripete la lettura solo durante una pubblicazione in corso. Il lettore non acquisisce il lock del writer. Il limite di attesa resta 1000 ms; corruzione stabile, output atomico, contesa e recupero esplicito mantengono la distinzione precedente.

La regressione pubblica atomicamente un catalogo coerente e mantiene occupato il lock mentre attende il lettore: fallisce col vecchio codice, passa col nuovo. Lo stress aggiornato passa 32/32 esecuzioni, in gruppi di quattro concorrenti.

## Verifica del codice integrato

- Build Release completa dell'applicazione e di tutti i target configurati: PASS.
- Runner integrato filtrato `Draft`: 6/6 gruppi PASS; runner revisioni: PASS.
- Clang 23: controllo sintattico dei quattro C++ modificati PASS. Il flag Qt/GCC locale `-mno-direct-extern-access` è stato rimosso esclusivamente dal comando di questa verifica con Clang.
- ASan/UBSan sui cinque translation unit modificati/di ingresso dei due runner: PASS, senza diagnostiche. Librerie di supporto e Qt non sono strumentati; rilevamento leak disabilitato. Non è una certificazione dell'intera applicazione con sanitizer.
- Suite CTest completa locale sul codice integrato: **162 PASS / 1 SKIP OpenGL offscreen / 0 FAIL su 163**, in 465,53 s; runner generale kernel **459/459**. Manifesti dei test copiati con la sola sostituzione della cartella esempi con tre documenti congelati; hash delle copie invariati, nessun CAD originale modificato.
- Mappa architetturale: 548 file, 7572 relazioni, 258 unità AST, zero errori. Aggiornamento, sette confini di chiamata e direzioni entrata/uscita call/include/reference PASS; HTML dei blocchi offset e revisioni ispezionato.
- Correzioni pubblicate su `main` nel commit `f75015b0bea3428bf92e6cce03f298cfe0323e46`. L’[esecuzione GitHub 41509525775](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/41509525775) ha compilato Linux; i test erano in corso al push successivo. Il job macOS è stato annullato da GitHub dopo15minuti senza acquisire un runner, con annotazione «The job was not acquired by Runner of type hosted even after multiple attempts» e avviso di capacità ARM64 insufficiente: nessuna build macOS eseguita. Il push del [fix loft aggiuntivo richiesto](loft-guide-proiettate-2026-11-02.md), commit `ec08b12040a30da3b64cfeeeafa66d4f40606143`, ha annullato automaticamente quel run secondo la concorrenza del workflow. La [nuova CI41513461524](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/41513461524) include entrambi i fix ed è **conclusa con successo**: job macOS terminato il2novembre alle20:11:56UTC, Linux alle20:18:51UTC; esito e artifact verificati il3novembre2026.

Nessun test disabilitato, nessuna soglia geometrica allargata, nessun cambiamento alle opzioni del workflow. La certificazione del rendering nativo resta separata dalla CI offscreen.

Checkpoint locale finale con il fix loft:163PASS/1SKIP OpenGL offscreen/0FAIL su164CTest e kernel459/459; dettagli e archivio nel [rapporto loft](loft-guide-proiettate-2026-11-02.md).


## Esito remoto conclusivo

[CI41513461524 sul commit ec08b12](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/41513461524): **compilazione e test riusciti su entrambi i sistemi**.

| Job | PASS | SKIP | FAIL | Test configurati |
| --- | ---: | ---: | ---: | ---: |
| Linux CPU |56|2|0|58|
| macOS CPU e Metal |60|1|0|61|

Su entrambi passano il catalogo revisioni, il kernel generale459/459 e il nuovo runner loft10/10. Su Linux vengono saltati soltanto GPU broad-phase e overlay OpenGL, su macOS soltanto overlay OpenGL; i test Metal previsti passano. Gli skip sono riportati dai JUnit ufficiali, senza modifiche al workflow o disabilitazioni aggiunte.

[Archivio CI verificato](history/ci-riparazione-2026-11-02-verifica.zip):126file, con log/artifact ufficiali prima e dopo, JUnit, hash e confronti causali; nessun CAD, eseguibile o oggetto. Le verifiche locali/GUI del loft sono nell'archivio distinto collegato al suo rapporto. Il commit conclusivo della documentazione usa `[skip ci]` soltanto dopo questa esecuzione verde: nessun sorgente o configurazione di build cambia rispetto al commit verificato.
