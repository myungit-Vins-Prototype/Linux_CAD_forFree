# Supporto — prova originale e audit indipendente

Questo archivio contiene solo gli artefatti prodotti dall'agente di audit: certificati Fraction degli originali, report di revisione, sorgente testuale del contributo affine UV e controllo sintattico. Nessun CAD, binario o oggetto.

- `mathematical-audit.md`: formule, contratti, distinzione fra prova indipendente e contributo implementativo.
- `cap-radial-fraction.py.txt`, JSON e log: prova continua della geometria originale e dei due microtratti di bordo.
- `final-contract-audit.md/json`: verifica finale di attivazione, retry transazionale e loop chiuso sullo SHA3b8e21…; manca una copia bytewise della revisione intermedia aacb50…, quindi non si dichiara un confronto letterale con quella revisione.
- `app-path-audit.md`: indagine progressiva, nomi dei corpi, propagazione errori, causa numerica, proposte e verifiche private.
- `affine-pcurve-certificate.inc.txt`: contributo privato poi integrato dal core nel modulo finale. Non viene presentato come test indipendente di sé stesso.

Il codice originale della prova Fraction usa `/tmp/supporto-review` per input/output. Per rigiocarlo, ricreare quella directory, copiarvi il getter log `../core/geometry.log` e rinominare la copia `.py.txt` in `.py`; il log sorgente viene controllato medianteSHA256 e i coefficienti binary64 sono riportati nel JSON. Nessun accesso al CAD originale è necessario. Il file sintattico conserva i path temporanei del momento della verifica; il modulo completo finale e i relativi helper sono archiviati dal core.

Le prove runtime finali sono nel percorso coordinato `../tests/`; quelle del core in `../core/`. Questo agente non modifica il software di produzione o CLAUDE e non attribuisce al proprio audit una build dell'app.
