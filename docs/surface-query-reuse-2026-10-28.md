# Riuso delle query sulle superfici e misura delle attese CUDA

28 ottobre 2026. Branch `solver-mesh-gpu`. Implementato e verificato su Linux.

Il riuso locale delle proiezioni identiche riduce la rigenerazione del CT attuale da **27,130 a 26,015 secondi**, circa **4,11% di tempo risparmiato**. Il risultato è incrementale: nessun 10× globale e nessun nuovo kernel CUDA.

## Percorso seguito

1. **Suddivisioni:** il precedente albero riutilizzabile non aveva migliorato il CT, quindi non è stato reintrodotto. Valgono i confronti già descritti nel [rapporto precedente](boolean-projection-filter-2026-10-28.md).
2. **Query per superficie:** misurati i riusi nei due passaggi sui semi e nei centri delle pezze della ricerca ricorsiva. Confrontati cache generale, lotto indicizzato, cache selettiva e due varianti di ricerca entro soglia. Adottata soltanto la cache selettiva. Questo riusa risultati identici nella stessa operazione; non costituisce ancora una nuova API generale per elaborare in parallelo punti diversi.
3. **CUDA:** misurati preparazione, invio, attesa, copia dei risultati e intervalli GPU. Le attese osservate non spiegano il costo applicativo. Nessun accorpamento fra facce o nuovo filtro geometrico CUDA integrato senza un vantaggio dimostrato.

## Cambiamento adottato

`ProjectionWorkspace`, interno a `kernel/fk_marching.cpp`, conserva il risultato completo di `projectPoint(surface, point)`, compresi parametri, punto e distanza. Una cache appartiene a una sola superficie immutabile e al worker della coppia in esame; non è condivisa fra thread o documenti.

La chiave contiene i bit dei tre double del punto, senza arrotondamenti, quantizzazione o fusione di punti vicini. Il limite è **4096 voci per superficie**, non un budget dichiarato in byte. La cache non cresce oltre il limite: le nuove query vengono calcolate normalmente; le voci già presenti restano riutilizzabili. Piani e cilindri usano direttamente le loro proiezioni economiche. La cancellazione viene controllata anche prima di un hit.

I due contesti sono `Marcher::traceFrom`, che interroga nuovamente i semi nei passaggi singolare e regolare, e `subdivisionSeeds`, dove più coppie di pezze possono riproporre lo stesso punto sulla stessa superficie. Ordine di ricerca, suddivisioni, semi, tolleranze e verifica geometrica restano invariati. `FK_PROFILE=1` abilita i contatori su stderr.

Sul CT con la libreria produttiva finale: **3.164.672 richieste, 1.552.644 riusi**, 334.113 richieste non memorizzate dopo il raggiungimento del limite. Picco per cache: 4096 voci. La somma delle voci delle cache create nel documento non è memoria simultaneamente residente.

## Confronto prestazionale

Tre coppie alternate di processi nuovi, qualità Media/1, app chiusa, nessuna compilazione o altro benchmark concorrente. Stesso CT congelato del precedente intervento, SHA256 `049a83c7e6686cd60903f7f54cc43f9dfa4ec133973899625b92c115042f5229`.

| Variante | Mediana | Minimo–massimo |
| --- | ---: | ---: |
| Riferimento prima di questo intervento | 27,130 s | 27,042–27,142 s |
| Cache selettiva | 26,015 s | 26,002–26,118 s |

Il riferimento include già il precedente filtro delle proiezioni degli spigoli. Non è main e non è il riferimento storico da 43 s. I rapporti di prove su versioni o salvataggi diversi non vengono moltiplicati.

Le tre coppie confrontano il candidato poi adottato. La versione produttiva differisce nel formato del codice e nell'integrazione dei contatori con `FK_PROFILE`; è stata compilata e verificata nuovamente sui tre documenti. Il suo controllo CT con diagnostica attiva dura 26,413 s: non è un ulteriore campione del benchmark. Il timer misura `rebuildAllFeatures` (Undo, geometria e mesh), escludendo caricamento, firme e presentazione UI. Tre campioni non forniscono intervalli di confidenza; frequenze e temperature non sono controllate.

Durante la prima serie l'app era stata riaperta per la prova manuale. Il processo non era visibile al controllo nella sandbox; il controllo sul sistema host l'ha rilevato. L'utente l'ha chiusa e tutti i tempi precedenti sono stati esclusi dal confronto finale. I controlli di uguaglianza geometrica di quei processi restano validi.

## CUDA: dove viene speso il tempo

Sonda sul mouse2 congelato, Quadro RTX 3000, app chiusa, contesto CUDA verificato prima dei timer. Nella rigenerazione: **26 lotti riusciti, 50.732 punti**, da 621 a 6142 punti per lotto.

