Prove finali cavita - 16 novembre 2026. Raccolta congelata, nessun binario/CAD/object/library incluso.

Baseline congelata: kernel9eccfa56..., fixture8d7d8d50...; fonti/artifact completi in metadata.json. Originale8test:7/8PASS; probe8casi:3/8PASS con5fallimenti equivalenti r0.5; nuovo file finale6test:2/6PASS,4fallimenti geometrici. Non sono expected-failure e non sono state allargate tolleranze.

Fonte corretta dfad2aa7...; test finale626a8f42.... Stage8/8probe,6/6nuovi test,8/8originaliPASS. Tempi normali29.50s/23.64s/22.09s rispettivamente. Il target dedicato nuovo con TIMEOUT90 ha margine misurato. I tre B-rep r0.5 forward/reversed/seed17 sono byte-identici.

Actual audit dei7corpi finali:734fin,426917intervalli comuni da nodi originali edge+PC,2134585eval su5punti/intervallo; zero PC o endpoint fuori dai budget originali checkBody. Campionamento significativo, NON certificato continuo. I raccordi mantengono costruzioni tolleranti gia presenti: endpointmillimetrici entro budget registrato; nessun claim di precisione globale1e-7. L'asserzione sul raggio resta assoluta1e-7mm per tutte le scale.

ASan+UBSan+LeakSanitizer fuori sandbox: probe8/8exit0(539.25s),regressioni6/6exit0(340.35s),zero diagnostiche,detect_leaks=1,nessuna soppressione. Quattro TU uniche strumentate:fk_blend_surface.cpp,probe_cavity_variants.cpp,test_blend_cavity_regressions.cpp,fk_test_main.cpp. Ogni eseguibile ne collega2/3; tutto il resto del kernel e Release. Verifica mirata dei percorsi eseguiti, NON certificazione memoria dell'intero kernel. Fonti e comandi esatti nel buildmetadata.

History e OUTPUT esplicitamente azzerato all'ingresso della API storica. La prima proposta di preservare una sentinella su eccezione inventava una garanzia assente: fonte/log di questa assunzione superata sono isolati in superseded-history-assumption/, non rappresentano la baseline dei6test finali e non attestano un ulteriore bug. I test finali richiedono input immutato, OperationCancelled/categoria specifica consumo, contesto cancellazione restaurato e outputhistory vuoto senza pubblicazione parziale. Il wrapper pubblico non e stato modificato.

I metadata iniziali baseline-probe-and-four-test-source-hashes.json si riferiscono al probe e ai primi4test; i6test finali sono attribuiti dai hash nel metadata.json e sanitizer-build-metadata.json. Il root mantiene separatamente Release/app/CTest/Wayland/mappa. Nessuna ulteriore prova richiesta o eseguita dopo il sanitizer finale.
