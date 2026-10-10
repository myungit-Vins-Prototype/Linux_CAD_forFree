# Mouse.prt: mesh CUDA, storyboard e dimensioni — 2026-10-10

Intervento richiesto prima delle proposte finali dell'audit prestazionale. Documento originale `File_Esempio/Mouse.prt` conservato. Copia corretta e compatta: `File_Esempio/Mouse_verificato.prt`. Ambiente Linux con Quadro RTX 3000; prove GPU effettuate fuori dalla sandbox con `prime-run`.

## Vertici fuori dalla superficie

Riproduzione sul B-rep finale originale, qualità 2: CPU 204.758 triangoli, nessun vertice con deviazione dalla propria superficie oltre 0,001 mm; vecchio CUDA 239.864 triangoli, **293 vertici errati**, deviazione massima **89,9947 mm**. Difetti sulle facce F16, F24 e F34, con punti prossimi all'origine. Una bounding box globale non li rileva: quei punti restano nel box del Mouse.

Causa in `cuda_support.cu`: upload con `cudaMemcpy` da vettori host paginabili, seguito da valutazione su stream non bloccanti. Il ritorno della copia host→device non garantisce che il trasferimento DMA sia terminato; la valutazione poteva leggere poli ancora indisponibili. Sincronizzato lo stream dell'upload prima di pubblicare l'handle della superficie. Riferimento primario: [NVIDIA, API Synchronization behavior](https://docs.nvidia.com/cuda/cuda-runtime-api/api-sync-behavior.html). Nessuna modifica al calcolo B-spline, ai B-rep o alle tolleranze.

La nuova regressione permanente carica 32 superfici grandi (1.200×31 poli), con quattro thread: backend precedente **8.838 punti errati**, corretto **0**. Restano validi i 36 confronti numerici dell'audit precedente: coprivano superfici piccole e non questo difetto di upload. Sul Mouse corretto, cinque processi nuovi a qualità 2 producono tutti 204.758 triangoli, 412.971 punti GPU e zero vertici anomali. Verificate anche qualità 0 (23.220 triangoli) e 1 (54.516), zero anomalie. Residuo massimo 0,000435699 mm su un vertice di bordo F114, presente anche sulla CPU: tolleranza dell'edge, non il difetto CUDA.

## Corpo planare rimasto acceso dopo la cucitura

Riprodotto il passaggio da `Superficie planare 1` (stadio 11) a `Cucitura 14` (stadio 12). L'albero nascondeva il corpo consumato, ma la scena continuava a usare il flag di visibilità impostato nel rollback. `applyHistoryPosition` filtra ora i corpi consumati nella posizione corrente. La preferenza dell'utente è conservata separatamente dal filtro della scena, anche dopo un cambiamento del documento e una riapertura; tornando prima della cucitura il corpo può riapparire ed essere spento. Operazioni fallite, soppresse o senza B-rep non consumano i loro operandi.

Regressioni: booleana semplice con cicli, preferenza mostrato/nascosto, cambiamento del colore, riapertura, operazione fallita, soppressione e riattivazione; sul Mouse cinque cicli planare↔cucitura, salvataggio e riapertura. Verifica CPU e copia del runner Qt ricollegata al backend CUDA reale. Rendering OpenGL NVIDIA della copia e anteprima dell'ultimo raccordo riusciti; screenshot `docs/benchmarks/mouse-2026-10-10/mouse-fixed-final.png`. Il target viewport ufficiale continua a usare il backend CPU; l'uso reale della GPU nel runner ricollegato è confermato dal contatore, 310.893 punti durante il rendering a qualità 1.

## Cache compatta senza semplificare la geometria

L'originale contiene 18 stadi, 15 B-rep; circa 111 MiB quasi interamente di cache. I B-rep serializzati ripetono molte superfici fra gli stadi: il finale contiene 325.721 poli spline. Nuova cache interna versione 5: blocchi con confini determinati dai byte, condivisi fra snapshot mediante confronto esatto; tutti i B-rep storici conservati. Le mesh degli stadi nascosti si rigenerano dal B-rep; viene salvata quella visibile. I vettori `QVector3D`, già float in memoria, vengono scritti a 32 bit anziché promossi a 64: verificata uguaglianza esatta delle mesh rilette.

| Documento | Byte | MiB |
|---|---:|---:|
| Originale | 115.950.511 | 110,58 |
| Copia verificata CPU | 26.530.772 | 25,30 |

Riduzione **77,12%**. Payload delle definizioni decompresso **identico byte per byte** (162.422 byte); ID delle feature/corpi e stadi conservati; nessuna decimazione. Le sequenze di byte dei 15 B-rep attraversano writer/reader dei blocchi senza perdita. Il B-rep dopo la lettura del documento coincide con un normale round trip binario del kernel, senza la nuova cache. Il primo confronto diretto prima/dopo deserializzazione non era appropriato: il lettore del kernel normalizza già i frame analitici e può cambiarne gli ultimi bit; il riferimento è stato corretto eseguendo lo stesso round trip ordinario, senza allargare tolleranze. La sola tassellazione lascia invece identici i byte dei B-rep in memoria.

Le cache precedenti restano leggibili; i loro B-rep sono conservati e le vecchie mesh vengono rigenerate, così eventuali artefatti CUDA già salvati non vengono perpetuati. Formato esterno ancora 39. Per usare la nuova cache occorre il binario aggiornato; una versione vecchia la ignora e prova a ricostruire il documento dalle definizioni.

Prova singola, stesso lettore aggiornato, solo lettura del documento (senza viewport/tassellazione): originale 815 ms e picco RSS 835.428 KiB; compatto 214 ms e 524.080 KiB. Dati indicativi con cache del filesystem calda, non benchmark ripetuto dell'intera applicazione.

Esperimenti scartati: eliminare geometrie morte non risparmiava byte sul Mouse; blocchi fissi da 64 KiB condividevano troppo poco, perché gli inserimenti spostano i record successivi. Nessuna semplificazione geometrica applicata.

## Verifiche e consegna

- Build Release Linux CUDA sm_75: riuscita.
- Regressione CUDA numerica, upload concorrenti e loft: riuscita su GPU reale.
- Cache: dati casuali ripetuti/spostati, round trip esatto, riferimenti e lunghezze invalidi, troncamenti: riusciti.
- Mouse: conservazione dei B-rep, mesh float, storyboard, dimensioni e riapertura: riusciti, CPU e GPU.
- Suite applicativa: **26/27 PASS** (65,31 s), esclusi runner kernel completo e test CUDA, quest’ultimo eseguito separatamente sulla GPU con PASS. Unico fallimento `forgecad_view_tests`: errore geometrico `blendEdges: impossibile ricostruire tutti i contorni del raccordo entro tolleranza; il risultato non sarebbe completamente visualizzabile`. Il target falliva già prima dell’intervento; il messaggio attuale è diverso dalla diagnosi SP-curve dell’audit precedente e non prova da solo identità di causa. Regressioni specifiche Mouse, storyboard, soppressione e cache PASS.
- `git diff --check`: pulito.

Durante le regressioni è stato individuato un riferimento pendente nel test di misura: conservava un riferimento a un elemento di `QVector`, poi aggiungeva un altro corpo e dereferenziava l'elemento invalidato. Il test ora conserva una copia. Nessuna modifica alla geometria del kernel per questa diagnosi.

Binario del lanciatore Linux aggiornato: `forgecad-release/forgecad`; precedente conservato come `forgecad-release/forgecad.before-mouse-2026-10-10`. Riavviare ForgeCAD e aprire `File_Esempio/Mouse_verificato.prt`. Il vecchio CMakeCache di `forgecad-release` conserva il percorso sorgente precedente: per ricompilare usare una build nuova, come `/tmp/forgecad-linux-cuda-review`, oppure rigenerare la directory di build. La copia `.prt` è ignorata da Git come gli altri documenti, ma presente nel checkout locale. Nessun commit o push. Il problema geometrico del flacone già segnalato nell'audit resta distinto da questo intervento.