| Misura diagnostica aggregata | Tempo |
| --- | ---: |
| Preparazione host e acquisizione buffer | 1,057 ms |
| Submission, inclusi eventi diagnostici | 0,707 ms |
| Attesa host sullo stream | 33,151 ms |
| Copia dall'area pinned agli output | 0,223 ms |
| Intervallo eventi H2D | 0,720 ms |
| Intervallo eventi attorno al kernel | 29,583 ms |
| Intervallo eventi D2H | 1,418 ms |

La rigenerazione diagnostica dura 7,266 s. Queste somme si sovrappongono fra stream e fra misure host/device: **non vanno sommate fra loro né interpretate come quote wall-time**. Gli eventi attorno al kernel possono includere attese di scheduling; non misurano esclusivamente istruzioni GPU attive. Gli eventi creati per ciascun lotto rendono la sonda invasiva. Sono contati i lotti arrivati alla valutazione; le richieste rifiutate prima di acquisire uno slot restano al fallback CPU.

Il primo profilo, prima della chiusura dell'app, aveva 27 lotti e 1,816 ms di attesa aggregata: è conservato solo come dato esplorativo. La variabilità impedisce di trattare un singolo tempo come latenza fissa. Entrambi i profili indicano un costo piccolo rispetto alla rigenerazione: nessun beneficio applicativo da CUDA o da un nuovo accorpamento è dichiarato. I dati delle superfici erano già residenti per la durata dell'acceleratore.

La scelta di misurare il carico reale prima di trasferire altri calcoli e di includere trasferimenti e sincronizzazioni segue la [CUDA C++ Best Practices Guide di NVIDIA](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html). Il seguito utile resta condividere preparazione e ricerca per query geometriche diverse e individuare lotti realmente dominanti; l'attuale valutatore mesh non è un backend geometrico esatto.

## Verifiche e varianti non adottate

- **50/50 test kernel:** Marching 10, Boolean 38, cancellazione 2. Include tangenze, rami singolari, confronto OCCT e concorrenza.
- Nuova regressione `MarchingProjectionCacheOverflow`: due pezze piane NURBS ortogonali, 4101 semi distinti fuori dalle superfici e un seme valido finale; nessuna alterazione delle curve, SP-curve o classificazioni. Passa anche sul riferimento senza cache.
- **8/8 CTest applicativi:** storia booleane/raccordi/completa, cache, anteprime, riapertura, soppressione e Rigenera tutto.
- CT, mouse2 e assieme con **B-rep, mesh completa, errori e visibilità identici** al riferimento. CT e mouse2 senza errori; assieme con gli stessi quattro errori degli importati privi di sorgente. File di ingresso invariati.
- App completa `forgecad-linux-solver/forgecad` e runner compilati. Soli warning preesistenti nei test tavole. Nessuna installazione desktop o compilazione macOS.
- Mappa rigenerata e verificata: 499 file, 6789 relazioni, 236 unità C++, zero diagnostiche; collegamenti controllati e dettaglio del marching ispezionato visivamente.

I piloti a processo singolo, dopo la chiusura dell'app, sono riportati nel JSON: cache generale 26,021 s; lotto indicizzato 26,539 s; filtro seguito dal minimo globale 27,489 s; query diretta entro soglia 26,850 s; riferimento 27,071 s. Non sono confronti confermativi e non dimostrano la superiorità generale di una variante. Solo la cache selettiva ha ricevuto le tre coppie finali.

La query diretta entro soglia ha superato un controllo sperimentale con 1350 confronti, anche a un ULP dalla distanza minima e sui nodi; non è stata integrata. Nessuna delle cache ricorsive, del lotto indicizzato o delle modifiche sperimentali CUDA è rimasta nel codice produttivo.

## Artefatti e riproduzione

[Dati completi](surface-query-reuse-2026-10-28.json). Sonde, input CT congelato, libreria di riferimento, sorgenti sperimentali, log, profilo CPU e cattura della mappa sono nella directory locale ignorata `forgecad-linux-solver/benchmarks/surface-query-reuse-2026-10-28`.

Per ripetere una coppia CPU con l'app chiusa:

```sh
prove=/home/myungit/Linux_CAD_forFree/forgecad-linux-solver/benchmarks/surface-query-reuse-2026-10-28
for versione in baseline production; do
  QT_QPA_PLATFORM=offscreen QT_QPA_OFFSCREEN_NO_GLX=1 \
  FORGECAD_GPU_BROAD_PHASE=0 FORGECAD_EAGER_MESH=0 \
  "$prove/$versione" "$prove/ct-current.prt" 1
done
```

Confrontare `rebuild_seconds`, `bodies` e `rebuild_errors`; ripetere invertendo l'ordine. Per misure prestazionali non impostare `FK_PROFILE`, `FORGECAD_PROFILE`, `LD_PRELOAD` o `CPUPROFILE`. Le sonde isolano impostazioni e recupero e assegnano un'identità indipendente al documento. Binari locali Linux, non portabili. Nessun commit/push.
