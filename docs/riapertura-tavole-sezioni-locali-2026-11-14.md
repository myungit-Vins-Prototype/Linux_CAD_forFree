# Riapertura delle tavole con sezioni locali — 14 novembre 2026

## Problema e causa

L'apertura di `Part23.drw` falliva con «Schema degli agganci degli assi non valido».
Riprodotto con il runner applicativo precedente sul documento originale e con un nuovo test automatico prima della correzione.

Il foglio contiene cinque viste, due sezioni locali, 29 quote, un asse e cinque centri selettivi. Il documento geometrico usa lo schema14 e il foglio lo schema29.
Il salvataggio degli agganci copia tutte le viste nel documento di supporto: la presenza di una sezione locale impone lo schema14 anche se l'asse o la nota appartengono a una vista diversa. Il lettore degli assi e quello delle note non ammettevano questo schema, pur essendo generato dal proprio scrittore.

## Correzione

- `cad_drawing_sheet.cpp`: schema14 ammesso per gli agganci di assi e annotazioni; controllo coerente della versione del foglio. Resta attiva la validazione geometrica e strutturale; nessuna sostituzione dei riferimenti, variazione delle tolleranze o conversione dei file originali.
- `tests/test_drawing_local_tools.cpp`: fixture con quote radiali, assi geometrici, note su curve e assi, centri e sezione locale nella stessa vista oppure in un'altra vista; conservazione integrale dopo riapertura e rifiuto atomico di schema declassato, futuro, frazionario, stringa e nullo. Include il caso precedente senza sezione locale.
- `tools/generate_architecture.py`: descrizioni aggiornate del codec e delle verifiche.

## Verifiche eseguite

- Nuovo test prima della correzione: **FAIL**, stesso errore della schermata. Dopo la correzione: **PASS**.
- Build completa dell'applicazione `forgecad`, runner `forgecad_view_tests` e target configurati: **PASS**.
- **34/34 CTest delle tavole PASS**, incluse sezioni locali, assi/note, multifoglio, preparazione, quote, revisioni, ripristino, template e PDF.
- Lettura, codifica/decodifica identica in memoria e copia dei fogli di tutti i **14 `.drw` disponibili** nelle cartelle di esempio: **PASS**, originali invariati.
- Finestra nativa Wayland su copia congelata di Part23: apertura, aggiornamento esplicito al modello salvato, esportazione PDF, salvataggio di una copia e successiva riapertura **PASS**. Cinque viste, due sezioni locali, 29 quote, un asse e cinque centri conservati.
- Architettura: **714 file / 10281 relazioni / 349 TU**, zero diagnostica; `--check` PASS, dati incorporati nell'HTML identici al JSON; collegamenti codec file → foglio → documento controllati per chiamanti e dipendenze.
- `git diff --check` PASS. Launcher desktop verificati: entrambi avviano `forgecad-release/forgecad`. Nessun processo utente chiuso; nessun commit/push richiesto.

## Rettifica successiva sul collegamento al modello

Il primo controllo segnalava un'impronta diversa e, dopo aggiornamento esplicito della copia, 15/29 quote Ready. L'indagine successiva ha dimostrato che la differenza era **solo la visibilita' di uno schizzo**; il solido non era cambiato. Le 14 quote rimanenti risentivano di nomi topologici differenti ripristinati dalla vecchia cache dello specchio.

Questo limite e' stato corretto nell'intervento successivo: **29/29 quote ripristinate**, con migrazione della cache, salvataggio e riapertura verificati. Cause, prove e nuova build sono nel [rapporto aggiornato](part23-quote-associative-cache-2026-11-14.md). Le misure e gli hash sotto documentano l'intervento precedente sul solo lettore.

## Riproduzione e integrità

Copie e log di prova: `/tmp/drawing-axis-schema-2026-11-14`.

```sh
cmake --build forgecad-release --target forgecad forgecad_view_tests forgecad_drawing_local_tools_tests -j 4
ctest --test-dir forgecad-release -R 'forgecad_.*drawing.*' --output-on-failure -j 2
python3 tools/generate_architecture.py --build-dir forgecad-release --jobs 4
python3 tools/generate_architecture.py --check
```

SHA256 degli originali, invariati dopo le verifiche:

- Part23.drw: `206eec7aefe319e69a403205f45a9e5a95fc2e9502e8cf1f66c7f9bd13b313ca`
- Part23.prt: `90577c6d772d1841581ac1b06ef9ddbbad5b7efedd06faa4bef52a1e269099f6`
- CT0578.drw: `8919196fdf43c92e0685e03704b6bb4389ab0c1b7713e276bec49ca9eecb7e05`
- CT0578.prt: `7a1767172ed2eab2fd38f7c463cdcdab602bc1af1e5ccd8579ca59dadaf7272c`

Build verificata:

- forgecad: `7b86c9f4c1270a40aae3b0d7ca701a7bf2f2904ac4955013149427f28d89fa2f`
- forgecad_view_tests: `e823ef778fb0d117b088dd1bd06c7ceca425ee001c1ea7ff12de2aa5e0caaa2b`

L'applicazione va riaperta per caricare il nuovo eseguibile.
