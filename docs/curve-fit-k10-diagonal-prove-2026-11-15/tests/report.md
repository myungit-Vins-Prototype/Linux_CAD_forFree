# K10 mapping diagonale: regressioni finali

Baseline corrente congelata prima build in tests/before: fk_surface_mapping.cpp0c76d3b2…, fk_mapping_carrier.cppa98eb194…, fk_curve_fit_bounds.cppee02df4e… e header. I tre oggetti privati vengono anteposti all'archivio corrente; nessuna dipendenza dalla versione non congelata dell'archivio per queste funzioni.

Il probe baseline-before.cpp (copia dell'independent precedente) native_rotation_shift, angle.29, shift(.03,.02), anchor(.05,.1), source.6×.2, targetExtrusion degree6: accepted1, deviation4.2489214391262393e-11, errore bordo effettivo.001, carrier BSpline interamenteZ0, checkBody0. L'errore bordo si misura sulla mappa operativa nello stesso parametro del bordo originale e comprende i pullback dei parametri target nascosti .1/.10025/.1005/.10075/.101, oltre al controllo denso.

Test proposto test_mapping_diagonal_validation.cpp,7gruppi:12successi API obbligatori poly/rational×scale.01/1/1000×reverse, tutti con posa target3D(rotation+translation) e sourceFrame3D;2successi wrapper generatorTrimmed+Transformed/sourceTrimmed annidato+BSP1range2..5+fori+history+roundtrip;1successo originalLineExtrusion;2fitdirect poly/rational con pullback nodi e candidato piano errato;3fitdirect Line2Ucostante/Line2obliqua/BSP1duesegmenti con querychiuse sinistra/destra/cross-junction;1fitdirectdu1e-16 senza snap; failureatomic dominio e cancellazioneOperationCancelled, tensorBSplineunsupported e normalOffset materialmente nonzero legacy espliciti. Totale25scenari:21successi obbligatori,2fallimenti/cancellazione,2unsupported. I query endpoint/junction sono subcontrolli addizionali.

Ogni successo API verifica source/target checkBody0, optional helper presente, carrier originale stesso parametro, bordo vs mappa operativa con campioni e pullback, corpo finale valido, history dichiarata, deviation entro budget, immutabilità source/target. I controlli densi sono regressioni indipendenti e non il certificato. Il controllo carrier a stessiUV verifica la nuova conservazione integrale del supporto originale, anche oltre la regione trim: un suo fallimento legacy non è da solo prova di errore geometrico, perché il legacy riparametrizza e costruisce un carrier finito della regione mappata. Analogamente il punto target esterno al trim può verificare il requisito carrier intero, ma l'accettazione geometrica errata è provata separatamente dall'errore sul bordo operativo .001 e dal baseline pianoZ0. LineExtrusion legacy è una regressione di conservazione parametrica, non un nuovo difetto geometrico attribuito.

Prima versione:5/7logtest-fixed-v1; OriginalLine falliva per errore del controllo projectPoint su dominio naturale infinito. Corretto il solo probe usando finestra finita reale target[0,1]×[0,scale]: OriginalLinePASS1/1(test-line-v1). Resta solo rifiuto scale1000posedpoly «trim originale oltre budget», successo ancora mandatory, nessuna soglia cambiata.

Baseline suite7gruppi con vecchio helper/pubblico override e nuovo provider/bounds per rendere compilabili i directtest:4/7(test-before-v1.log). Le differenze3gruppi operativi includono nuovi requisiti di preservazione parametrica; leggere i numeri geometrici come sopra. Il probe baseline-before usa invece tutti3core vecchi congelati e dà prova operativa indipendente.

Comandi privati: python3 build-baseline.py; ./baseline-before; python3 build-tests.py; ./tests-fixed; python3 build-tests.py --before; ./tests-before. Sanitizer preparato su test+3core modificati(affine provider,bounds,carrier); mappingpubblico invariato congelato e archivio restanteRelease non strumentati. Run finale solo dopo freeze.

Controlli atomicità estesi: cancellazione OperationCancelled verificata sia dall'API pubblica sia direttamente dalla factory; normalOffset.01 restituisce unsupported senza mutare sentinel. Gruppo atomic-v1 PASS.


## Esito V2 normale e baseline

V2 **7/7 PASS**,0.72s,maxRSS6492KiB (`test-fixed-v2.log`), senza variazioni di soglia o budget. Il caso scale1000 falliva perché il fit consumava quasi tutto2e-8 mentre il bridge dei fin originali superava quello riservato al solo edge: la nuova implementazione riserva il massimo bridge di edge e tutti i fin prima di fit e recert. Lo storico test-fixed-v1.log resta conservato; il successo scale1000 è rimasto mandatory.

Baseline finale **4/7**,.90s (`test-before-final.log`), compilando vecchi helper/pubblico da snapshotbefore e anteponendoli all'archivio corrente; provider/bounds nuovi permettono di eseguire i nuovi directtest. Il probe iniziale baseline-before usa invece tutti3core vecchi. Nuovo repro pubblico finale: native_rotation_shift accepted1, deviation1.6641748144111367e-8, edge_error1.0235892836792716e-8 (baseline.001), carrier_type5Extrusion, checkBody0 (`repro-fixed-final.log`). Il campo carrier_absZ0 del probe è calcolato soltanto per BSpline e non indica planarità del carrierExtrusion finale.

Test congelato SHA256e97e04443ffe31cb69dd2838b8213bbee0f79311d547b5e58dcbe8062c6285ba. Core V2: affine4f00481ed10f996cc45d339f70283b5d2ecbcd41efc9f9d4dd2399c2bbb4cfea, boundse2f4d46b0650b4f8e17556cecfe2fb7d11a8d186cf7c04377ed9bbb1224956a0, carrier691290d76313caadc441ee5ebe452ff36686b5317c966368045ab8e3ba1ae989. Nessuna modifica produzione dal subagenttest; root integra file e plumbing.


Gate di regressione prima del freeze: i9test precedenti carrier (source403a1ac55… congelato) con V2 mostrano8/9, iso posedscale1000 fallisce per fit limite (`test-old-carrier-v2.log`). Nessun sanitizerV2 eseguito: compiler preparato soltanto. Frame sta correggendo adapter per mantenere il precedente snap isoparametrico solo quando la deviazione è continuamente limitata entro budget; il nuovo provider diagonale non deve fare snap. Ripetere7nuovi+9precedenti prima di dichiarare versione finale.


## Freeze V3 finale

Adapter corretto con preferenza per traccia iso precedente solo quando la deviazione spaziale completa è provata entro il budget esistente; provider diagonale originale e tinydu restano senza snap. Gate indipendente **nuovi7/7**,.72s,maxRSS6616KiB (`test-fixed-v3.log`), **precedenti9/9** (`test-old-carrier-v3.log`), test tutti invariati. Test finale SHAe97e04443ffe31cb69dd2838b8213bbee0f79311d547b5e58dcbe8062c6285ba. Core freeze affine4f00481ed10f996cc45d339f70283b5d2ecbcd41efc9f9d4dd2399c2bbb4cfea, boundse2f4d46b0650b4f8e17556cecfe2fb7d11a8d186cf7c04377ed9bbb1224956a0, carrier1c769c5136df3502903afea4771636c939425735a975319e1d68848d0316ab25. Repro finale V3 `repro-fixed-v3.log`; i valori geometrici del caso diagonale restano quelli V2.


## Sanitizer e consegna

Freeze finale **7/7 ASAN/UBSAN/LSAN PASS**,6.08s,maxRSS343732KiB,zero diagnostiche (`sanitizer-final.log`). Strumentati4TU esatti: test_mapping_diagonal_validation.cpp, fk_affine_surface_fit.cpp, fk_curve_fit_bounds.cpp, fk_mapping_carrier.cpp. Pubblico fk_surface_mapping.cpp invariato congelato0c76d3b2… e resto archivio Release/main non strumentati. Leak detection attiva (`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1`, `UBSAN_OPTIONS=halt_on_error=1`), esecuzione fuori vincolo ptrace sandbox,exit0. `build-sanitizer.py` riproduce gli oggetti privati; nessuna build condivisa avviata dal subagent.

Esito finale: nuovi7/7normali+7/7sanitizer,precedenti9/9normali; baseline nuovo test4/7 su vecchio helper/pubblico compilati da sorgenti snapshot,nuovi provider/bounds per compilabilità dei directtest. Geometria del repro operativo .001→1.0235892836792716e-8, deviazione dichiarata finale1.6641748144111367e-8 entro budget invariato. Nessun certificato globale di iniettività aggiunto; diagonali tensorBSpline, normaloffset materiale e altre famiglie non supportate restano esplicitamente fuori scope/legacy. Cancellazione diretta e pubblica verificata per tipoOperationCancelled,storia/deviation atomiche. Root ha integrato test e6core dopo SHA; buildapp/CTest/mappa gestite dalroot e non certificate da questo report privato.

Manifest finale `final.sha256` include fonti/test/header/oggetti,prima baseline,binari e log utili. TestSHAe97e04443ffe31cb69dd2838b8213bbee0f79311d547b5e58dcbe8062c6285ba invariato dal freeze normale fino al sanitizer. I log intermedi V1/V2 sono prove storiche dichiarate, non risultati attribuiti al freeze finale.
