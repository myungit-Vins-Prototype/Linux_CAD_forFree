# K10: quadranti delle coniche, progresso e cancellazione — 17 novembre 2026

Corretto un ciclo senza avanzamento in `detail::quadrantBreakpoints`, condiviso da cerchi, ellissi e superfici periodiche. L’indice `double` veniva incrementato di 1 senza verificare che il valore cambiasse, e il ciclo non controllava la cancellazione. Ora intervalli non finiti o invertiti sono rifiutati, l’indice deve avanzare e l’annullamento viene controllato all’ingresso e a ogni iterazione.

**Implementato e verificato localmente**, senza commit/push. È una prosecuzione di K10: il conteggio dei 33 rilievi funzionali della review resta invariato. Nessuna modifica a mesh, geometria, parametrizzazione, formula dei quadranti o tolleranza esistente.

## Riproduzione prima e dopo

Il coordinatore ha compilato un piccolo driver collegato alla libreria reale, senza copiarla, chiamando `Circle<2>::breakpoints`:

| Caso | Baseline | Candidata |
| --- | --- | --- |
| Intervallo `[1e20,1e20+1e9]` | Non termina; watchdog esterno a 0,3 s, exit 137 | `std::domain_error`, circa 4 ms incluso avvio processo |
| Intervallo `[-Inf,0]` | Non termina; stesso watchdog, exit 137 | `std::invalid_argument`, circa 3 ms |
| Token già annullato, intervallo breve finito | Restituisce i breakpoint ignorando il token | `OperationCancelled` |
| Token attivato dopo 5 ms, intervallo `[0,1e12]` | Non eseguito | `OperationCancelled`, circa 8 ms incluso avvio/processo/thread |
| Intervallo ordinario `[-0,3;8]` | Sequenza di riferimento | Sequenza bit-identica |

I due watchdog hanno terminato soltanto processi privati della prova. Nei testimoni bloccati la condizione di inserimento nel vettore è falsa, quindi il problema riprodotto è consumo di CPU, senza crescita del vettore. I tempi sono osservazioni locali, non soglie di prestazione garantite.

L’auditor Sol ha verificato indipendentemente un singolo passo IEEE/Fraction: per il primo intervallo l’indice iniziale è `63661977236758134784`, con ULP 8192. Aggiungere 1 restituisce lo stesso `double`; la condizione del ciclo rimane vera e nessun altro stato cambia. Questo prova il mancato avanzamento, oltre all’osservazione del timeout. Per `-Inf`, anche `-Inf+1` rimane `-Inf`. L’auditor non ha eseguito cicli C++ patologici; runtime e compilazioni sono del coordinatore.

I parametri enormi sono fasi angolari fuori dall’uso CAD ordinario, e `-Inf` è un input invalido: **nessun documento CAD ordinario è dichiarato risolto** da questi riproduttori.

## Correzione e compatibilità

Una sola TU di produzione, `kernel/fk_curve.cpp`, con candidata congelata SHA‑256 `7f47f426d3d19182cc2261741beb4fc6efb94ac72994946c2009863219b98011`. Nessun header pubblico modificato.

- Intervalli non finiti o invertiti: `std::invalid_argument` prima di costruire il vettore.
- Durante una iterazione effettivamente richiesta, `next=k+1` non finito o non maggiore di `k`: `std::domain_error` invece del ciclo fermo.
- Token attivo: `OperationCancelled`, distinta dalle eccezioni geometriche e propagata come nel resto del kernel.
- Intervallo puntuale: preservato il risultato legacy con entrambi gli estremi, anche coincidenti e con zero firmato.

Le espressioni `ceil`, `k*pi/2`, l’ordine delle operazioni e la tolleranza legacy sono conservati. Nessun riporto di fase o limite arbitrario al numero di quadranti. Le eccezioni dai generatori dei breakpoint possono propagarsi ai chiamanti: non si convertono automaticamente in un residuo +∞. La pubblicazione dei job `computePCurves` avviene dopo il ricontrollo delle eccezioni, come prima.

