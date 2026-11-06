# Surface219: apertura e nuova tavola 2D — 6 novembre 2026

## Correzione completata e verificata su Linux

Richiesta: nuova tavola non aperta dopo dieci minuti; misurare e ridurre anche i minuti di apertura del modello.
Copia congelata `/tmp/surface219-drawing-20261106/input.prt`, SHA256
`2e09473fa5f7a6a4a438b5fcd75e188deee693f32ef36e4d827c3d1396ae4976`.
Originale non modificato, processi utente non interrotti.

La lettura standard impiega 507 ms e restituisce 40 feature, zero B-rep: la cache supera il budget.
`PdfWindow::newDrawing` passava soltanto il percorso; `createFromModel` rileggeva e
`prepareDrawingEditorModel` ricostruiva tutte le feature mancanti prima di mostrare la finestra.
Anche piani/curve gia' valutati, normalmente privi di B-rep, venivano ricalcolati.

Implementato: passaggio dello stato valutato dal viewport, verifica dell'impronta del file salvato
senza espansione della cache, riuso degli stessi B-rep e preparazione con progresso/annullamento.
L'apertura espande la cache una sola volta, dopo la lettura dei metadati per l'accesso.
Il percorso che riceve soltanto un file rimane compatibile e ricostruisce la geometria mancante.

## Misure e seconda causa individuata

Ricalcolo senza cache su copia della sola definizione (timeout diagnostico 180 s):
Svuota 1 68.836 ms, Offset 33 7.376 ms, Taglio 32 6.551 ms. Loft 11/13/15:
117/1.501/2.220 ms. Loft 36 avviata intorno a 97 s e ancora in calcolo al timeout.
Non è una misura del tempo totale di ricostruzione: la coda è stata interrotta
solo nel processo QA. La sessione CAD dell'utente non è stata interrotta.

Prima prova nativa con budget adeguato: apertura 32.744 ms ma due errori:
Curva tagliata 3D 9 risolveva entrambi i riferimenti sullo stesso bordo, e
Loft 36 perdeva il riferimento nello Schizzo 15. Le ripetizioni sostituivano
operandi prima di registrare la genealogia dei B-rep salvati a valle.
La registrazione completa della genealogia dello snapshot prima del caricamento
runtime conserva le origini dichiarate: prova Wayland successiva 29.305 ms,
27 B-rep e zero errori nelle 40 feature; nuova tavola 524 ms dopo una preparazione diagnostica. I B-rep del 3D
restano condivisi e immutati; modello originale e copia di prova non salvati.

Picco RSS della prova nativa: 9.528.700 KiB. Per lasciare spazio a decodifica,
strutture B-rep e genealogia, il budget interattivo è limitato a **un ottavo**
della memoria disponibile, fino a 4 GiB. Il budget limita i byte della cache,
non l'intera RAM dell'app. Il codec generico conserva il precedente limite
fisso di 1 GiB; le preferenze possono ridurre il budget interattivo.
La preferenza e' in **Opzioni → Memoria per la cache all'apertura**. La scelta esplicita
puo' abbassare il limite, ma resta soggetta alla memoria disponibile.

## Verifica finale

- Compilazione completa Release: app e runner aggiornati, esito positivo.
- 18 CTest pertinenti: tutti superati in 12,33 s. Coprono lettura/budget, tavole e revisioni,
  condivisione B-rep, rifiuto di snapshot senza provenienza o con file cambiato, riferimenti
  genealogici dopo ripetizioni, storia, curve estratte e persistenza.
- Prova nativa Wayland con policy definitiva automatica: memoria disponibile iniziale
  24.982.462.464 byte, budget 3.122.807.808 byte; apertura **28.361 ms**, 40 feature,
  27 B-rep, **zero errori**. Nuova tavola **1.261 ms**, questa volta chiamata prima della
  preparazione diagnostica, senza riscaldamento esplicito. Foglio vuoto pronto e visibile.
- Stessi puntatori dei B-rep prima/dopo la tavola; impronta della copia congelata invariata.
  Picco RSS 9.553.720 KiB. Screenshot della tavola e del modello verificati visivamente.
- Mappa architetturale rigenerata: 624 file, 8.830 relazioni, 303 unita' analizzate,
  zero diagnostiche. `--check` superato; chiamate interessate controllate nei due versi;
  sei dettagli HTML renderizzati in XCB, call/include/reference distinti.
- README italiano/inglese e registro condiviso aggiornati; `git diff --check` pulito.

Una prima ripetizione della prova finale, con circa 12,35 GB disponibili e budget 1,54 GB,
ha scartato ancora la cache. E' stato fermato soltanto quel processo QA. La prova successiva,
con piu' memoria libera, e' quella misurata sopra; nessun processo utente e' stato chiuso.
Questo limite resta intenzionale: non si certificano gli stessi tempi con RAM insufficiente,
cache assente/invalida o preferenza manuale troppo bassa. Non e' stato allentato per far
passare il benchmark. La cache serializzata non misura tutta la memoria del modello.

Lo svuotamento e la loft complessa restano costosi se effettivamente modificati: queste
correzioni eliminano ricostruzioni superflue all'apertura e nella nuova tavola, senza cambiare
algoritmi geometrici o tolleranze. Il tempo della rigenerazione integrale non e' stato misurato
fino al completamento. La prova rapida della tavola riguarda il foglio iniziale, non il calcolo
di tutte le viste proiettate di questo modello. macOS non ricertificato in questa sessione.

Log e riproduttori locali: `/tmp/surface219-drawing-20261106/`, in particolare
`build-final.log`, `tests-final.log`, `native-final.log`, `architecture-final.log`,
`architecture-edges.json`, `architecture-render/` e gli screenshot `tavola-pronta.png`
e `modello-aperto.png`.
