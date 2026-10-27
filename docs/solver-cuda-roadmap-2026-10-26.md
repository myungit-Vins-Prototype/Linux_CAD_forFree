# Branch solver-mesh-gpu: avanzamento del 26 ottobre 2026

## Implementato

- Lettore cache a blocchi comune alle piattaforme, verificato su Linux: [protocollo e risultati](cache-read-optimization-2026-10-25.md).
- CUDA double: kernel specializzati per gradi (1,1), (2,2), (3,3), con generico fino al grado 15 e fallback CPU per superfici non supportate. **Specializzazioni sperimentali opt-in** con `FORGECAD_CUDA_SPECIALIZED=1`; `FORGECAD_CUDA_GENERIC=1` prevale e forza il generico. I confronti iniziali non dimostrano un guadagno stabile rispetto al vecchio binario, quindi non vengono abilitate per impostazione predefinita.
- Pool CUDA: cache inattiva di64MiB complessivi pinned/device e, nel seguito verificato, prenotazioni dei buffer attivi limitate a128MiB e4richieste contemporanee. Riuso dello slot sufficiente più piccolo, free fuori dal mutex, statistiche e trim esplicito; pressione sul budget attivo produce fallback CPU temporaneo senza disabilitare CUDA. Superfici caricate, contesto/driver e altri backend esclusi. [Contratto, prove e limiti](cuda-active-budget-2026-10-26.md).
- CTest distingue GPU assente (`SKIP`, codice 77) da errore. `--require-gpu` e `--benchmark` falliscono se CUDA non è disponibile.
- Rimappatura delle SP-curve nelle cuciture: cache locale limitata dei campioni già calcolati, con algoritmi/tolleranze e B-rep/mesh invariati. Su mouse2 ricostruzione completa35,596→31,377s,11,9% di tempo in meno in quattro processi per versione. [Diagnosi e verifiche](sew-rebuild-2026-10-26.md).
- Proprietà di massa: quadrature per faccia in parallelo con somme nello stesso ordine, percorsi seriali per corpi piccoli e worker annidati. Nel nuovo lotto mouse2, riferimento già con cache UV32,937→26,803s (18,6% di tempo in meno), B-rep/mesh invariati. [Verifiche e limiti](mass-parallel-2026-10-26.md).
- Cuciture e loft: proiezioni esaustive entro distanza con scarto conservativo dei tratti lontani e cache locale degli integrali esatti. mouse2 ricostruito **27,649→10,808 s (2,558×)** su quattro processi per versione, B-rep/mesh invariati; aperture con cache e assieme sostanzialmente invariati. Il 10× globale resta aperto, come una anomalia GL intermittente emersa nella QA. [Prove, campioni e limiti](geometry-10x-2026-10-26.md).
- Valutazioni B-spline, seguito27ottobre: gradi/ordini comuni specializzati senza modificare formule o tolleranze. Sul mouse2 aggiornato ricostruzione9,295→7,896s (**15,05% di tempo in meno**), assieme19,565→16,945s (**13,39%**), quattro processi per versione e firme invariate. Documento diverso dalle vecchie prove: non combinare i rapporti. [Risultati e limiti](bspline-evaluation-2026-10-27.md).
- Sonde permanenti sui documenti: lettura/ricostruzione con firme B-rep e mesh, contatore punti CUDA, confronto solver denso/partizionato e analisi dei gradi di libertà.

## mouse2.prt

