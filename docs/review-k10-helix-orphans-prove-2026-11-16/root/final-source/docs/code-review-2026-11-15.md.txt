# Review generale ForgeCAD — 15 novembre 2026

**Stato più recente, 16 novembre:** **29 rilievi confermati corretti complessivamente**. [Helix nello schizzo e cinque orfane](review-k10-helix-orphans-2026-11-16.md): proiezione delle eliche trasformate con limite continuo e ricontrollo dei poli Qt; due alias reali ora rifiutati atomicamente. Eliminate cinque funzioni private/locali, **17 API pubbliche senza usi interni ancora da valutare**. App completa e avvio isolato Wayland PASS;13gruppi Qt e sanitizer mirati,37guardie indipendenti,8varianti/624campate Fraction e11export Release identici PASS. **CTest20/21: un fallimento del raccordo cavità a0,5mm è riprodotto anche sul kernel precedente ed è da diagnosticare**; test conservato. Mappa772/11165/385 verificata. **K10 resta aperto:19/24siti iniziali parzialmente trattati,5interamente da migrare**, oltre ai fallback. Precedenti [cucitura/PRE8](review-k10-sew-preview-2026-11-16.md), [UV/PRE7](review-k10-uv-preview-2026-11-16.md), [K11 Supporto.prt](supporto-booleana-2026-11-16.md), [PRE5/PRE6/UI2](review-preview-unification-2026-11-16.md) conservati; K2 in produzione ed [ellisse per estremi](ellisse-estremi-2026-11-16.md) completata. Uniformazione globale UI ancora aperta. I conteggi storici successivi descrivono le rispettive consegne precedenti.

**Aggiornamento successivo:** i cinque rilievi della [seconda review](review-followup-2026-11-15.md) K7/K8/K9/PRE4/UI7 sono corretti: **24 difetti precedenti corretti complessivamente**. La [prosecuzione K10](curve-fit-k10-2026-11-15.md) corregge il fit degli offset NURBS di estrusioni/rivoluzioni con controllo continuo, input finiti e fallimenti atomici; build completa e regressioni PASS. **K10 resta aperto per i chiamanti generici ancora campionati**. Le 22 orfane restano candidati distinti. Stato storico della prima review sotto.

**Stato dopo gli interventi successivi:** **19 dei 19 difetti confermati sono corretti**. M1/M2: [cache](cache-bspline-2026-11-15.md); K1/K3: [sweep](sweep-review-fix-2026-11-15.md); K4/K5/K6, S1/S2, UI1–6 e PRE1–3: [prosecuzione](review-fix-next-2026-11-15.md); **K2 integrato e verificato**: [controllo globale sweep](sweep-k2-completamento-2026-11-15.md). Le 22 funzioni senza chiamanti restano candidati manutentivi separati. Il prototipo K2 precedente è superato. Riscontri, righe e prove sotto descrivono la review iniziale, prima delle correzioni.

Review del codice locale, iniziata dal kernel ed estesa a memoria/concorrenza, solver, persistenza, interfaccia e, su richiesta aggiunta durante il lavoro, gestione delle anteprime. Quattro agenti hanno lavorato su aree distinte; i riscontri sono stati ricontrollati e raccolti con riproduttori. **Alla consegna iniziale non erano state implementate correzioni software:** erano stati aggiunti soltanto rapporti, prove documentali e aggiornamenti alla memoria condivisa. Lo stato successivo è riportato in apertura.

Base: HEAD `ce03a6d5941840886416aafe055763fb98209e94`, includendo le modifiche locali preesistenti a solver, sweep e test. Percorso effettivo `/home/myungit/Linux_CAD_forFree`; il percorso `/Users/...` registrato nella chat non era presente su questo host. L'hash iniziale dei file versionati è stato conservato per distinguere la review da modifiche concorrenti.

## Riscontri confermati

**19 difetti confermati: 5 P1, 12 P2 e 2 P3**, oltre a 22 funzioni senza chiamanti verificate separatamente. Tre dei 19 riguardano specificamente le anteprime e sono descritti nella sezione seguente.

P1: correggere prima di affidarsi ai risultati interessati; P2: bug operativo o incoerenza da correggere; P3: difetto latente/manutenzione. La priorità non misura la frequenza: i casi geometrici hanno condizioni specifiche documentate nei riproduttori.

