# Prestazioni Linux e verifica CUDA del 10 ottobre 2026

Aggiornamento successivo: il caso reale Mouse ha rivelato un difetto di sincronizzazione dell’upload CUDA non coperto dalle superfici piccole di questo audit. Correzione, regressioni e copia compatta documentate in [Mouse: mesh e storyboard](mouse-mesh-storyboard-2026-10-10.md).

Il backend CUDA compila ed esegue correttamente le prove numeriche sulla Quadro RTX 3000. CPU e GPU producono la stessa mesh del loft di controllo, anche nell'esportazione STL e OBJ. La suite Linux conserva due fallimenti nei raccordi: uno nel confronto con OCCT e uno nel viewport, entrambi nel percorso CPU. Non sono state modificate geometria, tolleranze o sorgenti di produzione.

La richiesta riguarda test di prestazioni e revisione delle parti non provate nella precedente verifica macOS. `CLAUDE.md` e `code-review-2026-10-10.md` sono stati usati come contesto; le vecchie proposte non sono state considerate richieste di implementazione. Le correzioni della review già presenti nel codice non vengono presentate come problemi ancora aperti.

## Sistema e compilazione

Linux x86_64, kernel 7.2.9-2-cachyos-bore; Intel Xeon W-10885M, 8 core e 16 thread logici; RAM circa 31 GiB. NVIDIA Quadro RTX 3000, VRAM 6144 MiB, driver 615.78.08; CUDA 13.4.92, GCC 16.2.1, CMake 4.4.4, OpenCASCADE 7.9.3. Revisione Git `ed15461`, working tree inizialmente pulito.

Build Release separata in `/tmp/forgecad-linux-cuda-review`, con test viewport abilitati e architettura CUDA 75, corrispondente alla GPU rilevata. La build preesistente puntava a `/home/myungit/Documenti/Linux_CAD_forFree`: non è stata usata per le misure. Nel contesto della chat il percorso workspace era indicato come `/Users/myungit/Linux_CAD_forFree`, ma il checkout reale è `/home/myungit/Linux_CAD_forFree`.

La sandbox nasconde `/dev/nvidia*`: al suo interno `cudaGetDeviceCount` restituisce errore 100 e zero dispositivi. Fuori dalla sandbox restituisce successo e un dispositivo. Anche `prime-run` è stato verificato, sia per il test CUDA sia per OpenGL su X11/XWayland. PRIME seleziona la GPU per il rendering; non sostituisce l'accesso ai dispositivi necessario a CUDA.

Configurazione e build senza architettura esplicita riuscite, ma `nvcc -arch=native` nella sandbox segnala l'assenza di una GPU e usa l'architettura di default. In questo toolkit il cubin ottenuto è sm_75: compatibile con questa macchina, non una garanzia per una GPU diversa. Per build in CI o container specificare l'architettura destinazione.

```sh
cmake -S . -B /tmp/forgecad-linux-cuda-review \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CUDA_ARCHITECTURES=75 \
  -DFORGECAD_BUILD_VIEW_TESTS=ON
cmake --build /tmp/forgecad-linux-cuda-review -j 6
prime-run /tmp/forgecad-linux-cuda-review/forgecad_cuda_tests
```

## Verifiche CUDA e Linux

- Test numerici CUDA reali PASS: 36 superfici B-spline/NURBS, gradi U 1, 2, 3, 5, 7, 15 e V 1, 3, 4, razionali e non razionali; parametri casuali, bordi e nodi multipli. Scarto massimo dei punti relativo alla scala: **6,7e-16**; scarto delle normali: **2,71e-14**. Grado oltre il limite rifiutato.
- Loft: **32.248 triangoli** sia CPU sia GPU; scarto massimo dei vertici dalla superficie **4,92644e-8 mm** in entrambe le modalità. Elaborazione CUDA verificata con il contatore dei punti.
- Prova dedicata STL/OBJ sullo stesso loft: **32.248 triangoli STL**, **14.739 quad e 2.770 triangoli OBJ** in entrambe le modalità. Distanza massima fra i vertici float delle anteprime STL: **0**; CUDA elabora **77.600 punti per formato**. Il confronto OBJ verifica conteggi ed esito, non l'identità di tutti i byte del file. Nessun documento utente scritto.
- Test di esportazione esistente ricollegato temporaneamente con CUDA: PASS, ma **0 punti GPU**; i suoi casi non coprono la valutazione B-spline accelerata. La prova dedicata sopra colma questo limite per un loft, senza dichiarare coperti tutti gli esportatori e documenti.
- Test OpenGL reale `prime-run forgecad_view_tests --shape-analysis --gl`: **PASS**. Le altre prove GUI della suite usano Qt offscreen. Il binario viewport ufficiale usa il backend CPU per scelta esplicita del CMake.
- Backend senza toolkit e backend compilato CUDA senza dispositivi: loft valido, zero punti GPU e ritorno alla CPU. Provato il backend CPU con un probe collegato a `cuda_support_cpu.cpp`; il fallback runtime con il medesimo probe collegato a `cuda_support.cu`, dentro la sandbox.
- Chiamate CUDA con zero punti o conteggio oltre 2^28: rifiutate prima di accedere ai buffer, PASS.
- Build Linux completa PASS. Suite applicativa escluso runner kernel: **25/26 PASS**, con test CUDA eseguito realmente. Kernel: **309/310 PASS**. Complessivamente **25/27 target** superati contando il kernel; i due target falliti sono descritti sotto. Il test OpenGL e i probe dedicati sono verifiche aggiuntive, non target CTest.

