# Tavole 2D: centri selettivi, quote raggio e sezione locale — 14 novembre 2026

## Richiesta e comportamento

Le immagini dell’utente mostrano centri automatici sovrapposti, una quota raggio bloccata durante il primo posizionamento e un esempio di sezione locale a sinistra. L’utente ha confermato esplicitamente **sezione locale con contorno e profondità**, distinta dalla mezza sezione.

- **Centri/assi:** selezionare i bordi circolari o ellittici e usare «Centri su bordi selezionati» oppure «Assi su bordi selezionati». Senza preselezione, il comando permette di cliccare i bordi. Tab alterna i bordi sovrapposti; Esc termina; un altro clic rimuove il centro applicato. «Rimuovi centri della vista» elimina anche le vecchie opzioni automatiche. Applicare un centro esplicito disattiva i centri globali della relativa vista. Le tavole legacy restano leggibili.
- **Raggio:** dopo il clic sul bordo, il testo segue subito il cursore; il successivo clic conferma senza salto. Se il centro geometrico è esterno all’ingombro della vista, il richiamo resta corto, fra bordo e testo. Restano disponibili orientamento del testo e inversione delle frecce esistenti.
- **Sezione locale:** selezionare la vista, usare **Inserisci → Sezione locale**, tracciare i vertici del contorno e impostare quota del piano iniziale e profondità verso l’interno. Applica, Invio o clic sul primo vertice confermano; Backspace rimuove un vertice; Esc annulla. Riaprire lo stesso comando per modificare profondità o ridisegnare il contorno. «Rimuovi sezione locale» ripristina la proiezione completa.

Il piano iniziale suggerito usa le quote dei vertici B-rep reali. I box conservativi delle curve circolari possono essere molto più grandi del pezzo e non sono un buon riferimento di partenza: nel riproduttore del foro cieco proponevano Z22 invece della superficie Z16. Per corpi senza profondità fra i vertici resta un suggerimento conservativo modificabile. Il riferimento memorizzato è una quota esplicita nel frame della vista; verificare la superficie di partenza desiderata.

## Implementazione

`cad_drawing_center_selection.h` conserva un aggancio genealogico per ciascun bordo scelto e risolve centri/assi senza riassociazione per vicinanza. I centri concentrici non producono croci duplicate. Identità, rimappatura del modello, copie dei fogli e migrazione dei riferimenti comprendono i nuovi centri.

La causa del raggio bloccato era `quotePosition`: forzava a ogni movimento il testo al centro del segmento radiale. La nuova posizione segue il cursore dal primo clic. Il ramo di disegno sopprime il lungo segmento fino al centro remoto, conservando valore esatto, riferimento sul bordo e freccia coerente in anteprima e alla conferma.

`cad_drawing_local_section*` separa definizione, validazione, preparazione e interazione. Il piano del taglio è `front - depth` lungo Z della vista. Il kernel calcola le intersezioni e la proiezione trattenuta sul B-rep; la rappresentazione compone la proiezione ordinaria fuori dal contorno con sezione e tratteggio al suo interno. Il contorno e la linea di strappo sono grafici e non quotabili. Le curve conservano la provenienza topologica e le quote delle sezioni usano il piano effettivo del taglio, anche in presenza di più rami della stessa faccia. La fusione delle due proiezioni richiede la stessa provenienza esplicita e mantiene gli agganci originali.

Comprende PDF, Undo/Redo, ripristino, riapertura, ombreggiatura di presentazione, clipping dei dettagli e delle viste interrotte. Le opzioni incompatibili vengono rifiutate senza mutare la tavola. Il formato del foglio passa a **29** solo per centri espliciti o sezioni locali; il documento geometrico passa a **14** solo per sezioni locali. Le tavole senza questi dati mantengono gli schemi precedenti.

Modificati editor, preparazione, codec, contenitore multifoglio, menu e CMake; aggiunti i moduli mirati e `tests/test_drawing_local_tools.cpp`. Conservate le modifiche precedenti su Part23/raccordi/storyboard.

## Verifiche eseguite