La tolleranza storica dei quadranti non costituisce una garanzia di partizione completa a fasi enormi. Un intervallo enorme ma con indice ancora progressivo può richiedere molta CPU/memoria se non annullato: ora è cancellabile, ma non è stato introdotto un budget di risorse generale. Restano aperti il fit/carrier globale, Newton e le famiglie non coperte di K10; questa correzione non certifica la geometria nel continuo né la topologia B-rep.

## Verifiche correnti

Tre agenti GPT‑6.1 Sol per candidata, audit e test, esclusivamente in staging. Integrazione e runtime del coordinatore, compilazioni/analisi seriali.

| Verifica | Esito |
| --- | --- |
| Golden finiti prima e dopo, nove intervalli su 14 utilizzatori | PASS, bit-identici |
| Quattro nuovi gruppi: positivi, intervalli invalidi, mancato progresso, cancellazione preattiva | 4/4 PASS |
| ASan/UBSan con leak detection attiva | 4/4 PASS, nessuna segnalazione |
| Build completa app, runner e target pertinenti | PASS |
| CTest: nuovo target, proiezioni curve, deviazione pcurve, inversione UV | 4/4 PASS, nessuno skip |
| Storici selezionati: `ArcLength` (2), `ProjectOnEllipseMatchesOcct`, `TrimmedCurveOnPeriodicBasis` | 4/4 PASS |
| Architettura: descrizioni, chiamate/include/riferimenti nei due versi, hash/configurazione/HTML e `--check` | PASS, 799 nodi / 11681 relazioni / 405 TU |

I 14 utilizzatori sono helper diretto, Circle/Ellipse 2D/3D, U di cilindro/cono/sfera/toro, V di sfera/toro, U di estrusione con cerchio e U/V di rivoluzione con meridiana circolare. La baseline della nuova TU ha eseguito **solo `SafeControls` (1/1)**: gli altri gruppi contengono testimoni che bloccherebbero il vecchio ciclo; i due testimoni originali sono stati eseguiti separatamente sotto watchdog. Non è dichiarato un confronto baseline 4/4.

ASan/UBSan strumentano `fk_curve.cpp`, la nuova TU e il main; restante kernel Release. La prova di cancellazione temporizzata è un driver privato non strumentato, separato dai test permanenti per evitare dipendenze temporali. Nessuna analisi completa dei leak dell’app. Nessun test UI, avvio grafico, intera suite dei 483 test o nuova CI. Il binario sul disco è aggiornato, l’app aperta non è stata riavviata.

## Conservazione

Eliminati soltanto 11 ELF/oggetti privati ricreabili di questa prova: 13,900,768 byte, circa 13.3 MiB. Nessuna copia aggiuntiva della libreria statica, nessun CAD o cartella QA dell’utente modificati. Sorgenti, log e hash conservati.

L’audit finale controlla i 7.510 file baseline: sole cinque modifiche esistenti attese, 7.505 preservati, più i nuovi file dichiarati. Verificati 35 pacchetti precedenti e 4.888 artefatti. HEAD/origin `47c7c19`, index vuoto e nessun commit/push. I generati architetturali ignorati non vengono aggiunti a Git.

[Memoria precedente completa](history/claude-prima-k10-quadranti-2026-11-17.md), [prove e comandi verificabili](review-k10-quadranti-prove-2026-11-17/README.txt), [review generale](code-review-2026-11-15.md). Il pacchetto contiene solo testo e sorgenti inerti; `SHA256SUMS` autentica i percorsi effettivi. La CI storica non certifica queste modifiche locali.

Eseguibile attuale SHA‑256: `953b2408cef7d0ba79db480a8b5e6d65a1f69929349be8ec88c4ce8e4a2580c5`.
Kernel attuale SHA‑256: `9b2c5a0814782d405385963e35bc1323360cf173de4419a9270e6338a2385b69`.
