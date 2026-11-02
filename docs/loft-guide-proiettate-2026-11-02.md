# Loft con guide proiettate — 2 novembre 2026

## Caso e causa

Nel pannello «Loft di superfici» di `SurfaceModellingExample219.prt`, sezioni **Curva 3 di Schizzo 8** e **Curva estratta 3D 1**, guide **Curva estratta 3D 3/5**, G1 al100%, la build precedente falliva con `Loft non riuscito: planarImage: tipo di curva non gestito`. Il difetto è riprodotto anche nel pannello di produzione.

La guida5 è un tratto dell'intersezione implicita esatta dei due schizzi proiettati. Le sezioni selezionate come riferimenti3D sono invece entrambe piane: poli a Z=15 e X=-80. Il loft le marcava come spaziali e tentava una proiezione2D generica incompatibile con la guida implicita.

## Correzione nel kernel

`fk_loft.cpp` certifica il piano della sezione razionale dai suoi poli e usa le intersezioni esatte `planeRoots<3>` sulla guida originale, verificando poi l'appartenenza3D alla sezione. Una sezione rettilinea usa due piani indipendenti, anche quando la guida giace nel primo. Nessun fit della guida, polilinea di display o mesh entra nella geometria.

Il criterio di planarità ammette soltanto l'arrotondamento double: `128*epsilon*max(1,norm(origin),length)`. Se supera `min(tolleranzaLocale,kLinearResolution)`, la sezione non viene certificata. Le coordinate lontane non ampliano il budget geometrico.

La regressione ruotata/traslata ha trovato anche una perdita di radici in `fk_sketch_projection.cpp` per piani quasi paralleli alla normale di estrusione. Ora il contributo massimo `abs(dot)*2*sqrt(3)*kSizeBox` viene riservato dal budget esistente, riducendo della stessa quantità il budget della ricerca razionale; il residuo originale viene verificato. Nessuna soglia è stata aumentata.

La curva sorgente conserva la sua rappresentazione implicita. La superficie risultante è la normale NURBS del loft, costruita con il procedimento adattivo già esistente: budget1e-9mm e verifiche fino a4e-9mm. Non si dichiara una coincidenza algebrica globale fra la NURBS finale e qualunque guida implicita. Nessun nuovo tipo persistito, header/ABI o formato; formato nativo60 e BodyIO invariati.

## Verifiche

- Build Release completa di app e target configurati: PASS; eseguibile `forgecad-release/forgecad` aggiornato.
- Nuovo runner indipendente da OCCT `forgekernel_loft_projected_guide_tests`: **10/10 gruppi PASS**, anche nella build integrata. Due/tre sezioni, trasformazioni, ordine inverso, rango1 con piano coincidente, incroci mancanti e multipli, nonplanarità, origine lontana, fedeltà e BodyIO.
- Causalità: kernel precedente FAIL al primo caso con planarImage; solo fix loft FAIL sul caso trasformato; entrambi i fix PASS10. Regressioni precedenti di proiezione21 e riferimenti39 PASS.
- Clang23 sui tre C++ del nuovo intervento: PASS. ASan/UBSan sui due moduli e sul nuovo test: PASS10 senza diagnostiche; supporti non strumentati e leak detection disabilitata, non app completa.
- Copia CAD iniziale: lamina valida con **1 faccia/4 bordi**, scarto massimo campionato sulle sezioni `3.18e-14mm` e sulle guide `5.10e-10mm`. Sorgente ancora implicita e invariata, BodyIO PASS; i campioni verificano il risultato e non definiscono la geometria.
- Pannello con oggetti di produzione finali: **Wayland e XCB PASS** per anteprima, Conferma, Undo/Redo, salvataggio/riapertura con cache e senza cache e rigenerazione. Controlli B-rep e tassellazione PASS; contesti GL validi, GL_NO_ERROR; Intel/Mesa su Wayland e Quadro/NVIDIA su XCB. Catture ispezionate: pannello pronto e superficie confermata. Nessun processo utente chiuso.
- Anche una copia dell'ultima versione sul disco supera gli stessi controlli funzionali del pannello offscreen. Le prove scrivono soltanto in `/tmp`; piani datum nascosti nelle copie QA per la leggibilità delle catture.
- Suite completa aggiornata **164 CTest in corso**; il checkpoint CI precedente sul codice prima del loft era162PASS/1SKIP GL/0FAIL e kernel459/459.
- Mappa aggiornata: **549 file/7598 relazioni/259AST**, zero errori; controllo,11confini e direzioni separate call/include/reference PASS. HTML loft e proiezione ispezionato; snapshot generati ignorati da Git.

## Documenti di prova e limiti

Prima copia:26918byte, SHA256 `c0f55b07e446682fe4dcf0f6f982a80bc8ff6b87fabded0dc57c3196ea930b7b`. Durante le prove il file originale è cambiato; le sonde non lo hanno scritto. La seconda copia,26454byte, SHA256 `f23d7343087212592c1278b8fdc2a6771f609e38351580971cffcfde022d0fff`, passa anch'essa nel pannello corretto. Le misure numeriche sopra si riferiscono alla prima copia.

Una guida implicita con sezioni realmente non planari resta rifiutata esplicitamente. Una guida complanare con una sezione curva o sovrapposta a una retta non determina un attraversamento univoco nel percorso abilitato. Nessuna certificazione globale di auto-intersezioni dichiarata. Questi limiti sono distinti dal caso reale corretto.

Diagnosi CI, commit e verifiche GitHub sono nel [rapporto CI](ci-riparazione-2026-11-02.md). Archivio dei comandi, hash, log e catture: da consolidare al termine della suite.
