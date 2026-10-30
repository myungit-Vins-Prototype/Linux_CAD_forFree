# Punti delle superfici B-spline — 27 ottobre 2026

Su `solver-mesh-gpu` è stato aggiunto un percorso dedicato al calcolo del solo punto delle superfici B-spline. **Il confronto finale non dimostra un’accelerazione del CAD completo.** Il risultato resta nel branch sperimentale; il 10× globale è ancora aperto.

## Implementazione e scelta

Un solo file produttivo cambia: `kernel/fk_bspline_surface.cpp`. Per `order == 0` accumula direttamente punto e peso, evitando di preparare i buffer e la ricorrenza delle derivate A4.4. Coefficienti, prodotti, ordine delle somme, divisione finale e cancellazione nelle basi restano identici. Gli ordini superiori conservano il percorso precedente. Nessuna nuova cache, memoria persistente, tolleranza, API o formato.

Il profilo precedente contiene 1.616 campioni a 100 Hz: 10,02% flat e 25,74% inclusivo in `BSplineSurface::evaluate`. La quota inclusiva comprende altre chiamate e non predice il guadagno globale. Due candidati sono stati provati separatamente: solo punto e template anche degli ordini 1/2. Entrambi bit equivalenti; la variante estesa non mostrava un vantaggio applicativo chiaro e non è stata adottata. I numeri esplorativi sono conservati separatamente.

Codice CPU C++17 comune alle piattaforme. Applicazione completa e runner Release compilati su Linux in `forgecad-linux-solver`; nessuna build macOS, nuovo percorso CUDA/Metal o modifica a main e all’installazione desktop. Il confronto documentale usa il runner CPU, con CUDA non disponibile e zero punti CUDA.

## Operazione completa: risultato sostanzialmente invariato

Quattro processi nuovi per versione e scenario, 32 totali, ordine alternato, qualità 1 CPU. Timer: lettura più `loadDocument`; firme fuori timer, RSS include la validazione. Profiler spenti e nessuna compilazione, mappa o sonda contemporanea. Clock, temperatura, carico esterno e cache del filesystem non controllati. Cache salvate e ricostruzione sono scenari distinti.

| Scenario | Prima, mediana | Dopo, mediana | Prima/dopo |
|---|---:|---:|---:|
| mouse2, ricostruzione | 8.022 s | 8.141 s | 0.985× |
| mouse2, cache salvate | 1.247 s | 1.248 s | 0.999× |
| Assieme, cache salvate | 5.335 s | 5.355 s | 0.996× |
| Assieme, ricostruzione | 16.893 s | 16.721 s | 1.010× |

Gli intervalli prima/dopo si sovrappongono in tutti e quattro gli scenari. Non si presenta il piccolo spostamento delle mediane come un guadagno o una regressione statisticamente accertati. Sul solo punto, la microprova a quattro lotti nello stesso processo mostra circa 1,10–1,51× per i gradi frequenti 1..5; i gradi più alti sono sostanzialmente invariati, con rumore anche negativo. Questo dato del componente non si trasferisce automaticamente all’applicazione.

A parità di scenario tutti i 32 processi conservano identici byte B-rep, firme della mesh visibile, errori e visibilità. Mouse2: 36 feature, 14 schizzi, 20 B-rep, 30.366 triangoli visibili, zero errori. L’assieme contiene 1.200 corpi, non la vecchia fixture da 1.299: con ricostruzione sono validi 1.196 B-rep; i quattro importati senza geometria sorgente (274, 281, 288, 295) falliscono anche nel riferimento. Con cache sono conservati tutti i corpi validi salvati. Equivalenza degli esiti, non certificazione di ricostruzione riuscita per tutti i corpi.

## Correttezza e limiti

- 22.445.748 confronti bit per bit del kernel produttivo finale con il valutatore precedente: gradi 1..31, polinomiali e razionali, pesi 1e-4..1e4, nodi ripetuti e stretti, estremi e vicinanze a un ulp, extrapolazione, ordini 0/1/2/4, guardie e cancellazione. Due richieste realmente concorrenti nella sonda finale; la prima strumentazione avviava l’asincrono dopo la prima richiesta, corretta e rieseguita senza cambiare il prodotto.
- Regressioni permanenti: ordine 0 contro OCCT e punto del percorso generale; Bernstein tensoriale indipendente in long double fino al grado 31; nodi ripetuti/stretti, prefissi degli ordini bassi, limiti dei buffer, concorrenza sullo stesso oggetto e cancellazione senza scritture dell’output. **7/7 casi superfici passano anche sulla libreria precedente.**
- **217/219 casi kernel selezionati superati.** I due fallimenti si riproducono anche prima della modifica, con test invariati: `BlendVanishingAtTangentMitreEnd` (volume rimosso identico 0,011830715410212633, atteso 0,011296344312029305) e `BlendArcsMeetingSegments` (confronto OCCT). Restano aperti; nessuna soglia allentata o test disabilitato. Non è la suite completa del kernel.
- **44/44 CTest applicativi superati** in 13,75 secondi: superfici, loft, proiezioni, mesh, anteprime, storia, ripetizioni, soppressione, dipendenze e durata asincrona. L’elenco completo CTest comprende anche target non costruiti; non è stata eseguita la suite completa.
- QA nativa Linux/XCB su mouse2: Rigenera tutto senza errori, Raccordo 33 visibile con owner 17 e 19 facce, riselezione faccia/raggio 3→2,5, Undo/Redo e copia indipendente riaperta. Tre riaperture anteprima, cache GL ricreata: zero pixel diversi e zero errori GL. Immagini ispezionate; nessuna correzione generale delle precedenti intermittenze del renderer dichiarata.
- Mappa aggiornata, verificata e ispezionata: 496 file, 6.735 relazioni, 235 unità C++, zero diagnostiche; `--check` superato, 80.801 siti con righe valide, quattro confini nei due versi e include distinti. HTML/JSON generati ignorati da Git.

## Riproduzione e seguito

```sh
python3 tools/benchmark_document_mesh.py /percorso/copia-mouse2.prt \
  --build-dir forgecad-linux-solver \
  --reference-binary /percorso/view-before \
  --output /percorso/nuovo-lotto --no-profile --repeats 4
# Aggiungere --cached per mantenere le cache salvate.
```

Un campionamento finale separato contiene 1.534 campioni; nel contesto loft restano 285 campioni con costo di quadratura/valutazione delle curve e raffinamento dei nodi. Sono i prossimi costi da misurare insieme alle proiezioni/intersezioni; evitare di estendere altre specializzazioni in base al solo microtest. Il rapporto non somma né moltiplica i guadagni storici.

Mouse2 SHA256 `7ffa64559f38820ed0f070fa233efc27a2b5ea82ee749e04817a756cb1ae92ce`; assieme SHA256 `5b3d658321f460e826ff1e3c1415054f003ebc03325aa9e69da2a346be227791`. Copie congelate e originali ricontrollati, senza scritture di CAD, archivi, sessioni o lock dell’utente. Dati, intervalli, impronte, protocolli e microprove nel [JSON](surface-evaluation-2026-10-27.json). Binari, sonde, log e immagini locali in `forgecad-linux-solver/benchmarks/loft-refinement-2026-10-27` (nome della prima ipotesi di profilazione, mantenuto per tracciabilità). Nessun nuovo commit/push.
