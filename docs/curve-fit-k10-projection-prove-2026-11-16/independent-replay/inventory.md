# K10 — dieci siti iniziali ancora interamente da migrare

Inventario riletto sui sorgenti correnti il 16 novembre 2026. I numeri di riga indicano le chiamate effettive al fitter campionato, non dichiarazioni o helper del fitter stesso. Rimane il conteggio operativo **14/24 siti iniziali parzialmente trattati, 10 completamente aperti**: questo blocco ha aggiunto il solo sito `Projector::finish` a quelli trattati. Le famiglie non riconosciute e i fallback dei quattordici trattati restano separatamente aperti; non diventano nuovi siti iniziali.

| N. | File e riga | Responsabilità ancora campionata |
|---:|---|---|
| 1 | `kernel/fk_offset.cpp:680` | `offsetCurve`: offset generico a partire da una funzione procedurale; occorre conservare e limitare la provenienza geometrica. |
| 2 | `kernel/fk_offset.cpp:1905` | Ricostruzione UV `trimSeams`: `projectLocal` sul supporto prolungato e scelta del ramo originale. |
| 3 | `kernel/fk_offset.cpp:2398` | Ricostruzione UV del bordo sostitutivo: seed modificato, inversione locale e distanza firmata dal supporto. |
| 4 | `kernel/fk_blend_mitre.cpp:129` | Giunto dei raccordi: curva media di due valutazioni di supporti con parametri risolti. |
| 5 | `kernel/fk_blend_surface.cpp:1655` | Continuazione del raccordo: curva procedurale della continuazione implicita. |
| 6 | `kernel/fk_surface_mapping.cpp:189` | Trasferimento UV `image.parameters` nella deformazione su carrier conservato. |
| 7 | `kernel/fk_quilt_mapping.cpp:713` | Trasferimento UV e box parametrico per l'offset del carrier nel mapping di un insieme di superfici. |
| 8 | `kernel/fk_exchange.cpp:740` | Rimappatura SP-curve nella cucitura: corrispondenza non affine sorgente/destinazione ottenuta mediante proiezione. |
| 9 | `kernel/fk_sheet.cpp:407` | Giunta delle superfici estese: intersezione/continuazione implicita `sample(t).point`. |
| 10 | `cad_sketch_refs.cpp:218` | Proiezione nello schizzo di curve procedurali riconosciute da `needsProjectionFit`; il fit 3D precede la proiezione affine dei poli. |

Ricerca realmente eseguita: `rg -n 'fitCurve\(' --glob '*.cpp' --glob '!kernel/tests/**' --glob '!kernel/occt_tests/**' --glob '!archivi-locali/**' --glob '!docs/**'`, seguita dalla lettura del contesto di ogni famiglia.

Il numero di corrispondenze testuali non coincide con i dieci siti completamente aperti. Per esempio `fk_project.cpp:511`, `fk_offset.cpp:1356/1379`, `fk_sheet.cpp:219/599`, `fk_quilt_mapping.cpp:468` e le due chiamate di `cad_sketch_offset.cpp` conservano fallback campionati in siti parzialmente trattati; le chiamate verso percorsi generali preceduti da un ramo analitico/certificato vanno considerate nel loro controllo di flusso. La definizione `fk_offset.cpp:1254` non è un chiamante.

Questi limiti richiedono certificati della provenienza/supporto e dei solve impliciti; non basta aumentare i campioni o la tolleranza. Questo replay non modifica né migra nessuno dei dieci.
