# Onion2: raccordo costante dei 18 giunti — 7 novembre 2026

## Richiesta e riproduttore

Correggere l'errore «estremo della catena con più di 4 spigoli», raccordare tutti i 18 giunti e cercare il raggio più grande utilizzabile.
Analizzata una copia congelata di `/home/myungit/Documenti/Cad_Example/Onion2.prt`, salvato dall'utente alle 15:23:12:
5.815.653 byte, SHA-256 `856105c8ba21378fe5f4c63da2de0f52b34605a038a8a6612069244f1c76642c`.
L'originale e la copia congelata hanno lo stesso hash anche dopo il QA; l'originale non è stato scritto.

Il risultato «Solido cucito 24» contiene 18 facce, 18 edge e due vertici, ciascuno di valenza 18. Il B-rep iniziale è valido.
L'errore viene dal montaggio degli estremi in `fk_blend_surface.cpp`, limitato ai vertici con tre/quattro edge a raggio positivo.
Inoltre ogni giunto cambia convessità due volte: trattarlo interamente con il verso rilevato a metà bordo non costruisce il raccordo corretto.
La punta superiore richiede una calotta comune; il polo inferiore è liscio e ammette il collasso della sezione anche mantenendo R positivo.
Il precedente supporto R0 per la lamina Onion aperta non risolve questo caso.

## Correzione implementata

- `kernel/fk_blend_polar.{h,cpp}` riconosce ventagli chiusi di facce B-spline a due bordi fra due poli e richiede tutti i giunti convergenti.
  Costruisce simultaneamente i contatti, le pezze e le calotte sferiche tangenti comuni. Ai poli lisci mantiene il vertice con il limite della normale dalle derivate della superficie.
- I cambi di convessità si individuano sui supporti tramite le SP-curve originali. Il nuovo percorso divide il raccordo nei tratti con verso coerente;
  i punti di tangenza sono condivisi esplicitamente nella topologia. La ricerca iniziale degli eventi è campionata e raffinata: non è un isolatore globale di tutte le radici per qualunque superficie.
- `fk_blend_surface` espone un ponte interno al solver esistente: guida intrinseca, continuazione nel raggio, contatti sferici imposti agli estremi e sezioni collassate G1.
  Il raggio resta costante; non viene sostituito con una legge variabile o ridotto automaticamente.
- Le copie circolari riusano una pezza dopo un controllo di tutti i coefficienti NURBS di supporti, curva e parametrizzazione, entro il solo arrotondamento delle trasformazioni.
  Questo evita che le pose diverse cambino il ramo numerico vicino alle tangenze. Le origini topologiche restano dichiarate, senza riaggancio per prossimità.
- Il target del fit rimane 1e-9 mm. Residui geometrici oltre la risoluzione preesistente di 1e-7 mm vengono rifiutati; non si allargano le tolleranze dei vertici o degli edge.
- `fk_blend.cpp` richiama il percorso simultaneo prima del raggruppamento/sequenziamento delle catene. Smussi, coniche, leggi variabili e topologie estranee seguono i percorsi precedenti.

Il risultato Onion2 ha **73 facce, 126 edge, 55 vertici e una shell solida chiusa**:
18 supporti rifilati, 54 pezze di raccordo e una calotta superiore. Tutte le origini di facce/edge/vertici sono dichiarate.

## Raggio e copia consegnata

Ricerca numerica sul corpo completo, senza cambiare la geometria originale. Raggi 0,1 e 0,3 mm non risultano costruibili.
Dopo il restringimento dell'intervallo sono stati provati tutti i valori del pannello fra 0,092797 e 0,092804 mm:
**0,092797 mm passa**, tutti i passi da **0,092798** a **0,092804 mm** sono rifiutati per mancata continuazione/discontinuità del raccordo.
È il massimo verificato in questa ricerca alla precisione di sei decimali del pannello; non costituisce una dimostrazione del massimo geometrico globale né una garanzia per altre geometrie.

Salvata **`output/Onion2-raccordato.prt`**, circa 7,8 MiB, con una feature «Raccordi 18 giunti», modo **Costante**, R=0,092797 mm e tutti i 18 riferimenti.
Il limite è locale a questa geometria e al percorso numerico supportato; non è stato introdotto un pulsante di ricerca automatica del massimo.

## Verifiche eseguite

- Build completa `forgecad-release/forgecad` riuscita. L'app già aperta deve essere riavviata per usare il nuovo kernel.
- Nuovo target indipendente da OCCT `forgekernel_polar_blend_tests`: **3/3 PASS**. Copre 18 giunti con/senza cambi di convessità,
  due calotte sferiche, polo liscio, raggio e G1, posa/scala/edge invertiti, genealogia completa, input immutabile, serializzazione e mesh;
  selezioni parziali e raggi eccessivi rifiutati.
- **6/6 CTest PASS**: polari, istanze di superficie, genealogia, tolleranze, profili e nodi dei contatti. Ripetuto il solo target polare dopo l'ultima aggiunta: PASS.
- Suite esistente **Blend: 62/62 PASS**.
- Sul risultato reale a R=0,092797: `checkBody({true,true})` senza errori; 73/73 facce tassellate senza fallimenti.
  Controlli geometrici campionati sui tratti: scarto radiale dalla palla <=7,15e-11 mm, distanza dal supporto <=4,17e-11 mm,
  errore `1-abs(dot(n1,n2))` <=1,61e-14. Queste misure non certificano un massimo globale fra tutti i campioni.
- QA **Wayland** sul pannello reale Costante: 18 selezioni, raggio nel pannello, conferma, Undo/Redo, salvataggio;
  codec con/senza cache e rigenerazione integrale, 73 facce valide, OpenGL PASS anche al valore finale.
  Una finestra nuova ha riletto il documento: risultato visibile con mesh, nessun errore; cattura dopo inizializzazione di layout/camera.
- Mappa aggiornata: **635 file / 9017 relazioni**, zero TUs con diagnostica, `--check` PASS.
  HTML: panoramica e cinque dettagli PASS; collegamenti in ingresso/uscita e chiamate risolte dispatcher→modulo polare→solver verificati separatamente da include e riferimenti.
- `git diff --check` PASS. Nessun commit/push e nessun processo utente chiuso.

## Limiti conservati

La nuova costruzione riguarda ventagli chiusi riconosciuti e un raggio costante comune: non rende universali i raccordi ad alta valenza.
La lamina Onion aperta continua a richiedere R0 alla cuspide per il caso precedentemente verificato.
La validazione è geometrica e topologica con controlli campionati; non è una certificazione globale di assenza di auto-intersezioni per qualunque B-spline.
L'integrazione delle proprietà di massa del nuovo corpo non è stata certificata: un tentativo sul prototipo è stato interrotto per durata eccessiva.