- **22/22 CTest 2D**, 33,98 s: geometria di base, dimensioni, visibilità, preparazione, scale, editor, viste proiettate, modifica viste, storia layout, quote GUI, linee nascoste, pagina, derivate, dettaglio, ausiliarie, interruzioni, presentazione, sezioni complete, centri, assi e controlli UI.
- **4/4 CTest con runner completo aggiornato**, 2,33 s: collegamenti delle tavole, revisioni, file proiettati e multifoglio.
- Nuovo test: preselezione e mouse sui soli bordi scelti, persistenza e lettura invalida atomica, Undo/Redo, raggio mobile prima del piazzamento, conferma senza salto, **R207 senza segmento al centro remoto**, due rami di una faccia cilindrica sul piano locale Z3, foro cieco sopra e sotto il fondo, lato esterno al contorno conservato, contorno non quotabile, rifiuto di contorni auto-intersecanti e profondità nulla, pannello reale/Applica/Esc, passaggio fra centri/numerazione/contorno senza intercettare i clic del comando successivo, attivazione del tratteggio locale e Undo, riapertura, rotazione, scala, PDF e sola lettura.
- Dopo le ultime rifiniture dei comandi e del tratteggio: nuovo test e suite quote GUI rieseguiti, **2/2 PASS**, 12,31 s; ricompilati applicazione e runner completi.
- Nuovo test eseguito anche su **Wayland**, PASS con catture e PDF. Test legacy aggiornati solo dove il comportamento richiesto è cambiato: azioni dei centri selettive e prima posizione libera del raggio; mantenute le verifiche delle opzioni globali legacy tramite API.
- **Part23, copia congelata:** B-rep della feature Raccordo23, proiezione 510 curve; sezione locale sinistra a Z30, 575 curve. Un centro scelto a destra e quota **R207** esatta. Proiezione 1,3–1,6 s; sezione 7,5–8,9 s con altre compilazioni concorrenti: misura locale, nessuna promessa generale di prestazioni. Wayland, riapertura, codec, PDF e copia multifoglio dei centri PASS; rifiutata un’identità di centro duplicata tra fogli. PDF di una pagina A4, valore R207 leggibile dall’estrazione testo. Nessun CAD originale modificato.
- **Applicazione e runner completi compilati**. SHA256 app `46df1a6f8550e527eb83322cf043278c9b0e40c0021a3709d92c0a718bc95a58`; runner `93d10486bb37b1947f8b8544bf44a33ff5db354502dec26f9806e9ee8c2e427f`. Riavviare l’app per usare questi binari.
- **Architettura:** catalogo aggiornato, 706 file/10202 relazioni/344 TU, zero TU con diagnostica; `--check` PASS, HTML e JSON identici, chiamanti/dipendenze verificati nei due versi. File generati ignorati da Git. `git diff --check` PASS.

Copia congelata Part23: SHA256 `61c1468f07ef0ea87db8f8527027a38a196705464a276ef5eca0145566dd4e7c`; B-rep `4df233d2a1ccf3cf523c25fb027a38636fc880c4a6589193d53d7f69c4023925`.

## Ambito e limiti

Un contorno poligonale semplice per vista proiettata, da 3 a 4096 vertici; non si sovrappone a una sezione completa, a gradini o allineata. Quota del piano iniziale e profondità sono esplicite e modificabili. I bordi grafici di ritaglio non diventano geometria del modello. Riferimenti alle intersezioni locali devono essere eliminati o modificati prima di rimuovere la sezione. Restano le avvertenze di precisione della rappresentazione già emesse dai backend esistenti sulle facce complesse; non sono una certificazione generale del display. Non rieseguita la suite generale non pertinente: il precedente limite della cache legacy di Applicatore è documentato nel rapporto Part23.

[Anteprima Part23](../output/Part23-tavola-sezione-locale.png) · [PDF](../output/Part23-tavola-sezione-locale.pdf)

Log e riproduttori di questa sessione: `/tmp/drawing2d-*`. Memoria precedente: [archivio](history/claude-prima-tavole2d-2026-11-14.md).
