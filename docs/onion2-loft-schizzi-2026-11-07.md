# Onion2: superficie fra Schizzo 3 e Schizzo 4 — 7 novembre 2026

## Richiesta e riproduzione

L'utente vuole una superficie tra Schizzo 3 e Schizzo 4 di `Onion2.prt`. Dopo richiesta di salvataggio, acquisita una copia congelata di `/home/myungit/Documenti/Cad_Example/Onion2.prt`: 1943 byte, SHA-256 `98d08776b1d40c53d6bc924cb65c0afae819cb011d5a621fd038e17550c3c1db`. L'originale non è stato scritto dal QA; hash ancora identico alla conclusione delle prove geometriche.

Il file ha quattro schizzi e tre feature: Rivoluzione 1, Piano 1, Piano 2. Schizzo 3/4 contengono ciascuno una NURBS `Converted`, cubica a sette poli, non di costruzione. Le due curve bastano a creare un loft superficiale valido. Il calcolo geometrico diretto riusciva già, ma la normale creazione della feature era bloccata da:

```
Riferimento associativo non risolto nello schizzo Schizzo 3:
La topologia del riferimento e' cambiata: selezionare nuovamente la sorgente.
```

Causa: ciascuna sezione completa generava due meridiani. Dopo l'eliminazione di un ramo, il rimappatore conservava una sola entità ma perdeva la sua posizione nell'uscita completa. Il refresh pretendeva ancora tutte le curve e dichiarava un cambio topologico. Il loft non deve aggirare quel controllo: occorre conservare la selezione associativa.

## Correzione

- `cad_sketch_section_selection.h`: descrive gli indici sorgente dei rami mantenuti, con i conteggi completi di segmenti/curve. Informazione serializzata nel nome già persistito del riferimento di sezione (`section-subset-v1/...`), senza cambiare il formato 66. Valida conteggi, tipo, intervalli e unicità degli indici. I lettori precedenti continuano a diagnosticare la sezione parziale come non risolta; non viene persa silenziosamente la selezione.
- `cad_constraints.cpp`: prima di compattare le entità del collegamento registra gli indici conservati. Le istantanee Undo restano immutabili. Cancellazioni successive compongono la selezione; eliminando tutto si rimuove il collegamento.
- `cad_sketch_refs.cpp`: rigenera la sezione completa dalla stessa sorgente e applica il sottoinsieme esplicito prima del controllo e dell'aggiornamento. Non ricrea i rami eliminati; una vera variazione topologica resta un errore.
- Vecchi file già parziali: recupero limitato ai due meridiani di una lamina di rivoluzione a una faccia, con un solo ramo NURBS conservato, sulla medesima sorgente esplicita. Devono coincidere tipo, grado, nodi, pesi e verso, con tutti i poli uguali entro l'arrotondamento `64 * epsilon * scala`. Non si usa ricerca per prossimità, fit o aumento delle tolleranze geometriche. Nel file utente la sola differenza numerica sul secondo profilo è circa `1,47e-39 mm`; l'altro ramo dista circa 14 mm nei poli.
- `tests/test_sketch_linked_refs.h`: regressioni per entrambe le scelte di ramo, modifiche della sorgente, cache/no-cache, codec/riapertura/rebuild, snapshot precedenti, migrazione, rifiuto di una NURBS diversa anche di soli 1e-8 mm e di selettori malformati, topologia diversa, cancellazioni successive e complete di sezioni poligonali.
- `CMakeLists.txt` e `tools/generate_architecture.py`: registrazione e responsabilità del nuovo header.

La modifica non cambia la geometria della rivoluzione né i profili conservati dall'utente.

## Risultato e verifiche concluse

- Build Release completa Linux dell'app `forgecad-release/forgecad` e del runner `forgecad_view_tests` PASS.
- Fixture isolata con le nuove regressioni PASS; runner ufficiale: **13/13 CTest PASS**, senza skip. Otto test in `ctest.log`: riferimenti associativi, workflow dei riferimenti, visibilità, uso, costruzioni, corrispondenze loft, storia completa e revisioni native. Cinque test in `ctest-loft.log`: loft puntuali, loft da bordi, sezioni proiettate, tangenza su profili aperti e soppressione della storia.
- QA nativo Wayland nel pannello Loft reale: solo Schizzo 3 e Schizzo 4 selezionati, «Solo superficie» attivo, conferma riuscita. **Una faccia B-rep, superficie, zero problemi di checkBody**, area circa **31,5107 mm²**; tassellazione senza facce fallite.
- Undo/Redo della nuova feature PASS. Codec con e senza cache, riapertura e rigenerazione completa PASS; ogni schizzo conserva un solo ramo, ancora associativo e senza errori. Contesto OpenGL valido e immagini del pannello/risultato ispezionate.
- Copia autonoma `output/Onion2-superficie.prt`, identità distinta, con la nuova feature «Superficie Schizzi 3 e 4». Rivoluzione, piani e schizzi conservati ma nascosti nella copia per mostrare la superficie; nessuna geometria utente eliminata.

Sonde, log e immagini: `/tmp/onion-loft/`. Build definitiva `build-final.log` PASS; **13/13 CTest finali PASS** (`ctest-final.log`, 1,29 s). QA Wayland con gli oggetti della build definitiva PASS (`native-final.log`), inclusa la restrizione del recupero ai vecchi meridiani.

Mappa conclusiva: **632 file / 8958 relazioni / zero diagnostiche AST**, `--check` PASS. Verificati gli archi call/include in entrambi i versi fra il nuovo header, `cad_constraints.cpp` e `cad_sketch_refs.cpp`, mantenendo distinti i riferimenti a funzione. Rendering HTML della panoramica e di quattro dettagli PASS; nuovo modulo ispezionato visivamente. HTML/JSON restano ignorati da Git. `git diff --check` PASS.

## Limiti

La migrazione richiede identità univoca del ramo; forme cambiate, parametri diversi, corrispondenze ambigue o topologia diversa restano da riselezionare. Non è una riassociazione generale di riferimenti persi. Questo intervento verifica Linux/Wayland; macOS e CI non eseguiti. Per caricare la build aggiornata occorre riaprire ForgeCAD; nessun processo utente è stato chiuso.
