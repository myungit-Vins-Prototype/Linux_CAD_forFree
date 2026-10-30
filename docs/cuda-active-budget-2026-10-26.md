# CUDA: budget dei buffer attivi e fallback temporaneo

Passaggio del 26 ottobre 2026 della [roadmap](solver-cuda-roadmap-2026-10-26.md), implementato e verificato su Linux/Quadro RTX 3000. La cache dei buffer inattivi era già limitata a 64 MiB, ma ogni nuova richiesta poteva allocare altri buffer attivi senza un tetto comune. Ora il processo ammette al massimo **128 MiB complessivi di buffer pinned host/device e 4 richieste attive**.

## Contratto e implementazione

- Ogni richiesta prenota l'intera capacità prima delle allocazioni/copie dei buffer di lavoro, includendo capacità arrotondata e buffer riusati. Minimo 1 MiB host/device per slot. Per ogni punto sono necessari 8 double sul device e 8 sull'host: 128 byte complessivi.
- Al raggiungimento dei byte o dei 4 slot, restituisce false senza toccare gli output e senza disabilitare CUDA. Il tassellatore conserva il fallback CPU preesistente; non attende uno slot. Un lotto maggiore di 1.048.576 punti ripiega integralmente sulla CPU.
- La prenotazione copre allocazione, trasferimenti, kernel, copia agli output e free degli slot eccedenti/incompleti. Se lo slot entra nella cache, viene trasferita atomicamente da attivo a inattivo. Le chiamate CUDA restano fuori dal mutex.
- Cache inattiva di 64 MiB e scelta dello slot sufficiente più piccolo conservate. Uno slot in cache troppo grande per il budget attivo residuo resta disponibile, mentre una richiesta minore può crearne uno adatto. Il trim non invalida i lavori attivi.
- `ForgeCudaPoolStats` espone prenotazioni attive, slot, cache, picchi e contatori di allocazione/riuso/rifiuto. Sono capacità logiche prenotate, **non RSS o memoria GPU totale**. Stub CPU con statistiche nulle.

Superfici caricate, array del chiamante, contesto/driver, filtro box e altri processi sono esclusi. Non è un limite alla RAM totale del CAD o a tutta la VRAM. Kernel double, ordine dei calcoli, upload delle superfici e selezione delle specializzazioni invariati; specializzazioni ancora opt-in. Nessuna modifica Metal.

## Verifica

Nuovo `forgecad_cuda_pool_tests`, con piano obliquo e oracolo analitico indipendente: confronto di tutti i punti/normali, riuso senza allocazioni, output intatti in caso di rifiuto, lotto esattamente da 128 MiB ammesso e poi liberato, richieste e trim concorrenti. Otto thread provano lotti da 4.096 e 600.000 punti. Raggiunti 4 slot attivi; limiti rispettati e prenotazioni nulle alla fine. Passa anche con specializzazioni abilitate. Picchi pubblici cumulativi: il massimo di 128 MiB comprende la prova individuale al confine e non dimostra 128 MiB di kernel simultanei.

Sonda identica con array validi per 1.048.577 punti: backend precedente accetta, nuovo rifiuta prima delle allocazioni dei workspace. Entrambi riescono a valutare subito dopo una richiesta piccola. Stub realmente compilato senza runtime CUDA: PASS; dispositivo nascosto: uscita 77, richiesta GPU obbligatoria: errore 1. Errori reali di allocazione/driver non iniettati.

App `forgecad-linux-solver/forgecad`, runner viewport/CUDA/pool/export ricompilati. **29/29 CTest PASS**, zero FAIL/SKIP, 10,65 s. Scarto massimo dei test numerici: punti 7,01e-16 relativo e normali 4,75e-14; upload concorrenti senza errori; loft con 32.248 triangoli su CPU/GPU. Incluse regressioni solver, mesh, export STL/OBJ, anteprime, storia e correzione RAM.

## Documento reale mouse2.prt

