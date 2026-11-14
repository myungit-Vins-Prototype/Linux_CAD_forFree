# Documenti 3D e 2D nella stessa finestra — 14 novembre 2026

## Richiesta e pubblicazione preliminare

Prima della modifica dell'interfaccia, tutti gli interventi accumulati su raccordi,
appoggi, storyboard, tavole, template, sezioni locali e snap sono stati consolidati
in un solo commit: `2b180e4f5ef82b66ae31c92375efd375d6f221f2`.
Push di `main` su `origin/main` riuscito; remoto verificato allo stesso SHA.
La nuova gestione delle schede è successiva a quel commit.

## Comportamento implementato

- Un unico contenitore applicativo con una pagina per ogni modello `.prt` o tavola
  `.drw`. La pagina comprende menu, ribbon/barre, albero, pannelli e barra di stato
  del relativo ambiente. Indicatori 3D/2D, nome, modifiche e consultazione restano
  visibili; le schede si possono riordinare trascinandole.
- `Visualizza > Schede documenti > In alto / In basso`, oppure menu contestuale
  sulla barra delle schede. Posizione persistente in QSettings
  `interface/documentTabsBottom`, disponibile in entrambi gli ambienti e conservata
  anche quando si ricrea l'editor dopo un cambio foglio.
- Nuovo modello apre un'altra pagina. Apri accetta modelli e tavole, riusa la prima
  pagina vuota e attiva il documento già aperto confrontando il percorso canonico.
  La riga di comando accetta più documenti. Dal 2D sono disponibili Nuovo modello
  e Apri documento, senza dover ritornare al 3D.
- Tavole collegate, revisioni storiche, documenti recuperati e membri dei rilasci
  vengono pubblicati nello stesso contenitore. Le tavole sono indipendenti dalla
  pagina del modello: chiuderlo lascia la tavola aperta.
- X della scheda o `Ctrl+W` chiudono il documento. Esci o la chiusura della finestra
  controllano prima tutti i documenti: Annulla preserva anche le pagine per cui si
  era già risposto. Il 3D termina poi il recupero su disco in modo asincrono e
  rilascia i lock prima di eliminare la pagina; il 2D mantiene i controlli di
  calcolo, salvataggio e revisioni.

Il cambio scheda riusa l'editor e il viewport, senza ricostruire la geometria.
Scorciatoie limitate al documento attivo; Undo/Redo, unità, stato della tavola e
cache rimangono separati. Pannelli flottanti nascosti/ripristinati con la pagina;
pannelli dei comandi, WindowLock e sfondo dei menu risolvono il workspace
proprietario anziché il primo viewport della finestra. La guardia del focus 2D
non riattiva una pagina nascosta. Le preferenze della geometria della finestra
si applicano al contenitore, le disposizioni dei pannelli al relativo workspace.

## Moduli e architettura

Consultate prima delle modifiche le descrizioni e le chiamate del grafo per avvio,
PdfWindow, DrawingWindow, revisioni, pannelli, menu e ripristino del focus.

- `cad_document_tabs.*`: contenitore, apertura/attivazione, menu e chiusura comune.
- `cad_widget_context.h`: risoluzione del workspace proprietario.
- Avvio, finestra 3D/2D, pannelli e UI delle revisioni adattati alla pubblicazione
  delle pagine; formati dei CAD e calcoli geometrici invariati.
- `tests/test_document_tabs.h`: regressioni del ciclo reale di vita e delle
  scorciatoie. Qt Test richiesto soltanto dal runner opzionale delle interazioni;
  nessuna nuova dipendenza dell'eseguibile distribuito.
- Catalogo delle descrizioni aggiornato; HTML/JSON generati restano ignorati da Git.

## Verifiche

