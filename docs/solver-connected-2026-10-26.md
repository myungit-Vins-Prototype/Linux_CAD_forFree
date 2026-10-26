# Solver: componenti connesse a banda — 26 ottobre 2026

Implementato sul branch `solver-mesh-gpu`, base `c372139`. Il solver ora sfrutta la banda stretta delle componenti grandi, comprese le catene di vincoli inserite in ordine sfavorevole. Il cambiamento è C++17 comune alle piattaforme; verifiche e compilazione eseguite qui su Linux.

## Contratto del calcolo

In `kernel/fk_constraint_linear.h` rimane il passo `-Jᵀ (J Jᵀ + μI)⁻¹ r`, con la stessa regolarizzazione globale e gli stessi controlli geometrici del solver. L'analisi del rango e dei DOF non cambia.

Per componenti di almeno32righe si costruiscono le incidenze colonne/righe. La distanza massima fra righe che condividono una colonna delimita strutturalmente la banda di `J Jᵀ`: anche eventuali cancellazioni numeriche restano incluse. Se la banda naturale supera16 si tenta una BFS inversa deterministica, con grado stimato dalle incidenze e senza materializzare il grafo quadratico delle coppie. Si adotta l'ordine nuovo solo se riduce la banda.

La fattorizzazione a banda si usa soltanto se il suo storage dimezza almeno quello della matrice piena. Il fill-in interno alla banda viene conservato; nessuna soglia numerica azzera coefficienti. Una colonna condivisa da più di metà delle righe esclude subito il percorso stretto. Componenti piccole o larghe conservano ordine e Cholesky precedenti, senza allocazioni del grafo per quelle piccole. Pivot non valido o soluzione non finita nel tentativo a banda provocano il ripiego sulla fattorizzazione piena originale prima di accumulare il passo.

Non è una fattorizzazione sparsa generale. La BFS è un'euristica, senza promessa di banda ottimale. Non cambia CUDA, Metal, formati documento o tolleranze.

## Misure sul solver completo

Fixture permanente `--connected-solver-benchmark`: catena orizzontale, coincidenze inserite dopo le orizzontalità, trascinamento del primo estremo da y=0 a y=0,2. Il test verifica ogni vincolo, il target e la soluzione analitica fino all'ultimo segmento, comprese le coordinate x. Copie originali per ogni chiamata, un warmup e cinque ripetizioni; controlli fuori timer. Geometria 3D e rendering esclusi dal tempo.

Il riferimento usa la **stessa fixture finale e tutti gli stessi oggetti**, sostituendo soltanto l'oggetto `cad_constraints.cpp.o` precedente, conservato prima di modificare l'header. È il solver partizionato di questo branch, non una build di main. Il blocco della fattorizzazione locale densa rimane identico al precedente. Le librerie/binary e i sorgenti sono identificati nel rapporto JSON e ricontrollati dopo le misure.

Lotto iniziale: tre processi nuovi per versione, ordine prima/dopo alternato. Conferma: sei processi nuovi per versione, tre coppie per ciascun ordine. Nessuna build, mappa o altra nostra sonda contemporanea; frequenze, temperatura e carico esterno non controllati. Ogni valore sotto è la mediana delle mediane per processo del secondo lotto; intervalli tra processi.

| Segmenti | Precedente, ms | Nuovo, ms | Rapporto |
|---|---:|---:|---:|
| 64 | 0.675 (0.670–0.719) | 0.232 (0.213–0.237) | 2.9× |
| 256 | 35.732 (35.355–36.619) | 0.899 (0.859–0.938) | 39.7× |
| 512 | 291.823 (289.045–302.845) | 1.692 (1.680–1.752) | 172.4× |

Le stesse catene nel primo lotto migliorano rispettivamente3,10×/40,23×/166,51×; entrambi i lotti sono conservati, senza unirli. Due iterazioni per trascinamento su entrambe le versioni. Massimo scarto prima/dopo delle coordinate2,776e-17mm. Questi risultati riguardano questa classe sintetica di sistemi connessi; non sono accelerazioni dell'intera applicazione.

## mouse2.prt

