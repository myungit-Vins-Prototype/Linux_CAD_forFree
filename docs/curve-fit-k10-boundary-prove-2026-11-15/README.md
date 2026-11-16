# Prove K10 BoundarySheet, 15 novembre 2026

- `root/`: build app, gruppi kernel/CTest, grafo, conservazione e hash.
- `implementation/`: provider continuo, integrazione, misure e sanitizer mirato.
- `tests/`: regressioni permanenti e confronto con la versione precedente.
- `independent/`: audit, query avverse e prove su curve/superficie effettive.

Script e sorgenti delle prove hanno suffisso `.txt` per non entrare nel catalogo C++.
I comandi originali conservano i percorsi staging: per ripetere ricreare `/tmp/forgecad-k10-boundary-20261115` o adattarli.
Alcune prove riutilizzano il precedente staging K10: rapporti e script distinguono gli oggetti strumentati dal resto dell'archivio. Binari e oggetti non sono inclusi.
`change.diff` confronta i file esistenti con l'inizio di questo intervento, preservando le modifiche pregresse.
Il manifest verifica i file delle prove; i test positivi e i limiti della certificazione sono distinti nel rapporto principale.