| ID | Priorità | Problema e conseguenza | Posizione primaria | Prova |
|---|---|---|---|---|
| K1 | P1 | Sweep a torsione minima: elica di 16 giri classificata rettilinea; profilo deformato e volume circa dimezzato | `kernel/fk_sweep.cpp:110` | 25,3261 invece di 50,5724; Frenet corretto, checker senza errori |
| K2 | P1 | Sweep con spire sovrapposte accettato come solido; volume conta più volte le sovrapposizioni | `kernel/fk_sweep.cpp:722` | Volume 19,7417 maggiore del box contenitore 12,6 |
| K3 | P1 | NURBS spaziale chiusa in un tratto: cucitura UV errata, area e volume nulli nonostante la mesh visibile | `kernel/fk_sweep.cpp:681` | Un tratto V=0; due tratti V=0,822445; SP-curve stampate. Limite già noto, ora localizzato |
| M1 | P1 | Riconoscimento cerchi nelle tavole: cache liberata mentre un altro thread la percorre | `cad_drawing_curve_recognition.h:48` | AddressSanitizer: heap-use-after-free |
| M2 | P1 | Copia implicita BSplineCurve e inizializzazione della cache accedono allo stesso shared_ptr senza sincronizzazione coerente | `kernel/fk_bspline.h:66` | ThreadSanitizer: data race |
| K4 | P2 | Sweep valido rifiutato se il percorso è NURBS e si ruota il frame di un profilo circolare equivalente | `kernel/fk_sweep.cpp:630` | Tre rappresentazioni riescono, la quarta fallisce: geometria identica |
| K5 | P2 | Loft aperta interpretata come chiusa dopo una traslazione, con facce e area diverse | `kernel/fk_loft.cpp:2055` | X=0: una faccia/area 0,5; X=1e6: due facce/area 0,998611 |
| S1 | P2 | Angolo non valido persistito: wrapAngle entra in un ciclo senza avanzamento | `cad_constraints.cpp:235` | Codec accetta infinito e 1e20; solver fermato dal timeout del probe |
| S2 | P2 | Vincolo NaN considerato soddisfatto | `cad_constraints.cpp:2177` | Dopo codec: ok=true, residuo=0, zero iterazioni |
| UI1 | P2 | Riempimento: influenza e pesi adimensionali convertiti come lunghezze | `forgeCad2026_dialogs_surfaces.cpp:1646` | In cm digitare 1 produce coefficiente 10 su tre campi |
| UI2 | P2 | Chiudere il pannello della scheda A lascia A bloccata mentre è aperto un pannello più recente in B | `forgeCad2026_dialogs_common.cpp:1615` | Due schede reali Qt; A si sblocca solo chiudendo B |
| UI3 | P2 | Annulla Mapping/Riempimento pattern lascia la selezione riferimenti attiva senza callback | `forgeCad2026_dialogs_mapping.cpp:100` | Dopo Annulla: picking=true, documento sbloccato |
| UI4 | P2 | Cambio G1-facce→G0 mantiene il picker, ma disabilita il pulsante necessario a gestirlo | `forgeCad2026_dialogs_common.cpp:1566` | Picking=true, pulsante checked e disabled |
| UI5 | P2 | Colori di stato/errori locali non seguono il tema chiaro e diventano poco leggibili | `forgeCad2026_dialogs_common.cpp:1135` | Contrasto misurato 1,26–2,15:1; testo ordinario 15,22:1 |
| UI6 | P3 | Aggiornare la lista dei riferimenti perde la selezione multipla anche se i riferimenti non cambiano | `forgeCad2026_dialogs_common.cpp:1278` | Due righe selezionate diventano una |
| K6 | P3 | reverseEdge su una lamina solleva dopo una modifica parziale, lasciando il corpo incoerente | `kernel/fk_topology.cpp:125` | Checker da 0 a 2 errori; nessun percorso GUI confermato |

Dettagli, cause, riproduzioni e proposte: [kernel](review-2026-11-15/kernel.md), [memoria](review-2026-11-15/memoria.md), [solver/persistenza](review-2026-11-15/solver.md), [interfaccia](review-2026-11-15/interfaccia.md).

