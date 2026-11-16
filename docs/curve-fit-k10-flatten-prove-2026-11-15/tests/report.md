# K10 planar flatten: regressioni finali

## Difetto operativo riprodotto

SurfaceQuiltMap::flattenSheet su UNA faccia Plane e bordo NURBS grado27 single-span valido accettava una perdita geometrica fra campioni del fitter. Fonte input, oggetti core e grafico sono congelati prima delle build successive in before/, before.sha256 e graph-before.json. Override privati fk_quilt_mapping.o, fk_mapping_fit.o e fk_offset.o anteposti all'archivio: non viene usata la versione mutevole dell'archivio per questi tre moduli.

Probe API reale flatten_probe.cpp, build-probe.py, probe-before.log:

| Source | checkBody source/output | reported boundaryDeviation | errore stesso parametro | distanza geometrica minima verso TUTTI bordi output |
|---|---|---|---|---|
| polynomial | 0/0 | 2.4765581702771594e-10 | 5.7424751730714065e-5 | 5.7424751730650314e-5 |
| rational, pesi positivi variabili | 0/0 | 2.8941041063890748e-10 | 5.4162191958085001e-5 | 5.4162191958084804e-5 |

Tolleranza operazione1e-6, budget bordo2e-8. Source immutato binariamente, historyface1 e metriche circa1. Quindi non sono input invalidi né solo differenze di parametro. Il fit ignora lobature della curva nota pur restituendo deviazione estremamente piccola. Il reviewer ha verificato anche regolarità/monotonia X della fixture; il bordo non si auto-interseca.

## Nuovo test permanente

File test_planar_flatten_validation.cpp, SHA2566a95304ed60f2bdbc05ecc5a41e38baf2a34373af3918c84f47943241d03d211. Sei gruppi,27varianti di gruppo:

- 12successi degree27 polynomial/rational × scale(.01,1,1000) × sourceface sense(false,true). SourceFrame e outputFrame entrambi posati3D, normali/assi non canonici e traslazioni distinte. I loop del source sensefalse sono coerentemente invertiti: checkBody source0, area positiva.
- 4successi trim annidati parziali su dominio naturale[2,5], intervallo attivo[2.3,4.7], verso del bordo invertito con range negativo e fin.sensefalse, fori, sourceface orientata, codec B-rep e roundtrip geometrico.
- 1successo Line2 con intervallo finito nonunitario[2,5], coefficiente affine originale e parametro preservati.
- 1successo conica angolare Circle2 sul fallback campionato esplicitamente legacy, senza conversione angular→NURBS dichiarata same-param.
- 7scenari di errore/cancellazione:6tolleranze invalide e cancellazioneOperationCancelled, senza mutare source, stats o history sentinel.
- 2orientamenti source per gate fold:4angoli della finestra,4punti esterni rifiutati anche con carrierOnly=true e2punti entro il margine fisico di mezza risoluzione, che mantengono il clamp del comportamento precedente. Questi assert multipli sono subcontrolli delle2varianti e non vengono contati come altre varianti.

I18successi flatten richiedono successo operativo, source/output checkBody0 e area positiva. Bordo verificato nel parametro originale rispetto alla mappa affine indipendente, con errore entro il budget bordo invariato; al peggior punto viene controllata anche la distanza geometrica minima su tutti gli edge output. I controlli densi sono regressioni indipendenti, non la certificazione continua. Per curve note si verificano inoltre grado, nodi, pesi e range conservati. Trimmed3 con base BSP3 esplicita è ammesso, coerentemente col contratto; non si richiede impropriamente eliminare il wrapper che conserva l'intervallo trim. Le genealogie edge sono seguite tramite descendants, senza supporre indici uguali dopo inversione dei loop. Controllati stats stretch/area≈1, flatten/fold con quota lungo la normale orientata, immutabilità binaria source e codec B-rep dove previsto.

## Risultati

Freeze coreV3: quilt9f2cc08b051e92569dac5081aa558f61e431e332abc652f51a23096ff286b826; helperfa746a9dfbe1f722e0590a60899f2a651efffee2beb1bb5b912034d47ca6c30f; header2379037d… (hash completo nel manifest).

Normale finale **6/6 PASS**,1.26s,maxRSS6508KiB (test-fixed-final.log). Baseline vero **3/6**,1.25s, usando i3oggetti congelati prima dell'archivio corrente (test-before-final.log): i2gruppi NURBS falliscono anche la geometria, il gruppo Line2 fallisce solo il nuovo requisito di grado/nodi/coefficiente preservato e NON viene attribuito a un difetto geometrico della vecchia retta cubica. Gli altri3gruppi rimangono compatibili.

Repro finale poly e rational: errore stesso parametro0, distanza geometrica0, reported8.526979022893637e-14, source/output check0, source byte immutato, historyface1, stretch1 (probe-fixed-final.log). Il confronto geometrico iniziale non dipende dai nuovi assert rappresentativi del test.

Comandi privati riproducibili: python3 build-probe.py; ./flatten-before; python3 build-tests.py; ./tests-fixed; python3 build-tests.py --before; ./tests-before. Gli oggetti congelati sono stati copiati dalla build prima della compilazione root; le relative fonti sono incluse per ricreazione. Nessuna build condivisa o modifica produzione/CMake/documenti utente eseguita da questo subagent; root integra test/core e gestisce app/CTest/grafico.

## Cronologia dei controlli e limiti

Prima fixture sensefalse aveva loop non invertiti, quindi checkBody source falliva: corretto il solo input, non attribuito a bug kernel. Il primo assert metadata richiedeva impropriamente direttamente BSP3 nel caso Trimmed: dopo conferma del contratto controlla la base esplicita, mantenendo identici i controlli geometrici e il range. Log iniziali conservati per trasparenza. Nessun test è stato indebolito sul budget geometrico o sul successo delle varianti valide.

Specializzazione limitata alla singola Plane strutturale. Due facce e supporti curvi rimangono legacy esplicito e non sono dichiarati continuamente certificati da questi test. Le misure campionate generali della deformazione non diventano limiti globali. Il kernel mantiene la distinzione tra errore continuo di costruzione affine e gap numerico endpoint; non viene venduta una enclosure esatta della valutazione NURBS floating point. Build app e verifiche globali sono responsabilità root e non sono certificate dal solo runner privato.


## Sanitizer e manifest finale

**6/6 ASAN/UBSAN/LSAN PASS**,3.36s,maxRSS474688KiB,zero diagnostiche (sanitizer-final.log). Strumentati esattamente3TU: fk_planar_flatten.cpp, fk_quilt_mapping.cpp, test_planar_flatten_validation.cpp. Main e restante archivio Release non strumentati. ASAN_OPTIONS=detect_leaks=1:halt_on_error=1, UBSAN_OPTIONS=halt_on_error=1; esecuzione fuori dal vincolo ptrace sandbox,exit0. Fonti helper/quilt e test coincidono con freeze normale e integrazione root. build-sanitizer.py riproduce gli oggetti privati.

Conteggi strutturati e risultati in results.json; manifest final.sha256 include fonti/header/oggetti congelati, test, script, binari, log e risultati. Nessun edit del test dopo SHA6a95304e comunicato a root.