Originale `/home/myungit/Documenti/File_Esempio/mouse2.prt`, SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617`, invariato. Su entrambi i lotti e tutte le coppie, stessa selezione di sette trascinamenti ammissibili su13schizzi. Il runner verifica successo, target, vincoli, geometria rispetto al riferimento denso, struttura delle curve, rango/DOF ed entità definite.

Nel primo lotto il solver aggregato passa0,434→0,461ms; anche i percorsi di controllo invariati sono più lenti nei processi nuovi. Il lotto bilanciato dà **0,441→0,442ms**, intervalli0,431–0,472 e0,437–0,482ms. Analisi DOF invariata:0,381→0,390ms, intervalli sovrapposti. Non emerge un guadagno su mouse2; non si attribuisce un costo stabile al nuovo percorso da questi piccoli scarti. Il vantaggio sulle catene non viene generalizzato al documento reale o alla rigenerazione CAD.

## Algebra, memoria e regressioni

Nuovo target permanente `forgecad_constraint_linear_tests`, senza link Qt/GPU. Riferimento indipendente con Gauss a pivot parziale su matrice globale; soluzioni minime note costruite come `Jᵀ*z`. Copertura: componenti vuote/piccole/disgiunte, catene ordinate e mescolate, banda3 con fill-in, righe ridondanti, scale differenti e μ globale, clique densa, input immutato,24sistemi con archi supplementari e rifiuto NaN anche nel percorso a banda.

Microbenchmark separato, stesso binario, modalità densa locale contro automatica, sei processi con cinque ripetizioni alternate per caso e verifica fuori timer. Catene128/384/768righe, ordinate e mescolate. A768righe il solo storage allocato per la matrice di Cholesky passa589824→1536double, **4,5MiB→12KiB (384×)**. Questo contatore esclude Jacobiano, grafo, vettori ausiliari e allocatore; non è una misura RSS. Campioni numerici completi nel JSON.

Build Release dell'applicazione `forgecad-linux-solver/forgecad` e dei runner PASS. **42test CTest distinti PASS**, inclusi algebra, solver connesso/partizionato, maniglie, schizzi fissi, editing, riferimenti, pattern, storia/anteprime, mesh/export e CUDA. La prima passata ha40PASS/1FAIL: la fixture storyboard non creava `/tmp/sketch-story`; riprodotto lo stesso FAIL con il binario precedente, creata soltanto la directory e test ripetuto PASS. Algebra eseguita separatamente1/1PASS. Nessun test saltato per far passare il solver.

ASan/UBSan PASS. Il primo LeakSanitizer nella sandbox si ferma per ptrace; ripetizione fuori sandbox PASS. Prova OpenGL nativa mouse2 con la build aggiornata: tre riaperture della superficie planare, prima immagine e cache ricreata identiche (zero pixel diversi, zero errori GL), B-rep e originale conservati. Non rieseguita una ricostruzione geometrica completa, non modificata da questo intervento.

Mappa rigenerata:494file/6678relazioni,235unità, zero diagnostiche; controllo aggiornamento e collegamenti/range delle righe PASS. HTML/JSON della mappa rimangono locali e ignorati. Compilazione e QA native macOS ancora aperte; nessun backport a main, installazione desktop, commit o push in questo intervento.

## Riproduzione

```sh
cmake -S . -B forgecad-linux-solver
cmake --build forgecad-linux-solver --target forgecad forgecad_view_tests forgecad_constraint_linear_tests -j4
ctest --test-dir forgecad-linux-solver -R 'forgecad_(constraint_linear|connected_solver|solver_partition)_tests$' --output-on-failure
forgecad-linux-solver/forgecad_constraint_linear_tests --benchmark
QT_QPA_PLATFORM=offscreen forgecad-linux-solver/forgecad_view_tests --connected-solver-benchmark
python3 tools/benchmark_connected_solver.py --build-dir forgecad-linux-solver \
  --previous-runner /percorso/runner-con-stessa-fixture-e-solver-precedente \
  --document /percorso/mouse2.prt --output /percorso/nuova-directory --processes 6
```

Per il confronto prima/dopo conservare l'oggetto del solver prima della modifica e ricollegare il comando `CMakeFiles/forgecad_view_tests.dir/link.txt` dopo la build finale, sostituendo soltanto `cad_constraints.cpp.o` e la destinazione `-o`. Il vecchio runner originale non contiene la nuova fixture. Il comando effettivo è conservato in `relink-previous.json` negli artefatti locali.

[Dati verificati e campioni](solver-connected-2026-10-26.json). I grandi array di coordinate sono sostituiti nel rapporto versionato da hash; copie complete, runner, vecchio oggetto, comandi e log restano in `forgecad-linux-solver/benchmarks/solver-connected-2026-10-26/{measures,confirmation}` e nella directory padre.

Prossimi punti: componenti con struttura larga/non locale e nonlinearità diverse, analisi del rango su gruppi grandi, misure separate della geometria e del rendering. Saturazione CUDA prolungata e budget aggregato dei backend restano aperti.

Seguito richiesto dall’utente: [nuovo lotto e tempi di apertura/ricostruzione completa mouse2](solver-speed-verification-2026-10-26.md). Conferma il vantaggio sulle catene e non mostra un incremento significativo nel documento reale.
