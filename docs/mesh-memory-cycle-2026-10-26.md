# RAM dopo il cambio di tassellazione: diagnosi e correzione

Confermato sul grande assieme il mancato ritorno della RAM residente dopo
bassa → massima → bassa. Concorrono due meccanismi: pagine libere trattenute
dall'allocatore glibc e risorse CPU/VBO dei frame precedenti ancora nella
cache grafica. `DisplayCache::beginFrame` era identico in `origin/main` e
nella base `5758982` del branch. La correzione è applicata al branch di lavoro;
main è stato soltanto letto per confronto.

## Assieme completo, CPU senza rendering

File locale `2550_ASSIEME-ST5674-COMPLETO.prt`, 1.204 feature, 1.191 visibili.
Originale SHA256 invariato in tutte le esecuzioni:
`c68ee31e378a177320d82d98d5e347860b26f1944c6d32911ec53fbd60c354af`.
Nessuna ricostruzione B-rep richiesta e nessun documento salvato.

| Fase | Prima, RSS GiB | Dopo, RSS GiB | Triangoli |
|---|---:|---:|---:|
| Bassa iniziale | 1,411 | 1,419 | 2.424.419 |
| Massima | 11,706 | 11,413 | 114.185.197 |
| Ritorno alla bassa | **7,692** | **0,562** | 2.424.419 |

Secondo processo corretto: bassa 1,418 GiB, massima 11,554 GiB, ritorno
**0,547 GiB**. Non si attribuiscono significati prestazionali alle differenze
di picco o tempo: misure con campionamento, build/test/mappa concomitanti.

Prima del fix, al ritorno alla bassa gli array delle mesh occupano circa
0,251 GiB di capacità referenziata e glibc dichiara circa 0,504 GiB in uso
nell'heap, ma conserva circa **7,669 GiB liberi nelle arene**. Un trim solo
diagnostico porta RSS da 7,692 a 0,588 GiB senza cambiare i dati della scena.
Con il fix il rilascio avviene automaticamente durante il cambio qualità;
il successivo trim diagnostico non produce una riduzione significativa.
La RAM finale può risultare inferiore all'apertura: vengono restituite anche
pagine libere della lettura del documento. Gli indirizzi virtuali riservati
alle arene possono rimanere grandi, pur avendo restituito le pagine fisiche.

La massima qualità produce qui circa 47 volte i triangoli della bassa:
richiede realmente molta memoria. Il fix riguarda il rilascio al ritorno,
non una promessa di riduzione della mesh massima o di tutti i picchi.

## Cache grafica e prova nativa

La cache conserva una copia implicitamente condivisa del `BodyDisplay` e i
VBO. Il vecchio criterio `used + 2 < frame` attende ulteriori ridisegni per
rimuovere quelli non usati: una scena statica può trattenere la mesh alta.

Confronto nativo XCB/OpenGL su 64 feature dell'assieme, con lo stesso runner
e la stessa sonda che attende ogni frame effettivo:

| Dopo il primo frame a qualità bassa | Prima | Dopo |
|---|---:|---:|
| Storage array referenziato dalla cache | 210,562 MiB | 4,649 MiB |
| Entry cache | 128 | 64 |

Prima del fix, anche dopo i frame sufficienti a eliminare le vecchie risorse,
RSS resta circa 1,541 GiB; il trim diagnostico lo riduce a circa 0,395 GiB.
Con il fix il ritorno alla bassa produce circa 0,392 GiB e la cache contiene
solo la mesh corrente. Queste RSS comprendono l'allocazione iniziale del file
completo, poi limitato a64feature: non sono una stima per corpi omogenei.

La prova nativa sull'assieme completo è stata **interrotta** dal monitor
quando la RAM disponibile è scesa a circa3GiB (RSS sonda~8,986GiB), prima di
completare la qualità massima. Terminato soltanto il processo di prova;
originale invariato. Non si dichiara completato il ciclo GUI su1.204feature
né riprodotto esattamente il valore15,4GiB osservato dall'utente.

Il riferimento conserva i tre oggetti produttivi precedenti (cache,
viewport/setter, paintGL), ricollegati al runner con sonda aggiornata;
nessuna ricompilazione del riferimento dai sorgenti corretti. I primi
campioni grafici pilota con repaint coalescibili restano separati ed esclusi
dal confronto primario. Manifest e hash degli oggetti conservati localmente.

## Correzione e verifiche

- `DisplayCache::endFrame` rilascia le risorse non usate nel frame appena
  disegnato. Quelle correnti restano riusabili in più passate e nei frame
  successivi; risorse GL distrutte con il contesto corrente.
- Se il ritorno a una qualità inferiore riduce di oltre64MiB gli array di
  vertici/normali, il viewport rilascia la cache GL e chiama
  `releaseUnusedHeapMemory` dopo la distruzione dei temporanei dei worker.
  Su glibc usa `malloc_trim(0)`; sugli altri allocatori è un no-op.
- Nessuna modifica a oggetti vivi, Undo/Redo, geometria, politiche CUDA o
  parametri globali dell'allocatore. Le istantanee legittimamente ancora
  referenziate restano vive: il trim non le elimina.
- Statistiche diagnostiche della cache: capacità degli array referenziati,
  deduplicata per puntatore, e byte logici dei buffer pronti. Escludono B-rep,
  indici del picking e memoria interna del driver; non sommarle come RSS.

**28/28 CTest PASS** (10,95s), compresi nuovo test della durata cache,
mesh differite, snapshot/rollback, anteprime, picking/etichette e CUDA.
Il test dedicato verifica con ownership debole il rilascio nel primo frame
sostitutivo, frame vuoto, riuso stabile per50frame e risorse diverse usate
insieme. Anche la variante OpenGL nativa PASS senza errori GL.
QA nativa delle anteprime sul flacone Linux PASS, immagine ispezionata e
originale invariato. In ogni ciclo del documento la mesh bassa viene
riprodotta esattamente per vertici/normali/FaceId e i puntatori B-rep
restano invariati. Non è una nuova verifica completa di topologia o volume.

Applicazione, runner e test cache compilati in `forgecad-linux-solver`.
Mappa:490file/6657relazioni,233unità senza diagnostiche, controllo aggiornamento
PASS e collegamenti nei due versi verificati. Nessuna prova nativa Mac del
nuovo rilascio; cache condivisa, trim dell'allocatore specifico Linux/glibc.

```sh
ctest --test-dir forgecad-linux-solver -R forgecad_display_cache_memory_tests --output-on-failure
QT_QPA_PLATFORM=xcb forgecad-linux-solver/forgecad_display_cache_memory_tests --gl
QT_QPA_PLATFORM=offscreen forgecad-linux-solver/forgecad_view_tests \
  --mesh-memory-benchmark /percorso/assieme.prt
```

`--gl` abilita la prova nativa; `--memory-trim` aggiunge i campioni diagnostici
espliciti dopo il ritorno alla bassa. La correzione produttiva è già attiva
senza quell'opzione. La sonda diretta non impone limiti RAM: per le prove grandi
è stato usato un monitor esterno con riserva3GiB, tettoRSS20GiB e420s.

[Rapporto JSON](mesh-memory-cycle-2026-10-26.json). Artefatti, monitor, sorgenti
conservati e immagini in
`forgecad-linux-solver/benchmarks/mesh-memory-cycle-2026-10-26`.
Nessun commit/push, installazione desktop o processo dell'utente modificato.
