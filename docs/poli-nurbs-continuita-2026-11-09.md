# Continuità e propagazione dei poli NURBS — 9 novembre 2026

Richiesta: opzioni selezionabili che mantengano i bordi collegati e la tangenza, muovendo anche poli non selezionati.

## Implementazione e comportamento

Due opzioni, inizialmente spente per compatibilità: **Mantieni i bordi connessi (G0)** e **Mantieni la tangenza esistente (G1)**. G1 include G0. Le facce NURBS dello stesso componente adiacente vengono incluse nella lista e nelle gabbie. I poli compensativi sono verdi nella vista e nella tabella; la selezione manuale resta separata. I valori della tabella e i trascinamenti comandano i poli del gesto corrente. I compensativi vengono ricalcolati ogni volta; Esc ripristina la fotografia completa precedente al gesto, evitando accumuli.

Il solver usa esclusivamente adiacenze e SP-curve della topologia B-rep. Le righe sui bordi isoparametrici clamped con basi razionali compatibili, anche con U/V scambiate o invertite, impongono l'uguaglianza dei poli corrispondenti. G1 conserva il rapporto delle derivate trasversali opposte dei giunti già tangenti e muove anche la prima fascia interna. I poli del gesto corrente sono vincoli rigidi; tutti gli altri, anche se modificati in precedenza, seguono minimizzando gli spostamenti rispetto alla forma attuale. Il sistema sparso riparte dalla stessa fotografia completa a ogni movimento del mouse, evitando salti e accumuli. Gli ancoraggi espliciti a spostamento zero vengono conservati. Comandi incompatibili vengono rifiutati. I pesi delle righe vincolate restano fissi; i pesi interni sono ancora modificabili.

Gli spigoli esistenti non diventano raccordi: G1 agisce sui giunti riconosciuti tangenti entro1e-5rad. Le facce analitiche restano fisse. Griglie differenti, rifilature non isoparametriche o tangenze non rappresentabili dal rapporto trasversale supportato vengono protette: il pannello ne mostra il numero; non si crea una corrispondenza per vicinanza e non si allargano le tolleranze. Su una faccia isolata non esistono giunti da vincolare.

Il controllo dell'anteprima mostra apertura massima campionata e scarto angolare G1 dei giunti inizialmente tangenti. Dopo la propagazione restano i controlli B-rep, delle SP-curve e della regolarità; G1 viene nuovamente verificata. Questi controlli non certificano globalmente auto-intersezioni o G2. Anche la griglia neutra corretta dai vincoli deve rispettare lo scostamento di approssimazione scelto; gli spostamenti intenzionali restano distinti dallo scarto iniziale.

Si salvano lo stato risolto completo e l'opzione, con schema13 delle estensioni superficie. Lo stato risolto viene controllato in anteprima, applicazione, riapertura e rigenerazione senza cache; i compensativi vengono ricalcolati a ogni nuovo gesto. Il verde evidenzia i poli che seguono nel pannello corrente. Il parametro fa parte della chiave dell'anteprima. Gli schemi precedenti ripristinano l'opzione spenta.

## Verifiche eseguite

Build completa di `forgecad`, runner grafico e suite kernel terminata. Otto suite CTest passano (12,53 s): continuità, multifaccia, griglie grandi, strumenti superficie, pannelli, kernel superficie, compressione documenti e dipendenze delle funzioni. La nuova prova del pannello passa anche nella sessione Wayland/OpenGL.

Le prove kernel coprono G0/G1, poli già modificati che seguono il nuovo gesto, ancoraggi a zero, U/V invertite e scambiate, pesi razionali, spigoli, comandi incompatibili e refit. Sulla fixture piana G0 muove un compensativo e lascia circa 0,0533 rad; G1 ne muove tre e restituisce apertura 1,39e-17 mm e scarto angolare 1,14e-17 rad, con B-rep valido. Una sonda su quattro facce all'incrocio propaga a 15 poli: apertura 1,39e-16 mm, angolo 4,54e-17 rad e nessun errore B-rep. Il test con U/V scambiate ha individuato un errore nell'indice delle derivate, corretto prima delle verifiche finali.

La prova del pannello verifica inclusione delle facce adiacenti senza perdere le modifiche delle facce prima deselezionate, colori, trascinamento ripetuto senza accumulo, Esc, alternanza G0/G1 senza salto della forma, un polo modificato in precedenza che segue il nuovo gesto, ritorno a zero stabile, conflitti, anteprima, conferma, Undo/Redo, codec13 e rifiuto di opzioni non valide. Riapertura e rigenerazione senza cache riproducono l'intera griglia risolta.

Sulla copia congelata di Hook, Cucitura8 include sette facce NURBS e 2.288.269 righe virtuali. Uno spostamento sul giunto muove un compensativo; apertura massima campionata dei cinque giunti interessati 2,02472e-8 mm, nessun errore B-rep, sorgente invariata, conferma/Undo/Redo/codec13 riusciti. Il giunto mosso è uno spigolo: questa prova verifica G0 su Hook; G1 è verificata sulle fixture piane e razionali, anche con parametri scambiati e all'incrocio. La regressione precedente su Corpo 7 isolato resta riuscita: griglia 18×162 e scarto 0,00070033 mm entro il limite vincolante di 0,001 mm, deformazione/Undo/codec/riapertura. L'originale Hook non viene salvato né modificato.

Mappa architetturale aggiornata: 668 file, 9.584 relazioni, 327 unità C++ e nessuna diagnostica. Controllo `--check`, confini di chiamata, include/riferimenti distinti, collegamenti nei due versi e sei viste HTML verificati. I file generati restano locali e ignorati da Git.

Checkpoint precedenti: `30d4e72` pubblica rotazione/Alt, recupero mouse e adattamento U/V con scarto vincolante; `b6c10df` multifaccia e `9f455a9` primo riquadro. Dettagli e prove storiche: [multifaccia](poli-nurbs-multifaccia-2026-11-09.md), [griglia](poli-nurbs-griglia-2026-11-09.md).

Implementazione pubblicata su `origin/main`: `160e185`. L’eseguibile locale è stato ricompilato; riaprire ForgeCAD per caricare la nuova versione.
