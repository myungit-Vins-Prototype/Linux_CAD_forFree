# UI9 colori, UI10 layout Sezione e quattro wrapper — 16 novembre 2026

La prosecuzione segue la CI verde di `046685e` su Linux/macOS, registrata in `a99c411`. Tre agenti GPT-6.1 Sol hanno preparato correzione, test e audit indipendente. **UI9 è una sola causa in tre contesti (Sezione, massa, separatore Raccordo); UI10 è un difetto distinto.** Le verifiche integrate sono PASS: il totale della review passa da **31 a 33 rilievi confermati corretti**. Le quattro rimozioni manutentive non sono quattro bug.

## UI9 — foreground locali indipendenti dal tema

Sezione imponeva `#9fc6e8` a un QLabel dentro un QDialog ordinario; Proprietà di massa imponeva colori HTML a etichette, nota e messaggio d'errore. La propagazione globale UI8 aggiornava correttamente palette e QSS, ma quegli override restavano fissi anche a pannello aperto.

Sezione ora usa `QPalette::Link`. Il report massa eredita `Text` e usa i ruoli `Normal`/`Error` del pannello comune: rimossi i tre override HTML, senza nuovo handler o rigenerazione del documento al cambio tema. Titolo e messaggio del report fallito condividono il ruolo Error. Il tema non richiama calcoli di massa; struttura, valori, unità, cache, clipboard, selezione e scroll sono preservati.

Lo stesso driver Qt sulla baseline dava 13 fallimenti su 64 controlli; il candidato colori 64/64 PASS su offscreen e Wayland. Audit indipendente: 104 controlli e 14 misure PASS. Contrasto misurato nel contesto verificato: Sezione 5,39/9,73, report normale 16,29/10,88, errore 7,35/6,17 nei temi chiaro/scuro. Sono prove di oggetti Qt, documento e raster effettivi; non certificano tutti i temi desktop o il renderer 3D. Nella prima fase il separatore Raccordo restava candidato non dimostrato; il completamento descritto sotto lo verifica e corregge.

## UI10 — messaggio Sezione tagliato nel pannello già aperto

Alla dimensione naturale verificata di 257×271, il cambio del combo alla scelta del piano lasciava 24 px al QLabel contro 34 px necessari. La finestra cresceva soltanto alla riapertura. Baseline e prima correzione dei colori avevano geometrie identiche: non era una regressione di UI9 né un ritaglio artificiale.

La V2 centralizza tutti i quattro percorsi dei messaggi, invalida il layout e ne calcola `totalHeightForWidth` alla larghezza corrente. Espande soltanto l'altezza mancante; riesegue il controllo dopo entrambi i percorsi di apertura, compresa la riapertura dopo messaggi cambiati da nascosto e resize utente. Nessun numero di righe o pixel prefissato, nessun reset di larghezza, posizione o altezza già maggiore, nessun resize globale.

Audit indipendente V2: 14/14 PASS; tre fallimenti della baseline colori riprodotti per dimensione naturale, descrizione lunga reale e riapertura. Altezza del testo 34/34 px nel caso naturale e 85/85 px con descrizione lunga; larghezza/posizione scelte dall'utente preservate, nessuna riduzione dopo un messaggio corto. Il riposizionamento della prima apertura da parte di Qt è osservato anche sulla baseline.

## API e contabilità

Rimossi soltanto `CadViewport::basisMatrix`, `normalizedViewAngle`, `extrusionNames` e `ForgeCad::usesSketch`: quattro wrapper senza consumatori osservati, non quattro difetti funzionali. Gli omonimi attivi di ViewportCamera e `sketchesOf`, dati, nomi ed enum persistiti restano. Sei query/cataloghi sono conservati intenzionalmente; sette ingressi legacy restano aperti per una pulizia coerente di callback/provider. Nessuna promessa di compatibilità con consumatori binari esterni; nessun nuovo test che replichi i wrapper eliminati.

L'audit preparatorio rimane storico nel [rapporto dei prossimi punti](review-prossimi-punti-2026-11-16.md). K10 resta 24/24 siti iniziali parzialmente trattati; Newton e famiglie non supportate ancora campionati. Questa correzione non chiude K10 o l'uniformazione globale della UI.

## Verifiche della produzione finale V2

