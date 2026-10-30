# Metodi generali di prestazione — 27 ottobre 2026

Introdotto su `solver-mesh-gpu` un metodo condiviso per evitare ricerche globali quando basta verificare uno scarto entro tolleranza. Si applica a importazione e controlli delle booleane, senza condizioni su file, dimensioni o nomi delle feature. La rigenerazione dell’assieme passa da **15.135 a 12.680 secondi**, **16.22% di tempo in meno**, nelle mediane di quattro processi per versione. I risultati sugli altri documenti sono riportati sotto: nessun 10× globale dichiarato.

## Metodo e contratto

`surfaceDeviationAboveTolerance` in `kernel/fk_surface_distance.h/.cpp` restituisce zero se lo scarto è entro il budget, altrimenti la distanza del proiettore globale originale. Un punto concreto della superficie entro metà del budget certifica il caso positivo: la distanza minima non può essere maggiore di quella di quel punto. Fino a 12 passi locali cercano questo certificato, con parametri limitati al dominio e una verifica finale mediante `surface.point`. Un tentativo fallito o singolare usa `projectPoint`; sotto la risoluzione lineare del kernel si usa sempre il percorso originale.

Il punto candidato serve soltanto a decidere entro/fuori soglia. Non sostituisce il punto più vicino richiesto per fitting, normali o parametri UV. `projectPoint` e le sue formule, la politica delle tolleranze e i formati rimangono invariati. Nell’importatore il metodo è usato esclusivamente nei due massimi per i quali gli scarti fino a 1e-7 erano già ignorati; un massimo oltre soglia conserva la distanza precedente. Le booleane lo usano in cinque controlli che confrontavano soltanto distanza e tolleranza. Gli altri impieghi delle proiezioni rimangono completi.

La stima per campioni consecutivi è una variabile locale del chiamante, senza cache persistente o stato condiviso aggiunto. Cancellazione e ripiego restano espliciti. È C++17 CPU comune alle piattaforme; non è un nuovo kernel CUDA/Metal.

## Diagnosi e priorità dell’intero branch

Il profilo dell’assieme corrente contiene 17.542 campioni CPU aggregati fra thread: 50,4% inclusivo nelle proiezioni B-spline, 38,5% nella tassellazione e 55,9% nell’importazione. Le quote si sovrappongono e non sono percentuali del tempo wall. Un diagnostico separato su mouse2, con otto schizzi e modifiche valide selezionate fuori timer, misura circa 0,54ms aggregati per l’analisi partizionata e 0,56ms per il solver. Era eseguito durante altre verifiche: serve a scegliere le priorità, non è un nuovo confronto formale del solver.

La priorità scelta è ridurre il lavoro geometrico richiesto. Un prototipo che anticipava Newton nel proiettore globale non mostrava un beneficio chiaro e cambiava alcune firme: non adottato. Un secondo prototipo di riuso dei buffer e riduzione delle copie nei calcoli di Bernstein conserva i byte nei campioni ma mostra vantaggi applicativi piccoli, senza conferma statistica: non adottato. Di questo seguito è implementato soltanto il nuovo metodo certificato e lo strumento di misura.

La sonda separata della versione finale conta 312.512 certificati su 724.846 interrogazioni B-spline ammesse nell’assieme; mouse2 24/25, CT0578 1031/2498. Conta tutto il processo caricamento con cache più Rigenera tutto. Tempi strumentati esclusi dai confronti: i contatori spiegano il numero diverso di opportunità, non misurano un guadagno.

## Pipeline completa

24 processi nuovi sequenziali: quattro per versione e documento, ordine delle versioni alternato e dei documenti ruotato. Qualità media CPU. Nessuna compilazione, mappa, prova o profilazione contemporanea ai lotti. Clock, temperatura, carico esterno e cache del filesystem non controllati. Cache: `loadDocument`, lettura precedente esclusa. Rigenera tutto: metodo reale del controller, con Undo/geometria/normalizzazione/preparazione mesh, escludendo caricamento iniziale, firme e presentazione OpenGL/albero.

| Scenario | Prima, mediana | Dopo, mediana | Tempo risparmiato |
|---|---:|---:|---:|
| Assieme (1.200 corpi): cache | 3.836 s | 3.809 s | 0.69% |
| Assieme (1.200 corpi): Rigenera tutto | 15.135 s | 12.680 s | 16.22% |
| mouse2: cache | 0.814 s | 0.809 s | 0.63% |
| mouse2: Rigenera tutto | 7.122 s | 7.104 s | 0.25% |
| CT0578: cache | 5.071 s | 5.063 s | 0.15% |
| CT0578: Rigenera tutto | 27.160 s | 26.983 s | 0.65% |

