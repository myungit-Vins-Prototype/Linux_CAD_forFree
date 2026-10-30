# Verifica aggiuntiva della velocità — 26 ottobre 2026

Richiesta: misurare l'incremento della nuova ottimizzazione del solver. Nessun nuovo sorgente funzionale o binario modificato; prima e dopo le prove SHA256 confermano i medesimi sorgenti e build già verificati nel [rapporto di implementazione](solver-connected-2026-10-26.md).

## Protocollo

- Riferimento: stesso runner finale, stessi oggetti e librerie; sostituito soltanto l'oggetto precedente `cad_constraints.cpp.o` conservato da `c372139`. È il precedente solver partizionato del branch, non main.
- Solver:8processi nuovi per versione, ordine bilanciato (4prima/dopo e4dopo/prima), un warmup e5misure per processo.40misure per caso/versione, mediana delle mediane dei processi. Stessi target in entrambi i percorsi; validazione fuori timer.
- Documento:6processi per versione con cache, poi4processi per versione con tutti i B-rep e le mesh ricostruiti; ordine prima/dopo bilanciato e processi sequenziali. Profilazione disattivata. `open_seconds` misura lettura+load/preparazione, firme escluse; comprende geometria e mesh nel caso ricostruito.
- Tassellazione CPU, qualità1; runner Qt offscreen, senza rendering GL/framerate. Il filtro GPU è disattivato. Nessuna nostra build, mappa o altra sonda contemporanea; frequenza, temperatura e altri processi del sistema non controllati. Cache del filesystem non svuotata: processi nuovi non significa lettura a disco freddo.
- File originale letto soltanto: `mouse2.prt`, SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617`, invariato.

## Risultati

| Operazione | Precedente | Nuovo | Rapporto precedente/nuovo |
|---|---:|---:|---:|
| Solver, catena 64 segmenti | 0.678 ms | 0.223 ms | 3.04× |
| Solver, catena 256 segmenti | 36.127 ms | 0.885 ms | 40.83× |
| Solver, catena 512 segmenti | 294.767 ms | 1.732 ms | 170.20× |
| Solver, 7 trascinamenti validi mouse2 | 0.435 ms | 0.438 ms | 0.994× |
| Apertura mouse2 con cache | 0.266 s | 0.271 s | 0.979× |
| Apertura e ricostruzione completa mouse2 | 35.838 s | 35.792 s | 1.001× |

Il miglioramento del solver è ripetibile sulle catene connesse e non va applicato ai tempi dell'intero CAD. Su mouse2 i trascinamenti selezionati non mostrano un incremento stabile; apertura e ricostruzione devono essere valutate con i rispettivi intervalli sotto, non con il rapporto delle catene.

| Caso | Intervallo precedente | Intervallo nuovo |
|---|---:|---:|
| Catena64 | 0.668–0.710 ms | 0.216–0.275 ms |
| Catena256 | 35.567–36.631 ms | 0.831–0.929 ms |
| Catena512 | 293.511–298.392 ms | 1.710–1.753 ms |
| Solver mouse2 | 0.429–0.502 ms | 0.426–0.597 ms |
| Apertura con cache | 0.264–0.275 s | 0.267–0.277 s |
| Ricostruzione completa | 35.747–36.498 s | 35.639–36.021 s |

## Correttezza e limiti

Le catene verificano target, ogni vincolo e soluzione analitica fino all'ultimo segmento su tutte le chiamate. Il massimo scarto delle coordinate prima/dopo è 2.776e-17mm. Su mouse2 risultano gli stessi7target ammissibili su13schizzi in tutti i processi. Il runner controlla geometria rispetto al riferimento denso, struttura curve, vincoli, target e rango/DOF.

Per ciascuna modalità documento, tutte le firme B-rep e mesh, visibilità ed errori coincidono bit per bit fra versioni e ripetizioni. Cache salvata e ricostruzione sono scenari diversi e non devono avere necessariamente lo stesso numero di triangoli:

- Cache salvata: 16 B-rep disponibili, 49407 triangoli visibili; punti CUDA0, coppie GPU0.
- Ricostruzione: 16 B-rep disponibili, 49279 triangoli visibili; punti CUDA0, coppie GPU0.

Dati integrali dei timer, intervalli, hash e controlli nel [JSON](solver-speed-verification-2026-10-26.json). I grandi array delle coordinate sono sostituiti da hash nel rapporto versionato; dati completi/log sono in `forgecad-linux-solver/benchmarks/solver-speed-verification-2026-10-26/{solver,cached,rebuilt}`. La replica dell'algebra densa/banda è inclusa nel lotto solver ma distinta dai tempi del solver completo.

Le suite correttezza precedenti non sono state ripetute senza modifiche. Nessuna modifica CUDA/Metal, nuova build/installazione, commit/push o test nativo macOS in questa verifica. Nessun miglioramento di framerate o interazione GUI completa dedotto dalle misure offscreen.

## Riproduzione

```sh
python3 tools/benchmark_connected_solver.py --build-dir forgecad-linux-solver \
  --previous-runner /percorso/runner-con-stessa-fixture-e-solver-precedente \
  --document /percorso/mouse2.prt --output /percorso/solver-nuovo --processes 8
python3 tools/benchmark_document_mesh.py /percorso/mouse2.prt \
  --build-dir forgecad-linux-solver --reference-binary /percorso/runner-precedente \
  --output /percorso/cache-nuovo --no-profile --cached --repeats 6
python3 tools/benchmark_document_mesh.py /percorso/mouse2.prt \
  --build-dir forgecad-linux-solver --reference-binary /percorso/runner-precedente \
  --output /percorso/ricostruzione-nuovo --no-profile --repeats 4
```

Seguito implementato: [cache locale dei campioni UV delle cuciture](sew-rebuild-2026-10-26.md), con primo miglioramento misurato nella ricostruzione completa del documento reale. I tempi sopra restano quelli del solo intervento sul solver.
