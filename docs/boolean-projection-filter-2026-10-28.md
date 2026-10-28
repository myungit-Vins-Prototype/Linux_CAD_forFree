# Booleane: filtro delle proiezioni entro tolleranza

28 ottobre 2026. Branch `solver-mesh-gpu`. Implementato e verificato su Linux.

Ridurre le proiezioni inutili accelera la booleana Sweep10 del CT storico di **4,55×** e la rigenerazione del CT attuale di **1,56×**. Il 10× globale resta aperto. CUDA è stata misurata sulla versione ottimizzata, senza un vantaggio applicativo dimostrato.

## Cambiamento adottato

`BooleanBuilder::splitAtPoints` deve inserire un parametro soltanto se il punto è entro tolleranza dalla curva. Per le curve diverse da rette e cerchi usa prima `projectPointCandidatesWithin`; quando il filtro trova candidati esegue ancora la proiezione globale originale. Restano invariati parametro scelto, ordinamento, pareggi fra minimi, geometria e tolleranze.

`bsplineCandidates` seleziona con due ricerche binarie i soli tratti Bézier che intersecano il range. Entrambi i lati dei nodi sono inclusi, anche per intervalli puntuali. Prima il filtro scandiva tutti i tratti della curva: applicarlo senza questa seconda modifica peggiorava Sweep10 a 71,186 s nel campione esplorativo. Questa variante è stata scartata.

## Misure finali

Tre coppie di processi nuovi per confronto, ordine alternato, qualità 1, nessuna build/profilazione/altro benchmark durante i lotti. App utente chiusa. Mediana e minimo–massimo, secondi:

| Scenario | Riferimento / CPU | Nuovo / CUDA | Esito |
| --- | ---: | ---: | --- |
| CT attuale, Rigenera tutto | 42.961 (42.905–43.441) | 27.528 (27.298–28.245) | 1,56×; 35,92% di tempo risparmiato |
| CT storico, sola booleana Sweep10 | 18.752 (18.688–19.177) | 4.118 (4.104–4.126) | 4,55×; 78,04% di tempo risparmiato |
| mouse2, Rigenera tutto CPU/CUDA | 7.684 (7.574–7.698) | 7.672 (7.595–7.774) | Intervalli sovrapposti; nessun vantaggio dimostrato |
| CT attuale, caricamento con cache | 10.581 (10.561–10.944) | 10.852 (10.354–10.900) | Intervalli sovrapposti; nessun vantaggio dimostrato |

Il caricamento misura `loadDocument`, escludendo lettura file e presentazione OpenGL/albero. Rigenera tutto è il comando reale del controller e comprende Undo, ricostruzione e mesh, escludendo caricamento iniziale, firme e presentazione. Il timer Sweep10 esclude costruzione dello strumento e controlli finali. Le sonde differiscono soltanto per la libreria kernel di riferimento/finale; CPU/CUDA soltanto per il valutatore. Tre campioni non danno intervalli di confidenza; frequenze, temperatura e carico esterno non sono controllati.

CT attuale: SHA256 `049a83c7e6686cd60903f7f54cc43f9dfa4ec133973899625b92c115042f5229`, copiato dal file utente prima delle prove e originale riconfermato invariato. CT storico: `b87b96efa0e8971d89f8fe5bdad5b63d8119831d3f09430122d51770dc851fbd`. Sono salvataggi diversi; non confrontare direttamente le durate né moltiplicare speedup con rapporti precedenti. Il riferimento è il checkout prima di questo intervento, comprese le modifiche locali già presenti, non main.

## CUDA reale

Quadro RTX 3000 da 6 GiB. Sul CT storico alla qualità scelta il contatore registra **zero punti CUDA**: la sonda non è un benchmark di accelerazione GPU. Il controllo obbligatorio ha segnalato il caso e il confronto è stato spostato su mouse2.

Su mouse2 tre processi CUDA hanno elaborato **47.063–52.468 punti**; mediana CPU 7,684 s, CUDA 7,672 s. La differenza è circa 0,16% e gli intervalli si sovrappongono. Nessuna nuova accelerazione CUDA dimostrata. Nessuna modifica a kernel CUDA, specializzazioni, budget o tolleranze. La disponibilità/context initialization precede i timer del documento; upload delle superfici, trasferimenti e attese durante la rigenerazione sono inclusi. Conservato l’ambiente `CUDA_DISABLE_PERF_BOOST=1`.

