# Shower Head — spessore di 2,5 mm verso l’interno

8 novembre 2026. Correzione implementata e verificata nell’app ricompilata.
Richiesta confermata: **2,5 mm interni, conservando la forma esterna**.

Risultato: [`output/Shower-Head-spessore-interno-2.5mm.prt`](../output/Shower-Head-spessore-interno-2.5mm.prt),
21.411.984 byte, formato67, identità nuova. Undicesima funzione parametrica
`Spessore interno 2,5 mm`, sul corpo Cucitura10, lato interno. Il file è stato
salvato **dopo una rigenerazione completa senza cache**, poi riaperto.

## Origine e riproduzione

Originale `/home/myungit/Documenti/Cad_Example/Shower Head.prt`, non modificato:
1.465.000 byte, SHA256 `c163329eed236434965a274786908049e8cd2c5ea230fe1549197772d40c6d94`.
Copia congelata `/tmp/shower-head-2026-11-08/Shower-Head.prt`;
fixture kernel `kernel/tests/data/shower_head_sheet.body`.
10 funzioni/8 schizzi; Cucitura10 è una lamina di13 facce/25 edge/17 vertici,
con5 bordi liberi su due aperture planari. Loft7 comprende4 NURBS razionali,
grado15×5, griglie16×930 (una31×930). Input `checkBody` completo PASS.

I fallimenti erano distinti:

- Spessore normale: rifiuto immediato dello spigolo vivoE2, con il messaggio
  che richiedeva facce tangenti. Il problema non era la memoria.
- Offset cucito±2,5:97–111s, poi fallimento della prima B-spline. La stessa
  faccia sul dominio naturale passa in3,2s; il margine rettangolareUV0,5%
  non necessario al trim finale provoca il fallimento dell’estrapolazione.
- Verso interno: E3 è tangente nel dominio naturale, ma la giunzione con il
  cilindro richiede un breve prolungamento oltre v=1. L’ultima campata misura
  circa3,16e-5; il polinomio terminale amplifica il rumore dei due loft.
  Ridurre la sola regolarizzazione non risolve.
- All’estremo opposto del loft il raggio interno locale scende a circa0,70mm.
  Il vecchio vertice seguiva il ramo ripiegato dell’offset. Esiste invece
  un’intersezione regolare precedente: il tratto ripiegato va rifilato via.
- Controllo collaterale verso l’esterno: a+2,5 la gola larga5mm perde la
  larghezza assiale; le due corone si incontrano aX80. Cucire il cilindro di
  area nulla causava una traslazione SP-curve di periodo infinito.
  Questo prototipo esterno non è il risultato richiesto dall’utente.

## Correzione

`kernel/fk_open_offset` tratta le lamine a giunzioni vive fra famiglie diverse
di supporti e aperture planari. Chiude temporaneamente le aperture con piani
esatti, rifila i supporti sul contorno necessario, toglie i cappucci e ricuce
le due pareti con corone planari. Mantiene esplicite le origini delle facce.
Il percorso storico dei loft suddivisi su pieghe resta attivo, conservando
i bordi isoparametrici richiesti da Estendi superficie.

`kernel/fk_offset_support` aggiunge una continuazione NURBS omogenea C2 solo
**fuori** dal dominio naturale: le pezze aggiunte usano il jet quadratico
terminale, evitando l’estrapolazione instabile di campate minuscole. Il nuovo
modo è esplicito e usato per il percorso interno; i chiamanti precedenti di
`offsetJoinedFaces` conservano la continuazione polinomiale predefinita.

Vicino alla cuspide, il solutore cerca un’intersezione con fattori principali
dell’offset positivi. Le coordinateUV del ramo scelto guidano sia il bordo
sia le SP-curve. La verifica della curvatura usa il trim parametrico reale,
così le pieghe eliminate dal contorno non invalidano il rettangolo di supporto.
I tratti ripiegati ancora dentro la faccia vengono rifiutati.

Le sezioni fra piano e cilindro/cono mantengono cerchi analitici esatti,
compresi i vertici dei bordi chiusi. Il collasso esatto di una gola cilindrica
rimuove il cilindro e le due corone opposte, ricostruendo la corona residua
senza spostare le superfici sopravvissute. L’inversione oltre il limite resta
rifiutata. Nessuna tolleranza geometrica o di fit è stata aumentata.

## Verifiche

- Solido interno:28 facce, nessun bordo libero, `checkBody`0errori,
  tassellazione0facce fallite. Le13 superfici esterne sono gli stessi oggetti
  immutabili del sorgente, una volta ciascuna e con lo stesso verso.
- Classificazione indipendente: materiale a1,25mm all’interno e vuoto sia
  all’esterno sia oltre la parete interna, su tutte e quattro le NURBS.
- Offset interno separato:13 facce valide;36 confronti indipendenti rispetto
  a S−2,5N entro2e-7mm. Input serializzato invariato.
- Cinque regressioni nuove: gola analitica (volumi interno/esterno entro
  1e-6mm³,2,49mm valido e2,51mm esterno rifiutato), Shower esterno, continuità
  C2 razionale/derivate, Shower interno/esterno conservato, piega conservata
  rifiutata e piega eliminata dal trim accettata. PASS,80,07s complessivi.
- App Wayland/OpenGL: creazione30,31s, Undo/Redo, identità del corpo logico,
  codec67/B-rep, rigenerazione di tutte le11 funzioni senza cache40,69s,
  salvataggio/riapertura e framebuffer nativo PASS. Idle0frame/1,6s.
- App e runner compilati;33/33 CTest pertinenti PASS (snapshot e precisione
  inclusi). Suite di riferimento attuali: Offset27/27, Thicken6/6, Shell18/18,
  Sew12/12 PASS. OCCT rimane confinato ai test.
- Mappa aggiornata:652 file/9337 relazioni/0diagnostiche, `--check` PASS;
  quattro dettagli HTML verificati nel browser, chiamate nei due versi fra
  offset/aperture/spessore/supporti e collegamenti entrata/uscita controllati.
- Originale Shower invariato per SHA256. Prove e log in
  `/tmp/shower-head-2026-11-08/`; anteprima `Shower-inward-native.png`.

## Bottle e limiti

Ricontrollato Bottle con la build finale: copia iniziale con33 funzioni,
182 facce finali, codec67/B-rep di tutte le funzioni e nuovo schizzo senza
ricalcolo dei solidi PASS; idle0frame/2,006s e0,078msCPU.

Nel frattempo Bottle è stato salvato esternamente. Congelata anche questa
versione (96.739.124byte, SHA256
`e7abc620a25ec25306a0e6e3a506a4b3569d7381085ff163711184d0acb74017`):
33 funzioni/8schizzi/182facce, codec67/B-rep e nuovo schizzo PASS;
idle0frame/2,087s e0,045msCPU. RSS dopo lettura2,16GiB; dopo roundtrip e
viewport caldo3,54GiB. Questi passaggi differiscono dal confronto controllato
9,95→2,75GiB del rapporto Bottle; non confrontarli direttamente.

Rimangono da certificare geometrie arbitrarie con aperture non planari,
collassi più complessi e l’uso del comando Svuota sul Shower chiuso. Qui sono
verificati Spessore normale e Offset sul foglio, oltre alle regressioni Shell.
La ricerca numerica dei rami e i controlli campionati non costituiscono una
prova globale dell’assenza di auto-intersezioni su qualsiasi superficie.
L’app già aperta va riaperta per caricare la nuova build; nessun processo
utente è stato chiuso o riavviato durante le verifiche.