Il primo tentativo CTest kernel nella sandbox è stato interrotto prima del completamento e non conta come suite valida: i test STEP/IGES richiedono temporanei in `/var/tmp`. La suite completa riportata è stata eseguita fuori dalla sandbox. Il test CUDA nella sandbox, che stampa “test saltato” ed esce con zero, non conta come verifica GPU.

## Problemi emersi nella revisione

### Test CUDA che possono riportare un falso successo

`tests/test_cuda_tessellation.cpp:62-65` restituisce 0 quando manca la GPU; `CMakeLists.txt:183` non configura un codice di skip. CTest mostra quindi PASS anche senza eseguire prove CUDA. Riprodotto nella sandbox. Usare un codice dedicato con `SKIP_RETURN_CODE`, oppure una modalità che richieda la GPU nei job destinati a verificarla.

Alle righe 92-95 e 141 gli scarti vengono accumulati con `std::max`, senza un controllo `std::isfinite`. Un NaN come secondo argomento non aumenta un massimo finito: un probe C++ verifica che `std::max(0.0, NaN)` rimane 0 e supera il controllo `<1e-13`. Inoltre vengono saltate tutte le normali GPU nulle, anche in punti regolari sulla CPU. È una lacuna dei test, **non un NaN osservato nelle prove GPU odierne**. Verificare separatamente finitezza, normalizzazione e singolarità corrispondente sulla CPU; non confrontare buffer dopo una valutazione fallita.

### Memoria CUDA trattenuta dal pool

`cuda_support.cu:211-257`: gli slot liberi restano in `freeSlots` senza limite o rilascio esplicito. Ogni slot conserva lo stream, il buffer host pinned e il buffer device con la capacità massima raggiunta. Il distruttore dell'acceleratore libera le superfici, non questi buffer.

Per un lotto da **1.048.576 punti** vengono riservati **64 MiB host pinned e 64 MiB device** nello slot (8 double per punto). Probe reale: dopo la valutazione incremento device di **66 MiB**; dopo il rilascio della superficie restano **64 MiB**. `cudaMemGetInfo` è globale al dispositivo: una prima prova con altre attività grafiche ha mostrato circa 51 MiB, quindi il delta isolato va distinto dalla formula dell'allocazione e dai picchi di processo. La seconda prova coincide con la capacità prevista dal codice.

È riuso intenzionale che evita riallocazioni, non crescita a ogni chiamata identica. Tuttavia molti worker e un singolo lotto grande possono trattenere memoria fino alla fine della sessione, anche quando CUDA viene disabilitato o passa al fallback. Opportunità: limite alla memoria degli slot inattivi o rilascio dei soli slot eccedenti. Il costo prestazionale di questa politica non è stato misurato e non è stata implementata.

### Stato CUDA disponibile memorizzato dopo un errore

`cad_cuda_tessellation.cpp:66-68` memorizza una volta il risultato di `forgecad_cuda_available()`. Il backend reale aggiorna invece `broken` dopo un errore CUDA (`cuda_support.cu:157-161`). Se il primo controllo era positivo, l'API dell'app può continuare a indicare disponibile un backend successivamente disabilitato; gli acceleratori continuano a essere creati e le valutazioni ricadono sulla CPU. Rilievo statico della sequenza, **errore del driver non iniettato**. Separare la capacità iniziale dallo stato corrente o interrogare il backend a ogni richiesta.

### Parti specifiche della piattaforma

Il ramo AppKit e Objective-C++ è protetto da `Darwin` in CMake e da `Q_OS_MACOS` nel main: la build Linux non lo include. La conversione di `ru_maxrss` nel probe distingue correttamente byte macOS e KiB Linux. Non sono stati trovati in questi percorsi nuovi errori dovuti alla conversione delle misure macOS.