| Verifica | Stato effettivo |
| --- | --- |
| App completa V2 | PASS,162,780 s; ricompilazione finale del runner con guardia appunti PASS, 130.933 s |
| Avvio isolato Wayland | PASS per 5 s, eseguibile atteso verificato; nessun CAD o processo utente toccato |
| Architettura | PASS: 778 file, 11.373 relazioni, 390 TU, zero errori; HTML/JSON e archi incidenti ricontrollati |
| 12 CTest integrati | PASS,12/12 |
| Driver finale 74 controlli Wayland | PASS,74/74 sui pannelli Sezione/massa |
| Guardia clipboard finale | PASS offscreen: sentinelle MIME12/12, eccezione dopo Copy13/13; conserva appunti e Selection se supportata. Ramo Selection non attribuito a offscreen |
| Commit/push della presente correzione | PASS,`958ed68ff8f3930d17c9d5c5f1e500c6fd20b8e5` pubblicato su main |
| Nuova CI sul nuovo commit | [43943310329](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/43943310329), commit `958ed68ff8f3930d17c9d5c5f1e500c6fd20b8e5`: build e test PASS. Linux108PASS/2SKIP; macOS112PASS/1SKIP. [Log effettivi](review-ui-colori-api-github-prove-2026-11-16/README.txt). |

Sorgenti finali: GUI SHA256 `12afcc0d8088eb99ea2475e1d9eb6ec0c6bf8a21de60c92e549a997fb1afba37`; analisi SHA256 `3e74eba725601726a5c2ec2c9fddb002bbb65c4733fb6ea3a0ac5bcec63468b4`. La precedente build V1 e i suoi test restano distinti dalla V2. Header finale test SHA25657fc820e437b1c14ffd514298c84c2639880b5bf610f1092ac7256702ee2f9da; runner3d1010e64b28bf8100cfeb7e93485a6095e3d6148528126549b673d27b40b490. I dati preesistenti degli appunti non vengono registrati: la copia di preservazione resta soltanto in memoria.

Root ha congelato 5.176 file e 19 sigilli precedenti. Mappa consultata prima del codice: 778/11.369/390 TU. Nessuna modifica a kernel, tolleranze, massa, carta, contenuto tecnico o preferenze utente. Build, avvio, CTest, Wayland e mappa sono verifiche integrate di root. Kernel e runner generale byte-identici alla precedente CI verde; compile_commands identico (811record). Non viene ripetuto il kernel invariato: sarà rieseguito nella CI nuova. Prove in [archivio locale](review-ui-colori-api-prove-2026-11-16/README.txt). La CI usa VIEW_TESTS=OFF: compila l’app, ma il nuovo test GUI specifico è verificato localmente, non attribuito a GitHub.


Limite nuova ripetizione Wayland generale delle schede: timeout90s e120s (42PNG prima della fermata); sessione Active=yes,LockedHint=yes. Lo stato bloccato suggerisce un’attesa dei frame del compositor, ma nessuno stack la dimostra. Nessuna modifica a impostazioni/sessione utente e nessun PASS attribuito a questo tentativo. Il metodo delle schede preesistente è invariato ed è PASS offscreen nelle12CTest; i74controlli nuovi dei pannelli sono PASS anche Wayland.


## Completamento UI9 — separatore Raccordo

Il candidato è stato infine confermato con FunctionDialogPanel di produzione e lo stesso QFrame HLine/Plain: il letterale#35485c resta su360pixel in Chiaro/Scuro/Chiaro. Lo sfondo cambia e il contrasto scende da8,03 a1,61 nel tema scuro. Rimosso soltanto setStyleSheet locale: WindowText ereditato segue#202020/#d8e4ea. Nove verifiche raster del componente offscreen/Fusion PASS; non è una certificazione visiva dell'intero dialogo Raccordo o di tutti gli stili desktop. Nessun algoritmo/parametro/preview/forma del raccordo modificato.

App completa aggiornata PASS15,788s; ulteriori3/3CTest pertinenti (selezione Raccordo, pannelli comuni, foreground) PASS2,418s e avvio isolato5s PASS. Le12suite e74Wayland della fasef46c036 restano prove su codice GUI/analisi/test byte-identico; dopo il separatore sono ricontrollati i3target interessati, senza ripetere il kernel immutato. Mappa778/11373/390TU,389cache+1TU: hash/HTML/archi bidirezionali PASS. Il separatore è un terzo contesto dello stesso UI9: totale33rilievi invariato, senza contarlo come ulteriore bug. Le quattro wrapper non incrementano il totale.

Il pushf46c036 aveva già avviato la CI; il successivo push sostituisce quella esecuzione tramite concurrency del workflow e certifica il codice comprensivo del separatore. Nessun fallimento di codice è stato nascosto o marcatoPASS. [Prove separatore e piano legacy](review-ui-raccordo-prove-2026-11-16/README.txt).


Registrazione finale soltanto documentale con `[skip ci]`: sorgenti, test, generatore e workflow identici al commit verificato sopra. Gli SKIP sono esclusioni della capacità del runner, non esecuzioni passate.
