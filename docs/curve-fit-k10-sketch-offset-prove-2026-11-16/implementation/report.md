# K10 — offset dello schizzo, implementazione in staging

Letti AGENTS.md, CLAUDE.md e grafo descrizioni/archi bidirezionali per cad_sketch_offset, fk_offset, fk_nurbs, fk_curve_fit_bounds e fk_curve_ops. Baseline immutata in baseline/; candidato in candidate/; nessuna modifica produzione, CMake, documentazione condivisa o build condivisa.

## Modifica candidata

- OffsetPiece conserva anche la distanza con segno rispetto al parametro sorgente crescente. La normale di extrusionOffsetErrorBound con asse Z è a destra: distanza provider = -signedDistance.
- Entrambi i fit (extended per crossings e finale Free) usano fitCurveCertified per B-spline/NURBS e wrapper Trimmed originali. Famiglie non supportate restano sul fit storico; errori del provider su famiglie riconosciute propagano il rifiuto, senza fallback.
- trimOffsetLoops conserva le Bezier retained già dotate di domini sorgente originali. Dopo join, ogni cubica effettivamente emessa è validata contro l'offset originale usando quel dominio e i quattro poli finali stride 3. Include il primo polo sostituito al giunto. La mappa [k,k+1] → dominio retained è affine normalizzata e non trasforma i coefficienti.
- Nessun loop: viene restituito direttamente il fit certificato. Nessun nuovo certificato su coefficienti intermedi arrotondati; il candidato finale IEEE è quello realmente passato all'oracle. Il trasferimento a QPointF/Converted conserva double/poli/nodi; recalculateCurve aggiorna solo campioni e validità.
- Restano fitTolerance=1e-7 sia prima sia dopo trim; non occorre riserva o inflazione. Cancellazione esplicita nel percorso wrapper e in ogni query finale.

## Verifiche eseguite dall'implementatore

- -fsyntax-only con compile_commands reale: PASS senza diagnostica.
- Probe C++ isolato con candidate inclusa e libforgekernel esistente, senza modifiche alla libreria: PASS. Quattro combinazioni verso/segno e percorso extended, parabola offset interno 1 con trim reale (193 poli,64 domini retained), confronto esatto dei quattro poli validati con output reale, rifiuto forzato del bound posttrim 1e-6, fallback unsupported Other.
- Nodo C0 con angolo 90°: entrambe le finestre [0,.5]/[.5,1], sorgente originale e wrapper Trimmed, endpoint sinistro/destro coerenti: PASS. Il provider seleziona i tratti sorgente corretti senza coinvolgere il lato estraneo al dominio richiesto.
- L'agente test separato ha comunicato candidate 24/24 PASS contro baseline 3/24 PASS, con varianti nascoste polynomial/rational, catene concave, loop e refresh associativo; dettagli nei suoi file tests/.

## Limiti

Il certificato riguarda posizione rispetto all'offset parametrico originale sui tratti retained. Se la query finale non conclude viene rifiutato atomicamente il risultato. Il rilevamento degli anelli, la scelta del ramo e il controllo campionato di collasso/proiezione restano quelli storici; non è una nuova prova globale di distanza minima o topologia. Famiglie non supportate conservano il fit campionato. Compilazione completa app/runner, integrazione, mappa e memoria condivisa restano al root.
