# Miglioramenti di calcolo, memoria e affidabilità — 10 ottobre 2026

Implementati il filtro spaziale delle booleane, il percorso per solidi disgiunti,
la riduzione dei temporanei e la scelta automatica dei thread sui piccoli lavori
planari. Corrette inoltre la consegna asincrona al viewport e la selezione del
risultato durante il rollback. Conservata la correzione della cache B-spline
introdotta nella revisione precedente.

I guadagni maggiori sono nei corpi con molte facce e pochi contatti. Il raccordo
combinato del flacone da 0,3 mm **continua a fallire**. Non sono state allargate le
tolleranze o eliminate verifiche geometriche per farlo passare.

## Misure prima/dopo

Baseline geometrica e algoritmica: commit `f0dfbb5`. Versione dopo: working tree
di questo intervento. Mediane di processi nuovi, alternando prima/dopo;
5 processi per versione per prismi e isoparametrica, 3 per box, molla e raccordo.
Il tempo comprende solo il calcolo indicato, escludendo costruzione degli input,
controllo finale di volume, validità e mesh. I controlli interni del kernel sono
sempre attivi. La RAM è il **picco RSS dell'intero processo** rilevato con
`getrusage`, campionato al termine del calcolo prima delle verifiche esterne.
MiB = 1.048.576 byte. Non è la sola memoria dell'algoritmo, né la RAM dell'app GUI.

| Prova | Tempo prima | Tempo dopo | Variazione del tempo | Picco RAM prima → dopo |
|---|---:|---:|---:|---:|
| 5.000 sottrazioni tra box, thread automatici | 1,707 s | 1,053 s | **−38,3%** | 3,84 → 3,39 MiB |
| Stessi box, thread singolo esplicito | 1,086 s | 1,047 s | −3,6% | 3,33 → 3,33 MiB |
| Sottrazione tra prismi disgiunti, automatico | 1,274 s | 0,00824 s | **−99,35%**, circa 155× | **147,25 → 11,55 MiB (−92,2%)** |
| Stessi prismi disgiunti, thread singolo | 0,849 s | 0,00842 s | −99,01% | 146,88 → 11,48 MiB |
| Prismi con piccola sovrapposizione, automatico | 1,721 s | 0,474 s | **−72,4%**, circa 3,6× | **148,58 → 19,86 MiB (−86,6%)** |
| Prima isoparametrica fuori dai nodi, B-spline 512×512 | 6,264 ms | 0,0109 ms | −99,83% sulla sola prima chiamata | **29,86 → 7,75 MiB**, circa **22,1 MiB evitati** |
| 10 sottrazioni cilindro/molla B-spline, automatico | 2,839 s | 2,804 s | −1,3%, sostanzialmente invariato | 6,36 → 6,55 MiB |
| 1.000 raccordi semplici, raggio 1 mm | 0,488 s | 0,490 s | +0,5%, sostanzialmente invariato | 4,11 → 4,13 MiB |

Prismi: poligono regolare a 1.024 lati, raggio 10 mm, altezza 10 mm,
1.026 facce per corpo. Traslazione dell'utensile di 100 mm per la prova disgiunta,
19 mm per la piccola sovrapposizione. Box: lato 10 mm, utensile traslato di
(5, 3, 2) mm. Raccordo: un edge del cubo di lato 10 mm. Molla: sweep del cerchio
di raggio 0,5 mm lungo mezza spira, raggio 4 mm, passo 1,5 mm, sottratta dal
cilindro di raggio 4 mm e altezza 5 mm.

Tutte le **64 esecuzioni del probe** hanno restituito `valid=1`, prima e dopo.
Volume box atteso 720 mm³; per i prismi il volume comune è calcolato con un
clipping poligonale 2D indipendente. Il raccordo è confrontato con il volume
analitico 997,853981634 mm³; molla e cilindro mantengono volume
246,645628615 mm³, 7 facce, 11 edge e nessuna faccia non tassellata. Per la molla
si confrontano anche risultato automatico e seriale. L'isoparametrica è
confrontata con i punti della superficie esatta.

