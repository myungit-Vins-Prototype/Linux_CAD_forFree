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
- Suite CTest completa locale: in corso sul codice integrato, con copie congelate dei tre documenti esterni; nessun CAD originale modificato.
- Mappa architetturale rigenerata: 548 file, 7572 relazioni, 258 unità AST, zero errori; controllo di aggiornamento PASS.
- Verifica GitHub Linux/macOS del nuovo commit: da eseguire dopo il push. I risultati delle precedenti esecuzioni restano falliti e non attestano la correzione.

Nessun test disabilitato, nessuna soglia geometrica allargata, nessun cambiamento alle opzioni del workflow. La certificazione del rendering nativo resta separata dalla CI offscreen.