Tre processi finali nuovi: CPU, CUDA generico, CUDA con specializzazioni. Concordano su 31 feature, 13 schizzi, 16 B-rep e 49.279 triangoli visibili. Vertici identici nei bit, metadati e firme dei B-rep identici. Restano 23 normali diverse con massimo 7,4505805969e-9, già documentate prima del budget; zero faccette degeneri e zero inversioni.

| Percorso | Punti CUDA | Picco prenotato host/device | Picco slot | Rifiuti budget |
|---|---:|---:|---:|---:|
| CPU | 0 | 0 | 0 | 0 |
| CUDA generico | 90.675 | 3 MiB | 3 | 0 |
| CUDA specializzato opt-in | 90.675 | 4 MiB | 4 | 0 |

Nei due processi GPU restano rispettivamente solo 3 e 4 MiB inattivi e zero prenotazioni attive. Il documento non satura il budget; questa prova non misura il fallback sotto pressione nell'intero CAD. Originale invariato, SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617`.

La prima serie aveva confronti geometrici riusciti ma non includeva i nuovi contatori: il compilatore aveva letto l'header prima della sua modifica durante la build. Ricompilata esplicitamente la sola unità diagnostica e ripetuti tutti e tre i processi; solo la serie finale entra nei dati principali. Tempi documentali non usati come benchmark, anche per l'analisi architetturale concorrente.

## Campioni seriali e limiti

Prima serie: tre processi per binario conservato/nuovo, ordine alternato, 24 casi, mediana di 9 chiamate dopo warmup, generico forzato. Rapporto vecchio/nuovo minimo 0,745, mediano 0,896, massimo 1,180; forti variazioni tra processi. Approfondimento con lo stesso driver per entrambi i backend e almeno 2 secondi di precondizionamento nello stesso contesto: minimo 1,008, mediano 1,125, massimo 1,324. Conservati separatamente entrambi i protocolli e tutti i campioni.

Nessuna build/mappa/sonda concorrente durante i microbenchmark; trasferimenti inclusi, upload e riferimento CPU fuori timer. Frequenze, temperatura e carico esterno non controllati. I rapporti mediani cambiano direzione fra protocolli: **nessuna accelerazione o regressione stabile dimostrata**, nessuna misura della velocità del CAD o dei carichi saturati. Obiettivo del passaggio: ammissione e uso limitato delle risorse.

Rimangono budget aggregato fra backend/superfici, comportamento prolungato sotto saturazione, altre GPU e componenti grandi connesse del solver. Nessuna nuova verifica macOS/Windows o trasferimento della geometria esatta alla GPU.

## Riproduzione e artefatti

```sh
cmake --build forgecad-linux-solver --target forgecad forgecad_view_tests forgecad_cuda_tests forgecad_cuda_pool_tests -j4
ctest --test-dir forgecad-linux-solver -R 'forgecad_cuda(_pool)?_tests' --output-on-failure
python3 tools/compare_document_mesh_backends.py /percorso/mouse2.prt --build-dir forgecad-linux-solver --output /tmp/mouse-budget-nuovo --repeats 1 --specialized
```

[Dati, campioni e hash](cuda-active-budget-2026-10-26.json). Artefatti locali in `forgecad-linux-solver/benchmarks/cuda-active-budget-2026-10-26/`: backend/runner conservati, sonde e driver manuale, campioni, snapshot e log. I driver manuali sono compilati con C++17/O3/sm75; il driver caldo include lo stesso `tests/test_cuda_tessellation.cpp` in entrambe le varianti e usa solo il segmento dopo MEASURE.

Mappa rigenerata: 491 file, 6.663 relazioni, 234 unità senza diagnostiche; controllo aggiornamento PASS. Moduli, collegamenti entranti/uscenti e righe verificati; CUDA resta catalogato con include, senza copertura AST garantita delle chiamate ai kernel. HTML/JSON generati ignorati. Installazione desktop sulla build main già corretta; app di sviluppo aggiornata nella propria build. Nessun commit/push in questo intervento.