I casi di controllo non mostrano un cambiamento netto di velocità. Intervalli
osservati: molla prima 2,773–2,846 s / dopo 2,796–2,819 s; raccordi prima
0,465–0,503 s / dopo 0,490–0,516 s. I circa 0,19 MiB aggiuntivi nella prova
della molla e i 0,016 MiB nel raccordo sono riportati senza attribuirli tutti
all'indice: RSS, allocatore e thread incidono sulla misura. L'indice introduce
comunque memoria lineare aggiuntiva; il risparmio cresce quando evita numerosi
confronti e voci di cache. Non si promette un guadagno su ogni documento.

Ambiente: macOS 27.0.1, arm64, 8 CPU logiche, Apple Clang 21.0.0, build CMake
Release. Stesso sorgente del probe compilato con `-std=c++17 -O2`. La baseline
usa l'archivio precedente agli interventi sulle booleane e l'oggetto B-spline
estratto da `f0dfbb5`, collegato prima dell'archivio per ripristinare la cache
originale. Nessun benchmark concorrente avviato dall'agente; carico esterno e
frequenza della CPU non controllati. Dati individuali, argomenti e mediana:
[`benchmarks/performance-2026-10-10.json`](benchmarks/performance-2026-10-10.json).

## Modifiche implementate

- **Booleane:** indice gerarchico immutabile dei box delle facce, usato sia per
  le coppie da intersecare sia per la ricerca delle superfici coincidenti nella
  classificazione. Query ordinate per ID come prima; espansione dei box di
  classificazione anche con la tolleranza dei vertici. Gli edge tolleranti e
  le coincidenze B-spline parziali conservano i controlli esistenti. Il caso
  denso può ancora richiedere un numero quadratico di coppie.
- **Solidi disgiunti:** intersezione vuota immediata; sottrazione restituisce
  il primo corpo, conservando l'opzione di unificazione e `checkBody`.
  Unione e operazioni sulle lamine conservano il percorso generale. Test
  aggiunto con corpo invalido per verificare che il percorso rapido non
  trasformi un risultato invalido in un successo.
- **Temporanei:** archi e tagli trasferiti con move, nodi di map/set riutilizzati
  con merge; i risultati intermedi vengono liberati dopo il trasferimento,
  prima della raccolta dei tagli. Conservata la regola dell'ultima voce per
  chiavi duplicate. Il contributo isolato di questa modifica non è misurato.
- **Thread:** in modalità automatica, piccoli lavori composti solo da piani
  (al massimo 128 edge totali, meno di 32 job nella fase) restano seriali.
  Richieste esplicite e superfici curve conservano il parallelismo.
- **B-spline:** una prima richiesta fuori dai nodi evita di costruire tutte le
  isoparametriche U/V. Curva esatta, lookup a cache pronta e tolleranze invariati.
  Il confronto include questa correzione già realizzata durante la revisione.
- **Viewport:** anteprima ed evidenziazione consegnano i risultati a `qApp`;
  il `QPointer` è verificato nel thread GUI prima della callback. Nessuna
  attesa sincrona aggiunta alla chiusura. Regressione con worker accodati,
  viewport vivo/distrutto e controllo delle due consegne al viewport vivo.
- **Rollback:** il gruppo Corpi risolve l'ultimo stadio valido precedente al
  cursore, saltando feature fallite/soppresse. Le feature future non consumano
  più i rami dell'albero; i corpi non ancora creati non sono elencati durante
  il rollback. Test di inserimento dal tip selezionato e ricalcolo a valle.

File di produzione: `kernel/fk_boolean.cpp`, `kernel/fk_boolean.h`,
`kernel/fk_bspline_surface.cpp`, `forgeCad2026_gui.cpp`. Test/diagnostica:
`kernel/tests/test_boolean.cpp`, `kernel/tests/test_bspline_surface.cpp`,
`kernel/tests/probe_code_review.cpp`, `tests/test_viewport.cpp`, `CMakeLists.txt`.
Registro aggiornato in `CLAUDE.md`.

## Verifiche e fallimenti

| Verifica | Esito |
|---|---|
| Build completa Release | PASS |
| Kernel completo dopo le modifiche finali | **310/310 PASS** |
| CTest escluso il runner kernel | **24/25 PASS** |
| Regressioni finali rollback + vita asincrona | **2/2 PASS** |
| Benchmark prima/dopo | **64/64 validi** |
| Diff senza errori di whitespace | PASS |

