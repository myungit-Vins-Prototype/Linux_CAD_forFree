# CT0578 — ripartenza dei badge dal numero iniziale

Richiesta dell’11 novembre 2026: dopo la riapertura, con **Numero iniziale = 1** e **Riassegna esplicitamente i numeri** attivo, la prima quota mostra 20.

## Causa verificata

File letto senza modificarlo: `/home/myungit/Documenti/File_Esempio/CT0578_portabile_20261030/CT0578.drw`, 59 974 byte, 47 quote.
Badge disattivati, con 23 associazioni persistenti e progressivo 24. Le quote eliminate conservano 1–19, 21 e 22; due quote esistenti conservano 20 e 23.
`renumberDrawingDimensions` rimuoveva soltanto le assegnazioni delle quote ammesse e saltava ogni riserva storica.
Riproduttore C++ sui layout e metadati effettivi: numero iniziale 1, prima quota 20, 47 badge, 68 assegnazioni, progressivo 69.
Il risultato coincide con la schermata inviata. La riapertura conserva correttamente i metadati: il difetto era nella riassegnazione esplicita.

## Modifica implementata

- `cad_drawing_numbering.h`: la riassegnazione esplicita ricostruisce le assegnazioni delle quote ammesse dal numero richiesto, libera le vecchie riserve e imposta il progressivo dopo l’ultima assegnazione.
- Le quote escluse non consumano numeri; se riattivate dopo una riassegnazione ricevono il nuovo progressivo.
- Le operazioni ordinarie, inclusa l’attivazione senza riassegnazione, conservano numeri e riserve.
- `cad_drawing_editor.cpp`: tooltip e istruzioni del pannello spiegano la liberazione delle riserve. Anteprima, Annulla e singolo Undo restano disponibili; nessun formato nuovo.
- `tests/test_drawing_dimension_numbering.h`: regressioni con riserve CT0578, iniziale 1/10, badge attivi/disattivati, esclusioni, riattivazione, anteprima GUI, annullamento, conferma, codec e Undo/Redo.
- Aggiornati lo studio delle tavole, le descrizioni della mappa e la memoria condivisa; cronologia precedente conservata in `docs/history/claude-prima-badge-2026-11-11.md`.

## Verifiche eseguite

- Build Release completa di `forgecad` e del runner GUI quote riuscita. Il collegamento del desktop avvia `/home/myungit/Linux_CAD_forFree/forgecad-release/forgecad`.
- Riproduttore dopo la correzione: prima quota 1, 47 badge, 47 assegnazioni, progressivo 48.
- Decoder/encoder ufficiale sul file reale: sequenze 1–47 e 10–56, progressivi 48/57, salvataggio e riapertura coerenti; originale identico byte per byte.
- Quattro CTest ricompilati pertinenti tutti PASS: quote geometriche, GUI quote, scale e cronologia layout (13,56 s complessivi).
- Test isolato di tutta la numerazione su Wayland reale PASS: ordine per vista e vicinanza, riserve CT0578, ripartenza, filtri, anteprima, Annulla, conferma, Undo/Redo, codec e PDF.
- Acquisite e controllate anteprima e pannello, senza testo troncato. Evidenze temporanee in `/tmp/forgecad-badge-restart-qa/`.
- Mappa rigenerata: 681 file, 9 800 relazioni, 334 unità C++, zero diagnostiche. `--check` PASS; HTML e collegamenti entranti/uscenti verificati per numerazione, editor e test; chiamata alla riassegnazione risolta correttamente.
- `git diff --check` PASS. Modifiche preesistenti di altre chat conservate.

## Limiti e stato operativo

La suite GUI completa su Wayland si interrompe, dopo i test della numerazione riusciti, nel controllo distinto `Tab cicla i riferimenti sovrapposti`. Il test isolato della numerazione passa; la suite completa offscreen passa. Non è stato indagato qui il comportamento del tasto Tab né verificato su una baseline precedente.

Originale `.drw` e modello `.prt` non modificati. SHA-256 del disegno prima/dopo: `2f63b96c4ee41d51ff4695c3359819aed7ebe82bd96e9f99b4c3ee076b95c5c6`.
SHA-256 dell’app ricompilata: `926774a51d30522296f96210cf169b210db1078ec6a9b8307893d3b115ede69d`.
L’app già aperta deve essere chiusa e riaperta per caricare la correzione; quindi usare **Riassegna esplicitamente i numeri**, impostare il numero iniziale e scegliere prima quota/frecce. Nessun processo utente è stato chiuso. Nessun commit o push eseguito.
