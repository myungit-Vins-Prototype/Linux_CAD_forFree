# Test K10 staged — API certificata

Sorgente: test_curve_fit_validation.cpp. Solo staging, nessun CMake/build globale/produzione modificato.

Build privato: python3 /tmp/forgecad-k10-20261115/tests/build-tests.py
Run: timeout 90s /tmp/forgecad-k10-20261115/tests/tests
Esito: **7/7 gruppi PASS**, tests.log; build-tests.log pulito.

- Degree27OffsetPoseScaleRational:12 varianti (3scale×2pesi×2pose), ciascuna SUCCESSO obbligatorio fitCurveCertified+offsetSurface e confronto10001punti nello stesso parametro con errore<=tolerance. Nessun catch sui validi.
- NestedTrims: due TrimmedCurve annidate, razionale reale, fit e offset con successo e bound.
- RevolutionMeridian: meridiano polinomiale/razionale reale degree27, spostamento radiale via TransformedCurve e trim; factoryproduction e offsetSurface Revolution con SUCCESSO, errore<=1e-7.
- InvalidAndAtomic: tol0/neg/NaN/Inf e rangeinvalidi rifiutati prima callback; output deviation sentinella; NaN/Inf nelcallback ed eccezionecallback senza outputmodificato.
- UnresolvedAndCancellation: oracleInf/NaN/negative non porta a successo anche al limite depth/span; deviationimmutata; cancel iniziale/primo callback/dentrooracle, OperationCancelled conservata.
- ConicParameterNotAssumedPolynomial: factorynoncertifica Circle angolare usando conversione rationalconic con parametro diverso.

Manifest byteesatto sorgenti/oggetti usati: test-manifest.json. Confronti densi sono controlli indipendenti aggiuntivi; garanzia sul continuo viene dal provider geometrico di produzione, non dai campioni. Test fakeoracle0 usato esclusivamente per il controllo di flusso su retta, non per certificare geometriaadversa. Non aggiunte assunzioni su callbackgenerico legacy sampled.

Copertura rinviata all'integrazione parent: app, CTestglobali, shell/blend/map/project/exchange; i24callers sono classificati in report.md. Non tutti sono migrati alprovider, nessuna dichiarazionecontraria dai test.

Freeze finale: test riformattato4spazi, fixture documentata, singoli statement separati; nuovo CurveFitLegacyInvalidInputAndAtomic copre tol invalide, callbacknonfinite e breaksNaN/±Inf suentrambiAPI con sentinella e0callback. Corefinitebreak aggiornato ricompilato dall'agente kernel: primo link concorrente era obsoleto6/7, relinkfinale7/7PASS. tests.log e manifest sono quelli finali.

Ultimo freeze11/11PASS: aggiunti3gruppi ereditati distinti da test_curve_fit_bounds (pesi1e±100 su estrusione/rivoluzione, trim/transform; multispanunclamped; providerempty/outofdomain/cancelfactory) e seamNURBSchiusa intervallo[2pi-.2,2pi+.2]. Ultima prova con offsetperiodico SHA4563435968a70fdcb311ca0ecc041e8057462cd36ef107c412f2fe98cfe069b6. Copiato SOLO test_curve_fit_validation.cpp in produzione su ownership/istruzione parent; nessun CMake/doc/sharedbuild modificato.
