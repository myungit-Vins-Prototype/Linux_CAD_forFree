# Ribbon, loft compatta e misure di filettatura — 5 novembre 2026

## Pubblicazione preliminare

Come richiesto, le modifiche precedenti (Hook, corrispondenze loft, snap intersezione,
ripetizioni variabili e prestazioni) sono state committate e pubblicate prima di queste
modifiche: `262f067`, `origin/main`. Ricontrollati 26/26 CTest, `git diff --check` pulito.
Nessun CAD originale incluso. Le modifiche UI descritte sotto sono successive e locali.

## Comportamento implementato

- Filettatura: 62 combinazioni ISO metriche, serie grossa M1–M68 (40 misure) e 22
  passi fini comuni. Restano disponibili passo manuale, lunghezza, mano e inversione.
  Le designazioni precedenti sono conservate; l'ordine per diametro mantiene il passo
  grosso come prima proposta a parità di diametro. Gli altri standard restano invariati.
- Nel riquadro sinistro del ribbon, al posto di Compatta, una funzione aperta offre
  Conferma e Annulla. Conferma attiva il pulsante del pannello, con le stesse verifiche
  e la stessa applicazione/Undo; un errore lascia aperto il pannello. I pannelli privi
  di conferma mostrano Chiudi. Senza pannello, uno schizzo aperto offre Termina;
  fuori dalle operazioni il pulsante torna Compatta. Funziona anche in ribbon compatto.
- Chiudendo un pannello aperto durante uno schizzo, il ribbon torna su Schizzo se
  la modifica dello schizzo è ancora attiva. Verificati Estrusione e Rivoluzione,
  con conferma e annullamento; il corpo viene creato soltanto confermando.
- Loft: larghezza iniziale 440 px, minima 420 px. Il ridimensionamento manuale salvato
  resta rispettato. Sezioni e Guide hanno etichette locali, senza la colonna vuota
  imposta dalle altre etichette lunghe. Frecce ↑/↓ da 28 px accanto agli elenchi,
  con tooltip e nomi accessibili. Selezione/rimozione restano sotto gli elenchi.
  I controlli di continuità, influenza, corrispondenza e unione sono conservati;
  Automatico occupa una riga separata per consentire la larghezza ridotta.

## Fonti del catalogo

