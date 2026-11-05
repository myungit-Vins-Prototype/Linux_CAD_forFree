# Loft: sezioni puntuali e tangenza a un piano

Richiesta: usare l’entità Punto come sezione e imporre la tangenza della sezione a un piano.

## Uso e limiti geometrici

- Nel pannello Loft, «Scegli sezioni nella vista» accetta anche punti: entità Punto,
  vertici e riferimenti puntuali già supportati dal picker. Sono disponibili nella
  vista e nelle righe espanse della storyboard. Uno schizzo contenente un solo
  punto effettivo può essere selezionato interamente.
- Il punto deve essere la prima o l’ultima sezione. Serve almeno una sezione curva;
  una sezione puntuale intermedia viene rifiutata con l’indice della sezione.
- Primo/Ultimo profilo include «G1 — tangente a un piano». Il riferimento può
  essere un piano principale, un piano di costruzione o una faccia piana. La
  sezione deve appartenere al piano; non si tratta della modalità normale al profilo.
- Il riferimento al piano è unico e sostituibile; un secondo clic lo rimuove.
  L’influenza regola la derivata. Le guide seguite esattamente devono avere una
  tangente compatibile con il piano agli incontri, altrimenti la loft segnala errore.
- Le corrispondenze restano disponibili; sulla sezione puntuale sono fisse.
  Le condizioni al piano sono abilitate nel pannello Loft, non estese alla
  diversa funzione Superficie rigata in questo intervento.

## Implementazione

`LoftSection::point` rappresenta una sezione esatta, senza cerchi di raggio piccolo.
La preparazione eredita dal profilo vicino soltanto la parametrizzazione delle
pezze; prima di interpolare, tutti i poli della sezione e i suoi vertici coincidono
esattamente con il punto. La topologia usa un vertice condiviso, elimina i bordi
collassati e omette il coperchio sulla punta. Guide e corrispondenze conservano
l’ordine del profilo vicino, mentre gli incontri delle guide sono risolti sul punto.

`LoftContact::plane` rappresenta il piano senza inventare una faccia di appoggio.
Per i profili regolari si usa il campo G1 esistente; sulla punta si vincola il
ventaglio delle derivate trasversali al piano. La verifica finale usa il limite
ricavato dalla derivata mista della NURBS, poiché la derivata lungo la sezione è
nulla nel punto. Le sezioni curve e le tolleranze esistenti sono conservate.

La regressione del volume di un cono ha rilevato una mancanza preesistente nella
quadratura di una faccia NURBS con lato collassato: il contorno UV non includeva
il tratto senza edge fisico sulla punta. `fk_mass.cpp` ora integra quel tratto
quando l’isoparametrica è certificata costante dai suoi poli. Il volume è verificato
nei due ordini delle sezioni, sia per loft liscia sia rigata.

L’adattatore risolve i riferimenti esatti da schizzi, punti e piani; i normali
visitatori delle dipendenze gestiscono rigenerazione, rimappatura e Undo/Redo.
Formato documento 66 per il modo G1 piano (valore 4), senza cambiare i valori
precedenti. Conservata lettura legacy e confronto degli hash delle tavole 65;
una definizione con il nuovo modo non viene retrocessa silenziosamente a 65.

## Verifiche

- Nuova regressione kernel: coni nei due versi, volume analitico, B-rep, mesh,
  sezioni aperte e chiuse, punti ai due estremi, guide, corrispondenze, G1 al piano
  su punta e profilo regolare, STEP della punta riletto, casi negativi: PASS.
- Prova aggiuntiva con unione facce tangenti: PASS.
- Nuova regressione app: punto singolo e schizzo puntuale, dipendenze, rigenerazione,
  spostamento del piano fuori sezione, modifica del punto, Undo/Redo, salvataggio
  senza cache e rilettura, codec65/66, picker piano e storyboard: PASS nel probe.
- Pannello reale: scelta del punto e del piano, controllo delle corrispondenze
  sulla punta, chiusura; offscreen e Wayland PASS. Immagine locale
  `/tmp/loft-point-native.png`, nessun file CAD utente modificato.
- Build definitiva di app e runner eseguita. 17/17 CTest PASS: sette suite kernel
  loft e dieci suite app (punti, corrispondenze, bordi, sezioni proiettate, tangenza,
  lettura, revisioni native/tavole, file tavola e storyboard). Suite di confronto
  con OCCT: Mass 11/11 e Loft 42/42 PASS. Runner definitivo Wayland PASS.
- Mappa finale: 596 file, 8294 relazioni, 286 unità senza diagnostica; check PASS.
  Verificati collegamenti entranti/uscenti distinti per call/include/reference e
  chiamate fra picker, risolutore, datum, persistenza/hash e topologia. Rendering
  QtWebEngine dell’overview e dei sei dettagli PASS.
- Modifiche locali non committate; file CAD originali invariati. L’app aperta
  dall’utente deve essere riavviata per caricare la nuova build.

Log locali: `/tmp/loft-point-validation-build.log`, `/tmp/loft-point-ctest.log`,
`/tmp/loft-point-app-probe.log`, `/tmp/loft-point-native.log`,
`/tmp/loft-point-mass-tests.log`, `/tmp/loft-point-loft-tests.log`,
`/tmp/loft-point-architecture-final.log`.

Vista: `/tmp/loft-point-architecture-render.log`, `/tmp/loft-point-architecture-links.txt`.
UI definitiva: `/tmp/loft-point-final-native.log`, `/tmp/loft-point-final-native.png`.
