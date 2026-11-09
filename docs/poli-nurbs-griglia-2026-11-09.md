# Griglia e controlli dei poli NURBS — 9 novembre 2026

## Richiesta e riproduzione

Ripristinare la griglia di **Modifica poli NURBS**; usare `Hook.prt`, **Corpo 7**, come esempio. Precisazione successiva: scegliere il numero di poli con spinner e vedere lo scostamento dovuto alla griglia più o meno fine.

Input: `/home/myungit/Documenti/Cad_Example/Hook.prt`, copia congelata `/tmp/forgecad-poles-Hook-input.prt`. SHA256, riconfermato sull'originale e sulla copia:

`13883a7873687759cc18c108ba3d0786117d4d07f848a58961284cf086c96f64`

Corpo 7 corrisponde alla feature **Copia facce 16**, indice 15: una faccia NURBS, gradi U/V 12/5, **193 × 1483 = 286219 poli**. Il vecchio pannello rifiutava griglie oltre 4096 poli complessivi. Controllo negativo con il precedente oggetto del pannello, collegato alla stessa sonda: **tabella=0, gabbia=0**; nuova versione: **286219 righe, 3818 poli di display**.

## Comportamento implementato

- Tabella virtuale: tutti i poli sono accessibili, conservando in memoria soltanto le celle modificate. Le griglie originali grandi hanno una gabbia di display campionata; un polo selezionato in tabella viene sempre aggiunto con i suoi indici originali. Nessuna modifica geometrica è implicita in questo campionamento.
- L'opzione **Semplifica la griglia (approssima la superficie)** abilita spinner e cursori U/V, da 4 a 64 poli per direzione. Le variazioni ricostruiscono automaticamente la griglia dopo 180 ms di inattività. Il pulsante di aggiornamento resta disponibile. Cambiare U/V o disattivare la semplificazione azzera gli spostamenti già inseriti, come indicato nel pannello.
- La ricostruzione produce una vera superficie bicubica ai minimi quadrati, con il numero richiesto di controlli. Conserva il dominio UV e i quattro angoli. La distribuzione dei nodi usa le differenze quarte delle sezioni come criterio del fit; non è un certificato geometrico.
- Si mantengono topologia e rifilature UV, inclusi i fori; curve 3D e vertici vengono ricostruiti sul nuovo supporto. La sorgente immutabile resta disponibile nella storia. Si possono confermare anche la sola ricostruzione o una ricostruzione con spostamenti/pesi.
- Il pannello indica lo **scostamento campionato iniziale** e il limite ammesso scelto dall'utente, distinto dalla tolleranza di costruzione del B-rep. Se il limite non è rispettato, la conferma è disabilitata. La costruzione ripete il controllo, anche fuori dal pannello.
- Conteggi e limite sono persistiti nel blocco superfici, schema 10 opzionale (`refit`); i documenti precedenti conservano la griglia originale. Chiavi di anteprima, Undo/Redo e riapertura tengono conto dei nuovi parametri. Record fuori intervallo o su funzioni incompatibili sono rifiutati.

## Risultato sul corpo 7

**8 × 48 = 384 poli**, scostamento campionato iniziale **0,0848055 mm**. Ricostruzione pura e spostamento di prova di un polo: una faccia, nessuna issue di `checkBody` con geometria e incroci dei loop. Prova finale Wayland con il runner compilato: aggiornamento automatico degli spinner, gabbia, limite troppo stretto, modifica, conferma, Undo/Redo, codec e riapertura del pannello PASS.

Originale invariato; nessun processo utente chiuso. App completa e runner ricompilati. Per usare la nuova versione occorre riaprire l'applicazione.

## Verifiche finali

**6/6 CTest PASS** sui sorgenti finali:

- `forgecad_surface_poles_large_tests`: fixture autonoma 193 × 1483 con foro; tabella/gabbia, selezione fuori campionamento, indici, trascinamento/Esc, controllo U/V automatico, applicazione, Undo/Redo, codec, riapertura e rigenerazione senza cache.
- `forgecad_surface_tools_tests`: poli/pesi, selezioni multiple, assi globali/locali, anteprima al rilascio, estensione/rete, storia, revisioni e sola lettura.
- `forgecad_function_panels_tests`.
- `forgekernel_surface_tools_tests`, incluso `SurfacePolesRefitControlCounts`: conteggi, piano esatto, trim/foro, ricostruzione e modifica, controlli negativi; superficie razionale e scarto su una griglia indipendente.
- `forgecad_document_compression_tests`.
- `forgecad_feature_dependencies_tests`.

Ulteriori prove: modello virtuale con locale italiano, pesi zero/NaN e limite di 4096 modifiche; sonda applicativa sul documento congelato, sia offscreen sia Wayland. Log in `/tmp/forgecad-poles-refit/`; immagini del pannello e della gabbia in `/tmp/forgecad-poles-large-qa/`.

Una prima sonda della griglia grande premeva OK prima della validazione e raggiungeva il timeout: corretta per aspettare il pulsante abilitato; esclusa dagli esiti positivi. La prima build della ricostruzione ha richiesto la correzione della firma di estensione; la build completa e le regressioni finali includono la correzione.

Architettura: **658 file, 9411 relazioni, 322 unità AST, zero diagnostiche**. `--check` PASS; cinque moduli controllati in entrata/uscita e tre confini di chiamata verificati. Overview e tre dettagli HTML renderizzati e ispezionati; righe confrontate con i sorgenti finali. HTML/JSON restano generati e ignorati da Git.

## Limiti

Lo scostamento è misurato fra punti con gli stessi parametri UV, anche ai nodi e nei loro intervalli, sull'intero supporto. È un controllo campionato, non un massimo globale certificato. Gli spostamenti successivi dei poli sono modifiche intenzionali: non rientrano nello scostamento iniziale dell'approssimazione. Regolarità campionata e B-rep valido non certificano l'assenza globale di auto-intersezioni o la continuità con facce adiacenti.

Non è stata rigenerata integralmente senza cache tutta la storia di Hook: la nuova funzione è verificata sulla sorgente congelata; il replay senza cache è verificato sulla fixture autonoma. Nessun cambio dei default globali di tolleranza, nessuna approssimazione su mesh, nessun commit/push.

## Moduli

`cad_surface_poles_model.*`, `kernel/fk_surface_refit.*`, pannello `forgeCad2026_dialogs_surface_tools.cpp`, `cad_surface_tools.cpp`, `cad_types.h`, `forgeCad2026_feature_preview.cpp`, API e rendering della gabbia nel viewport, CMake, test delle superfici, README italiano/inglese e catalogo dell'architettura.