Documento reale con 31 feature e 13 schizzi, SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617`. Tutte le prove lavorano su copie in memoria, senza salvataggio.

Nel primo processo, cinque ripetizioni alternate danno analisi aggregata mediana 9,212 → 0,498 ms (circa 18,5×). Gli schizzi agli indici 6 e 8 hanno rispettivamente 1304 e 1754 variabili: sono casi più significativi dell'assieme senza schizzi. Rango, DOF ed entità definite coincidono tra percorsi.

Il test di trascinamento sposta il primo punto disponibile di (0,01; 0,02) mm. In 11 schizzi su 13 entrambi i solver rifiutano il target e preservano la geometria; solo due modifiche riescono. La mediana aggregata 0,862 → 0,720 ms **non dimostra un'accelerazione delle modifiche valide su tutti gli schizzi**. Il guadagno chiaro riguarda l'analisi dei vincoli, non l'intera apertura o rigenerazione. Confronto coordinate entro 1e-7, struttura delle curve, pesi e nodi conservati. L'analisi è eseguita sullo schizzo originale.

Conferma finale su tre processi nuovi, ciascuno con cinque ripetizioni alternate: mediana dell'analisi **9,017 → 0,476 ms** (circa 19×); solver 0,866 → 0,728 ms, con gli stessi limiti sui target rifiutati. Originale SHA256 invariato.

Ricostruzione completa CPU/CUDA: 16 B-rep disponibili, nessun errore di feature, 49.853 triangoli visibili, 90.675 punti effettivamente valutati da CUDA. Tutti i B-rep identici; la firma completa della mesh visibile differisce. Non è stata quantificata la differenza punto per punto di questa mesh reale: non si dichiara identità grafica. I test numerici separati sulle superfici e sul loft passano. Singolo campione per percorso: preparazione 35,260 s CPU contro 35,071 s CUDA; differenza insufficiente a dimostrare un guadagno, nessuna statistica sull'intera ricostruzione. Cache originale e ricostruzione non sono lo stesso scenario (49.407 contro 49.853 triangoli).

## Verifica CUDA e protocollo

Test con superfici polinomiali/razionali, gradi comuni/misti/fino a 15, parametri interni/bordi/nodi, valori finiti e riferimento CPU; upload concorrenti; loft CPU/GPU. Aggiunti riuso, lotto da 600000 punti oltre budget e svuotamento cache. Il test senza dispositivo è stato verificato nascondendo CUDA al processo.

Microbenchmark: 24 casi (gradi 1/2/3/5, razionali/non razionali, 64/1024/16384 punti), tre processi per modalità, ordine alternato; ogni caso usa la mediana di nove chiamate dopo warmup. Trasferimenti inclusi, upload della superficie e verifica CPU fuori timer. Il rapporto conserva sia il confronto col binario precedente sia quello generico/specializzato nello stesso binario. Nessun controllo di clock, temperatura o carico esterno: niente promesse di accelerazione universale.

Riproduzione:

```sh
forgecad-linux-solver/forgecad_cuda_tests --require-gpu
FORGECAD_CUDA_GENERIC=1 forgecad-linux-solver/forgecad_cuda_tests --benchmark
FORGECAD_CUDA_SPECIALIZED=1 forgecad-linux-solver/forgecad_cuda_tests --benchmark
QT_QPA_PLATFORM=offscreen forgecad-linux-solver/forgecad_view_tests \
  --document-solver-benchmark /percorso/mouse2.prt