Combinando il runner kernel con i 25 test CTest applicativi/diagnostici:
**25/26 PASS**. Unico fallimento finale: `forgecad_view_tests`, sul raccordo del
flacone da 0,3 mm, elica + fine-filetto. Già presente nella baseline; stesso
errore anche nel probe kernel prima/dopo:
`blendEdges: discontinuita' interna della superficie lungo il raccordo`.
Non c'è quindi una riduzione misurata dei fallimenti geometrici di questo caso.
Il rischio asincrono è stato corretto e verificato con una sequenza deterministica;
non si dichiara una riduzione percentuale dei crash, perché un crash reale
dell'app non era stato riprodotto nella revisione.

Diagnostica del flacone: dal B-rep della feature precedente al raccordo sono
stati isolati E22 (elica) ed E21 (fine-filetto). Lungo E21, intorno al parametro
0,76489172, la soluzione del contatto salta di circa 0,01 mm pur con residui
Newton nell'ordine di 1e-12. Questo dimostra un salto del ramo numerico;
non dimostra che la superficie di partenza sia geometricamente discontinua.
Resta da verificare una continuazione robusta dello stesso ramo e gli estremi.
Tentativo con limite più stretto al salto del predittore: stesso fallimento.
Tentativo invertendo l'ordine dei gruppi: errore di prolungamento
dell'intersezione non convergente. **Entrambi scartati**, nessuna modifica ai
file di produzione dei raccordi.

Altro limite individuato nel test di rollback: l'utensile consumato da una
booleana futura torna come ramo indipendente in Corpi, ma il flag di visibilità
conservato può restare spento. Il ripristino automatico della visibilità non è
implementato. Il test dei rami verifica struttura e tip; non attesta la
riapparizione automatica della geometria dell'utensile. La selezione dello
stadio futuro, oggetto della revisione, è invece corretta e verificata.

Log finali locali: `/tmp/implementation-build-final.log`,
`/tmp/implementation-view-test-final-build.log`,
`/tmp/implementation-kernel-final.log`, `/tmp/implementation-app-final2.log`,
`/tmp/implementation-targeted-final.log`, `/tmp/implementation-measurements-final.log`.
Log temporanei non garantiti dopo la pulizia del sistema; risultati dei benchmark
conservati nel JSON del repository. La prima esecuzione applicativa aveva
un'aspettativa aggiuntiva sulla visibilità dell'utensile, che ha evidenziato
il limite sopra: la verifica finale è stata circoscritta alla correzione
effettivamente implementata, senza dichiarare risolto quel limite. Un primo
run del nuovo flag asincrono ha usato un binario non ancora ricompilato ed è
ricaduto nel test generale; ricompilato e rieseguito correttamente.

La suite kernel necessita dei temporanei STEP/IGES in `/var/tmp`; eseguita con
accesso autorizzato, senza saltare test. GUI verificata offscreen; non verificati
GPU/CUDA, Linux o velocità interattiva su documenti completi. Nessuna installazione
in `/Applications`, modifica ai documenti utente, commit o push.

## Ripetere le prove sulla versione corrente

```sh
cmake --build forgecad-release -j4
c++ -std=c++17 -O2 -I kernel kernel/tests/probe_code_review.cpp \
  forgecad-release/kernel/libforgekernel.a -o /tmp/forgecad-performance-probe
/tmp/forgecad-performance-probe boxes 0 5000
/tmp/forgecad-performance-probe boxes 1 5000
/tmp/forgecad-performance-probe disjoint 1024 0
/tmp/forgecad-performance-probe disjoint 1024 1
/tmp/forgecad-performance-probe sparse 1024 0
/tmp/forgecad-performance-probe iso 512
/tmp/forgecad-performance-probe spring 0 10
/tmp/forgecad-performance-probe fillet 1000
```

Per ricostruire la baseline usare un checkout separato di `f0dfbb5`, compilare
il kernel con la stessa configurazione Release e collegare lo stesso sorgente
aggiornato del probe al relativo archivio. Non compilare il vecchio probe,
che non contiene tutte le modalità o ha un protocollo diverso.
