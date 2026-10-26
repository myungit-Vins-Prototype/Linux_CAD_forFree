# Main: rilascio RAM dopo il cambio di tassellazione

Backport selettivo sulla base `origin/main` `5473456`, dopo aggiornamento del
main locale. La pipeline originale con `tessellateBody` è conservata: nessuna
importazione delle ottimizzazioni sperimentali solver, mesh differite,
CUDA/Metal o filtro delle faccette del branch `solver-mesh-gpu`.

La cache CPU/VBO rilascia a fine frame le mesh non usate. Quando il ritorno a
una qualità inferiore riduce di oltre64MiB gli array di vertici/normali, il
viewport svuota la cache GL con contesto corrente e richiede il rilascio delle
pagine già libere dell'allocatore (`malloc_trim` su glibc, no-op altrove).
Oggetti vivi, Undo/Redo e geometria non vengono eliminati o modificati.

## Verifiche main

Build Release/CUDA75 da zero: applicazione, runner, test cache e test CUDA.
**19/19 CTest PASS**, zeroFAIL/SKIP (6,00s), compresi cache, CUDA,
lettore, storie, anteprime, picking/etichette e workflow asincroni.
Anche test cache e anteprime con OpenGL reale PASS.

Assieme completo locale con1.204feature, ciclo bassa→massima→bassa della
sonda senza rendering:

| Fase | RSS GiB | Triangoli negli array |
|---|---:|---:|
| Bassa iniziale | 1,433 | 2.611.738 |
| Massima | 9,203 | 123.088.635 |
| Ritorno alla bassa | **0,581** | 2.611.738 |

La mesh bassa riproduce esattamente vertici/normali/FaceId iniziali nel ciclo;
puntatori B-rep invariati. Originale SHA256 invariato
`c68ee31e378a177320d82d98d5e347860b26f1944c6d32911ec53fbd60c354af`.
Nessun salvataggio CAD o nuova verifica generale di volume/topologia.
Le misure precedenti prima/dopo su `solver-mesh-gpu` non sono dati main.

Prova nativa sul sottoinsieme64feature: cache alta206,203MiB; al ritorno alla
bassa viene rilasciata e il primo frame conserva solo4,665MiB/64entry.
RSS circa0,292GiB. Il file viene letto completo prima di limitarlo: questa
RSS non è una stima scalabile. Nessun ciclo GUI completo sull'assieme
con1.204feature o miglioramento universale del picco dichiarato.

Mappa469file/6549relazioni,230unità senza diagnostiche; controllo aggiornamento,
collegamenti nei due versi e righe sorgenti verificati. HTML/JSON generati
restano ignorati Git. Verifica nativa Mac/Windows ancora da eseguire.

## Eseguibile e riproduzione

Compilazione separata in `/tmp/forgecad-main-memory-build-20261026`, dal
checkout main `/tmp/forgecad-main-memory-20261026`. Eseguibile verificato
copiato atomicamente in `forgecad-release/forgecad`, percorso già usato dal
desktop; versione precedente conservata in
`forgecad-release/benchmarks/main-memory-backport-2026-10-26/forgecad-before`.
Nessun processo utente riavviato: per usare il fix chiudere e riaprire l'app.

```sh
cmake -S . -B forgecad-release -DCMAKE_BUILD_TYPE=Release -DFORGECAD_BUILD_VIEW_TESTS=ON
cmake --build forgecad-release --target forgecad forgecad_view_tests forgecad_display_cache_memory_tests -j 4
ctest --test-dir forgecad-release -R forgecad_display_cache_memory_tests --output-on-failure
```

Eseguire questi comandi da un checkout di **main**. La sonda opzionale del
runner è `--mesh-memory-benchmark /percorso/assieme.prt`; `--gl` abilita il
rendering e `--memory-trim` aggiunge il trim diagnostico finale. Le prove su
file grandi usano un monitor esterno con riserva3GiB di RAM e420s.

[Rapporto JSON](mesh-memory-main-2026-10-26.json). Log, monitor e backup in
`forgecad-release/benchmarks/main-memory-backport-2026-10-26`.
Nessun push incluso nel backport locale.
