# Parallelismo del kernel e proiezioni degli apici

28 ottobre 2026. Branch `solver-mesh-gpu`, checkpoint `c57e0e1` con modifiche locali precedenti preservate. Implementato e verificato su Linux.

Sul CT attuale, la rigenerazione passa da **26,645 a 17,214 secondi**: **35,396% di tempo in meno**, circa **1,548×**. Il cambiamento adottato evita tre proiezioni globali molto costose tramite un filtro conservativo. Il prototipo di parallelismo aggiuntivo è stato provato, ma resta sperimentale. Nessun nuovo kernel parallelo o CUDA integrato e nessun 10× globale dimostrato.

## Scalabilità del percorso esistente

CPU Xeon W-10885M, **8 core fisici e 16 thread logici**. Il kernel parallelizza già coppie di facce, divisione/classificazione delle facce e altri lavori indipendenti. `parallelDepth` impedisce di moltiplicare i thread nei cicli annidati.

| Thread automatici nella sonda | Tempo rigenerazione | CPU processo nella finestra | CPU/tempo trascorso |
| --- | ---: | ---: | ---: |
| 1 | 60,963 s | 63,914 s | 1,048 |
| 2 | 40,113 s | 64,705 s | 1,613 |
| 4 | 31,825 s | 68,325 s | 2,147 |
| 8 | 26,504 s | 70,691 s | 2,667 |
| 16 | 27,018 s | 103,344 s | 3,825 |

Un processo nuovo per configurazione, ordine 16/1/8/2/4, app chiusa, qualità Media/1. Tutti i risultati geometrici coincidono. Questi sono campioni esplorativi: non bastano a cambiare il default globale a 8 thread. La sonda definisce localmente `std::thread::hardware_concurrency()` tramite `STUDY_THREADS`, controllando i worker automatici del kernel; non impone un limite a tutti i thread Qt o di librerie e non cambia il sistema. CPU/tempo trascorso è un indicatore aggregato del processo, non una misura di utilizzo per core fisico.

Il comportamento oltre 8 è compatibile con SMT e con la coda seriale osservata: 16 thread logici condividono le risorse degli 8 core. Questo non dimostra da solo saturazione hardware o una frazione seriale universale.

## Collo di bottiglia individuato

La traccia temporanea di `computeCuts` mostra tre fasi da **3,232 / 3,248 / 3,337 s**, dominate ciascuna da una sola coppia B-spline/cono da **3,227 / 3,242 / 3,333 s**. Le coppie partono quasi subito, entro 0,25 ms: cambiare l'ordine della coda non elimina questa attesa finale. Altre fasi usano già molti worker efficacemente.

`Marcher::registerApexes` proietta l'apice del cono sull'altra superficie solo per decidere se è entro tolleranza. In tre query B-spline la ricerca del minimo globale visita circa **1,745–1,748 milioni di nodi**, rifinisce circa **169 mila foglie** e supera **511 mila nodi** di coda. Il profilo CPU precedente conferma questo percorso; i campioni inclusivi aggregati fra thread non vengono trattati come frazioni del tempo trascorso.

## Prototipo parallelo effettivamente provato

Preparazione anticipata parallela di centri, limiti e rifiniture indipendenti; consumo dei risultati nell'ordine originale della coda. Cache diretta limitata a 4096 slot, lotti con frequenza limitata, una sola query proprietaria per processo. Eccezioni differite fino al punto in cui il lavoro originale le avrebbe osservate e cancellazione propagata. Tutti i piloti completati hanno B-rep e mesh identici.

| Variante | Worker proiezione | Lotto | Tempo rigenerazione | CPU processo |
| --- | ---: | ---: | ---: | ---: |
| move | 0 | 0 | 27,494 s | 105,343 s |
| bounded-spec1 | 1 | 64 | 31,008 s | 106,760 s |
| bounded-spec4 | 4 | 64 | 26,053 s | 108,985 s |
| large-spec4 | 4 | 512 | 26,687 s | 109,427 s |
| large-spec8 | 8 | 512 | 25,694 s | 111,415 s |

`move` è il controllo seriale che sposta i nodi invece di copiarli. Le configurazioni 1/4 worker con lotto 64 costituiscono un controllo della stessa preparazione anticipata; il costo a un worker evidenzia anche l'overhead introdotto. Quelli con lotto 512 sono una seconda configurazione, non repliche dello stesso esperimento. Il miglior pilota da 25,694 s contro il controllo da 27,494 s è promettente ma singolo; usa più CPU. Non dimostra un guadagno stabile o trasferibile ad altri documenti.

L'executor temporaneo aggiunge worker a quelli delle booleane e non risolve ancora il budget condiviso fra operazioni. Non è stato adottato. La prima variante senza limite alla preparazione ha generato lavoro eccessivo: è stata fermata esclusivamente la nostra sonda, il campione incompleto è escluso. Nessun processo dell'utente terminato.

## Cambiamento adottato

Aggiunta `surfaceMayBeWithinTolerance` in `kernel/fk_surface_distance.cpp/.h`, usata prima della proiezione degli apici in `kernel/fk_marching.cpp`.

