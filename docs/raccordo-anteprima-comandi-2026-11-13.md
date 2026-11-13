# Raccordo in modifica e riferimenti nei comandi — 13 novembre 2026

Richiesta: eliminare la sovrapposizione nell'anteprima di modifica del raccordo mostrata nell'immagine e proporre riferimenti negli schizzi soltanto durante un comando attivo.

## Intervento

Consultati il grafico architetturale, le descrizioni e i percorsi fra dialogo raccordo, anteprima, visibilità scena e renderer; per gli schizzi i collegamenti mouse → riferimenti/snap → badge.

Alla riapertura il risultato completo del raccordo riusa correttamente B-rep e mesh. La lista di sostituzione includeva però solo gli operandi consumati esplicitamente: i modificatori che proseguono lo stesso corpo nascondono normalmente la base attraverso la storia del corpo, senza dichiararla consumata. La forzatura della base per le anteprime locali la ripristinava anche sotto il risultato completo. La sostituzione ora include le dipendenze appartenenti allo stesso corpo logico e rimuove anche il loro ripristino. Vale per i risultati riusati, inclusi altri modificatori, senza ricalcolare la geometria. La base resta interrogabile dal picker dei bordi: una regola del renderer ne esclude le superfici e gli spigoli ordinari quando disegna il risultato completo, preservando selezione e occlusione dei bordi.

In Selezione non vengono calcolate né disegnate proposte associative al passaggio del mouse, inclusi piani/assi. Il clic risolve ancora un solo riferimento e conserva la selezione locale per i vincoli. I comandi di disegno mantengono snap e sorgente nominata; Quota mantiene la proposta di bordo/sezione e i vincoli compatibili. La fine del comando pulisce i suggerimenti.

Nessun cambiamento a kernel, tolleranze o formati. Moduli: viewport_preview, viewport_interaction, viewport_render, viewport_scene e viewport_sketch_reference. Regressioni nei test di riuso anteprima e hover; guida e catalogo architetturale aggiornati.

## Stato e verifiche

Build completa dei target `forgecad` e `forgecad_view_tests` PASS.

**18/18 CTest PASS** (9,29 s): selezione raccordi, affinamenti e raccordi avanzati; pannelli storyboard; prestazioni, riuso anteprime e mesh storiche; riferimenti nei comandi schizzo, strumenti, Mouse, utilizzo e intersezione; visibilità e riferimenti collegati, flusso vincoli, modifica schizzo, regressioni review e snap automatici.

**Tre prove Wayland/OpenGL PASS** sul runner finale:

- `--edit-preview-reuse --gl`: nessun worker alla riapertura, B-rep e buffer condivisi, tutte le componenti di ripetizione e pannelli reali; framebuffer del raccordo e pannello con picker attivo. Eliminare temporaneamente la sostituzione della base cambia il framebuffer e riproduce la sovrapposizione. Catture `qa-final/raccordo-riaperto.png` e `qa-final/pannello-raccordo.png` ispezionate.
- `--sketch-reference-hover --gl`: nessuna proposta né calcolo in Selezione; Quota con sorgente, faccia e vincoli leggibili; Polilinea/Spline/NURBS/Cerchio/Cerchio3 con sorgente nominata. Fine comando pulita; clic e vincolo manuale conservati senza duplicati. Cattura del badge ispezionata.
- `--fillet-selection-review --gl`: picker sulla base originale, occlusione/X-ray, selezione facce/bordi, Esc/Annulla/chiusura/conferma anticipata e Undo/Redo. Nessun indebolimento della regressione preesistente che ha segnalato la base non più selezionabile nella prima versione del fix.

Mappa aggiornata: **691 file, 9.966 relazioni, 338 unità C++ e zero unità con diagnostica**; `--check` PASS. Browser HTML/ricerca/dettagli/JavaScript e collegamenti nei due versi PASS: anteprima 32 entrate/17 uscite; scena 40/14; rendering 5/30; riferimenti 4/21; test riuso 2/12 e test hover 2/14. Verificati in `graph.json` i percorsi `useExistingPreviewResults → bodyOperands`, `drawExtrusions → isBodyVisibleInScene` e `mouseMoveEvent → updateSketchReferenceHover`.

`git diff --check` PASS. Registro e guide aggiornati. Il binario su disco è aggiornato; l'app già aperta va riavviata dall'utente. SHA256 del CAD originale invariato al termine. Nessun CAD originale modificato; prove in `/tmp/preview-raccordo-comandi-2026-11-13/`.

## Copie dei modelli e riproduzione

La versione corrente del file `girante.prt` è cambiata rispetto ai precedenti interventi: la copia congelata è di 2.288.471 byte, SHA256 `6983c310c3c6a0026e6659ef1f358676b0265d92fc7e17c29890350878a5b3bc`; contiene una storia diversa e nessun raccordo salvato. Lo stato esatto dell'immagine non è quindi presente nel file su disco. La correzione visiva è verificata con una fixture B-rep e il pannello reale del raccordo. Su una copia precedente della girante, il tentativo di ricreare R2,5 con gli identificatori E33/E46/E63 non corrisponde al raccordo dell'immagine e viene rifiutato dal kernel per una catena che termina in un angolo concavo; non sono state cambiate geometria o tolleranze per forzarlo.
