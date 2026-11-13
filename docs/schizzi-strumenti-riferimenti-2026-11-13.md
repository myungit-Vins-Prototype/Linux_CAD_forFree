# Schizzi, strumenti e riferimenti — 13 novembre 2026

## Implementato

- **Arco per tre punti**: anteprima senza segmento di raggio e senza misura R; restano angolo e lunghezza. Le quote di raggio aggiunte esplicitamente alle geometrie sono conservate.
- **Cerchio per tre punti**: tre clic sulla circonferenza, anteprima dopo il secondo punto; punti coincidenti/allineati rifiutati. Il risultato e un cerchio analitico ordinario, quotabile ed estrudibile. Il centro calcolato non riceve vincoli automatici. I tre punti notevoli agganciati possono definire completamente il cerchio.
- **Polilinea** come unico comando di disegno dei segmenti. Linea e rimossa da menu/ribbon/toolbar; i vecchi percorsi nelle barre e nei gruppi personalizzati sono migrati senza duplicare Polilinea. Enum persistite stabili e alias del vecchio setter conservato.
- **Un solo aggancio** per punto: badge e conferma condividono la scelta in `cad_snap`. Origine locale prioritaria rispetto a centri/estremi sovrapposti; uno snap Intersezione conserva le due sorgenti necessarie. Le connessioni intenzionali fra segmenti locali restano anche con snap disabilitati.
- **Sorgente riconoscibile**: badge con ruolo del punto, nome della funzione/schizzo ed entita; indicazioni anche per parallelo/perpendicolare esterni. Nessuna materializzazione del riferimento durante l'hover.
- **Riferimenti rotti**: nome conservato della sorgente, numeri e tipi dei vincoli coinvolti; elenco completo se si rompono piu sorgenti. Voci ROTTO, numerazione e tooltip nel pannello Vincoli; avviso nella storyboard. Anche la faccia di appoggio eliminata indica funzione, F e ID. I vincoli sani rimangono distinguibili.
- **Formato nativo 68**: memorizza il nome delle sorgenti associative; lettura dei documenti precedenti conservata.

## Responsabilita e connessioni controllate

| Modulo | Ruolo nell'aggiornamento |
| --- | --- |
| `cad_snap.*` | Scelta univoca del punto e risoluzione dell'esatta entita nella cache transitoria. |
| `forgeCad2026_viewport_sketch.cpp`, `selection.cpp`, `interaction.cpp`, `render.cpp` | Strumenti, anteprime, badge, conferma e trascinamento; selezione e quote riusano lo stesso criterio di riferimento. |
| `cad_sketch_refs.*`, `cad_constraints.cpp` | Nomi delle sorgenti, refresh e diagnostica con i vincoli interessati; descrizione dei vincoli associativi. |
| `cad_sketch_reference_usage.cpp`, `forgeCad2026_viewport_document.cpp` | Conservazione dell'elenco completo degli errori durante pulizia cache e rigenerazione; appoggi identificabili. |
| `cad_document_io.*`, `cad_types.h` | Campo leggibile persistente e codec68; nuovo comando solo di disegno, senza nuovo tipo di curva salvata. |
| `cad_toolbar.h`, `forgeCad2026_gui.cpp`, `cad_icons.cpp`, `cad_history_graph.cpp` | Migrazione delle disposizioni, comandi, lista vincoli e avvisi della storia. |

Il grafico esistente e stato consultato prima di proseguire sulle chiamate di selezione, quote, conferma, refresh e pulizia cache. Rigenerata la vista: **688 file, 9911 relazioni, 337 unita AST, nessuna diagnostica**. `--check` PASS; verificati esplicitamente i percorsi viewport→pointAttachment/resolvePointAttachment, selection→updateSnapReferenceNote e GUI→migrateSketchToolCommands. HTML verificato con Chrome locale: ricerca, dettaglio, entrata/uscita, assenza di errori JavaScript. Il modulo toolbar ha chiamanti interni e dipendenze Qt esterne, escluse per progetto dal grafo.

## Verifiche effettivamente eseguite

- Build Release completa di **forgecad** e **forgecad_view_tests**, non solo kernel/runner.
- **24/24 CTest PASS**: nuovi strumenti e riferimenti, snap automatici/intersezioni/punti medi/quadranti, quote di costruzione, visibilita e consumo delle cache associative, editing/asole/punti, vincoli delle maniglie, codec/precisione e storyboard/storia degli schizzi.
- Nuova regressione `--sketch-tools-refs`: clic reali, cerchio analitico e volume estruso, tre punti sorgenti, rifiuto di punti allineati, Undo/Redo, origine su faccia circolare reale senza collegamento spurio al cerchio, sovrapposizioni, badge e identificazione dei soli vincoli rotti, persistenza dei nomi, appoggio eliminato e migrazione delle personalizzazioni.
- La stessa regressione con **Wayland e OpenGL reale PASS**. Sonda visuale temporanea collegata agli oggetti della medesima build: arco3 intero con A/L e senza raggio, cerchio3 senza raggio ausiliario, badge completo della sorgente. Catture locali in `/tmp/sketch-arc3-full.png`, `/tmp/sketch-circle3-full.png`, `/tmp/sketch-associative-badge.png`.
- `git diff --check` PASS. Preferenze dei test isolate in directory temporanee; nessun documento CAD originale aperto o modificato e nessun processo utente chiuso.

## Limiti e uso

Riaprire ForgeCAD per caricare il nuovo eseguibile. Le nuove assegnazioni evitano i riferimenti multipli involontari; i vincoli gia salvati non vengono eliminati automaticamente. Per sorgenti gia eliminate in vecchi file che non conservavano il nome, non e possibile recuperare un nome assente: resta identificabile l'ID persistente e vengono elencati i vincoli interessati. I vecchi eseguibili che supportano al massimo il formato67 rifiutano i nuovi file68.
