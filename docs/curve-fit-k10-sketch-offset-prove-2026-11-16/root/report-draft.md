# K10 — offset NURBS dello schizzo, 16 novembre 2026

Prosecuzione con tre agenti GPT-6.1 Sol dopo le [pareti di Spessore](curve-fit-k10-thicken-2026-11-15.md). Trattati due ulteriori siti dell'inventario iniziale: il fit delle curve libere usato per i giunti concavi e quello delle copie finali dello schizzo.

## Difetto riprodotto

Una curva Converted NURBS regolare di grado 27 contiene una variazione fra i parametri controllati dal vecchio fitter. L'offset viene accettato con soli quattro poli e perde il dettaglio. A fronte del limite dichiarato di **1e-7 mm**, i due riproduttori polinomiale e razionale mostrano scarti geometrici di circa **5,74e-7 mm** e **5,42e-7 mm**. I coefficienti originali, le risposte precedenti e gli oggetti necessari al confronto sono stati congelati prima della build.

La prova indipendente usa i double originali come razionali esatti. Dimostra regolarità e monotonia della curva e dell'offset, quindi assenza di cuspidi e auto-intersezioni nei riproduttori. L'ordinata di un punto dell'offset esatto viene racchiusa con una radice quadrata intervallata e confrontata con l'involucro globale dei poli del risultato: il limite inferiore di errore riguarda l'intera curva risultante, non soltanto il medesimo parametro o una proiezione numerica. Le due famiglie superano il limite di oltre cinque volte.

## Correzione e contratto

`OffsetPiece` conserva la distanza con segno rispetto al parametro sorgente crescente. Per le NURBS riconosciute e i wrapper Trimmed, il provider del kernel `extrusionOffsetErrorBound` parte dai coefficienti originali, incorporati nel piano XY senza fit. Il segno tiene conto della sinistra della catena e del verso opposto della normale `T × Z`. Entrambi i siti usano `fitCurveCertified`. Dati riconosciuti invalidi o inconclusivi producono un errore; soltanto le famiglie non supportate mantengono il percorso campionato precedente.

Il taglio degli anelli può ricomporre intervalli disgiunti e cambiare il parametro finale. Ogni tratto conservato mantiene il dominio sorgente originale. Dopo `joinBezierPieces`, il codice ricontrolla i quattro poli **effettivamente emessi**, compreso il polo iniziale eventualmente sostituito al giunto, contro l'offset originale sul dominio corrispondente. La corrispondenza con il tratto finale [k,k+1] è affine. Lo scarto dell'estrazione, del taglio e della giunzione viene così verificato sull'output finale, senza trasformare una curva intermedia già arrotondata in una fonte esatta.

Il limite resta **1e-7 mm**, prima e dopo il rifilo, senza aumenti o fallback dopo un controllo inconclusivo. Il passaggio ai QPointF e alla Converted mantiene i coefficienti e i nodi double; la lettura della curva derivata li riusa direttamente. L'aggiornamento associativo ricalcola invece l'offset dalla sorgente e segue lo stesso percorso certificato. Sono presenti controlli cooperativi della cancellazione. La pubblicazione nello schizzo continua a usare copie locali di schizzo e selezione, preservando l'atomicità preesistente.

Il commento dell'API distingue ora le famiglie certificate dal fit campionato. È stata rimossa la precedente promessa generale C1: dopo il taglio le tangenti dei tratti possono differire; il certificato riguarda la posizione.

## Verifiche finali

__VERIFICATION__

## Stato e limiti

**13/24 siti iniziali parzialmente trattati, 11 interamente da migrare**, oltre ai fallback nei tredici già affrontati. K10 resta aperto. Rilevamento degli anelli, scelta del ramo, orientamento dei contorni chiusi, controlli di collasso e distanza minima mantengono gli algoritmi precedenti: il nuovo certificato posizionale non è una prova globale della loro topologia o iniettività. Le altre famiglie conservano un fit campionato; budget finiti e casi inconclusivi possono rifiutare anche geometrie valide.

I **24 rilievi precedenti corretti** e le **22 funzioni candidate orfane** mantengono il conteggio; K2 è in produzione. L'uniformazione completa di anteprime, pannelli e stili resta aperta. Nessuna nuova prova grafica OpenGL o certificazione globale dell'assenza di leak. CAD originali e modifiche pregresse preservati.

[Prove riproducibili e manifest](curve-fit-k10-sketch-offset-prove-2026-11-16/README.md).