`forgeCad2026.cpp:14-22` forza il renderer NVIDIA quando esiste il JSON EGL del driver. La presenza del file non dimostra che il driver funzioni: su un sistema ibrido con driver guasto può interferire con il fallback grafico. Rischio statico; su questa macchina il rendering PRIME provato funziona. L'override `FORGECAD_IGPU` è già presente. Non sono stati cambiati driver o impostazioni del sistema.

## Fallimenti dei raccordi

`BlendArcsMeetingSegments`, `kernel/tests/test_blend.cpp:1104`: fallisce il confronto con OCCT sul raccordo completo del contorno superiore, r=0,3 mm. Probe isolato con sola stampa diagnostica, sorgenti di produzione invariati:

| Caso | Volume ForgeCAD mm³ | Volume OCCT mm³ |
|---|---:|---:|
| Quattro bordi superiori raccordati | 49,1932764304266 | 65,4267088207268 |
| Solo arco raccordato | 49,4142547178179 | 49,4142547239621 |
| Solo arco smussato | 49,2282871777714 | 49,2282871774438 |
| Quattro bordi inferiori smussati | 48,7260211606195 | 48,7260211615719 |

Il volume iniziale analitico è **49,55263680443056 mm³**, ottenuto integrando il profilo `y=1+sqrt(13-(x-3)^2)` per x da 0 a 6 e altezza 2: `24+26*asin(3/sqrt(13))`. Il riferimento OCCT del raccordo completo supera il volume del solido iniziale, incoerente con una rimozione sul contorno convesso. Questo indica un problema nel riferimento OCCT di quel caso, ma **non costituisce da solo una validazione indipendente del volume raccordato ForgeCAD**. Gli altri tre confronti differiscono fra circa 3,3e-10 e 6,1e-9 mm³. Test lasciato fallente; non alterati la soglia o il riferimento.

`forgecad_view_tests`: eccezione “impossibile ricostruire tutti i contorni del raccordo entro tolleranza; il risultato non sarebbe completamente visualizzabile”. Il backtrace al lancio dell'eccezione indica `blendSurfaceChains`, `kernel/fk_blend_surface.cpp:2880`, `computePCurves`, con edge mappato E21 durante raccordi di catene su superfici curve. Il test è compilato con `cuda_support_cpu.cpp`: CUDA non partecipa a questo errore. Il messaggio è diverso dalla discontinuità interna descritta nella review macOS; non viene dichiarata la stessa causa numerica, né un problema risolto. Non rimosso il veto geometrico.

## Prestazioni misurate

Cinque processi nuovi per workload, misure su codice attuale, controlli di volume/B-rep/mesh fuori dal timer. CPU e GPU del loft alternate nello stesso ciclo, con identiche opzioni; entrambe seriali e con thread automatici. Build e suite concluse prima delle misure. I tempi del loft includono upload e allocazione del primo lotto, ma l'inizializzazione della disponibilità CUDA avviene prima del timer. Non è una misura di latenza al primo avvio dell'app. Picco RSS dell'intero processo via `getrusage`, non VRAM o memoria netta del kernel; include avvio del processo e runtime CUDA. Il runner può introdurre un livello minimo comune nel picco RSS dei processi piccoli.

| Workload | Mediana | Min e max | Picco RSS mediano MiB |
|---|---:|---:|---:|
| Prima isoparametrica fuori nodo su 512×512 | 0.012314 ms | 0.011906–0.015495 ms | 14.55 |
| 5.000 sottrazioni box seriali | 1.4639 s | 1.4524–1.4748 s | 14.55 |
| 5.000 sottrazioni box automatiche | 1.477 s | 1.4553–1.4862 s | 14.55 |
| Prismi disgiunti 1.026 facce seriali | 0.011286 s | 0.010894–0.01153 s | 14.55 |
| Prismi disgiunti 1.026 facce automatici | 0.011451 s | 0.010995–0.011718 s | 14.55 |
| Prismi sovrapposti 1.026 facce automatici | 0.55204 s | 0.54341–0.55682 s | 19.88 |
| 10 sottrazioni cilindro e molla B-spline | 2.2102 s | 2.1898–2.2243 s | 14.55 |
| 1.000 raccordi semplici r=1 mm | 0.66259 s | 0.65967–0.66338 s | 14.55 |
| Loft CPU seriale | 59.616 ms | 58.496–60.317 ms | 14.55 |
| Loft CUDA seriale | 50.982 ms | 50.365–53.155 ms | 232.97 |
| Loft CPU thread automatici | 33.03 ms | 32.548–33.666 ms | 17.78 |
| Loft CUDA thread automatici | 28.467 ms | 28.319–29.377 ms | 239.02 |