Campioni e intervalli completi nel JSON; non sommare questi rapporti a quelli di altri file o salvataggi. I 24 processi conservano identici B-rep, errori, visibilità, facce e mesh complete, incluse normali/bordi/ID/trasformazioni. Mouse2 e CT0578 hanno zero errori; nell’assieme restano gli stessi quattro importati privi di sorgente agli indici 274/281/288/295. Le identità dei file congelati e gli SHA degli originali a fine prova sono nel JSON. Nessun CAD, archivio, sessione o lock originale scritto dalle sonde; tutte le rigenerazioni e la QA usano identità indipendenti.

## Prova riproducibile del metodo

`tools/benchmark_surface_distance.py` genera una sonda C++ dalla configurazione CMake corrente e confronta lo stesso compito: zero entro soglia, distanza completa oltre soglia. Tre processi nuovi; otto casi ruotati; sette lotti alternati per metodo e 128 query ordinate per caso. Superfici piane/curve, polinomiali/NURBS, una o più pezze, casi tutti vicini/misti/tutti esterni. Cache scaldate prima del timer; risultati confrontati fuori timer, compresi tutti quelli dei lotti temporizzati. Non è una prova del CAD completo.

Mediane dei processi: **71.6–91.8×** sui soli controlli di punti vicini, **1.68–1.75×** sui carichi misti. Tutti esterni: **0.98–0.99×**, con il piccolo costo aggiunto del tentativo prima del ripiego globale. La frazione di casi certificabili determina il vantaggio.

```sh
python3 tools/benchmark_surface_distance.py --build-dir forgecad-linux-solver \
  --output /tmp/surface-distance-new-run --processes 3
```

Sonda CPU senza file CAD, compilata dal tool contro la libreria effettiva. Verificata qui su Linux; una compilazione o misura nativa macOS non è stata eseguita.

## Verifiche e stato

- 3/3 nuove regressioni: piano analitico, soglie 0/1e-14/1e-7/1e-4 e confini, superfici libere, stime fuori dominio/non finite, degenerazioni, concorrenza e cancellazione. Superate anche con adattatore di riferimento che usa il vecchio proiettore. Per distanze oltre soglia i risultati sono confrontati bit per bit.
- 231/233 casi kernel selezionati: restano `BlendVanishingAtTangentMitreEnd` e `BlendArcsMeetingSegments`, già riprodotti sul riferimento nei rapporti precedenti; nessun test disabilitato o soglia produttiva allentata. Prima fixture analitica lontana chiedeva erroneamente errore assoluto 2e-12: corretta secondo il margine relativo già presente nel proiettore, confermando gli stessi valori sul riferimento. Non è la suite completa.
- 47/47 CTest applicativi superati, 16,10s, più 1/1 CTest del contratto tolleranze (5/5 casi kernel), 48/48 distinti: inclusi solver, storia, anteprime, mesh, booleane e dipendenze; 7/7 casi STEP/IGES superati sulla libreria finale, con roundtrip, cuciture e poli.
- QA nativa mouse2: modifica faccia/raggio, Undo/Redo, tre anteprime e copia indipendente. Zero pixel diversi dopo ricreazione della cache grafica e zero errori OpenGL. Nessuna correzione generale del renderer dichiarata.
- Mappa aggiornata, controllata e ispezionata: 499 file/6760 relazioni/236 unità, zero diagnostiche, 81018 siti entro le righe e sei confini nei due versi. Include distinti dalle chiamate; la sonda C++ generata dal Python non è una TU registrata nella mappa. Il primo controllo cercava Surface::point nell’header: corretto sulla definizione reale fk_surface.cpp, senza cambiare il prodotto. Artefatti generati ignorati da Git.

Applicazione completa e runner Release Linux compilati: `/home/myungit/Linux_CAD_forFree/forgecad-linux-solver/forgecad`. Installazione desktop e main preservati; nessun nuovo commit/push. Dati e impronte nel [JSON](general-performance-methods-2026-10-27.json), artefatti locali in `forgecad-linux-solver/benchmarks/general-performance-2026-10-28`.

Le prossime strategie generali da valutare sono lotti di query per superficie, certificati su interi tratti e riuso delle SP-curve, raffinamento della mesh ed efficienza delle valutazioni per lotti. Il solver sparso sui gruppi larghi e i backend CUDA/Metal restano nella roadmap: vanno scelti con misure del collo di bottiglia e verifiche sulla macchina reale. Il 10× complessivo dell’applicazione resta aperto.
