# Correzioni della seconda review — 15 novembre 2026

**Prosecuzione più recente:** [K10 Estendi/Fill](curve-fit-k10-estendi-fill-2026-11-15.md): due offset planari NURBS con controllo continuo, proiezioni NURBS/rette esatte; residuo BoundarySheet confermato. K10 resta parzialmente aperto.

**Aggiornamento successivo:** [K10, primo percorso continuo](curve-fit-k10-2026-11-15.md) ora corregge gli offset NURBS di estrusioni/rivoluzioni; resta aperta la migrazione dei fit generici. Gli esiti e il difetto K10 indicati sotto sono la fotografia della passata precedente.

## Stato e perimetro

Prosecuzione autorizzata dei piani della [seconda review](review-followup-2026-11-15.md), con tre agenti **GPT-6.1 Sol**: kernel, interfaccia/anteprime e controllo indipendente. Il coordinatore ha integrato le modifiche, controverificato le regressioni e compilato app e runner. Nessun commit/push; CAD originali e modifiche locali precedenti preservati.

**K7, K8, K9, PRE4 e UI7 corretti.** Insieme ai 19 rilievi iniziali portano a 24 difetti corretti. È emerso **K10, P1, confermato e ancora aperto**: il fitter generale può accettare una spline di grado elevato fuori tolleranza. Le 22 funzioni orfane rimangono candidati manutentivi, senza rimozioni.

## Correzioni

| ID | Modifica | Prova mirata |
| --- | --- | --- |
| K7 | Eliminato il riconoscimento di traslazioni/rette/cerchi da nove valori del callback. Le costruzioni analitiche dipendono dai supporti e da limiti geometrici sul dominio; gli altri casi usano il fit con i nodi della curva. | Rigonfiamento di grado 4, scale 1 e 1000, verifica densa entro 1e−7; precisione di archi, estrusioni e prismi arrotondati anche ruotati. |
| K8 | Controllo analitico del segno di R+r·cos(v) sulla finestra toroidale originale e risultante. Intervalli periodici traslati e rami locali regolari sono gestiti esplicitamente; singolarità o inversione vengono rifiutate. | Toro completo R5/r1: d3 accettato, d4/d5 rifiutati. Trim esterni ancora ammessi; ramo interno spindle usa r−d; verifica anche da offsetFaces. |
| K9 | Checkpoint prima/dopo ciascuna valutazione del callback, nella ricorsione e prima di pubblicare la deviazione. | Token attivo: zero callback; cancellazione al ventesimo callback e nell’ultimo campione: eccezione propagata e deviazione invariata; reset funzionante. |
| PRE4 | Durante la scelta riferimenti si mostra il contesto degli operandi anche creando una nuova funzione, rispettando la posizione storica. Curve 3D e operandi esplicitamente ammessi restano visibili. | Riempimento pattern reale con preview pronta: sorgente visibile e selezionabile, geometria/chiave preview conservate; uscita, Esc e Annulla ripristinano lo stato. |
| UI7 | Passando a una mappa collegata, termina il picker della destinazione che non è più applicabile, prima di disabilitarne il pulsante. La scelta della sorgente resta valida. | Dialogo Mapping reale: cambio destinazione→mappa, OK riuscito; scelta sorgente conservata e Esc/cleanup verificati. |

Test kernel aggiunti al target autonomo `forgekernel_offset_validation_tests`, senza Qt/OCCT. I due nuovi test dei pannelli sono registrati nel percorso `--function-panels-review`, che già richiama le verifiche precedenti di coerenza anteprime e lifetime: nessuna duplicazione. Commenti dell’API aggiornati per distinguere errore campionato da limite globale.

Il primo tentativo di eliminare la scorciatoia aveva degradato la precisione dell’area degli archi cilindrici. I test esistenti hanno rilevato il problema; una seconda prova su pose ruotate ha rilevato il limite del riconoscimento ristretto agli assi coordinati. La correzione finale usa un controllo geometrico continuo per conservare gli archi quando giustificato. **Tolleranze e assert esistenti non sono stati allentati.**

## K10 — Fit campionato fuori tolleranza: aperto