## Anteprime delle funzioni

**Tre bug confermati (P2)**, con harness Qt ripetuto con successo:

| ID | Problema | Posizione primaria | Verifica |
|---|---|---|---|
| PRE1 | Estrusione: passando da distanza a limite su piano senza scegliere il piano, rimangono la vecchia forma e lo stato «pronta» | `forgeCad2026_dialogs_solids.cpp:947` | Dialogo reale: UI extent=3, preview extent=0, vecchia chiave invariata, nessun calcolo pendente |
| PRE2 | Loft: con una sola sezione mostra «in calcolo» senza worker; togliendo una sezione da un risultato valido può restare «pronta» senza geometria | `forgeCad2026_dialogs_surfaces.cpp:619` | Dialogo reale con una sezione: pending=false, key vuota, testo «in calcolo»; secondo percorso verificato nel codice |
| PRE3 | Sviluppo/rivestimento: le sorgenti nascoste nella preview non coincidono con quelle nascoste dopo OK | `forgeCad2026_viewport_preview.cpp:145` | Policy reale: mappingCopy=true, sorgente sostituita nella preview ma conservata nel risultato; conseguenza visiva tracciata nel renderer |

PRE1 non dimostra la conferma di una geometria sbagliata: il riuso finale confronta la chiave dei parametri. PRE3 verifica la policy e il ramo di visibilità, senza cattura dei pixel OpenGL.

La [matrice completa delle anteprime](review-2026-11-15/anteprime.md) confronta 15 gruppi di funzioni: avvio automatico/manuale, ritardi, selezione, input incompleti, rendering, conferma e riuso. La confusione segnalata ha riscontri anche oltre i tre bug:

- Raccordo/smusso espone «Anteprima automatica» e «Calcola»; le altre famiglie esaminate avviano il calcolo automaticamente.
- Estrusione/rivoluzione mostrano una patch trasparente; sweep, shell e varie superfici mostrano un corpo completo opaco. Il messaggio comune «in ambra» non chiarisce la distinzione.
- Durante la selezione alcuni comandi cancellano la preview e spiegano l'attesa, mentre mapping e riempimento pattern mantengono quella precedente senza uno stato equivalente.
- Aprire una feature senza modificarla può mostrare il risultato opaco già presente; al primo cambiamento tornano trasparenza e patch, secondo il tipo di funzione.
- Pattern, proiezione e ricerca automatica degli operandi possono richiedere un nuovo calcolo alla conferma, mentre altre operazioni riusano il B-rep pronto.

Queste differenze sono scelte attuali verificate nel codice, non ulteriori bug geometrici conteggiati. **Proposta di uniformazione, non implementata:** stati condivisi «seleziona gli input / da aggiornare / in calcolo / pronta / errore / sospesa», invalidazione immediata quando cambiano i parametri, scelta esplicita di conservare una vecchia forma solo indicandola come superata, un unico cleanup su `finished` e visibilità degli operandi coerente con il risultato finale. Colori semantici dipendenti dal tema e distinzione chiara fra patch locale e corpo completo. I controlli manuali e i tempi di attesa possono restare diversi quando motivati dal costo, mantenendo lo stesso significato degli stati.

## Funzioni orfane e memoria

**22 funzioni senza chiamanti nel repository esaminato** sono ricontrollate tramite ricerca testuale, grafo delle chiamate/riferimenti e dichiarazioni. Sono candidati manutentivi, non 22 bug funzionali. Fra queste: `setSelectedPointPosition`, `setEdgePickLinear`, `setExtendPickCallback`, `resumeExtendPick`, `firstFeature`, `forgeIntersectRay`, `nearestBodyEdge`, `appendFixedReferenceLine` e alcuni wrapper del viewport.

Le API usate soltanto dai test sono separate; override/callback Qt non sono classificati come orfani per la sola assenza di archi diretti. `ensureBodyDisplay` e `transformSurface`, segnalate nel vecchio inventario, sono ora usate in produzione e sono escluse. [Inventario delle 22 funzioni e prove](review-2026-11-15/orfane.md).

