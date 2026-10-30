# Metal: griglia 2D e priorità di prestazione

28 ottobre 2026, branch `solver-mesh-gpu`, riferimento `cb9e4a9`.

## Ambito del cambiamento

Il filtro Metal delle coppie di box usa una griglia bidimensionale da 262.144 coppie, con almeno due box per insieme: x individua il box del secondo insieme, y quello del primo. Si eliminano divisione e resto per coppia, mantenendo ordine della maschera, confronti float, espansione conservativa double→float, modalità matematica sicura e verifica double dei candidati sulla CPU. Il threadgroup segue la larghezza SIMD della pipeline ed è limitato a 256 thread e alla capacità della pipeline. `dispatchThreads` copre le code irregolari. Per i lotti più piccoli e quelli con un solo box su un asse resta il kernel lineare precedente: le misure preliminari non giustificavano il nuovo percorso in questi casi. La soglia è una scelta misurata su questa macchina, da rivalutare su altre GPU.

Pool, budget, sincronizzazione e API rimangono quelli precedenti. Il cambiamento è specifico di `gpu_box_support.mm`; i backend CUDA/CPU e la geometria esatta non cambiano. L'attivazione resta opt-in con `FORGECAD_GPU_BROAD_PHASE=1`.

Riferimento tecnico: Apple, [Calculating threadgroup and grid sizes](https://developer.apple.com/documentation/metal/calculating-threadgroup-and-grid-sizes). La scelta del threadgroup va comunque misurata sulla macchina reale.

## Misure e verifiche

Apple M1, 8 CPU logiche, 16 GiB RAM, macOS 27.0.1 (26A434), GPU Metal di sistema. Libreria precedente conservata prima delle modifiche; stessa sorgente C++17/O3 per i due benchmark, librerie CMake Release. Otto processi nuovi per il filtro e otto per la booleana, quattro per variante, ordine ABBAABBA. Nessuna compilazione, mappa, test o profilazione concorrente; frequenza, temperatura e carico esterno non controllati.

Ogni processo del filtro misura 31 chiamate singole o 15 gruppi di quattro. Preparazione dati, compilazione shader, warmup e oracolo fuori timer; gestione buffer, copie, invio, attesa e copia della maschera inclusi. Nei gruppi sono inclusi risveglio e join dei thread. Tabella: mediana delle quattro mediane di processo; i gruppi comprendono tutte e quattro le richieste.

| Richieste | Box | Dati | Prima | Misto finale | Prima/dopo |
| ---: | --- | --- | ---: | ---: | ---: |
| 1 | 256×256 | sparsi | 0,251 ms | 0,270 ms | 0,93× |
| 1 | 512×512 | casuali | 0,329 ms | 0,349 ms | 0,94× |
| 1 | 1024×1024 | sparsi | 0,571 ms | 0,432 ms | 1,32× |
| 1 | 4096×4096 | sparsi | 4,818 ms | 2,483 ms | 1,94× |
| 1 | 8192×32 | sparsi | 0,423 ms | 0,304 ms | 1,39× |
| 1 | 2047×2049 | casuali | 2,116 ms | 1,151 ms | 1,84× |
| 1 | 4096×4096 | tutti sovrapposti | 7,009 ms | 4,624 ms | 1,52× |
| 1 | 4096×4096 | casuali | 6,262 ms | 4,002 ms | 1,56× |
| 4 | 1024×1024 | sparsi | 1,621 ms | 0,878 ms | 1,85× |
| 4 | 4096×4096 | sparsi | 12,113 ms | 8,707 ms | 1,39× |
| 4 | 1023×1025 | casuali | 1,985 ms | 1,212 ms | 1,64× |

Lotto singolo 4096² sparso: intervalli osservati 4,275–5,259 → 2,451–2,502 ms; denso 6,182–7,091 → 4,148–5,137 ms; casuale 5,060–7,753 → 3,876–4,113 ms. Nel caso 256² il tempo mediano è superiore del 7,5% pur mantenendo il kernel lineare, con intervalli sovrapposti 0,228–0,294 e 0,235–0,286 ms. Anche al confine 512² la mediana è superiore del 6,1%, intervalli 0,297–0,367 e 0,293–0,381 ms: nessun beneficio stabile nei casi piccoli, nessuna causa dello scarto accertata. Per i casi con un solo box su un asse gli intervalli si sovrappongono. Campioni completi di tutti i quindici casi nel [JSON](metal-grid-2026-10-28.json); sono intervalli osservati, non intervalli di confidenza.

**Booleana completa su prismi a 256 lati:** sette chiamate per backend e processo, ordine CPU/Metal alternato. CPU: 76,583→75,787 ms; Metal: 78,014→73,951 ms. Intervalli Metal 73,726–78,523 e 72,966–75,146 ms sovrapposti. Questo caso usa il kernel lineare conservato, quindi lo scarto non dimostra un beneficio del nuovo kernel 2D e non viene presentato come accelerazione CAD. Tutti i 112 B-rep temporizzati coincidono byte per byte con il riferimento del proprio backend; impronta e dimensione dei riferimenti coincidono anche fra CPU/Metal e le due versioni. Nessun documento CAD completo, framerate o interazione nativa misurato qui.

Adottato il percorso misto per il beneficio ripetibile del filtro sui lotti grandi; attivazione ancora sperimentale e opt-in. Le sei prove esplorative iniziali e i sedici processi della variante tutta 2D sono conservati separatamente e non sostituiscono i campioni finali. Nessun fattore di questa tabella va applicato alla rigenerazione intera.

La regressione Metal aggiunge undici forme, incluse 1×1, 1×257, 257×1, 3×517, 513×129, 511×513, 512×512, 512×513, 1023×1025, 65537×1 e 1×65537. I tre casi intorno a 512² esercitano le due parti e il confine della selezione. Box casuali indipendenti sui tre assi, contatto esatto, maschera completa contro CPU e sentinelle sul buffer destinazione; restano riuso, budget, concorrenza e trim. Nessun test impone soglie temporali.

`tools/benchmark_metal_boxes.cpp --extended` conserva i sei carichi originali e aggiunge nove casi con forme irregolari, tutti i box sovrapposti e dati casuali. Ogni risultato viene confrontato con l'oracolo CPU fuori timer. `forgecad_gpu_broad_phase_tests --benchmark` aggiunge sette booleane per backend CPU/Metal in ordine alternato; ogni B-rep è confrontato byte per byte con il riferimento dello stesso backend fuori timer. Le impronte stampate permettono il confronto fra binari.

## Dove cercare il prossimo guadagno

| Percorso | Evidenza attuale | Prossimo intervento da valutare |
| --- | --- | --- |
| Proiezioni, intersezioni e suddivisione delle superfici | I profili Linux dell'assieme e CT mostrano ancora costi rilevanti qui; non sono profili Metal o quote wall | Riutilizzare suddivisioni e interrogazioni per superficie, certificare gruppi di campioni, ridurre il lavoro prima di Newton; preservare tolleranze e dettagli piccoli |
| Preparazione mesh e anteprime Mac | `makeTessellationAccelerator()` usa ancora lo stub CUDA su Mac; Metal non valuta le superfici | Misurare i lotti effettivi e progettare un percorso Metal per la visualizzazione con controllo dell'errore e ripiego CPU. L'attuale contratto di valutazione è double e serve anche l'export: un porting float richiede una scelta esplicita e verifiche dedicate |
| Filtri geometrici su GPU | Metal attuale filtra solo box di facce per booleane fra 65.536 e 16.777.216 coppie; i vecchi documenti reali non lo esercitavano | Valutare lotti di box più fini e liste compatte di candidati, confrontandoli con il BVH CPU. Una matrice completa contiene spesso quasi soltanto zeri e richiede ancora una scansione CPU |
| Viewport, selezione e invio comandi | Renderer OpenGL e picking separati dal backend compute Metal; nessun nuovo profilo di frame in questo intervento | Misurare rotazione/zoom/hover su modelli grandi prima di scegliere interventi su draw call, buffer o letture sincrone |
| Solver | Già ottimizzato sul branch; sui documenti misurati molte chiamate sono inferiori al millisecondo | Concentrarsi su gruppi connessi larghi solo quando un profilo dello schizzo ne dimostra il costo |

Le percentuali dei vecchi profili CPU aggregati non permettono di stimare direttamente il risparmio di tempo dell'applicazione. Le misure di [proiezioni certificate](general-performance-methods-2026-10-27.md) e [suddivisione delle pezze](boolean-patches-2026-10-28.md) sono riferimenti Linux di checkpoint specifici, da distinguere da questo confronto nativo Mac.

## Riproduzione

```sh
cmake --build forgecad-release --target forgecad forgecad_view_tests \
  forgecad_gpu_broad_phase_tests forgecad_metal_box_pool_tests -j 4
./forgecad-release/forgecad_metal_box_pool_tests --require-gpu
./forgecad-release/forgecad_gpu_broad_phase_tests --benchmark
c++ -O3 -std=c++17 -I. tools/benchmark_metal_boxes.cpp \
  forgecad-release/libforgecad_compute.a -framework Metal -framework Foundation \
  -o /tmp/metal-grid-benchmark
/tmp/metal-grid-benchmark --extended
```

I controlli hardware richiedono accesso al dispositivo: uno SKIP nella sandbox non è un PASS Metal. Artefatti locali in `forgecad-release/benchmarks/metal-grid-2026-10-28`, con libreria precedente conservata prima delle modifiche, sorgenti, binari e campioni.

Applicazione `forgecad-release/forgecad`, runner viewport e test aggiornati compilati. Suite finale 10/10 CTest PASS, 7,11 s, zero SKIP: filtro Metal e booleana, fallback/errori/cancellazione, pool/griglia, stub CUDA, algebra/partizioni solver, mesh e anteprime. Il primo lotto aveva un Not Run dello stub CUDA per target assente: compilato e verificato separatamente, poi incluso con successo nella suite finale. Test del pool finale PASS anche contro la libreria precedente; nuova versione 20 chiamate completate e picco di quattro richieste. Nessuna nuova suite kernel completa o QA visiva dei documenti dichiarata.

Mappa aggiornata: 499 file, 6764 relazioni, 239 TU, zero diagnostiche; `--check` PASS, 81207 siti entro le righe, sei confini verificati nei due versi e dettaglio Metal ispezionato nel browser. Include, chiamate C++ e riferimenti a funzione restano distinti. I messaggi Objective-C Metal e il testo degli shader non sono archi C++ risolti; il benchmark manuale ha catalogo/include senza AST CMake. HTML e JSON della mappa restano ignorati da Git.

Installazione desktop e CAD originali non modificati; nessun commit/push. Risultati limitati a questo Mac, con soglia e threadgroup da rivalutare su altre GPU.
