# Prove del blocco anteprime e pannelli — 16 novembre 2026

- `core/`: sorgenti prima/dopo, delta e probe privato del coordinatore, setup differito e chiusura sincrona.
- `tests/`: fase privata del probe con 115 asserzioni, controlli negativi, log e manifesto storico degli agenti.
- `orphans/`: ricontrollo statico delle 22 candidate, ricerca lessicale, contesto del grafo e hash.
- `root/`: congelamento iniziale, sorgenti integrate, build completa, 24 CTest, replay sui soli oggetti dell'app, diagnostica nativa, audit della mappa e conservazione del tree.

Il manifesto di `tests/` si riferisce alla fase privata e dichiara il replay Release allora pendente. L'esito finale effettivo è `root/replay-final-result.json`; il collegamento è `root/link-app-release-final.json`, senza oggetti di produzione sovrapposti. I file `initial-replay-*` conservano il primo esito Wayland, seguito dalla diagnosi e correzione del fixture senza modificare le asserzioni o la produzione.

Sorgenti e script sono archiviati con suffisso `.txt`, per non introdurre moduli nel catalogo del software. Non sono distribuiti binari, oggetti o copie dei CAD originali. I comandi contengono percorsi dell'host di verifica; adattarli prima della riproduzione altrove. `SHA256SUMS` usa percorsi relativi a questa directory.
