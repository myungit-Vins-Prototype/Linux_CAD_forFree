# Booleane complesse: suddivisione delle pezze Bézier

Data: 28 ottobre 2026. Branch: solver-mesh-gpu. Modifica locale CPU condivisa, verificata su Linux; nessuna compilazione macOS, modifica CUDA/Metal, integrazione main o pubblicazione in questo intervento.

## Metodo e limite

`splitPatch` compariva nel 19,52% dei 7285 campioni CPU del profilo CT0578 (caricamento e booleana Sweep10, thread aggregati; non percentuale del tempo reale). Ricostruiva curve NURBS temporanee per ogni riga/colonna e ne estraeva i tratti. Ora le pezze Bézier clamped vengono divise direttamente nelle coordinate omogenee, mantenendo l'ordine delle operazioni dell'inserimento del nodo. Si costruiscono e validano le due superfici finali; altre forme e intervalli non divisibili usano il percorso originale. Nessuna modifica di tolleranze, semi di intersezione, numero di suddivisioni, ordinamento topologico o cache persistenti.

## Misure

Sweep di un cerchio di raggio 0,5 su un'elica (raggio 4, passo 1,5, mezzo giro) contro cilindri di raggio 3,9/4,0. Sei operazioni per processo: unione, intersezione e differenza per entrambi i cilindri. Tre processi per versione/configurazione, ordine alternato, un thread e otto thread espliciti. Timer della sola booleana, costruzione degli operandi e controlli fuori dal timer; tutti i 72 risultati B-rep identici anche fra configurazioni di thread e topologia valida. Nessun altro test/build/profiler durante le misure; frequenza CPU, temperatura e altri carichi del sistema non controllati.

La tabella riporta la mediana della somma dei sei tempi per processo (non una singola operazione):

| Thread | Prima | Dopo | Tempo risparmiato |
| --- | --- | --- | --- |
| 1 | 2.360 s | 2.215 s | 6.1% |
| 8 | 1.207 s | 1.148 s | 4.9% |

I singoli casi migliorano del 3.4–7.0%. Tre ripetizioni non permettono inferenze statistiche forti; non si generalizza il risultato a ogni booleana.

CT0578, booleana effettiva Sweep10: tre processi/versione alternati, mediana 18.369→18.263s. Intervalli 18.280–18.834s e 18.110–18.951s sovrapposti: **nessuna accelerazione dimostrata su questa sola booleana**. Utensile con 8 facce e risultato con 269 facce identici bit per bit. Il candidato differisce dalla sorgente finale soltanto per l'include esplicito di fk_cancel.h; oracolo e rigenerazioni sono stati ripetuti sulla libreria produttiva finale.

Conferma applicativa con tre coppie ulteriori di processi prima/dopo, comando reale Rigenera tutto, stessa qualità 1 e confronto completo delle firme geometriche e mesh:

| Scenario CT0578 | Prima mediana (min–max) | Dopo mediana (min–max) |
| --- | --- | --- |
| Caricamento con cache | 5.062 (5.031–5.064) s | 3.982 (3.973–4.007) s |
| Rigenera tutto | 27.087 (26.962–27.420) s | 25.568 (25.220–25.636) s |

Il caricamento con cache comprende ricostruzioni interne per la madre della ripetizione; esclude lettura del file e presentazione OpenGL/albero. Rigenera tutto comprende Undo, geometria e mesh, esclude caricamento iniziale e presentazione. Questi dati vanno distinti dalla sola booleana finale Sweep10, dove non è dimostrato il beneficio. Ordine alternato, misure sequenziali senza altri lavori diagnostici, tre campioni per versione: nessuna promessa identica per altri documenti.

Microprova bicubica razionale: 20.000 divisioni per lotto, cinque coppie alternate nello stesso processo. Mediana 0.094992→0.010622s, **8.94× sul solo splitPatch**. Il confronto usa il codice precedente e quello nuovo compilati con la stessa configurazione Release; checksum utilizzato. Non e un 10× dell'applicazione, e questo fattore non va moltiplicato per i guadagni storici del branch.

## Correttezza e build

- Oracolo vecchio/nuovo: 170 casi, 660.228 valori double bit identici, gradi 1–31, razionali/polinomiali, scale 1e-6/1/1e6, micro domini, suddivisioni ripetute, superfici multi-tratto e non clamped. Ripetuto con la libreria produttiva finale.
- Nuove regressioni permanenti: conservazione di posizione e derivate delle pezze; 12 booleane che lasciano lamelle spline di 0,01/0,001/0,0001 mm, con volume atteso dal prisma originale e topologia valida. Entrambe passano anche sul kernel precedente.
- Selezione kernel: 235/237 PASS: i soli fallimenti restano BlendVanishingAtTangentMitreEnd e BlendArcsMeetingSegments, già riprodotti prima di questo intervento. Nessuna soglia cambiata o test disabilitato. Include intersezioni OCCT, booleane curve/tangenti, sweep, gusci, raccordi, concorrenza e cancellazione.
- CTest: 48/48 PASS; applicazione completa Release Linux, runner viewport e dipendenze compilati.
- Rigenera tutto su copie congelate di mouse2, CT0578 e assieme: B-rep, mesh completa, facce, triangoli, errori e visibilità identici al riferimento precedente. Mouse2/CT: 0 errori; assieme: stessi 4 importati senza sorgente. Questi tre processi finali sono controlli funzionali, non nuove statistiche di velocità.
- Mappa: 499 file/6765 relazioni/236 TU, 0 diagnostiche, 81.084 siti verificati, confini controllati nei due versi, include separati dalle chiamate e vista ispezionata.

Eseguibile aggiornato: `forgecad-linux-solver/forgecad`. Nessuna nuova prova visiva dell'applicazione o compilazione Mac dichiarata. Originali CAD non modificati; le sonde eseguono forkDocumentIdentity prima del caricamento nel controller.

## Riproducibilità e seguito

[Dati completi](boolean-patches-2026-10-28.json), SHA di sorgenti/binari, campioni e intervalli. Artefatti locali ignorati: `forgecad-linux-solver/benchmarks/boolean-patches-2026-10-28`; include sonde, sorgente precedente, libreria, script, risultati binari, log e vista della mappa.

Prossimi candidati: proiezioni e taglio degli spigoli in splitAtPoints, calcoli di Bernstein e riuso delle suddivisioni tra coppie di facce. Misurare separatamente i casi quasi tangenti e i dettagli vicini alla risoluzione, senza eliminare micro superfici o allargare tolleranze per guadagnare tempo. L'obiettivo 10× globale rimane aperto.