**Nessun memory leak permanente dimostrato.** Sono invece confermati i due difetti di sicurezza della memoria M1/M2. Non è stata eseguita una sessione completa LeakSanitizer dell'app né un profiling GPU prolungato; cache trattenute intenzionalmente e allocazioni perse non sono state confuse.

## Verifiche eseguite

- Mappa architetturale: **723 file, 10.397 relazioni, 352 unità C++ analizzate**, controllo di coerenza PASS. Questi numeri descrivono il grafo, non una lettura manuale di ogni riga.
- **45 test distinti verificati con esito PASS**: 44 tramite CTest nelle batterie mirate; recupero documenti passato con il comando equivalente in ambiente temporaneo isolato fuori sandbox. Le due batterie contengono 23 test ciascuna, con `document_read` in comune.
- Il primo test espressioni falliva per impossibilità di aprire il display; ripetuto con Qt offscreen è PASS. Il recupero documenti va in timeout nel sandbox e passa fuori: non è stato attribuito al prodotto un difetto non dimostrato. Log e condizioni restano conservati.
- **16/16 test Sweep PASS sul runner ricollegato al kernel corrente**, con ricompilazione dell'object obsoleto. I nuovi probe geometrici trovano casi non coperti dalla suite.
- **475/475 PASS sul vecchio runner kernel del 14 novembre**: baseline precedente, esplicitamente non usata per certificare le modifiche locali del 15 novembre.
- Probe numerici/codec, ASan, TSan, sei riproduzioni UI e tre verifiche delle anteprime con componenti Qt reali offscreen. I dettagli precisano quali file sono strumentati e quali scenari sono soltanto verificati staticamente.

[Prove e istruzioni](review-2026-11-15/prove/README.md). I programmi di prova sono conservati come `.cpp.txt`/`.py.txt`, fuori dai moduli di produzione e dai target CMake. Nessun CAD originale modificato, nessun processo utente chiuso. Eseguibile dell'app non ricompilato né aggiornato.

## Copertura e limiti

| Area | Copertura effettiva |
|---|---|
| Kernel | Contratti e percorsi principali di matematica/NURBS, topologia/checker, profili, sweep, loft, booleane, shell/offset, pcurve; probe sui difetti elencati |
| Memoria/concorrenza | Cache B-spline e riconoscimento curve, ownership B-rep, display, worker, cancellazione e vita del documento; ASan/TSan mirati |
| Solver e documenti | Valori non finiti, terminazione, codec, limiti di lettura, controlli di cicli STEP/IGES; test di vincoli, accesso, revisioni e recupero |
| Interfaccia 3D | Famiglie dialogs, controlli comuni, unità, temi, selezione e schede; confronto dei flussi e prove Qt |
| Tavole 2D | Riconoscimento curve e parallelismo, test mirati di viste/visibilità/quote/assi/centri/layout/focus |
| Anteprime | Matrice per famiglia e verifica di stati incompleti, aggiornamento, errore, selezione, conferma e cancellazione |
| Codice inutilizzato | Inventario verificato di 22 funzioni; callback, virtuali e API solo-test distinti |

Questa è una **review trasversale con approfondimenti riproducibili**, non una dimostrazione dell'assenza di bug né una lettura integrale di ogni riga del repository. Restano da approfondire esaustivamente tutti i casi di blend/offset/booleane, i menu dinamici delle tavole e le configurazioni opzionali. Non eseguita l'intera batteria dei 221 CTest registrati. Non certificati OpenGL/Wayland/XCB, macOS/Metal, Windows e CUDA; offscreen verifica gli oggetti e gli eventi Qt, non la resa del renderer.

## Ordine consigliato delle correzioni

1. M1/M2 e K1/K2/K3: corruzione della memoria o geometria accettata con proprietà errate.
2. K4/K5, S1/S2 e UI1: correttezza della costruzione e dei valori.
3. Anteprime e UI2/UI3/UI4: stato coerente del comando, selezione e indipendenza delle schede.
4. Leggibilità/selezione multipla e rimozione mirata delle orfane dopo verifica dell'eventuale contratto API.

Alla consegna iniziale tutte le correzioni erano proposte; gli interventi successivi e lo stato corrente sono indicati nell’aggiornamento in apertura.
