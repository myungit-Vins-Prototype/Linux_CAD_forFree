# CT0578 Sweep4: integrazione main — 27 ottobre 2026

Porting isolato sul checkout dell'ultimo origin/main `49d69b05eafcffa32960230aad2535d63b01e8cc`, in `/tmp/forgecad-shell-publish-main-20261027`. Conservato integralmente il fix locale di soppressione e le sue regressioni; nessun riallineamento dei branch locali divergenti. Nessuna ottimizzazione sperimentale solver/GPU inclusa nel porting.

## Modifica

Pannello e builder condividono `patternFeatureSupported`: la ripetizione della funzione accetta anche sweep solidi in unione/sottrazione. Il selettore identifica la sorgente per nome; il corpo di base resta fermo mentre si ripete lo strumento. Conservati i messaggi specifici di errore degli operandi. Nessuna modifica al formato, alla politica dei candidati o alla modalita di ripetizione del corpo intero.

Le copie a180/270 gradi di CT0578 incontravano anche un difetto indipendente nelle intersezioni: la forma quadratica con diagonali nulle e termine misto non nullo divideva per zero. Radici omogenee scalate risolvono le stesse equazioni, con tolleranze e punti di cancellazione invariati.

Regressioni permanenti portate: sella contro piano con diagonali nulle/quasi nulle e ordine delle superfici invertito; sweep unito/sottratto attorno a Y con volumi analitici, esclusioni, dialogo, Undo/Redo, codec, rigenerazione e diagnostici.

## Verifiche concluse

Compilazione Release mediante CMake in `/tmp/forgecad-shell-publish-main-build-20261027`, target applicazione `forgecad`, runner `forgecad_view_tests` e `forgecad_feature_dependencies_tests`. Kernel selezionato118/118 PASS, incluse le nuove singolarita. 25/25 CTest selezionati PASS (5,95secondi), incluse ripetizioni, sweep, storia, soppressione, anteprima e dipendenze. Non sono le suite complete.

QA nativa collegata agli oggetti main: snapshot precedente SHA3db83144... con Sweep4 come ultima feature, quattro tagli attorno a Y; anteprima geometrica/mesh, topologia, sorgente byte-immutata, esclusione1 occorrenza, Undo/Redo e codec con/senza cache + rigenerazione PASS,0erroriGL. Volumi1672,049471600 originale,1666,618638413 quattro tagli,1668,428916142 tre tagli. Pannello e finestra applicazione ispezionati; le catture framebuffer grezze possono contenere il compositore vetro e non sono un confronto pixel.

File attuale congelato SHA256 `56fcf389a7086d30f46c30559bbbbf16938d2e549a099d0d642efd0acf8301bb`:5schizzi/5feature, gia presente Ripetizione5 circolare della funzione con62occorrenze. Rigenerazione completa con oggetti main PASS,0errori, un solo risultato visibile,263facce. Nessuna modifica del documento originale. L’originale e stato salvato ulteriormente durante le prove: gli esiti si riferiscono alla copia congelata indicata.

Mappa main rigenerata469file/6558relazioni/230TU,0diagnostiche; --checkPASS. Verificati79446siti con righe valide,5confini nei due versi e include distinti. Viste cad_pattern.cpp e kernel/fk_marching.cpp aperte e ispezionate; HTML/JSON generati ignorati da Git.

Artefatti e log: `/home/myungit/Linux_CAD_forFree/forgecad-linux-solver/benchmarks/ct0578-main-2026-10-27`. Prove su copie congelate con identita indipendente in memoria; nessuna scrittura del CAD originale, archivio, sessione o lock. Commit del porting richiesto successivamente; push non richiesto. Installazione desktop preservata. Nessuna build nativa macOS o misura di velocita dichiarata.

## Isolamento del commit

Il commit CT0578 esclude il fix locale della soppressione e la relativa documentazione. I25CTest e la QA applicativa sono stati eseguiti nel checkout che conserva anche quel fix preesistente; il commit CT0578 da solo non corregge la soppressione. Il remoto main e stato ricontrollato prima dello staging ed e ancora49d69b0. Ramo locale dedicato `fix-ct0578-main` per conservare il commit senza riscrivere il ramo locale main divergente.
