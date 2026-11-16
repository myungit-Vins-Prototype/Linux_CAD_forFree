# Prove K10 appiattimento di contorni su piano, 15 novembre 2026

- `root/`: build app, gruppi kernel/CTest, grafo, conservazione e hash.
- `implementation/`: provider continuo, integrazione e misure.
- `tests/`: regressioni permanenti, confronto con la versione precedente e sanitizer mirato.
- `independent/`: audit, query avverse e prove su curve/superficie effettive.

Script e sorgenti delle prove hanno suffisso `.txt` per non entrare nel catalogo C++.
I comandi originali conservano i percorsi staging: per ripetere ricreare `/tmp/forgecad-k10-flatten-20261115` o adattarli.
Alcune prove riutilizzano il precedente staging K10: rapporti e script distinguono gli oggetti strumentati dal resto dell'archivio. Binari e oggetti non sono inclusi.
`change.diff` confronta i file esistenti con l'inizio di questo intervento, preservando le modifiche pregresse.
Il manifest verifica i file delle prove; i test positivi e i limiti della certificazione sono distinti nel rapporto principale.
