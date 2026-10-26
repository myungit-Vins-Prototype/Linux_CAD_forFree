# Metal: riuso dei buffer e richieste indipendenti

Prosecuzione Mac del branch `solver-mesh-gpu`, 26 ottobre 2026. Il filtro conservativo dei box ora riusa i buffer e ammette fino a quattro richieste indipendenti senza tenere il mutex durante allocazione, copie e attesa GPU. Nei controlli sulla GPU reale le maschere coincidono con il riferimento CPU; la booleana di controllo conserva validità, volume e conteggi topologici.

L'intervento segue le indicazioni Apple sul [riuso delle risorse persistenti](https://developer.apple.com/library/archive/documentation/3DDrawing/Conceptual/MTLBestPracticesGuide/PersistentObjects.html) e sulla [struttura di invio dei comandi](https://developer.apple.com/documentation/Metal/setting-up-a-command-structure). I command buffer rimangono monouso; si riusano i buffer di dati soltanto dopo il completamento del relativo comando.

## Implementazione e memoria

Ogni richiesta ha un lease esclusivo su tre `MTLBuffer` condivisi. Il pool sceglie il set sufficiente più piccolo; il mutex protegge solo prenotazioni, contatori e cache. Copie CPU→buffer, codifica, invio, attesa GPU e lettura della maschera avvengono fuori dal mutex. Nessuna richiesta può riusare il set ancora posseduto da un'altra.

Limiti del solo backend Metal:

- Quattro richieste attive al massimo.
- 128 MiB di buffer attivi, contando le capacità arrotondate a 4 KiB.
- 64 MiB di buffer inattivi trattenuti; set più grandi vengono rilasciati al termine.
- Slot o budget insufficienti: ritorno `false`, quindi BVH CPU preesistente.

I limiti contano le lunghezze dei buffer. Non sono un limite a RSS, memoria del driver o memoria totale dell'applicazione. `forgecad_metal_box_trim_idle()` libera solo gli inattivi; i lavori già in corso possono successivamente restituire nuovi buffer alla cache. Le statistiche riguardano il pool, non il consumo globale della GPU.

Il confronto float dei box e il suo arrotondamento conservativo non cambiano. Intersezioni e geometria B-rep restano CPU/double. Sorgenti CUDA e relativo budget restano invariati. L'attivazione rimane quella sperimentale `--gpu`; il nuovo pool non attiva Metal per default e non porta la tassellazione delle superfici sulla GPU Mac.

## Confronto isolato prima/dopo

macOS ARM64, otto CPU logiche, GPU Metal di sistema. Sorgente manuale [benchmark_metal_boxes.cpp](../tools/benchmark_metal_boxes.cpp) identica per entrambi i binari, compilata `-O3 -std=c++17`. Libreria precedente conservata dal checkout `e3db5f7`, prima di modificare il backend. Nessuna libreria di riferimento ricostruita dai sorgenti nuovi.

Tre processi nuovi per binario, ordine prima/attuale, attuale/prima, prima/attuale: sei processi e 36 casi verificati. Ogni processo misura 31 chiamate seriali o 15 gruppi di quattro richieste. Tabella: mediana delle tre mediane di processo; nei gruppi il tempo comprende tutte e quattro le richieste.

| Richieste per gruppo | Box per richiesta | Prima | Attuale | Rapporto |
| ---: | --- | ---: | ---: | ---: |
| 1 | 256 × 256 | 0,213 ms | 0,202 ms | 1,06× |
| 1 | 1.024 × 1.024 | 0,474 ms | 0,421 ms | 1,13× |
| 1 | 4.096 × 4.096 | 4,661 ms | 2,773 ms | 1,68× |
| 1 | 8.192 × 32 | 0,283 ms | 0,255 ms | 1,11× |
| 4 | 1.024 × 1.024 | 1,865 ms | 0,847 ms | 2,20× |
| 4 | 4.096 × 4.096 | 19,567 ms | 8,911 ms | 2,20× |

