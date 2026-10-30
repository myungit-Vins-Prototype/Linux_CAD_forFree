# Valutazioni B-spline: applicabilita a main — 27 ottobre 2026

La sola specializzazione CPU di `fk_bspline_basis.cpp` e' utile anche sul principale. Prova isolata su main `abc74a2d01e1554777538ca7902827c5daa87d39`, senza applicare le altre ottimizzazioni locali di solver-mesh-gpu.

| mouse2, rigenerazione completa | Prima | Dopo |
|---|---:|---:|
| Mediana apertura + rigenerazione | 31.006 s | 27.017 s |
| Intervallo | 29.842–31.548 s | 26.396–27.547 s |

**12.87% di tempo in meno, 1.148×** in questo scenario. Quattro processi nuovi per versione, ordine alternato, stessa copia SHA256 `a45aa16453f08abdd746c21f84e5f86188b78db8bec80e255fa10d1f86f1e24c`, qualita 1 CPU, nessun profiler/build/mappa concorrente. Lettura e loadDocument nel timer; serializzazione delle firme fuori timer. Nessun controllo di frequenza/temperatura o svuotamento della cache del filesystem.

## Isolamento e correttezza

L'implementazione originale delle basi su main e' identica al riferimento iniziale del ramo sviluppo. Copiata la libreria statica main, ricompilato il candidato con gli include/opzioni main e sostituito **solo** il membro `fk_bspline_basis.cpp.o`. Verificato l'intero elenco dei membri e il contenuto binario: tutti gli altri invariati. Nessuna cache loft, proiezione accelerata o modifica della pipeline sperimentale inclusa.

Main non espone il CLI document-mesh-benchmark: sonda esterna compilata con gli header/opzioni main e collegata agli stessi oggetti main prima/dopo, sostituendo solo il main dei test. Implementa lettura, azzeramento delle cache in memoria e loadDocument della build main; firme e controlli dopo il timer. La pipeline eager main rimane invariata. Gli iniziali tentativi con metadata/CLI non disponibili su main non sono campioni; aggiunto l'include Qt QIODevice richiesto dalla sonda.

B-rep serializzati, mesh visibile, errori e visibilita **identici in tutti gli otto processi**: 36 feature, 14 schizzi, 20 B-rep, 30958 triangoli visibili, 0 errori. Il numero di triangoli e i tempi assoluti non vanno confrontati con il ramo sperimentale: la prova valida solo prima/dopo della stessa main.

Test B-spline dedicati (curve/superfici, OCCT, formule analitiche, nodi ripetuti, buffer, concorrenza e cancellazione), compilati con header main: 14/14 test superati prima e 14/14 test superati dopo. La suite completa main non e' stata rieseguita; noto errore preesistente history_position sulla soppressione fuori ambito.

Codice C++17 CPU comune, senza dipendenza CUDA/Metal o modifica API/tolleranze/ordine aritmetico. Applicabile al codice macOS; build e prestazioni native macOS non misurate. Non dimostra 10× nell'intera applicazione o un guadagno equivalente per carichi/cache differenti.

Checkout e binari originali main verificati immutati e checkout pulito; Snapshot di benchmark SHA invariato. L’originale attuale e’ stato salvato alle 11:29:27 ed e’ diverso (SHA `7ffa64559f38820ed0f070fa233efc27a2b5ea82ee749e04817a756cb1ae92ce`): le misure si riferiscono alla copia congelata delle 10:21:27 del confronto precedente, non all’ultimo salvataggio. Nessuna scrittura di file/archivi/sessioni CAD utente, nessun backport, commit o push. Artefatti: `forgecad-linux-solver/benchmarks/bspline-main-applicability-2026-10-27` (sonda, oggetti, librerie isolate, log, campioni, prove di isolamento). [Dati](bspline-main-applicability-2026-10-27.json).