```

Il runner viewport ordinario usa lo stub CPU per la tassellazione. La sonda locale `benchmarks/roadmap/view-cuda` viene ricollegata con l'oggetto CUDA e il runtime NVIDIA per il confronto sul modello reale; il contatore `cuda_points` prova l'uso effettivo. Le misure sono offscreen e non comprendono framerate o interazione completa.

Confronto precedente del solo budget inattivo: tre processi per binario, kernel generico forzato in entrambi, vecchio binario conservato contro build finale. Rapporto delle mediane vecchio/nuovo sui 24 casi: minimo 0.856×, mediana 0.998×, massimo 1.009×. Il beneficio del budget è limitare la memoria inattiva, non una promessa di accelerazione; questi lotti non misurano un carico prolungato sopra budget.

Validazione finale: 14/14 CTest mirati PASS, specializzazioni opt-in PASS, assenza GPU verificata come SKIP. Mappa 478 file/6604 relazioni, 230 unità senza diagnostiche, controllo aggiornamento PASS. Collegamenti dei moduli esaminati in entrambi i versi; CUDA è catalogato con include, senza garanzia di risoluzione AST delle chiamate ai kernel.

Dati e campioni: [JSON](solver-cuda-roadmap-2026-10-26.json); log/binari locali in `forgecad-linux-solver/benchmarks/{cache-read,cuda-specialized,roadmap}`. Nessun documento originale modificato.

La firma diversa della mesh reale è stata successivamente approfondita: [confronto numerico mouse2](mouse2-mesh-cpu-cuda-2026-10-26.md). Vertici e metadati coincidono; differiscono 23 normali con scarto massimo circa 7,45e-9. Individuati anche 574 triangoli degeneri comuni ai due percorsi nella visualizzazione.

## Roadmap successiva, ancora aperta

1. **Verificato su macOS ARM64:** lettore comune, pipeline mesh, regressioni ed export CPU; confronto con cache sui due documenti locali, assieme 10,889→4,027s e flacone0,620→0,388s. [Risultati e identità dei file](macos-common-validation-2026-10-26.md). Corretto un difetto OBJ dipendente dalla contrazione FMA; suite export CPU/CUDA generico/specializzato ripetuta su Linux con successo dopo il fix.
2. **Completata una prima selezione verificata:** trascinamenti riusciti in7/13schizzi di mouse2; ricerca limitata e intervalli riportati nel [seguito](solver-cuda-export-drag-2026-10-26.md). Ulteriori classi di modifica restano aperte.
3. **Completato per STL/OBJ:** suite permanente con scrittura/rilettura e confronto CPU/CUDA su patch polinomiale e NURBS con foro; corregge anche degenerazioni/orientamenti introdotti dalla precisione dei formati. [Risultati](solver-cuda-export-drag-2026-10-26.md).
4. **Implementato e verificato il budget dei workspace CUDA attivi:**128MiB host/device e4richieste, fallback CPU temporaneo, confini e concorrenza testati. [Risultati](cuda-active-budget-2026-10-26.md). Restano aperti budget complessivo fra backend/superfici, comportamento sotto saturazione su carichi CAD prolungati e altre GPU. Specializzazioni ancora opt-in: abilitarle per default solo con vantaggio ripetibile.
5. **Completato un primo intervento sulle componenti connesse:** riordino e Cholesky a banda, ripiego denso e verifica indipendente. Catena512segmenti291,823→1,692ms (172× sul solo solver), mouse2 sostanzialmente invariato0,441→0,442ms. [Protocollo, due lotti e limiti](solver-connected-2026-10-26.md). Verificato anche un primo intervento sulla costruzione geometrica: [riuso dei campioni UV nelle cuciture](sew-rebuild-2026-10-26.md),11,9% di tempo in meno nella ricostruzione reale di mouse2. Verificato anche il [parallelismo delle proprietà di massa](mass-parallel-2026-10-26.md), mantenendo ordine numerico e tolleranze. Restano gruppi larghi, analisi del rango, altre fasi geometriche e rendering da misurare separatamente. Non è una fattorizzazione sparsa generale né un trasferimento del kernel esatto alla GPU.
6. **Completato su Linux:** filtro delle faccette viewport collassate dopo conversione float, condiviso da scena e anteprime. Su mouse2 rimossi soltanto i 574 degeneri, triangoli validi e 16 B-rep invariati; sei processi CPU/CUDA e 27 CTest PASS. [Risultati](viewport-degenerate-mesh-2026-10-26.md). Nel merge Metal confermati anche test sintetici Mac e QA Cocoa/OpenGL sul flacone; il confronto numerico mouse2 resta Linux.

7. **Proseguito verso il 10× applicativo:** nuovo intervento sulle [cuciture e lunghezze del loft](geometry-10x-2026-10-26.md), 2,558× sulla ricostruzione di mouse2 rispetto al checkpoint precedente. Restano quadrature/derivate del loft, volume, raccordi, lettura/mesh dell'assieme e riproduzione dell'anomalia GL; il guadagno non si estende automaticamente alle aperture con cache. Nessuna accelerazione globale 10× dichiarata.

8. **Completato un seguito sulle valutazioni:** [specializzazioni CPU B-spline](bspline-evaluation-2026-10-27.md),121casi kernel e39CTest pertinentiPASS, QA nativa modifica/anteprima e32processi di confronto prima/dopo. Nessun cambiamento CUDA/Metal o a main. Restano costi di loft, proiezioni/booleane e raccordi; obiettivo10× globale aperto.

Prosecuzione specifica Mac: [pool dei buffer Metal e richieste indipendenti](metal-box-pool-2026-10-26.md) implementati e verificati sulla GPU reale. I gruppi di quattro richieste sintetiche migliorano circa2,2×; nessuna accelerazione dell'intero CAD dichiarata. I budget attivo/inattivo descritti nel rapporto valgono soltanto per Metal e sono distinti da quelli dei workspace CUDA del punto4; resta aperto il budget aggregato fra backend e superfici.

Checkpoint Mac e Linux uniti sul branch, preservando entrambi i lavori. Applicazione compilata nelle build separate; installazioni desktop invariate. Verifiche della versione combinata e pubblicazione finale sono registrate in CLAUDE.md.

Correzione trasversale già verificata su Linux: [rilascio RAM dopo ritorno alla tassellazione bassa](mesh-memory-cycle-2026-10-26.md). Backport pubblicato anche su main in `d513fb8`; sul branch di sviluppo resta incluso nel lavoro locale insieme al nuovo budget CUDA.

Correzione comune successiva: [specchi storici e riparazione dei riferimenti](pattern-history-repair-2026-10-26.md), compilata e verificata su sviluppo e main. Rigenerare preserva il contratto dei vecchi aggregati; nuove ripetizioni ancora separate, scelta in modifica sullo stato precedente della storia.