**60/60 processi validi.** Il loft guadagna **14.5%** con CUDA in seriale e **13.8%** con thread automatici. Gli intervalli osservati CPU/GPU non si sovrappongono in questi cinque campioni; non sono intervalli di confidenza. CUDA accelera la valutazione delle superfici nel raffinamento della mesh: i benchmark di booleane e raccordi restano CPU. Non deriva da queste misure un'accelerazione dell'intera app.

Il picco RSS del loft passa da circa 14,55 a 232,97 MiB in seriale e da 17,78 a 239,02 MiB con thread automatici. Include contesto/runtime NVIDIA, memoria host pinned e strutture CPU: **non attribuire tutto l'aumento al pool, né leggerlo come VRAM**. Il processo grafico completo può avere altri consumi. Le misure macOS nella precedente review usano un'altra CPU, sistema e compilatore: non sono una baseline prima/dopo per questa macchina.

## Artefatti e riproduzione

Dati individuali e sintesi: [linux-cuda-performance-2026-10-10.json](benchmarks/linux-cuda-performance-2026-10-10.json). Probe del loft: [linux_cuda_tessellation_probe.cpp](benchmarks/linux_cuda_tessellation_probe.cpp). Runner: [run_linux_cuda_benchmarks.py](benchmarks/run_linux_cuda_benchmarks.py). Il runner usa i binari diagnostici in `/tmp` ed esegue otto workload del probe esistente più quattro configurazioni del loft, cinque volte. Probe aggiuntivi: [linux_cuda_export_probe.cpp](benchmarks/linux_cuda_export_probe.cpp) e [linux_cuda_pool_probe.cpp](benchmarks/linux_cuda_pool_probe.cpp). Sono strumenti manuali, non soglie temporali introdotte in CI.

```sh
c++ -std=c++17 -O2 -pthread -I kernel kernel/tests/probe_code_review.cpp \
  /tmp/forgecad-linux-cuda-review/kernel/libforgekernel.a \
  -o /tmp/forgecad-linux-review-probe
nvcc -std=c++17 -O3 -arch=sm_75 -c cuda_support.cu -o /tmp/forgecad-cuda-sm75.o
c++ -std=c++17 -O2 -pthread -I . -I kernel \
  docs/benchmarks/linux_cuda_tessellation_probe.cpp cad_cuda_tessellation.cpp \
  /tmp/forgecad-cuda-sm75.o /tmp/forgecad-linux-cuda-review/kernel/libforgekernel.a \
  -L /opt/cuda/lib64 -Wl,-rpath,/opt/cuda/lib64 -lcudart \
  -o /tmp/forgecad-tessellation-benchmark
c++ -std=c++17 -O2 -pthread -I . -I kernel \
  docs/benchmarks/linux_cuda_tessellation_probe.cpp cad_cuda_tessellation.cpp \
  cuda_support_cpu.cpp /tmp/forgecad-linux-cuda-review/kernel/libforgekernel.a \
  -o /tmp/forgecad-tessellation-cpu-benchmark
prime-run python docs/benchmarks/run_linux_cuda_benchmarks.py
```

Per il probe di esportazione, compilare il sorgente con gli include Qt6 e del kernel; collegarlo al posto dell'oggetto `tests/test_viewport.cpp.o` nella lista di `CMakeFiles/forgecad_view_tests.dir/link.txt`, sostituendo anche `cuda_support_cpu.cpp.o` con l'oggetto CUDA e aggiungendo `-lcudart`. Sono stati riusati gli oggetti della build separata, senza ricompilare o modificare l'app. La macro informativa `FORGECAD_HAS_CUDA` del target viewport rimane 0; la prova verifica l'API runtime e il contatore GPU, non l'etichetta dell'interfaccia.

Log conservati in [linux-cuda-2026-10-10](benchmarks/linux-cuda-2026-10-10/): build completa, suite applicativa, kernel completo, test numerico CUDA, OpenGL PRIME, esportazione B-spline, esportazione esistente ricollegata, pool, diagnostica OCCT, backtrace dell'eccezione e misure. Nessuna installazione, modifica ai documenti utente, commit o push.

Priorità successive: rendere espliciti gli skip CUDA e rafforzare i controlli di finitezza nei test; rendere permanenti le prove di esportazione B-spline con GPU attiva; valutare un limite alla memoria inattiva del pool con benchmark prima/dopo; isolare geometricamente i raccordi falliti senza allargare tolleranze. **Tutte proposte, non implementate in questo intervento.**
