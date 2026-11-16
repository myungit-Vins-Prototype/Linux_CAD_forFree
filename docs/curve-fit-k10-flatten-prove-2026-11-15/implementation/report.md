# K10 flattenSheet: singola faccia Plane

Staging soltanto; integrazione, CMake, architettura e documentazione condivisa restano al parent.

## Difetto riprodotto

`flattenSheet` fittava la composizione della SP-curve anche quando la chart era geometricamente affine. Una SP-curve NURBS di grado 27 con rilievo fra i parametri di controllo veniva sostituita con un contorno quasi rettilineo. Source e risultato passavano checkBody.

- Polinomiale: errore geometrico osservato 5.742475173065e-5; dichiarato 2.47655817e-10.
- Razionale positiva: errore 5.416219195808e-5; dichiarato 2.894104106e-10.
- Tolleranza operazione 1e-6, budget contorno 2e-8.
- Riproduttore e baseline immutabili: `../tests/flatten_probe.cpp`, `../tests/probe-before.log`.

## Correzione

Solo UNA faccia con carrier nativo Plane usa la chart affine strutturale. I fattori sono le norme double originali degli assi Su/Sv, non l'assunzione norme esattamente unitarie. Appiattimento, inversione, metriche e tagli longitudinali bend condividono fattori e ancore. Il dominio finito longitudinale resta controllato anche con carrierOnly; l'inversione mantiene il clamp entro il margine fisico preesistente.

`fk_planar_flatten` conserva grado, nodi, pesi positivi e parametro delle SP-curve Line/NURBS/Trimmed. Le Line diventano BSP1 su intervallo finito senza normalizzare la velocità. La trasformazione dei poli è racchiusa con aritmetica outward sui coefficienti UV ORIGINALI, assi/origine e ancore originali. Poiché i pesi positivi sono gli stessi, il residuo continuo è combinazione convessa dei residui dei poli. Non si usa una fonte già arrotondata come fonte esatta.

Il gate finale verifica anche la SP-curve EFFETTIVA prodotta da computePCurves: stessi grado/nodi/pesi, residuo per-polo fra edge3D e Plane(SPcurve2D). La somma outward fra bound della curva e ponte finale deve restare nel medesimo budget. Il gap topologico fra estremi numericamente valutati e vertici è sommato, con commento esplicito: NON costituisce enclosure della valutazione NURBS esatta degli estremi. Riuso del bordo noto nella stessa faccia: gate continuo per-polo; basi incompatibili falliscono conservativamente.

History viene preparata localmente. Ultimo cancellationPoint prima di pubblicare history/stats. Input riconosciuti nonfiniti, fuori dominio, pesi nonpositivi o risorse inconclusive falliscono senza fallback.

## Verifica privata

`build.py` riusa compile_commands.json; due TU nuove strumentate numericamente con -fno-fast-math e -ffp-contract=off. Nessuna build condivisa.

- Compilazione senza warning, log `build.log`.
- Riproduttore polinomiale e razionale: geometria conservata, errore misurato zero, bound nonzero; log `probe-fixed.log`.
- Runner preesistente surface_mapping rilinkato con i due oggetti nuovi: PASS, log `legacy-mapping.log`.
- Agente test: nuova matrice pose, scale .01/1/1000, pesi razionali, trim annidati, parametro nonunitario, fori, senso faccia e roundtrip; gate finale e sanitizer archiviati nella directory tests dal proprietario.
- Reviewer indipendente: prove dirette originali/nonclamped/risorse/cancellazione e downstream; risultati finali nella directory independent.

## Limiti espliciti

Due facce, carrier non Plane e SP-curve di famiglie diverse da Line/NURBS conservano il percorso legacy campionato. Non è una certificazione globale di chart arbitrarie, regolarità, iniettività o assenza di autointersezioni. Coordinate estreme o basi alternative inconclusive sono rifiutate nel ramo riconosciuto. I pesi identici positivi e la relazione affine sono le premesse della prova; non si convertono coniche angolari in fonti NURBS con parametro differente.

Fonti definitive e hash: `final/manifest.json`; patch rispetto alla baseline reale: `final/changes.patch`.
