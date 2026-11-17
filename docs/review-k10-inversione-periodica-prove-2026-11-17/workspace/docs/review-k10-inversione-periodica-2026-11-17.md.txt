# K10: residuo finale dell’inversione periodica — 17 novembre 2026

Corretto un falso successo di `invertPoint`: dopo una proiezione valida, il riporto degli UV vicino alla stima periodica poteva spostare il punto oltre la tolleranza a causa dell’arrotondamento. Ora `true` richiede UV finali e residuo finiti, con distanza entro il cap richiesto. **Ramo e tolleranze conservati; K10 globale resta parziale. Nessun commit o push.**

Review e regressioni preparate con tre agenti GPT‑6.1 Sol; riproduttore, compilazioni, integrazione e runtime gestiti da root in sequenza. Nessun CAD utente modificato, test/avvio UI, suite generale483 o nuova CI. Nessuna copia di archivio statico; binari temporanei rimossi a verifiche concluse.

## Difetto riprodotto

`invertPoint` verificava il successo di Newton o della proiezione globale prima dell’ultimo `unwrapNear`. Questo aggiunge un multiplo arrotondato del periodo per conservare la continuità vicino alla stima in ingresso. Per fasi grandi il parametro memorizzato non rappresenta più la stessa posizione entro il budget, ma il vecchio ritorno era incondizionatamente `true`.

API pubblica kernel: `CylindricalSurface(Frame3(),10)`, punto `S(0,25;2)`, tolleranza1e-8mm, scala10. La baseline e la candidata restituiscono gli stessi UV; cambia il segnale di successo quando il residuo finale è fuori cap.

| Stima U | Residuo finale osservato | Prima | Dopo |
| --- | --- | --- | --- |
| 0,25 | 0 | true | true |
| 100 | 3,87784e-14mm | true | true |
| 1e6 | 8,25877e-10mm | true | true |
| 1e8 | 6,33461e-8mm, oltre6cap | true | false |
| 1e12 | 3,19857e-4mm | true | false |
| 1e16 | 2,84789mm | true | false |
| 1e20 | 9,15879mm | true | false |

Non è un fallimento attribuito a un nuovo documento CAD: è un errore del contratto dell’API kernel riprodotto direttamente. Le stime periodiche NaN/Inf potevano inoltre arrivare al ritorno positivo con UV non finiti.

## Correzione

Dopo tutte le modifiche degli UV, comprese la scelta del seam BSpline e l’ultimo riporto periodico, la funzione rifiuta UV non finiti e verifica `distance(p,S(uv_finale))` finita e non maggiore di `tolerance`. Non allarga cap, non resetta al ramo canonico e non altera Newton, proiezioni o la scelta del ramo. Una fase non rappresentabile entro il cap può ora dare un fallimento conservativo.

Il valore booleano descrive i parametri effettivamente consegnati al chiamante. Gli UV su `false` possono restare aggiornati, come nell’API precedente: non viene introdotta atomicità dell’argomento in uscita. Il controllo è puntuale e non è un certificato continuo di una SP-curve, di Newton globale, dell’iniettività o della topologia.

## Verifiche

| Verifica | Esito e perimetro |
| --- | --- |
| Sei gruppi nuovi sulla baseline | 2/6 PASS: falliscono residui grandi di cilindro, cono/sfera, toro U/V e stime periodiche non finite. |
| Sei gruppi nuovi sulla candidata | 6/6 PASS; positivi con ±24giri, Plane e seam BSpline chiuso non periodico restano accettati. |
| Sanitizer mirati | 6/6 PASS con ASan/UBSan e rilevazione leak attiva; una TU produzione, nuova TU test e main strumentati, resto kernel Release. Non certifica la memoria dell’intera app. |
| App completa e quattro target kernel | PASS; app costruita nel primo passaggio (21.45s), completamento app/target nel secondo (3.72s), compilazioni seriali. |
| Quattro suite CTest pertinenti | 4/4 PASS senza skip in 39.73s: nuova inversione UV, mapping nonPlane, sheet join e istanze. |
| Audit indipendenti | Due letture statiche del diff e dei chiamanti, senza blocker; nessun runtime attribuito agli agenti. |
| Architettura e conservazione | PASS: 795 nodi, 11651 relazioni, 403 TU; hash sorgenti/configurazione/HTML e collegamenti nei due versi verificati;33 pacchetti precedenti preservati. |

Il primo comando di build ha completato l’app ma il processo Make, avviato prima della rigenerazione, non riconosceva ancora il nuovo target. Una seconda invocazione sui Makefile aggiornati ha completato tutti i target; log del tentativo iniziale conservato. Non è stato modificato il codice per questo passaggio.

I test sulle grandi fasi verificano che ogni successo rispetti il residuo finale: consentono un eventuale risultato valido ottenuto da un’altra libm, senza imporre un rifiuto universale. I positivi richiedono esplicitamente successo e ramo vicino alla stima, impedendo una correzione che rifiuti sempre. La prova del seam BSpline è ripresa come test kernel indipendente dalla TU storica che richiede OCCT; non viene dichiarato eseguito il confronto OCCT. I conteggi dei sei nuovi casi e del loro CTest si sovrappongono.

## Punti ancora aperti

Il riporto manuale nel percorso `nearPole` di `fk_pcurve.cpp` può bypassare `invertPoint`: è un rischio statico distinto, senza nuovo riproduttore runtime in questa consegna. Anche i chiamanti che ignorano il booleano richiedono una verifica propria; il controllo finale non li corregge automaticamente.

Per il carrier rispetto all’intera mappa è conservata una proposta di riproduttore C3 con piccolo rigonfiamento finito tra le sonde, tramite `mapBodyByFunction`. Non è stato compilato o eseguito in questo intervento e non viene contato come nuovo bug pubblico risolto. Rimangono da validare anche il candidato `arcParameter` con velocità finale nulla e le altre condizioni Newton/globali. Non si deduce l’assenza di altri difetti dai soli test passati. Il totale dei33rilievi già registrati resta invariato per questa prosecuzione K10.

[Prove e sorgenti inerti](review-k10-inversione-periodica-prove-2026-11-17/README.txt). [Memoria precedente](history/claude-prima-k10-inversione-periodica-2026-11-17.md). La CI storica non certifica le modifiche locali non committate.
