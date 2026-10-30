# Valutazioni B-spline: seguito delle ottimizzazioni — 27 ottobre 2026

Su `solver-mesh-gpu` la ricostruzione del mouse2 aggiornato passa da **9.295 a 7.896 secondi**: **1.177×**, 15.05% di tempo in meno. Confronto con lo stesso codice locale precedente, comprensivo delle ottimizzazioni e correzioni già presenti. Il 10× globale resta aperto. Il documento ha ora36feature/14schizzi, SHAa45aa164..., e differisce dalle fixture precedenti: non sommare o moltiplicare i rapporti storici.

## Implementazione

Questo intervento modifica un solo file produttivo: `kernel/fk_bspline_basis.cpp`. L'algoritmo A2.3 resta lo stesso; gradi1..5 e ordini0..2 hanno dimensioni note al compilatore, eliminando lavoro generico ripetuto. Per gli altri ordini resta l'implementazione generale con grado fisso; gradi rimanenti usano il percorso generale completo. Nessun cambiamento a formule, tolleranze, somme, API, thread, documenti o cache persistenti. Ogni chiamata mantiene la cancellazione.

È codice CPU C++17 comune; non è un nuovo kernel CUDA/Metal. Linux compilato e verificato, macOS non compilato in questo intervento. Main e installazione desktop invariati.

## Confronto ripetuto dell'operazione completa

Quattro processi nuovi per versione/scenario, ordine alternato, qualità1CPU. Profiler spenti; nessuna compilazione, mappa o altra sonda contemporanea al lotto. Timer=lettura+loadDocument; firme fuori timer, RSS include la validazione. Clock/temperatura/carico esterno non controllati, filesystem non svuotato. Con cache e ricostruzione sono scenari distinti.

| Operazione | Prima, mediana | Dopo, mediana | Prima/dopo | Tempo in meno |
|---|---:|---:|---:|---:|
| mouse2, ricostruzione | 9.295 s | 7.896 s | 1.177× | 15.05% |
| mouse2, cache salvate | 1.363 s | 1.315 s | 1.037× | 3.59% |
| Assieme, cache salvate | 4.951 s | 4.778 s | 1.036× | 3.49% |
| Assieme, ricostruzione | 19.565 s | 16.945 s | 1.155× | 13.39% |

Il vantaggio con cache è piccolo; per l’assieme gli intervalli si sovrappongono, quindi non viene presentato come un miglioramento robusto.

mouse2 ricostruito:20B-rep,30.422triangoli visibili,0errori. Intervalli prima9.268–9.537s, dopo7.682–8.149s. RSS massimo mediano1288.0→1272.8MiB; nessuna promessa di riduzione della RAM.

L'assieme contiene1200corpi e0schizzi, SHA5b3d6583...; non è la vecchia fixture1299corpi. Nella ricostruzione restano1196B-rep: i4importati senza sorgente (274,281,288,295) falliscono anche nel riferimento. Confronto equivalente degli esiti, non certificazione di ricostruzione riuscita per tutti i corpi.

Tutti i32processi conservano identici byte B-rep, firme della mesh visibile, errori e visibilità a parità di scenario. Originali e copie verificati conSHA256; nessuna scrittura in file/archivi/sessioni CAD utente.

## Verifiche e diagnosi

- 1.662.384confronti bit per bit con il valutatore precedente:5semi, gradi1..31, ordini0..5, nodi ripetuti, estremi/limiti sinistri/destri, concorrenza. Microprova5lotti alternati in un solo processo:1,69–4,30× nei gradi piccoli, alto grado sostanzialmente invariato; dato diagnostico del componente, non misura dell'applicazione.r
- 121/121casi kernel selezionati PASS, inclusi quattro nuovi test permanenti. OCCT su curve2D/3D razionali/polinomiali, gradi1..9, ordini bassi/prefissi bit identici; formule Bernstein indipendenti fino31, limiti dei buffer, derivate unilateri analitiche, concorrenza e cancellazione. Gli stessi18casi BSpline passano anche sul kernel precedente.
- La nuova fixture iniziale confrontava derivate OCCT su lati diversi a un ulp da un nodo non liscio e poneva al grado15 una soglia non rispettata già dal riferimento. Nessun vecchio test o tolleranza produttiva allentati: ai nodi ambigui mantenuti prefissi bit identici e controllo analitico unilatero; alto grado coperto da Bernstein e oracolo precedente. Non si certificano tutte le derivate razionali di alto grado contro OCCT entro quella soglia iniziale.
- 32/32CTest applicativi e7/7CUDA/broadphase/export PASS, standalone ricollegati al kernel finale. Non è la suite completa del kernel.
- QA nativa Linux/XCB sul mouse2 recente: Rigenera tutto0errori, Raccordo33 visibile con proprietario corretto e19facce, riselezione faccia/raggio3→2,5, Undo/Redo e copia indipendente salvata-riaperta PASS. Tre riaperture anteprima, cacheGL ricreata0pixel diversi/0erroriGL. Questa prova non risolve né esclude ogni precedente intermittenza del renderer.
- Campionamento separato prima:1.875campioni a100Hz,28,37%flat nelle basi e40,79%nel contesto loft. Profili di fase9,599→8,877s non usati per i risultati formali; primi tentativi con percorso profiler/include della sola strumentazione corretti e descritti nel registro, senza modifiche funzionali al prodotto.
- Mappa architetturale aggiornata, controllata e ispezionata in Firefox headless:496file/6728relazioni/235TU,0diagnostiche. Quattro confini chiamante/chiamato verificati nei due versi e80.586siti entro i limiti; include separati. HTML/JSON generati ignorati daGit. git diff --check PASS.

Dati completi e impronte nel [JSON](bspline-evaluation-2026-10-27.json). Binari precedenti, oracoli, log e immagini in `forgecad-linux-solver/benchmarks/bspline-evaluation-2026-10-27`.
Nessun commit/push, riallineamento forzato dei branch o sostituzione dell'app installata. Prossimi costi da misurare: loft residuo, proiezioni/booleane dei tagli e raccordi; nessuna accelerazione globale10× già raggiunta.
