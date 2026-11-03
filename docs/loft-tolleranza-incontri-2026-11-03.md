# Loft: incontri tolleranti tra guide e sezioni

Intervento del 3 novembre 2026, successivo a `4fafbaa`.

## Caso reale e causa

Il pannello di `SurfaceModellingExample219.prt` falliva con «approssimazione della guida fuori tolleranza».
Riprodotto dalla copia congelata `/tmp/forgecad-loft-guide-tolerance/SurfaceModellingExample219.prt`,
SHA256 `71ac418ead78c5da15b18038e23b8515135ffc525a29eccd5972ab0921dce627`, 11908 byte.
L'originale in `Documenti/Cad_Example` è stato letto soltanto, senza scritture o interventi sul suo processo.

Sezioni, nell'ordine: Schizzo 9, Schizzo 12 su Piano 3, Schizzo 10
(indici 6/8/7, storyId 9/14/13). Guide: i due Schizzo 12 su Piano 2 e XY
(indici 9/10, storyId 17/18), G1 e influenza 100%.
La variante con una guida riesce; aggiungendo la seconda fallivano sia profili G0 sia G1.

La seconda guida incontra il primo profilo con residuo 2,2351741790771484e-7 mm,
ammesso dal controllo d'intersezione. `followingGuide` forzava il profilo esatto
soltanto nel parametro dell'incontro: immediatamente accanto usava la guida originale.
La funzione passata al fit era quindi discontinua. Lo scarto finale misurato dal fit era
2,1756920176585481e-7 mm, superiore al suo budget di 1e-9 mm.
Aumentare suddivisioni o tolleranza non risolve correttamente questa incompatibilità.

## Correzione e budget

`kernel/fk_loft.cpp` raccorda i residui degli incontri mediante una quintica C2
con derivate prima e seconda nulle agli estremi. I punti delle sezioni rimangono esatti;
i jet delle guide negli incontri sono conservati. Il raccordo è una combinazione
convessa dei due residui, quindi il suo spostamento non supera il massimo dei residui ammessi.
Un controllo esplicito rifiuta residui oltre il budget di connessione già usato dal loft.

Restano distinti e invariati:

- connessione guida/sezione: il preesistente `1e-6 * scale`;
- approssimazione NURBS del raccordo continuo: `1e-9 mm`, con verifiche indipendenti a `4e-9 mm`.

La NURBS risultante segue la guida entro il residuo di connessione più l'errore di fit;
non si dichiara un errore di 1e-9 mm rispetto a una guida che già non tocca esattamente il profilo.
Non vengono modificati schizzi, curve sorgenti o riferimenti associativi; nessuna mesh entra nel calcolo.
Nessun cambiamento al formato del documento o al codec B-rep.

## Verifiche

- Compilazione Release completa di applicazione e runner PASS. Warning preesistenti degli
  inizializzatori delle tavole; due errori di scrittura del nuovo test corretti prima dei controlli finali.
- CTest pertinenti: **8/8 PASS**, inclusi guide/sezioni implicite, tangenza, costruzione e pulizia riferimenti.
- Gruppo storico kernel Loft: **42/42 PASS**.
- Suite forma loft: **11/11 gruppi PASS**. Il nuovo gruppo verifica due/tre sezioni,
  guide invertite, scale 1/10 e traslazione, sorgenti immutate, tangenti, assenza di creste,
  roundtrip B-rep e rifiuto di guide fuori connessione.
  Fit controllato separatamente contro il raccordo analitico: massimo 8,0e-14 mm;
  sezioni entro 3,1e-14 mm. Le soglie dei test preesistenti non cambiano.
- Controprova con `fk_loft.cpp` di `4fafbaa`, a parità del nuovo test:
  **10/11 PASS**, nuovo caso fallisce con l'errore originale.
- **Quattro flussi nativi PASS**, profili G0/G1 su Wayland e XCB:
  pannello reale, anteprima, conferma, Undo/Redo, salvataggio/riapertura con e senza cache,
  ricostruzione completa e controllo delle sorgenti. Sezioni entro 4,99e-14 mm;
  scarto massimo delle guide dai bordi longitudinali 2,23518e-7 mm, coerente con il residuo iniziale.
  Un primo controllo rispetto all'intera superficie era meno discriminante: il controllo finale usa i bordi.
- Mappa: **566 file, 7825 relazioni, 268 TU, zero diagnostiche AST**; generazione/check PASS.
  Verificati quattro confini e quattro moduli in entrambi i versi, distinti call/include/reference.
  HTML dei blocchi loft e test renderizzato e ispezionato; output generati ignorati da Git.
  Il browser nel sandbox ordinario non partiva; rendering riuscito con profilo temporaneo fuori da quel sandbox.

Le misure geometriche sono campionate: non certificano globalmente l'assenza di autointersezioni.
Questa correzione riguarda gli incontri già accettati; una guida che non raggiunge le sezioni
entro il budget resta un errore. Le vecchie verifiche generali e le CI precedenti non sono attribuite al nuovo codice.

[Evidenze, sorgenti delle sonde, log e immagini](history/loft-tolleranza-incontri-2026-11-03-verifica.zip).
L'archivio esclude CAD originali, copie .prt, preferenze, oggetti e binari.