- Le B-spline accettano già soltanto pesi finiti positivi. I box dei poli delle pezze Bézier contengono la superficie e il BVH è già memorizzato in `cachedPatchTree`.
- Un nodo viene escluso solo se un asse separa punto e box oltre tolleranza più margine numerico: `512 * epsilon * (uDegree+1) * (vDegree+1) * max(1, abs(lo), abs(hi), abs(point))`.
- Se nessuna foglia rimane possibile, il punto è scartato. Un `true` significa soltanto candidato: la proiezione globale e la verifica geometrica precedenti restano necessarie. Altre superfici e casi incerti mantengono quel percorso.
- Nessuna modifica a tolleranze, precisione, ordine dei semi o thread predefiniti. Traversata con stack locale e controlli di cancellazione; nessuna nuova cache persistente.

Il primo pilota del filtro misura 17,567 s. Il confronto seguente usa la libreria produttiva finale compilata, non quel sorgente temporaneo.

## Confronto formale finale

Tre coppie di processi nuovi, ordine riferimento/produttivo, produttivo/riferimento, riferimento/produttivo; **16 thread automatici**, qualità Media/1, tassellazione CPU e broad phase GPU disabilitata. App chiusa e controllata sul sistema host prima di ogni processo; nessuna compilazione, suite o altro benchmark concorrente. Durante l'indagine l'utente aveva riaperto la versione desktop: il controllo ha fermato il pilota prima delle misure, poi l'utente l'ha chiusa nuovamente.

CT congelato SHA256 `049a83c7e6686cd60903f7f54cc43f9dfa4ec133973899625b92c115042f5229`. Il riferimento include già i precedenti interventi sulle proiezioni e il riuso locale: non è main né il vecchio riferimento da 43 s. Non moltiplicare rapporti provenienti da checkpoint o documenti diversi.

| Variante | Mediana | Minimo–massimo |
| --- | ---: | ---: |
| Riferimento | 26,645 s | 26,630–27,019 s |
| Filtro adottato | 17,214 s | 17,163–17,370 s |

Timer su `rebuildAllFeatures`, compresa preparazione Undo, geometria e mesh; caricamento, impronte e presentazione UI esclusi. CPU da `getrusage` nella stessa finestra. Tutti e sei i processi hanno **B-rep di tutte le feature, mesh visibile completa, errori e visibilità identici**. Tre campioni non sono un intervallo di confidenza; clock, temperature e carico esterno non sono controllati.

## Verifiche e stato della build

- **90/90 casi kernel distinti PASS**: filtri Surface 44, Marching 10, Boolean 38, cancellazione 2; quattro casi compaiono in più filtri. Include confronti OCCT e rami singolari.
- Nuova regressione `SurfaceToleranceConservativeBounds`: oracolo analitico piano con più pezze, NURBS/pesi, scale 1e-6/1/1e6, bordi/nodi, soglie e ULP, punti lontani, superfici casuali, degenerazioni, concorrenza, input invalidi e cancellazione.
- **8/8 CTest applicativi PASS**: storia booleane/raccordi/completa, cache, anteprime, riapertura, soppressione e rigenerazione.
- Ulteriori rigenerazioni di mouse2 e assieme: B-rep, mesh visibile, errori e visibilità identici al riferimento precedente. Mouse2 senza errori; assieme con gli stessi quattro errori degli importati privi di sorgente. Input invariati. Queste sono verifiche funzionali, non ulteriori confronti prestazionali.
- App completa `forgecad-linux-solver/forgecad` e runner ricompilati; installazione desktop invariata. Nessuna nuova verifica macOS, commit o push.
- Mappa rigenerata: 499 file, 6794 relazioni, 236 unità C++, zero diagnostiche e `--check` PASS. Chiamate `registerApexes → surfaceMayBeWithinTolerance → cachedPatchTree` verificate in entrata/uscita e distinte dagli include; blocco ispezionato visivamente.

## Artefatti e seguito

[Dati e campioni](kernel-parallelism-2026-10-28.json). Sorgenti temporanei, riferimenti congelati, binari Linux, input, JSON, log e cattura della mappa nella directory locale ignorata `forgecad-linux-solver/benchmarks/kernel-parallelism-2026-10-28`. Gli script sperimentali conservano i percorsi della sessione; i binari archiviati consentono di ripetere il confronto finale:

```sh
prove=/home/myungit/Linux_CAD_forFree/forgecad-linux-solver/benchmarks/kernel-parallelism-2026-10-28
for versione in reference production; do
  QT_QPA_PLATFORM=offscreen QT_QPA_OFFSCREEN_NO_GLX=1 \
  FORGECAD_GPU_BROAD_PHASE=0 FORGECAD_EAGER_MESH=0 STUDY_THREADS=16 \
  "$prove/$versione" "$prove/ct-current.prt" 1
done
```

Ripetere invertendo l'ordine e confrontare `rebuild_seconds`, `bodies`, `rebuild_errors`; app chiusa, senza `FK_PROFILE`, `FORGECAD_PROFILE`, `CPUPROFILE` o `LD_PRELOAD`. Sonde con impostazioni/recupero isolati e identità documento indipendente. Per la prova manuale usare questa build di sviluppo, qualità Media e **Modifica → Rigenera tutto / Ctrl+B**; il tempo UI può differire dalla sonda.

La verifica dimostra che il parallelismo interno delle proiezioni è tecnicamente possibile, ma richiede granularità e budget condiviso adeguati. Prima di integrarlo va riprofilato il percorso ora alleggerito: le tre query più care sono state eliminate nei casi certamente lontani. Restano possibili lotti CPU/GPU sulle query residue indipendenti; nessuna accelerazione CUDA ulteriore è stata misurata in questo intervento.
