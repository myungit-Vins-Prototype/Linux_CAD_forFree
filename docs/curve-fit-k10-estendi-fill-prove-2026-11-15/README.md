# Prove K10: Estendi e Fill, 15 novembre 2026

- `root/`: build completa, gruppi kernel/CTest, grafo, conservazione e hash dei sorgenti/binari.
- `sheet/`: due regressioni/12 operazioni, confronto prima/dopo, misure e sanitizer mirato.
- `fill/`: cinque regressioni, confronto rappresentazione precedente, roundtrip B-rep e sanitizer mirato.
- `independent/`: audit, quattro proiezioni, otto offset di fori, tre riproduttori Boundary ancora difettosi e limiti continui di regolarità per due sorgenti.

Script e sorgenti delle prove hanno suffisso `.txt` per non entrare nel catalogo C++ del progetto. I comandi originali sono conservati negli script; per ripeterli ricreare lo staging `/tmp/forgecad-k10-next-20261115` o adattare i percorsi. Le prove sanitizer riutilizzano alcuni oggetti del precedente staging K10; i limiti di strumentazione sono indicati nei rapporti. Binari e oggetti non sono inclusi.

`change.diff` confronta i file esistenti con lo snapshot d'inizio intervento, senza confondere modifiche pregresse. Le fonti nuove sono nel repository e nelle copie degli agenti. Il manifest finale contiene gli hash delle prove conservate. Il report principale distingue regressioni superate e riproduttori di difetti ancora aperti.
