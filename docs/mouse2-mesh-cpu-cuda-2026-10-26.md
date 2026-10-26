# mouse2: confronto numerico delle mesh CPU/CUDA

La firma diversa osservata nel confronto precedente è spiegata da **23 campioni di normale su 149.559**: scarto massimo **7.45058059692e-09**, angolo massimo **4.25981272088e-07 gradi**. I vertici coincidono nei bit. Non è stata modificata la geometria produttiva per ottenere questo esito.

Due processi CPU, due CUDA generici e due con specializzazioni abilitate, nuovi e sequenziali, ricostruiscono `mouse2.prt` a qualità 1. Tutti concordano su 31 feature, 13 schizzi, 16 B-rep disponibili, una feature visibile e 49.853 triangoli. I 16 B-rep, la visibilità e gli errori sono identici; nessun errore di feature. Ogni processo GPU elabora effettivamente 90.675 punti. Il secondo processo CPU riproduce esattamente firma e snapshot del primo; i quattro processi CUDA producono tra loro lo stesso snapshot.

| Dato della feature visibile 30 | Campioni | Differenze CPU/CUDA |
|---|---:|---|
| Vertici per triangolo, con ripetizioni | 149.559 | Nessuna, neppure nei bit |
| Normali per vertice | 149.559 | 23; massimo 7.45058059692e-09 |
| Punti delle polilinee di spigolo | 6.199 su 43 polilinee | Nessuna, neppure nei bit |
| Etichette delle facce | 19 | Nessuna, neppure nei bit |
| Triangoli associati alle facce | 49.853 su 19 facce | Mappa identica |
| ID e relazioni facce/spigoli | Intero insieme esportato | Identici |

Le normali diverse appartengono alle facce 0/7/15/16, rispettivamente 5/6/6/6 campioni. Sulle facce 0 e 7 lo scarto massimo è circa 3,64e-12, sulle facce 15 e 16 circa 7,45e-9. Sono variazioni numeriche minime compatibili con l'arrotondamento dei percorsi CPU/CUDA; la causa immediata della diversa firma completa è circoscritta alle normali. Non sono cambiati vertici, appartenenza alle facce, etichette o spigoli.

Il controllo rileva anche **574 triangoli degeneri nella mesh viewport**, già presenti nel riferimento CPU. Maschera identica con CUDA, nessuna nuova degenerazione e nessuna inversione di winding nel confronto. Il test dimostra concordanza tra i percorsi; non dichiara la mesh priva di difetti. Questo limite della visualizzazione resta distinto dalle correzioni STL/OBJ dell'intervento precedente.

Gli snapshot numerici opzionali `--mesh-snapshot-dir` contengono array float32/int32 little-endian, conteggi uint64 e firma di schema `FCMSH001`. Sono file diagnostici esterni al documento, prodotti dopo il timer dell'apertura, con destinazioni esistenti rifiutate. Coprono vertici, normali, triangoli-facce, polilinee/ID, etichette, relazioni facce-spigoli e matrici; le basi instanziate annidate sono esplicitamente rifiutate. Il formato CAD rimane invariato.

Il comparatore controlla tutti i campioni per indice, con conteggi e mappa delle facce uguali, e produce anche risultati per faccia. Distingue differenze nei bit (incluso zero con segno) da differenze numeriche, misura distanze/angoli e segnala variazioni di metadati o degenerazioni. Non calcola una distanza continua tra superfici, non verifica volume/chiusura o tutti i possibili modelli. Soglie diagnostiche fissate prima dell'esecuzione: 1e-5 mm per le coordinate e 1e-5 come distanza tra vettori normali; nessuna tolleranza del kernel modificata.

La sonda CUDA riusa gli oggetti del runner, sostituendo soltanto lo stub con l'oggetto CUDA e il runtime NVIDIA. Il ricollegamento automatico richiede CMake Unix Makefiles; altri generatori possono usare `--cuda-binary` con una sonda compatibile. Il runner ordinario resta CPU per la tassellazione. La modalità con specializzazioni abilitate conserva il generico per i gradi non supportati; non è isolato il numero di punti di ciascun kernel specializzato.

Verifiche finali: **19/19 CTest PASS**, incluso il test del comparatore con nove casi (coordinate/normali, zeri con segno, soglie, topologia, inversioni/degenerazioni, metadati, file troncati e conteggi non validi). `--require-cuda` senza dispositivo restituisce77 prima della lettura del file, sia sullo stub CPU sia sulla sonda CUDA con dispositivo nascosto; directory snapshot mancante rifiutata. Mappa aggiornata: 482 file, 6622 relazioni, 231 unità senza diagnostiche; limiti AST per Python/CUDA documentati nella mappa.

I tempi accessori nel rapporto locale **non sono campioni prestazionali**: il generatore architetturale lavorava durante i primi processi. Non si deduce alcuna accelerazione da questa prova, né framerate o risultati su macOS/altre GPU.

Riproduzione, dopo la compilazione di `forgecad_view_tests` e `forgecad_cuda_tests`:

```sh
python3 tools/compare_document_mesh_backends.py /percorso/mouse2.prt \
  --build-dir forgecad-linux-solver --output /tmp/mouse-mesh-nuovo \
  --repeats 2 --specialized
ctest --test-dir forgecad-linux-solver -R forgecad_numeric_mesh_comparator_tests --output-on-failure
```

Richiede Python e NumPy; CTest registra il test Python se entrambi sono disponibili. Nessuna dipendenza nuova per l'applicazione. Snapshot, sonda ricollegata, campioni e log sono locali in `forgecad-linux-solver/benchmarks/mouse-numeric-mesh-2026-10-26/`; [rapporto versionabile JSON](mouse2-mesh-cpu-cuda-2026-10-26.json).

Originale invariato, SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617`. Ricompilato il runner; nessuna modifica produttiva in questo intervento, nessuna sostituzione dell'app desktop e nessun commit/push. Prossimo punto individuato: rimuovere dalla visualizzazione le faccette che collassano dopo conversione float, mantenendo coerenti picking e mappa delle facce; ancora da implementare e verificare. Restano inoltre aperti macOS, altre GPU e le componenti grandi connesse del solver.
