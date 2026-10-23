# Viste interrotte delle tavole 2D

Nell'editor della tavola selezionare una vista, poi **Inserisci → Vista interrotta…** oppure lo stesso comando nella toolbar o nel menu contestuale. Il dialogo mostra un'anteprima e permette di impostare:

- direzione orizzontale X o verticale Y del piano della vista;
- inizio e fine dell'intervallo da nascondere, in coordinate della vista e millimetri del modello;
- distacco tra le parti, in millimetri sul foglio, indipendente dalla scala.

Entrambi i limiti devono essere interni alla geometria proiettata. L'intervallo deve essere crescente e la sua ampiezza alla scala scelta deve superare il distacco, ammesso da 0,5 a 100 mm. Per rimuovere il taglio disattivare **Interruzione attiva** e confermare. Annulla e Ripristina comprendono inserimento, modifica e rimozione; Cancel non modifica il foglio.

Una barra lunga 100 mm, con intervallo 30–70 mm e distacco 6 mm, alla scala 1:1 appare lunga 66 mm. La quota completa resta 100 mm. Alla scala 2:1 appare lunga 126 mm e il distacco resta 6 mm.

Il ritaglio riguarda la presentazione: bordi visibili e nascosti, contorni apparenti, facce ombreggiate, centri e assi. Due linee spezzate indicano l'interruzione. I segni non diventano curve né riferimenti quotabili. Le curve esatte e i riferimenti topologici rimangono integrali; la selezione e lo snap usano le parti mostrate. Gli estremi delle quote e le linee di richiamo usano la mappatura del foglio; i testi conservano la loro posizione sul foglio. Se un aggancio cade nel tratto nascosto, il testo è segnalato da verificare e la sua geometria non viene disegnata; il valore numerico associativo resta disponibile. Anche il PDF segnala queste quote e questi richiami.

Se il modello non è disponibile, il foglio conserva il metadato e segnala l’interruzione non verificabile e i riferimenti mancanti; non inventa una geometria sostitutiva.

Il primo incremento supporta una sola interruzione per vista proiettata senza viste dipendenti, comprese le ausiliarie terminali. Sezioni e dettagli non sono interrompibili. Per creare una vista dipendente bisogna prima rimuovere l'interruzione dalla madre. Gli assi possono essere modificati dal loro dialogo; le maniglie grafiche di prolungamento sono disabilitate sulla vista interrotta.

Cambiare scala o aggiornare il modello conserva l'intervallo in coordinate modello e il distacco sul foglio. Se i limiti non risultano più interni alla geometria o la nuova scala non permette il distacco, l'operazione viene rifiutata senza sostituire la vista precedente: modificare o rimuovere prima l'interruzione. La visibilità viene calcolata sulla vista completa prima del ritaglio; questo incremento non riduce il costo HLR né certifica le prestazioni su grandi tavole.

Il foglio usa lo schema JSON 14 soltanto quando contiene un'interruzione. I precedenti schemi restano leggibili. Il contenitore `.drw` e il formato del modello `.prt` non cambiano per questa funzione. Le versioni precedenti non leggono il foglio con schema 14. La modalità sola lettura protegge il metadato; l'esportazione PDF resta disponibile.

La regressione `forgecad_drawing_break_tests` verifica mappatura X/Y e scale, separazione dei percorsi, quote originali, ombreggiatura trasparente, schema e rifiuti atomici, aggiornamento del modello, Undo/Redo, dialogo, sola lettura ed esportazione PDF. Risultati effettivi e limiti delle verifiche sono registrati in `CLAUDE.md`.

Ottimizzazione del ritaglio: i percorsi interamente esterni all’interruzione vengono conservati o traslati senza convertirli in polilinee; quelli interamente interni vengono scartati. Solo i percorsi che attraversano i limiti richiedono il taglio per segmenti, senza allocazioni temporanee per ciascun segmento. I sottopercorsi distinti restano separati anche quando condividono un estremo. Gli aggiornamenti numerici ravvicinati dell’anteprima vengono raggruppati dopo 30 ms di pausa; durante l’attesa la conferma è disabilitata. Questo riduce il lavoro del ritaglio, non quello del calcolo HLR iniziale.
