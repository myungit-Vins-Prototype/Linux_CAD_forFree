# Confronto Linux solver/mesh/CUDA — 25 ottobre 2026

Sul branch `solver-mesh-gpu` il nuovo percorso conserva i risultati del riferimento.
Sull'assieme locale la mediana migliora del **3,18% nella ricostruzione** e del
**16,22% nell'apertura con cache**. Con soli tre campioni per modalità e intervalli
sovrapposti, queste misure non dimostrano un'accelerazione stabile o universale.
Il filtro GPU non viene esercitato dall'assieme e non spiega queste differenze.

## Documento e macchina

Usato `/home/myungit/Documenti/2550_ASSIEME-ST5674-COMPLETO.prt`, formato 50,
386.446.418 byte, SHA256 `c68ee31e378a177320d82d98d5e347860b26f1944c6d32911ec53fbd60c354af`.
Contiene **1204 feature, zero schizzi**, 1200 B-rep in cache e 1191 mesh preparate.
La copia in `Documenti/File_Esempio` contiene 1200 feature; non è stato individuato
un documento con 1299 corpi. Entrambe le impronte originali sono rimaste invariate.
Il documento del Mac aveva un'altra impronta e una cache oltre il limite di lettura:
i tempi assoluti delle due macchine non sono un confronto controllato.

Xeon W-10885M (8 core/16 thread), 32 GiB RAM, Quadro RTX 3000 6 GiB,
driver 615.78.08, CUDA 13.4.92, GNU C++ 16.2.1. Build Release separata
`forgecad-linux-solver`, CUDA architettura 75. Il sandbox non espone NVIDIA;
le prove CUDA sono state eseguite fuori dal sandbox, senza modificare i driver.

## Risultati sul documento

Tempi in secondi: lettura + caricamento/ricostruzione. Picco RSS in MiB,
comprensivo della validazione fuori dal timer.

| Scenario | Modalità | Mediana s | Min–max s | RSS mediano MiB |
|---|---|---:|---:|---:|
| Ricostruzione completa | Riferimento eager | 24.523 | 23.664–24.693 | 2960.7 |
| Ricostruzione completa | Nuova pipeline | 23.744 | 23.301–23.884 | 2962.3 |
| Ricostruzione completa | Pipeline + filtro GPU | 23.942 | 23.062–24.147 | 2986.2 |
| Apertura con cache | Riferimento eager | 7.081 | 6.998–7.601 | 2960.5 |
| Apertura con cache | Nuova pipeline | 5.932 | 5.813–7.192 | 2960.5 |
| Apertura con cache | Pipeline + filtro GPU | 6.081 | 5.825–6.225 | 2985.1 |

Tutte le 18 esecuzioni producono 1200 B-rep, 1191 feature visibili e
11.736.766 triangoli visibili. Entro ciascuno scenario coincidono le firme SHA256
dei B-rep serializzati e delle mesh visibili, comprese normali e associazioni
topologiche; coincidono anche visibilità ed errori. Restano quattro record importati
vuoti agli indici 274/281/288/295, già presenti nel confronto Mac. Questo controllo
non sostituisce `checkBody` o una verifica di massa su tutti i corpi.
Il riferimento prepara 1200 mesh, la pipeline 1191. Memoria di picco sostanzialmente
uguale, circa 2,9 GiB; non è la sola memoria finale del viewport.

## Protocollo e riproduzione

`tools/benchmark_document_mesh.py` esegue nove processi nuovi per scenario,
tre per modalità, ruotando l'ordine. Qualità standard 1; profiling attivo in tutte
le modalità; nessuna build, analisi della mappa o altro benchmark dell'agente
concorrente. Frequenze, temperatura e carico esterno non controllati. Nessun
campione escluso. Il valore alto dell'ultima apertura pipeline con cache resta
incluso. File originali letti senza salvataggio o lock.

La sonda `--document-mesh-benchmark` nel runner viewport separa lettura e load;
le firme sono calcolate dopo il timer. `--inventory` legge solo il documento.
`--rebuild` invalida in memoria geometria/mesh lette dalla cache prima del load;
la lettura e decompressione iniziali restano incluse nel tempo di apertura.
Il confronto è fra i percorsi selezionabili nel medesimo binario del branch,
non fra due eseguibili compilati da revisioni diverse. Il riferimento attiva
`FORGECAD_SOLVER_DENSE=1` e `FORGECAD_EAGER_MESH=1`; la pipeline li disattiva.
La modalità GPU aggiunge `FORGECAD_GPU_BROAD_PHASE=1`.

```sh
cmake -S . -B forgecad-linux-solver -DCMAKE_BUILD_TYPE=Release -DFORGECAD_BUILD_VIEW_TESTS=ON -DCMAKE_CUDA_ARCHITECTURES=75
cmake --build forgecad-linux-solver --target forgecad_view_tests forgecad_gpu_broad_phase_tests forgecad_cuda_tests -j 4
python3 tools/benchmark_document_mesh.py /percorso/assieme.prt --build-dir forgecad-linux-solver --output /tmp/confronto-ricostruzione
python3 tools/benchmark_document_mesh.py /percorso/assieme.prt --build-dir forgecad-linux-solver --output /tmp/confronto-cache --cached
```

Usare cartelle di output nuove. Il runner interrompe il batch in caso di errore,
timeout o firme divergenti; registra impronta originale, sorgenti e binario.
La tassellazione del runner resta CPU (`FORGECAD_HAS_CUDA=0`); il filtro delle
booleane è collegato al backend CUDA. **Zero coppie GPU nel documento**, quindi
nessun guadagno CUDA misurato sul modello. L'applicazione compilata include invece
anche il backend di tassellazione CUDA, verificato dal test dedicato.

## Regressioni e misure sintetiche

16/16 CTest superati: solver partition, lazy mesh, pattern placement,
surface pattern, history suppression, boolean history, blend history,
preview performance, workflow UI, storyboard scroll, async lifetime,
sketch reference workflow, document read, GPU broad phase, CPU fallback e CUDA.
Il test esplicito `--require-gpu` passa su CUDA con 136260 coppie elaborate;
confronta anche volume e topologia della booleana con il riferimento CPU.
Le sue durate singole non vengono usate come benchmark prestazionale.

Tassellazione CUDA: errore massimo relativo dei punti 6,7e-16, normali 2,71e-14;
zero errori negli upload concorrenti; loft con 32248 triangoli sia CPU sia GPU.
Nessuna nuova prova di rendering hardware o framerate.

Solver, mediana di cinque chiamate alternate su vincoli orizzontali indipendenti:

| Vincoli | Denso ms | Partizionato ms |
|---:|---:|---:|
| 100 | 2,24559 | 0,125251 |
| 400 | 170,687 | 0,529272 |
| 800 | 1519,18 | 1,00385 |

Storia sintetica di 20 stadi: 40,5558 → 2,49604 ms, con 20 → 1 mesh,
mediana di tre caricamenti alternati. Questi casi esercitano benefici specifici;
non sono moltiplicatori applicabili al documento reale, che non contiene schizzi.

Dati numerici e impronte: [rapporto JSON](linux-solver-mesh-2026-10-25.json).
Log e firme per corpo locali in `forgecad-linux-solver/benchmarks/{rebuild,cached}`;
regressioni in `forgecad-linux-solver/validation`. Nessun CAD nel rapporto versionabile.
Applicazione aggiornata in `forgecad-linux-solver/forgecad`; per avviarla:

```sh
python3 tools/run_solver_mesh_gpu.py --build-dir forgecad-linux-solver
```