Verifica numerica separata sulla libreria finale: 30.366 triangoli; vertici, topologia, bordi e metadati identici. Tre normali differiscono al massimo di **1,1641532182693481e-10**, senza normali nulle, degenerazioni o inversioni aggiunte. Soglie diagnostiche preesistenti 1e-5 mm per posizioni e 1e-5 per normali; nessuna tolleranza produttiva cambiata. La verifica indicizzata non prova una distanza continua fra tutte le superfici.

## Correttezza e stato della build

- 65/65 casi kernel distinti: 38 Boolean, 24 Project, 1 CurveLength, 2 cancellazione. Inclusi contatti, parallelismo, volumi delle lamelle spline fino a 1e-4 mm e nuova regressione analitica su nodi/range puntuali di spline lunghe.
- Oracolo codice precedente/nuovo: 31.920 interrogazioni, 500.233 valori double identici bit per bit; 2D/3D, gradi 1–5, pesi, range e soglie. La nuova regressione passa anche sul kernel di riferimento.
- 53/53 CTest distinti, zero skip hardware. Primo lotto 52/53: mancava `/tmp/sketch-story`, richiesta per una cattura; stesso errore sul riferimento. Creata la directory, ripetuto soltanto quel test con successo. Nessuna modifica al test della storyboard.
- B-rep, mesh completa, errori e visibilità identici prima/dopo su CT attuale, mouse2 e assieme. CT e mouse2 senza errori; assieme con gli stessi quattro importati privi di sorgente. Le verifiche aggiuntive sono funzionali, non campioni prestazionali.
- App completa `forgecad-linux-solver/forgecad`, runner e target pertinenti compilati. Warning preesistenti nei test tavole. Nessuna installazione desktop o compilazione macOS.
- Mappa aggiornata: 499 file, 6786 relazioni, 236 unità, zero diagnostiche, 81.637 siti validati; arco chiamante/filtro verificato nei due versi e blocco visuale ispezionato. HTML/JSON generati ignorati da Git.

## Esperimenti scartati e seguito

L’albero riutilizzabile delle suddivisioni superficie/superficie conservava i risultati ma non migliorava CT: in tre coppie Sweep10 18,806→18,996 s, Rigenera tutto 26,111→26,198 s. Non integrato. La cache curva/superficie mostrava soltanto 1989 riusi su 14694 richieste; i tempi di quella fase erano disturbati dall’app aperta e sono esclusi.

Il prossimo passo verso CUDA richiede misurare le fasi residue del CT attuale e raggruppare le interrogazioni geometriche più costose. La sola valutazione GPU della mesh, nel caso verificato, non sposta il tempo complessivo. Non sono implementati né dichiarati un nuovo backend geometrico CUDA o il 10× applicativo.

## Artefatti e riproduzione

[Dati e campioni completi](boolean-projection-filter-2026-10-28.json). Sorgenti delle sonde, libreria di riferimento, binari finali, copie congelate, log e snapshot numerici sono locali nella directory ignorata `forgecad-linux-solver/benchmarks/boolean-projection-filter-2026-10-28`. Le sonde usano `forkDocumentIdentity` e impostazioni/recupero temporanei. Nessun CAD originale scritto, nessun commit/push.

Esempio da eseguire con i percorsi assoluti alla directory degli artefatti:

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_OFFSCREEN_NO_GLX=1 FORGECAD_GPU_BROAD_PHASE=0 FORGECAD_EAGER_MESH=0 /percorso/artefatti/final-reference /percorso/artefatti/ct-current.prt 1
QT_QPA_PLATFORM=offscreen QT_QPA_OFFSCREEN_NO_GLX=1 FORGECAD_GPU_BROAD_PHASE=0 FORGECAD_EAGER_MESH=0 /percorso/artefatti/final-cpu /percorso/artefatti/ct-current.prt 1
```

Alternare processi separati e confrontare gli array `bodies`/`rebuild_errors`. Per Sweep10 aggiungere `split` e usare `input.prt`. Per CUDA usare `final-cuda` con `STUDY_REQUIRE_CUDA=1` e il mouse2 identificato nel JSON, verificando `rebuild_cuda_points > 0`. I binari sono Linux locali, non artefatti portabili.
