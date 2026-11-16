# Prove K10 offset dello schizzo, 16 novembre 2026

- `root/`: build app, CTest, grafo, conservazione e hash.
- `implementation/`: adapter certificato, prove del rifilo e nodi C0.
- `tests/`: regressioni pubbliche, baseline e sanitizer mirato.
- `independent/`: prova geometrica sui coefficienti originali, query avverse e audit.

Sorgenti e script hanno suffisso `.txt` per non entrare nel catalogo C++.
Per ripetere ricreare lo staging `/tmp/forgecad-k10-sketch-offset-20261116`, ripristinare le estensioni dei sorgenti/script e adattare i percorsi locali.
Binari e oggetti non sono inclusi; main, librerie e unità strumentate sono distinti nei rapporti.
Gli script privati in `tests/` documentano il confronto con gli oggetti congelati della sessione;
quella directory di build non è distribuita nell'archivio. Per riprodurre oggi, ricompilare il kernel
corrente nella build Release del repository e usare gli script in `independent/`, che ricompilano
adattatore, solver e harness: `build.py before`, `public-before <stage>`, `proof.py`,
`build.py final`, `public-final`, `build-direct.py final`, `direct`.
La baseline dell'adattatore e le fixture numeriche originali sono incluse. Il test permanente
si ripete con il target CMake `forgecad_sketch_offset_validation_tests` e CTest omonimo.
Per ripetere il confronto di 27 casi sulla baseline, ricompilare lo stesso test con
`tests/baseline/cad_sketch_offset.cpp` al posto del sorgente corrente e il relativo header;
`cad_curve_solver.cpp` e il kernel non cambiano. I comandi dei rapporti distinguono le sei
unità strumentate dalle altre unità Release; non usare il risultato come sanitizer dell'intera app.
`change.diff` confronta i file esistenti con l'inizio di questo intervento; la baseline preserva le modifiche pregresse.
I confronti campionati sono regressioni; il certificato continuo e i suoi limiti sono descritti nel rapporto principale.
