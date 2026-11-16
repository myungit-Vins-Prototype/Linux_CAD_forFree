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

- Build completa Release di applicazione, runner e nuovo target dedicato: PASS. 56 avvisi in file invariati in questo intervento; nessuno nell'adattatore o nel nuovo test. Eseguibile `forgecad-release/forgecad` aggiornato: riaprire l'applicazione per usarlo.
- **27/27 regressioni pubbliche PASS**, contro **6/27 della baseline**: polinomiali e razionali, inversione dei poli, reverse/bothSides, giunti concavi e raccordati, finestre accorciate, nodi C0 nei due versi, tangente nulla, rifilo reale, rigenerazione associativa, controlli analitici e fallimenti atomici. Gli scarti osservati nei casi nascosti scendono a circa **3,93e-8 mm**, sotto 1e-7; ai nodi angolosi il massimo osservato è circa **7,02e-8 mm**. Le misure dense sono regressioni, il limite continuo deriva dai coefficienti.
- **14/14 suite CTest PASS**, comprese editing e riferimenti dello schizzo, offset associativo e con cuspidi, persistenza, Undo/pannelli, solver, ciclo di vita e test kernel pertinenti. Le verifiche già presenti sul workflow usano il percorso completo dell'applicazione; il nuovo target dedicato usa le API di schizzo senza viewport. Qt offscreen non certifica una nuova prova visiva OpenGL.
- **ASan/UBSan/LSan: 27/27 casi PASS**, senza diagnostiche. Strumentati nuovo test, `cad_sketch_offset`, `cad_curve_solver`, `fk_curve_fit_bounds`, `fk_curve_ops` e `fk_offset`; il resto dell'archivio kernel è Release congelato. Controllo mirato, non copertura integrale dell'applicazione. Il test dedicato collega soltanto QtCore: una prima esecuzione collegata a QtWidgets aveva caricato il driver NVIDIA e segnalato 183 byte in quattro allocazioni del driver, senza stack del prodotto. Il link finale riusa gli stessi sei oggetti strumentati, evita quelle librerie non necessarie e passa senza soppressioni; entrambi i log sono conservati. Un tentativo nel sandbox aveva completato i casi ma impedito il controllo LSan per ptrace; il risultato finale proviene dall'esecuzione esterna al sandbox.
- **36 verifiche indipendenti finali PASS**: 16 scenari pubblici, 12 controlli diretti e 8 prove esatte del difetto geometrico; query avverse su segno, estremi dei nodi, polo alterato, campata stretta, trim, peso invalido, domini conservati e poli finali dopo il taglio. Manifest confrontato con i byte integrati; [audit e ipotesi](curve-fit-k10-sketch-offset-prove-2026-11-16/independent/report.md).
- Mappa: **757 file / 10943 archi / 377 unità C++**, diagnostica vuota, `--check`, uguaglianza HTML/JSON e audit entrante/uscente di chiamate, include e riferimenti PASS.
- Conservati byte per byte **2477 dei 2484 file preesistenti censiti da Git, versionati o non ignorati**; i 7 modificati appartengono allo scope dichiarato. Hash dei sorgenti/binari, FP rigoroso dell'adattatore in tutti i target, link dedicato senza OCCT, `git diff --check`, memoria entro 16 KiB e manifest delle prove verificati nell'audit finale.

## Stato e limiti

**13/24 siti iniziali parzialmente trattati, 11 interamente da migrare**, oltre ai fallback nei tredici già affrontati. K10 resta aperto. Rilevamento degli anelli, scelta del ramo, orientamento dei contorni chiusi, controlli di collasso e distanza minima mantengono gli algoritmi precedenti: il nuovo certificato posizionale non è una prova globale della loro topologia o iniettività. Le altre famiglie conservano un fit campionato; budget finiti e casi inconclusivi possono rifiutare anche geometrie valide.

I **24 rilievi precedenti corretti** e le **22 funzioni candidate orfane** mantengono il conteggio; K2 è in produzione. L'uniformazione completa di anteprime, pannelli e stili resta aperta. Nessuna nuova prova grafica OpenGL o certificazione globale dell'assenza di leak. CAD originali e modifiche pregresse preservati.

[Prove riproducibili e manifest](curve-fit-k10-sketch-offset-prove-2026-11-16/README.md).

## Prosecuzione successiva

Il blocco [proiezioni su Plane](curve-fit-k10-projection-2026-11-16.md) porta K10 a **14/24 siti iniziali parzialmente trattati e 10 interamente da migrare**, oltre ai fallback. Aggiunge controllo continuo dagli originali, verifica dei giunti e scarto finale nel riepilogo applicativo; prove e limiti sono nel rapporto dedicato. I risultati 13/24 di questo rapporto restano riferiti alla fase precedente.
