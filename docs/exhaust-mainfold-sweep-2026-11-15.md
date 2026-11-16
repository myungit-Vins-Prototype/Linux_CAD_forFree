# exhaustMainfold: sweep su percorso 3D — 15 novembre 2026

## Richiesta e causa

L'utente chiede la funzione fra Schizzo 6 e Curva tagliata 3D 2 di
`/home/myungit/Documenti/Cad_Example/exhaustMainfold.prt`, segnalando Frenet.
Si tratta di una sweep: un profilo lungo un percorso, anziche' sezioni successive di una loft.
L'originale ha sei schizzi e sette feature; nessuna sweep gia' salvata.

Riprodotto sul file congelato: Frenet fallisce sul rettilineo iniziale, come previsto;
anche Torsione minima fallisce con lo stesso messaggio. La NURBS del percorso unisce
rettilinei e raccordi in piani diversi. PathFrames, nel ramo non piano, ricavava ancora
la torsione minima dal sistema di Frenet e dalla torsione, non definiti a curvatura nulla.

La validazione aggiunta sui nodi interni ha inoltre rilevato un disallineamento
di 1,716022554e-6 radianti fra il raccordo destro di Schizzo 4 e il segmento successivo.
Le tangenze erano gia' vincolate, ma alcuni vincoli indiretti retta/cerchio, senza
coincidenza diretta, accettano un piccolo errore angolare con residuo di distanza quadratico.
Non sono state allargate le tolleranze del kernel o modificate le equazioni del solver.

## Correzione applicativa

- `kernel/fk_sweep.cpp`: trasporto di Bishop N'=-(T'.N)T, integrazione RK4 con
  raddoppio del passo, controllo dell'errore e limite della rotazione tangente per evitare
  aliasing su giri completi; cache agli intervalli lisci e ai nodi. Derivate del frame
  ottenute dalle derivate esatte del percorso, senza divisioni per la curvatura.
- Restano i moti analitici per rette/archi e il frame esatto dei percorsi piani.
  Frenet mantiene il proprio rifiuto dei rettilinei e dei flessi.
- Controllo G1 anche ai nodi interni di una curva composta, come gia' fra PathSegment distinti.
- `kernel/fk_sweep.h`: descrizione aggiornata.
- `kernel/tests/test_sweep.cpp`: regressioni per percorso spaziale composto con rettilinei,
  versione analitica e NURBS con sezione quadrata, flesso spaziale e chiusura spaziale a due tratti.

Consultati descrizioni e collegamenti della mappa architetturale prima delle modifiche:
cad_feature_build -> cad_forge -> fk_sweep; frame, curve, fit Hermite e checkBody.

## Copia del modello

Prodotta `/home/myungit/Documents/Codex/2026-11-15/pre/outputs/exhaustMainfold-sweep.prt`,
con nuova identita' documento e otto feature. `Sweep 1`: Schizzo 6, curva 3D 2
(indice 6), Torsione minima, solido in corpo nuovo, senza unione automatica.

Nella sola copia sono state ripristinate analiticamente le tangenze gia' prescritte
dei due raccordi di Schizzo 4, intersecando le rette parallele ai lati distanti il raggio.
Raggi e quote mantenuti. Massimo spostamento dei punti: 0,0001015730973 mm.
Residuo massimo dei vincoli: 5,684341886e-13; solveSketch riuscito con zero iterazioni.
I riferimenti alle curve originali sono preservati e le curve derivate vengono rigenerate.

Per ricreare il risultato nell'app: Sweep, Profilo Schizzo 6, Percorso Curve 3D /
Curva tagliata 3D 2, Orientamento Torsione minima, Risultato Corpo nuovo.
Usare la copia che contiene le tangenze corrette. L'app gia' in esecuzione richiede
riavvio per caricare la nuova implementazione; nessun processo utente e' stato chiuso.

## Verifiche realmente eseguite

- Prima della correzione, entrambi i nuovi casi con rettilinei e flesso falliscono.
- Runner sweep collegato alla libreria di produzione aggiornata: **14/14 PASS**,
  inclusi elica, confronto OCCT esistente, piani, archi, tubi aperti, chiusure e sezioni miste.
- Build completa del target `forgecad`: PASS. Non solo kernel/runner.
- Copia: salvataggio, riapertura e ricostruzione di tutte le otto feature senza cache:
  errori vuoti, quattro facce nella sweep, zero diagnostiche checkBody.
- Tassellazione: zero facce fallite, 4358 vertici e 8436 triangoli nel controllo visivo.
  Vista del risultato verificata da immagine generata dai triangoli di display;
  nessuna nuova prova dell'interfaccia nativa, non disponibile in questa sessione.
- `git diff --check`: PASS. Nessun commit/push.
- Mappa architetturale: 722 file, 10385 relazioni, 352 TU, zero diagnostiche.
  Generatore --check PASS; HTML verificato nel browser, collegamenti in entrambi i versi e leggibilita' PASS.

SHA256 originale, verificato prima e dopo: `9acfe5a752b96675633e5e094e2c69243f4f8bab25b43749af088b623f9b9cdd`.
SHA256 copia finale: `e42d1c4edeb970679b5941def224d440939c4258c284010bbd6d0d03c9a1123b`.

## Limite preesistente incontrato fuori dal caso utente

Una NURBS spaziale chiusa trattata come un solo PathSegment puo' restituire volume
nullo pur passando i controlli topologici. Riprodotto anche prima della correzione,
non risolto qui. La stessa curva chiusa suddivisa in due tratti ha volume corretto e
nessun bordo laminare, ed e' inclusa nella regressione del trasporto. Il file utente
ha un percorso aperto e non incontra questo limite.

## Prosecuzione: risolutore dei vincoli

Su osservazione dell’utente, il difetto delle tangenze e' stato corretto nel solver.
La correzione analitica della prima copia era solo un aggiramento. Causa, correzione
generale, nuove prove e uso manuale nel [rapporto dedicato](vincoli-tangenza-transitiva-2026-11-15.md).