Intervalli delle mediane per processo sui gruppi: 1,810–1,981 → 0,738–0,893 ms per 1.024²; 18,871–26,049 → 8,480–9,331 ms per 4.096². Il lotto singolo 4.096² passa da 4,496–4,753 a 2,739–2,953 ms. Il caso 256² ha intervalli sovrapposti: nessun guadagno stabile dichiarato per i lotti piccoli.

Il picco RSS mediano dei processi di prova passa da 165,17 a 216,53 MiB. Include input, oracolo CPU, risultati, runtime, buffer e validazione; non è VRAM né memoria netta del pool. Il costo maggiore accompagna il mantenimento e la sovrapposizione dei set di buffer, senza attribuire tutto il delta a una singola componente.

Inizializzazione shader, preparazione dati e confronto di ogni byte delle maschere fuori timer. Allocazioni, copie e attesa GPU incluse; per quattro richieste sono inclusi anche risveglio e join dei thread. Nessuna build o analisi architetturale concorrente durante le misure; `caffeinate` limitato alla prova. Temperatura, frequenze e attività esterne non controllate. Gli intervalli sono campioni osservati, non intervalli di confidenza.

Sono misure del filtro Metal, non di booleane complete o dell'intero CAD. Le aperture dei documenti reali precedenti non esercitavano questo filtro: non si estendono a esse questi rapporti e non è dimostrato un miglioramento di un ordine di grandezza. Quattro richieste attive provano sovrapposizione delle chiamate, non quattro kernel eseguiti simultaneamente dall'hardware.

## Verifiche e riproduzione

Applicazione `forgecad-release/forgecad` e runner ricompilati. Suite mirata con accesso GPU: 15/15 CTest PASS, inclusi pool Metal, booleana CPU/GPU, fallback/errori/cancellazione e regressioni di anteprime, mesh differite, storia e workflow. Nel sandbox gli stessi controlli terminano con 13 PASS e due SKIP 77 per dispositivi non accessibili; non contati come verifiche hardware. Tre processi aggiuntivi del test del pool PASS con gli stessi contatori: sette set allocati, due riusi, nove chiamate completate e picco di quattro richieste.

Il test copre maschere complete, riuso senza allocazioni per lotti minori, set da circa 75 MiB ammesso attivo ma non trattenuto, richieste oltre i limiti rifiutate prima della lettura degli input, quattro lavori con maschere diverse e svuotamento concorrente. Non sono stati iniettati errori reali del driver o fallimenti di allocazione Metal. I tre warning della build applicativa riguardano fixture delle tavole preesistenti.

I tempi della booleana stampati dal test hardware iniziale sono diagnostici, con build/mappa concorrenti, ed esclusi dal confronto prestazionale.

```sh
cmake --build forgecad-release --target forgecad_metal_box_pool_tests
./forgecad-release/forgecad_metal_box_pool_tests --require-gpu

c++ -O3 -std=c++17 -I. tools/benchmark_metal_boxes.cpp \
  forgecad-release/libforgecad_compute.a \
  -framework Metal -framework Foundation -o /tmp/metal-box-benchmark
/tmp/metal-box-benchmark
```

Il test richiede accesso al dispositivo Metal. La compilazione ordinaria non avvia la mappa architetturale: aggiornata separatamente, 485 file/6.623 relazioni e 234 unità senza diagnostiche, `--check` PASS. Collegamenti test/adattatore/backend/contratto verificati nei due versi; il benchmark manuale non è un'unità CMake e ha soltanto catalogo/include, senza copertura AST garantita. HTML e JSON generati restano ignorati da Git.

Campioni, hash e limiti: [rapporto JSON](metal-box-pool-2026-10-26.json). Libreria e oggetto precedenti, binari, runner di confronto e log restano in `forgecad-release/benchmarks/metal-box-pool-2026-10-26/`. Nessun documento CAD è stato modificato o usato dal benchmark sintetico. Installazione desktop e processi dell'utente non sostituiti.
