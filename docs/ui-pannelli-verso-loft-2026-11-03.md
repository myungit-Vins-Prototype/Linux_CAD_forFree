# Pannelli, clic ribbon e verso G1/G2 — 3 novembre 2026

Corrette le richieste: pannelli sotto il ribbon anche a tutta altezza, deselezione dopo
OK/Annulla, rimozione dei riferimenti di un piano per tre punti gia definito, inversione
della normale ai profili della loft. Corretto anche il successivo problema dei clic
ripetuti, riprodotto e risolto prima della pubblicazione.

## Comportamento e cause

Ribbon e aggancio calcolavano due aree incompatibili: il primo spostava il pannello sotto
se stesso e il secondo lo riportava all'origine del viewport. Ora FunctionDialogPanel
riceve l'inset superiore dal ribbon; apertura, aggancio, altezza piena, trascinamento,
resize e ripristino delle dimensioni usano un unico rettangolo. Le dimensioni minime
si riducono temporaneamente al viewport senza sovrascrivere quelle preferite.

Il riconoscimento del pannello avviene per tipo C++, anche con objectName specifico.
Vincoli resta un pannello persistente. Hide/finished puliscono subito l'evidenziazione
del comando; una generazione protegge il comando nuovo dalla distruzione del vecchio pannello.

Il reset periodico introdotto durante questo intervento ricreava il pulsante dopo50ms
anche fra mousePress e mouseRelease: il rilascio perdeva il destinatario e servivano piu
clic. Riprodotto con una pressione120ms sulla vecchia versione. Eliminato il reset dal
timer; ogni rebuild attende il rilascio se un pulsante e premuto. Le nuove prove inviano
pressione/rilascio reali con durate0/120/350ms e forzano un rebuild durante la pressione.
Ogni singolo clic deve aprire il pannello reale Blocco, poi Annulla pulisce il comando.

Il piano aveva solo comandi di riselezione: ogni riferimento ora dispone di **× / Rimuovi**.
La rimozione cancella il picker attivo, il riferimento e l'anteprima fino al completamento.
Annulla conserva il piano originale. La prova analitica controlla il baricentro dei tre
punti, come previsto dal datum, e non assume che l'origine coincida con il primo punto.

SurfaceEndControls aggiunge **Inverti normale** indipendente per primo e ultimo profilo
G1/G2, anche per la superficie tra due profili. G0 e tangenza alle facce conservano le
loro condizioni. I flag entrano nella firma dell'anteprima, negli adattatori e in
LoftOptions dopo l'orientamento automatico. I profili esatti restano invariati.
Formato nativo61; per i file precedenti i flag valgono false. Il fallback della firma60
mantiene compatibili le tavole gia salvate; il downgrade non puo perdere il nuovo verso.
Le versioni precedenti dell'app non leggono il nuovo formato61.

## Verifiche effettive

- App completa `forgecad-release/forgecad` e runner applicativo compilati in Release.
- CTest pertinenti **15/15 PASS**: pannelli, lettura/compressione/unita, cache di storia,
  revisioni native, tavole collegate/revisioni/rilascio, datum, loft da bordi e tangenza,
  superfici, guide proiettate e forma della loft.
- Kernel: gruppo Loft **42/42 PASS**; forma loft **10/10 gruppi PASS**, comprese otto
  combinazioni G1/G2 e dei due versi, profili invariati e G0 senza effetto dei flag.
- Ribbon completo offscreen PASS, con clic reali e pannelli specifici.
- Wayland e XCB: clic0/120/350ms, rebuild concorrente, quattro nomi di pannello, OK/Annulla,
  riapertura/Hide, densita, aggancio libero/sinistra/destra, altezza piena, resize,
  trascinamento oltre il bordo e ribbon nascosto/mostrato PASS con contesto OpenGL reale.
- Wayland e XCB: pannelli reali piano/loft, rimozione e riselezione dei punti, cambio tipo,
  OK incompleto, Annulla, Undo/Redo, riapertura, firme tavola60 e stato dei pulsanti PASS.
  Conferma/Annulla del piano visibili; anteprima della loft invertita completata e valida.
- Mappa: **552file/7642relazioni/261AST**, zero diagnostiche; otto confini risolti e sedici
  moduli verificati in entrata/uscita per call/include/reference separatamente. Check e
  immagini HTML PASS; i due file generati restano ignorati da Git.
- `git diff --check` PASS. Nessun CAD originale aperto o modificato: fixture sintetiche.

Il runner ribbon delle prove e temporaneo: bootstrap originale, oggetti della app completa
e modulo ribbon ricompilato, con preferenze isolate. Non sono stati attribuiti esiti al
vecchio binario CMake separato del ribbon. Per la mappa sono state riusate soltanto cache
AST con chiusura degli include invariata, documentate nell'audit; le TU dipendenti dalle
fixture corrette e il ribbon modificato sono stati rianalizzati.

Due tentativi nativi piu ampi si fermavano prima dei nuovi casi, rispettivamente nella
cattura del fondale dei popup e nel vecchio controllo diagnostico H/V dei pannelli.
I loro log sono conservati; gli esiti nativi dichiarati sopra sono quelli delle prove
mirate. La suite completa nativa non e dichiarata verde. macOS e CI del nuovo commit
non verificati in questa sessione.

[Archivio delle prove](history/ui-pannelli-verso-loft-2026-11-03-verifica.zip): log, sonde,
metadati, audit e catture Wayland/XCB; esclusi binari, oggetti e documenti utente.

## Memoria storica conservata

La voce Memoria compatta del 2 novembre e consolidata nella sintesi e in questo rapporto:
registro precedente archiviato identico a HEAD, 2.279.833 byte; riduzione oltre99%,
link/struttura/diff verificati allora. Solo documentazione, senza build funzionale.