- Build completa dell'app riuscita; regressioni delle schede eseguite anche in un
  harness collegato a tutti gli oggetti dell'app: PASS offscreen e Wayland/GL.
  Verificati 3D/2D, percorso già aperto, B-rep/editor invariati, unità/Undo/Redo,
  Ctrl+S reale tramite Qt Test, cambio posizione persistente, cambio foglio,
  pannelli flottanti, pannello nel secondo viewport, Annulla su singola pagina e
  dopo Scarta di un'altra pagina, arresto asincrono e tavola dopo chiusura sorgente.
- Prova nativa Wayland con copie congelate di Part23 e CT0578: quattro pagine,
  una sola QMainWindow visibile, 35/35 e 47/47 quote Ready prima e dopo 80 cambi,
  editor e contesti GL mantenuti. 80 cambi con rendering: 2594 ms complessivi
  (circa 32 ms/cambio). Le aperture sono state effettuate durante build/analisi
  parallele e non costituiscono un benchmark comparativo dell'apertura 2D.
- Hash SHA256 delle quattro copie identici agli originali prima e dopo le prove;
  nessun salvataggio o aggiornamento degli originali. Part23.drw ora contiene 35
  quote, rispetto alle 30 dell'intervento precedente: letta la copia più recente.
- Guardia focus autonoma: CTest offscreen PASS. I test che pretendono l'attivazione
  globale sul desktop falliscono in punti diversi su Wayland/XCB anche con il solo
  header originale di HEAD, ricompilato separatamente. Questo caso nativo autonomo
  non è ricertificato; le nuove prove di schede e scorciatoie su Wayland passano.
- macOS non eseguito su questa macchina Linux.

Log e harness: `/tmp/document-tabs-2026-11-14/` (in particolare `native-final.log`,
`probe.log`, `probe-native.log`, `focus-test.log`, `focus-baseline-native.log`).
Immagini verificate: [schede in alto, 2D](../output/Part23-schede-2D.png),
[schede in basso, 3D](../output/Part23-schede-3D.png).

- App e runner finali compilati; **43/43 CTest pertinenti PASS**, 41,96 s. Inclusi
  tutti i 35 test 2D, schede documenti, unità/durata/recupero, pannelli, revisioni
  native/workspace e recupero catalogo GUI. Runner definitivo `--document-tabs
  --gl` su Wayland: PASS. Log `tests-final.log` e `runner-native.log`.
- Mappa finale: **722 file / 10384 relazioni / 352 TU**, zero diagnostica;
  `--check` PASS, JSON incorporato nell'HTML identico al file e collegamenti
  verificati in entrambi i versi fra contenitore, finestre 3D/2D, revisioni e
  contesto dei pannelli. La rigenerazione conclusiva ha riutilizzato tutte le TU
  e caricato il catalogo aggiornato. HTML/JSON restano esclusi da Git.
- `git diff --check` e controllo della mappa ripetuti prima della pubblicazione:
  PASS. Su successiva richiesta dell’utente anche la gestione delle schede viene
  consolidata in un commit dedicato e pubblicata su `origin/main`, dopo il commit
  preliminare `2b180e4`. Eseguibile aperto dall’utente non interrotto: riavviare
  ForgeCAD per usare la nuova gestione.

SHA256 eseguibili verificati prima della pubblicazione (nessuna nuova build in questo commit/push):

```text
forgecad            67e6ec57ac8b1cc0d3e15f76090f30c596532254a1a6b2d0b74e0af147ab8826
forgecad_view_tests a41599749c458144e2fe1d44757cbe045ff27ecae7c8c486dbd5073027a5dde0
```

SHA256 originali verificati invariati e identici alle copie congelate:

```text
Part23.prt  90577c6d772d1841581ac1b06ef9ddbbad5b7efedd06faa4bef52a1e269099f6
Part23.drw  005286548d455c6f0be4e8060a4a0efda837c4617f6d2774bceb9baeb4feb010
CT0578.prt  7a1767172ed2eab2fd38f7c463cdcdab602bc1af1e5ccd8579ca59dadaf7272c
CT0578.drw  8919196fdf43c92e0685e03704b6bb4389ab0c1b7713e276bec49ca9eecb7e05
```