Serie grossa: [Accu, Metric Thread Dimensions](https://i.accu.co.uk/documents/marketing/Thread-Size-Sheet-V2.pdf).
Passi fini: [Walter, Technical Compendium – Threading 2024](https://cdn.walter-tools.com/files/sitecollectiondocuments/downloads/global/manuals/en-gb/technical-compendium-threading-2024-en.pdf).
L'ampliamento riguarda le misure nominali proposte nel pannello; la faccia scelta
continua a fornire il diametro geometrico effettivo, come prima. Nessuna modifica
al kernel, alle tolleranze o al formato dei documenti.

## Moduli e verifiche

Modificati `forgeCad2026_dialogs_solids.cpp`, `forgeCad2026_dialogs_surfaces.cpp`,
`forgeCad2026_dialogs_common.cpp`, `cad_loft_correspondence_editor.cpp`,
`forgeCad2026_ribbon_preview.cpp`, il bootstrap della prova ribbon e CMake.
La proprietà opzionale `functionPanelPreferredWidth` agisce solo sulla dimensione
iniziale non salvata; tutti gli altri pannelli mantengono il dimensionamento precedente.

- App `forgecad-release/forgecad` e runner `forgecad_view_tests` ricompilati completamente.
- 6/6 CTest: loft corrispondenze, pannello ripetizioni variabili, bordi filettatura,
  loft bordi, sezioni proiettate e tangenza aperta.
- Nuovo scenario `FORGECAD_CONTEXT_UI_ONLY`: PASS sia offscreen sia Wayland nativo,
  catalogo, conferma con errore/valida, annullamento senza applicazione, ribbon
  normale/compatto, comandi reali Estrusione/Rivoluzione, uscita schizzo e riordino loft.
  Finestre fissate a 1280×720 e 1024×720; pannello iniziale 440 px e nessuno
  scorrimento orizzontale anche sui controlli inferiori. Immagini in `/tmp/loft-small-*`.
- Suite completa ribbon offscreen: PASS (`/tmp/ui-ribbon-full.log`).
- Regressione pannelli Wayland (`FORGECAD_PANEL_UI_ONLY`): PASS, compresi clic
  prolungati sul ribbon, chiusura, altezza piena e confinamento sotto il ribbon.
- Per le prove ribbon locali è stato ricompilato il bootstrap separato e collegato
  agli oggetti aggiornati dell'app (`/tmp/forgecad-ui-probe`). La registrazione CTest
  `forgecad_ribbon_context_tests` usa il normale target opzionale ribbon preview.
  I test isolano anche i recuperi in una cartella temporanea; nessuna azione sui
  backup utente. L'app già aperta non è stata chiusa né sostituita in memoria.
- Mappa aggiornata: 594 file, 8253 relazioni, 285 unità AST, zero diagnostiche;
  `--check` PASS. Controllati call/include/reference in ingresso e uscita dei cinque
  moduli, rendering QtWebEngine dell'overview e dei cinque dettagli PASS.

Prove locali: `/tmp/ui-native-final.log`, `/tmp/ui-panels-native.log`,
`/tmp/ui-offscreen.log`, `/tmp/ui-ctest.log`, `/tmp/ui-architecture-render.log`.

## Schizzi nuovi vuoti

Alla chiusura della prima sessione di modifica, uno schizzo nuovo senza segmenti
o curve viene scartato dalla storyboard. Anche i punti e la costruzione sono
geometria valida; un primo clic di un segmento o una curva ancora da completare
non sono entità salvate. Il controllo si applica ai piani principali, alle facce
e ai piani di costruzione, tramite `endSketchMode`, quindi anche al ribbon.

Un ID transitorio distingue la nuova creazione dalla modifica di schizzi esistenti.
Prima dello scarto vengono pulite le cache automatiche inutilizzate e controllati
i consumatori; schizzi esistenti, riferimenti usati e vincoli sono conservati.
La rimozione rimappa riferimenti e rigenerazioni differite, conserva il cursore
della storyboard e non aggiunge un secondo passo Undo di eliminazione. La
cancellazione esplicita mantiene il proprio percorso; selezionando un altro
schizzo si conserva la sua identità dopo la chiusura di quello vuoto.

Modificati viewport header/sketch/document, test viewport e scenario ribbon.
Aggiunta regressione `forgecad_empty_new_sketch_tests`: vuoti, disegno incompleto,
punti/costruzione, disegno-cancellazione con Undo/Redo, passaggio fra schizzi,
protezione degli esistenti/utilizzatori, faccia, piano e cancellazione esplicita.
App ricompilata; regressione mirata sugli oggetti aggiornati PASS, compresi
Undo/Redo, faccia e piano. Uscita dal ribbon Wayland e suite ribbon offscreen PASS.
Runner completo ricompilato: 11/11 CTest PASS (schizzi nuovi vuoti, storia schizzi e
piani, posizione della cronologia, modifica, costruzione, uso/workflow/riferimenti
collegati e snap automatico/intersezione). Le due fixture della cronologia che
creavano uno schizzo vuoto sono ora non vuote, per continuare a verificare
riordino e cursore senza dipendere dal comportamento precedente.

Mappa rigenerata e controllata: 594 file, 8255 relazioni, 285 unità senza
diagnostica; verificati collegamenti entranti/uscenti call/include/reference,
compresi rimozione documento, visita riferimenti e consumatori. Rendering
QtWebEngine dell’overview e dei quattro moduli interessati PASS.

Log: `/tmp/empty-sketch-final-build.log`, `/tmp/empty-sketch-final-ctest.log`,
`/tmp/empty-sketch-native.log`, `/tmp/empty-sketch-ribbon.log`,
`/tmp/empty-sketch-final-architecture.log`,
`/tmp/empty-sketch-architecture-links.txt`,
`/tmp/empty-sketch-architecture-render.log`.
