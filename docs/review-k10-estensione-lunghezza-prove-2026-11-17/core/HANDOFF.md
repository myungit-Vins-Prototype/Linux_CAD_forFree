# K10 arcParameter: controllo della lunghezza finale

## Finding pubblico riprodotto dal root

Fonte nativa regolare e a dimensioni ordinarie: BSpline cubica x(u)=(u-2)^3 su[-1,1], poliX[-27,-9,-3,-1], estrusaZ altezza1. Body::buildSheet con pcurve affini originali, bordo verticale u=1 scelto per incidenza, extendSheet distanza3. Il root ha compilato il driver pubblico probe.cpp e ha riferito/raccolto:

- y=0: inputIssues0, SUCCESS/outputIssues0; dominio finale termina a2, emittedArc1.0000000000000013 invece3.
- y=2^-36*u: fonte e continuazione regolari (velocità mai nulla); inputIssues0, SUCCESS/outputIssues0; uHi7.62031405918383, emittedArc178.53408777564366 invece3, maxX177.53408777564368. Coordinate ordinarie. Questo secondo caso non dipende da un endpoint singolare.

Log originali ROOT copiati inerti nel pacchetto. Nessuna compilazione o esecuzione C++ è stata effettuata da core. L'algebra Fraction separata conferma per il caso dyadico y0 che il primo endpointNewton2 ha velocità0 e arco1; non sostituisce la prova pubblica del root.

## Causa e candidata

arcParameter conservava u1 dopo velocità nulla, esaurimento60iterazioni o passo piccolo, senza verificare la lunghezza restituita. Il controllo B-rep finale verifica connessioni/fin ma non il parametro distanza richiesto dall'operazione.

Delta limitato a fk_sheet.cpp: l'helper interno riceve il budget fisico prefit già esistente tolerance=1e-9*scale da entrambi i chiamanti (BSpline trasversale ed Extrusion). Dopo l'ULTIMO update di u1, anche in uscita per speed0/60iter/stepsmall, ricalcola arcLength sul parametro effettivo. Successo soltanto con delta finito e direzione stretta corretta, lunghezza finita e abs(achieved-length)<=tolerance; altrimenti domain_error. Controlla input/initialspeed/u1/s/speed/step finiti e cancellazione cooperativa.

Le formule Newton, il limite60, la condizione del passo e l'accuratezza di quadratura1e-13 restano invariati. Non si introduce earlyexit né bracket/nuovo solver o nuovo budget. La quadratura1e-13 è una stima numerica distinta dal budget fisico1e-9scale: il controllo non è un certificato analitico della lunghezza, né di regolarità/assenza di autointersezioni. Un caso difficile può essere rifiutato conservativamente anziché essere esteso: il lotto garantisce che il solver non dichiari successo con un residuo numerico finale fuori dal budget.

## Contratti conservati e limiti

API pubblica/header invariati, sorgente input immutata. BodyHistory viene già svuotata all'ingresso di extendSheet; failure attesa con history vuota, non conservazione di una sentinella preesistente. Nessun retry, riassociazione per prossimità, aumento cap/tolleranza, cambio del fit globale o conversione geometrica. Nessuna estensione a Q/E razionali o nuovi wrapper.

## Consultazione e verifiche core

AGENTS/CLAUDE/politica lette; grafo descrizioni e call/include/reference entrambi versi consultato prima del codice e salvato. arcParameter ha soltanto due chiamanti in extendSheet; nessun simbolo extendSheetMode nell'HEAD attuale. Header documenta distanza fisica lungo la trasversale al punto medio sulle BSpline. Confronto baseline byte-identico al repo e review statica del diff effettuati. Nessuna scrittura repository, compile, archive/ELF copiato, UI, commit o push. Integrazione/build/test/runtime/mappa/memoria restano al root; risultati candidati ancora PENDING al momento del freeze.

V2 richiesta dal root: verifica larghezza finita prima di OGNI arcLength, e direzione/larghezza finali prima della quadratura finale. Restano aperti i controlli nonfinite/cancellazione INTERNA alla quadratura e la relazione tra il reach risolto e coefficienti di ulteriori ricostruzioni del carrier; non sono inclusi nel fix corrente.
