# Schizzi: scelta esplicita dei riferimenti e anteprima vincoli — 13 novembre 2026

Richiesta: correggere la perdita di riferimenti alla geometria esistente dopo la deduplicazione; al passaggio su un bordo o una faccia mostrare il riferimento ipotetico e i vincoli disponibili rispetto al punto/elemento già selezionato.

## Cause e intervento

Consultati il grafico architetturale e i collegamenti fra mouse, selezione, snap, cache associative e vincoli. La scelta `PointAttachment`, progettata per il singolo punto disegnato, era usata anche per selezionare un intero bordo: un punto di contatto sovrapposto poteva sostituire la curva proiettata. La selezione rifiutava inoltre l'origine durante la scelta esplicita e l'hover in Selezione non mostrava riferimenti esterni. Le cache selezionate non erano evidenziate perché nascoste nella visualizzazione ordinaria.

Nuovo modulo `forgeCad2026_viewport_sketch_reference.cpp`: selezione esplicita su identità geometriche, proiezione di bordi/curve, sezione di facce con il piano di schizzo, punti notevoli con Ctrl. Proposta transitoria con sorgente identificata e tipi di vincoli derivati dallo stesso `applicableConstraints` del pannello. La selezione locale ha priorità sulla geometria esterna sovrapposta.

Hover e geometria proposta rimangono fuori dal documento, dalla storia Undo e dai vincoli. Il clic materializza/riusa una sola sorgente e mantiene l'elemento locale; il vincolo viene aggiunto soltanto dal comando scelto. Anche Quota può usare il riferimento esplicito. Le cache restano nascoste, ma quelle selezionate vengono evidenziate. Gli agganci automatici continuano a usare una sola destinazione e a preferire l'origine, senza ripristinare i riferimenti doppi.

Una faccia viene proposta come sezione esatta con il piano attivo: quando non è disponibile una sezione, il badge mostra la ragione e non promette vincoli non applicabili. Nessuna modifica a kernel, tolleranze o formati persistiti.

## Stato

Build completa dei target `forgecad` e `forgecad_view_tests` PASS. Le nuove regressioni coprono bordi inclinati con contatto puntuale, facce, punto locale conservato, un solo vincolo, riuso della sorgente, Quota, centri Ctrl, codec e Undo/Redo.

**23/23 CTest PASS** (6,26 s): vincoli lineari, proiezione schizzo/curve, nuova selezione hover, strumenti/riferimenti, asole/punti, Mouse, utilizzo riferimenti, intersezione, costruzione, visibilità riferimenti, cache corpi, riferimenti collegati e relativo flusso, maniglie, quote di costruzione, modifica schizzo, regressioni review, snap automatici, nuovo schizzo, storia schizzo, compressione documento e riuso anteprime di modifica. In particolare restano verificate la priorità dell'origine e l'assenza del doppio aggancio introdotte nel precedente intervento.

Il runner finale `--sketch-reference-hover --gl` PASS su Wayland/OpenGL. Badge effettivamente disegnato (confronto framebuffer con/senza etichette); cattura `qa-final/faccia-vincoli-proposti.png` ispezionata: faccia e corpo identificati, sezione evidenziata e vincoli leggibili.

Mappa rigenerata: **691 file, 9.965 relazioni, 338 unità C++ analizzate e zero unità con diagnostica**. `--check` PASS. Browser HTML/ricerca/dettagli/JavaScript PASS: nuovo modulo 4 relazioni in entrata e 21 in uscita; interazione 11/29, rendering 5/30, snap 7/10 e nuove regressioni 2/14. Verificati i percorsi `mousePressEvent` e `mouseMoveEvent` verso il nuovo modulo e i collegamenti nei due versi. Anche l'evidenziazione transitoria viene pulita quando il puntatore esce dal viewport. Nessun CAD originale modificato; log e catture in `/tmp/sketch-reference-hover-2026-11-13/`. La build aggiorna il binario su disco; l'app già aperta va riavviata dall'utente.