Posizione: `fitCurve` in `kernel/fk_offset.cpp`, accettazione dei tratti Hermite e controllo finale di `offsetSurfaceImpl`. La suddivisione usa punti di prova fissi: una curva che coincide in quei punti ma varia fra essi può essere accettata. Questo difetto è distinto dalla falsa classificazione come curva esatta di K7.

Riproduttore reale: BSplineCurve di grado 27, una campata, 28 poli e pesi unitari, x(u)=u e

`z(u)=1e10·[u(u−1)(u−.25)(u−.5)(u−.75)(u−.05)(u−.275)(u−.725)(u−.95)]³`.

Estrusione lungo Y, offset 0,001, tolleranza 1e−7. Sia prima sia dopo K7 l’operazione restituisce successo, mentre 10001 valutazioni misurano uno scarto massimo **5,742475e−5**, circa **574 volte la tolleranza**. Non è una regressione della patch.

Il controllo indipendente esclude una sorgente singolare o un semplice errore numerico: conversione esatta dei coefficienti e bound Bernstein con aritmetica razionale sui double effettivi danno raggio di curvatura ≥14,5706778 e d·k≤6,8631e−5<1 sull’intero dominio. La valutazione del kernel differisce dalla forma fattorizzata in long double di ≤8,506e−15 in posizione; coordinate e pesi non sono estremi.

**Aperto:** introdurre una verifica del fit che usi informazioni e limiti geometrici per campata. Aggiungere soltanto altri campioni fissi non risolve la classe del problema. Per callback completamente arbitrarie il contratto deve esplicitare ciò che può essere verificato. Questa passata chiude i cinque rilievi già pianificati; non dichiara certificato globalmente il fitter.

## Verifiche finali

- **Build Release completa PASS**: applicazione `forgecad`, runner Qt e target kernel ricompilati. I log conservano anche i warning non bloccanti.
- **52 casi kernel distinti PASS**: filtro Offset 27, Shell 18 e 7 casi mirati di raccordo su superfici/curve libere, catene tangenti e supporti spline. Non è una certificazione della suite kernel completa.
- **18/18 CTest PASS**, 10.68 s: pannelli, visibilità/picking, Mapping/pattern, storia, lifetime e suite kernel pertinenti. Il nuovo target offset esegue **5/5 regressioni PASS**, incluse pose/scale e cancellazione. Elenco esatto e output integrale nelle prove; alcuni test CTest riesercitano casi già presenti nei filtri kernel.
- **60/60 controlli indipendenti PASS** sulla stessa versione finale di `fk_offset.cpp`: 12 casi toro/cancellazione e 48 casi bordi/estrusioni. Il riproduttore K10 continua a mostrare il difetto aperto e non è contato come correzione riuscita.
- Controverifica del coordinatore: `offsetFaces` con entrambe le politiche delle cuciture accetta d3 e rifiuta d4/d5 sul toro R5/r1; corpo regolare senza segnalazioni del checker. Rigonfiamento locale K7 entro la tolleranza.
- Mappa rigenerata: **737 file, 10600 relazioni, 362 unità C++**, zero diagnostica AST. `--check`, uguaglianza HTML/JSON e audit di **678 relazioni incidenti** PASS, distinguendo chiamate, include e riferimenti. Verificati entrambi i versi per i sette file interessati; nessuna nuova valutazione visiva del template, rimasto invariato.
- `git diff --check` PASS; controllo degli hash iniziali senza rimozioni o variazioni inattese. Le prove identificano il delta dell’intervento separatamente dalle modifiche locali preesistenti.

## Prove e limiti

[Prove ripetibili, log e audit](review-followup-fix-prove-2026-11-15/README.md). Le controprove sui sorgenti originali falliscono nei nuovi test pertinenti; i runner privati iniziali restano distinti dalle prove finali sulla build integrata.

Qt offscreen/Fusion verifica dialoghi, eventi, criteri di visibilità e ray picking topologico; non certifica i pixel OpenGL o i temi su desktop. Nessun nuovo ASan/LSan/TSan, nessuna certificazione dell’intera suite, macOS o backend opzionali. L’app ricompilata va riaperta per usare queste modifiche. K10 e le orfane restano esplicitamente aperti; nessuna dichiarazione di assenza generale di bug o leak.
