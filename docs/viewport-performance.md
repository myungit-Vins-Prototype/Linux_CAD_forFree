# Viewport: assi, piani, selezione e GPU

## Uso

- Gli assi XYZ mantengono una lunghezza proporzionale all'altezza della finestra quando cambia lo zoom. **Visualizza → Assi → Dimensione degli assi** ne regola la scala (2 = standard). Le lettere, la selezione e l'evidenziazione usano la stessa lunghezza.
- I tre piani standard sono quadrati e hanno sempre la stessa dimensione. La coordinata della geometria più lontana dall'origine determina la misura comune, con un margine del 20% e arrotondamento a un valore leggibile.
- Selezionare un piano standard o un datum, nella scena o nell'albero, e trascinare una delle quattro maniglie quadrate agli angoli. **Esc** annulla il trascinamento. Cambia soltanto la dimensione rappresentata, non il piano geometrico. Ridimensionare un piano standard aggiorna insieme tutti e tre e salva il fattore relativo condiviso in `view/planeScale`; i datum restano indipendenti, sono annullabili e vengono salvati nel `.prt`.
- Durante uno schizzo, selezionare un piano nell'albero o cliccarne il bordo nella scena. Cliccare un asse nella scena oppure usare **Schizzo → Riferimenti esterni → Assi del modello**. Viene selezionata una linea di costruzione fissa, utilizzabile insieme alle entità dello schizzo per quote e vincoli (parallelismo, perpendicolarità, coincidenza, simmetria...). Il riferimento viene aggiunto alla selezione esistente. Un riferimento già presente sulla stessa retta viene riutilizzato.
- I piani danno la loro **intersezione** con il piano dello schizzo; gli assi la loro **proiezione ortogonale**. Un piano parallelo/coincidente non determina una retta unica; un asse normale allo schizzo si proietta in un punto. Questi casi mostrano un messaggio senza creare una linea arbitraria.
- Questi riferimenti sono copie fisse, come gli altri riferimenti esterni già presenti: **non seguono successive modifiche del datum**. Sono salvati come normali segmenti di costruzione con vincolo Fisso, senza cambiare il formato del documento.

## Origine dei ritardi e modifiche

La selezione richiamava `firstRayHit` a ogni movimento e ricostruiva i box esatti di tutte le facce, inclusi quelli delle superfici B-spline. La ricerca dei bordi di una faccia confrontava i suoi punti con tutte le polilinee. Inoltre, per ogni punto proiettato venivano ricostruite le matrici della vista; selezione ed evidenziazione ridisegnavano ripetutamente vertici e normali con `glBegin/glEnd`.

Questi percorsi occupano il thread dell'interfaccia. Un singolo thread occupato per decine di millisecondi può interrompere la risposta al mouse senza portare in alto l'utilizzo complessivo di una CPU multicore; la GPU può intanto restare in attesa.

Ora:

- `RayFaceIndex` prepara una volta i box esatti durante la tassellazione del corpo. La selezione scarta i box non colpiti e visita le facce candidate per distanza. Intersezione e classificazione restano sulla B-rep esatta.
- Il risultato dell'ultimo raggio per corpo viene riusato tra selezione dell'oggetto e della faccia.
- La tassellazione mantiene gli identificatori degli edge, così i bordi della faccia si ricavano direttamente dalla topologia.
- Matrice di proiezione e polilinee degli spigoli proiettate vengono riusate; i box a schermo scartano gli spigoli lontani dal cursore. Camera e geometria invalidano le cache.
- `cad_display_cache` carica vertici, normali e polilinee in **VBO OpenGL**. Disegno normale, contorni di selezione e facce delle anteprime riusano i buffer. I dati sono condivisi finché immutati; quelli non usati vengono rilasciati con il contesto GL corrente.

Misura locale su `APP_errore.stp`, corpo `D241121_REV0`, 4.089 facce: **25 raggi, 545,458 ms senza cache dei box contro 19,0872 ms con cache**, 7 raggi colpiti in entrambi i casi (circa 28,6× sul solo percorso misurato). Preparazione dell'indice e importazione escluse dal tempo: avvengono prima della selezione. Non è una misura del framerate né dell'intera anteprima.

## Calcoli CPU e GPU

La GPU esegue trasformazioni, illuminazione, rasterizzazione, profondità, stencil e antialiasing OpenGL. I corpi usano shader GLSL 3.30, VAO e VBO persistenti; le polilinee degli spigoli sono riunite in un buffer indicizzato con primitive restart e inviate con una sola draw call per corpo. Un secondo renderer GLSL 3.30 con VBO dinamico disegna sfondo, griglia, assi, piani, datum, punti di snap, sketch, anteprime, contorni e sezione. Anche sfocatura e compositing dei pannelli usano attributi espliciti e buffer, senza immediate mode nel viewport. Il contesto Core richiede questo percorso e non mantiene un fallback fixed-function.

L'hover dei corpi usa un framebuffer GPU di identificatori RGB con profondità: viene rigenerato una volta per fotogramma della scena e il movimento del mouse legge un solo pixel. Il clic continua a interrogare la B-rep esatta. Se è attiva una sezione o un'anteprima, il picking torna automaticamente al ray test CPU. Le ripetizioni di corpi con bounding box sicuramente disgiunte condividono inoltre la mesh della base e sono disegnate con draw call instanziate anche nel pass di picking; copie che si toccano o si sovrappongono conservano la tassellazione del risultato booleano. I segmenti rettilinei degli schizzi, incluse le copie delle ripetizioni di schizzo, usano una linea base e trasformazioni instanziate raggruppate per stile, in lotti fino a 64 istanze.

Anche la sfocatura dei pannelli usa GLSL 3.30 e il blit tra framebuffer. La scena viene copiata una volta per fotogramma, sfocata con due passaggi separabili al 75% della risoluzione del viewport e ricomposta soltanto nelle aree arrotondate dei pannelli. Il 75% riduce la pixelatura rispetto alla precedente metà risoluzione, con 2,25 volte i frammenti del filtro ma ancora meno lavoro del formato pieno. Questo elimina la lettura sincrona del framebuffer e i filtri d'immagine sul thread dell'interfaccia. Se il percorso shader non è disponibile, rimane attivo il precedente fallback CPU condiviso e limitato a circa 15 aggiornamenti al secondo. Questa accelerazione non dipende da CUDA.

**CUDA valuta ora le superfici B-spline/NURBS della tassellazione di display.** Il raffinamento di `fk_tessellate` raccoglie a ogni giro i punti medi dei lati da provare e li valuta in un lotto: con un acceleratore (`SurfaceBatchEvaluator`, nell'app `cad_cuda_tessellation` + `cuda_support.cu`) i lotti di almeno 512 punti su una `BSplineSurface` vanno alla GPU (un thread per punto, in double, stessi algoritmi del kernel: scarto dalla CPU 7e-16 relativo sui punti, 3e-14 sulle normali, verificato da `forgecad_cuda_tests`), gli altri alla CPU. Il guadagno misurato e' piccolo, perche' dopo le ottimizzazioni la valutazione e' circa il 15% del tempo: su AP0730-REV00.STEP a qualita' alta la tassellazione passa da 1,9 s a 0,24 s per il lavoro sulla CPU (facce in parallelo, decisioni sui lati prese una volta, tabelle dei lati a indirizzamento aperto, punto valutato riusato come vertice) e a 0,20 s con CUDA; con un thread solo la GPU non conviene (latenza di circa 60 microsecondi per lotto, con la scheda in P8 a batteria). Booleane, misure, raccordi e la geometria esatta restano sulla CPU; si spegne con *Opzioni → Tassellazione con CUDA*.

Roadmap GPU: mantenere sempre il backend CPU e aggiungere CUDA come acceleratore opzionale, scelto a runtime. I primi candidati sono operazioni in grandi batch con dati regolari: valutazione/tassellazione di curve e superfici, classificazione preliminare di raggi e coppie di facce, analisi di mesh e anteprime. La topologia B-rep esatta, le booleane e la costruzione dei raccordi restano inizialmente sulla CPU: hanno molte diramazioni, strutture dinamiche e casi degeneri, quindi trasferirle per prime aumenterebbe la complessità senza garantire un guadagno. Risultati CUDA e CPU dovranno avere gli stessi controlli di tolleranza e il fallback CPU dovrà funzionare anche quando il progetto è compilato senza `nvcc`.

Gli impieghi GPU più promettenti per ForgeCAD sono la tassellazione di molte facce, il campionamento in parallelo di curve e superfici, la selezione preliminare tramite un buffer di identificatori e l'istanziazione grafica delle ripetizioni. La GPU può anche preparare coppie candidate di facce o box per intersezioni, lasciando al kernel CPU la verifica esatta. Le booleane B-rep finali, la modifica della topologia e gran parte del risolutore dei vincoli hanno molti rami, strutture dinamiche e dipendenze sequenziali; inoltre usano `double`. Sulla Quadro RTX 3000 il vantaggio CUDA in doppia precisione è molto inferiore a quello in `float`, quindi trasferirli integralmente rischierebbe di aumentare latenza e complessità senza un guadagno stabile.

Un ulteriore affinamento possibile è rendere asincrona la lettura del pixel del picking tramite PBO e ampliare il percorso GPU alle singole facce, continuando a verificare i clic sulla geometria esatta. Per anticipare il risultato delle operazioni geometriche servirebbe invece profilare singolarmente i moduli del kernel: spostare il calcolo esatto su CUDA è un intervento distinto, da validare numericamente.

## Migrazione OpenGL 3

Il contesto richiesto è OpenGL 3.3 Core. Corpi, spigoli, picking, ripetizioni e
overlay passano da shader, VAO e buffer; non rimangono chiamate immediate né
client-array. Proiezione, model-view, colori, illuminazione e materiali sono
stato esplicito degli shader. Il piano di sezione usa una uniform comune e
`gl_ClipDistance`; i tratteggi di linee e superfici sono prodotti nel fragment
shader, quindi non dipendono più dagli stati rimossi dal profilo Core.

Nei modelli dominati da molte chiamate di disegno, contorni e hover il guadagno
puo' andare da circa **2x a oltre 10x** nel solo percorso di rendering. Eliche e
spirali richiedono prima di tutto una cache delle loro polilinee e un indice di
selezione, per evitare proiezioni e test CPU a ogni movimento del mouse; senza
questo intervento OpenGL 3 lascerebbe intatto il principale collo di bottiglia.
Raccordi, booleane, sweep e loft appartengono invece al kernel B-rep CPU: OpenGL
3 non ne cambia robustezza o tempo di costruzione. Per questi servono profili
per fase, broad phase tra box, riuso delle intersezioni, tolleranze coerenti e
una strategia esplicita per casi degeneri e facce tagliate.

## Verifiche riproducibili

```sh
cmake -S . -B forgecad-cuda-build -DFORGECAD_BUILD_VIEW_TESTS=ON
cmake --build forgecad-cuda-build -j4
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ./forgecad-cuda-build/forgecad_view_tests
./forgecad-cuda-build/kernel/forgekernel_tests Tessellate
./forgecad-cuda-build/forgecad_selection_benchmark APP_errore.stp
```

Per confrontare una ricostruzione parametrica completa con l'apertura dello
snapshot B-rep + mesh salvato nel documento:

```bash
QT_QPA_PLATFORM=offscreen ./forgecad-cuda-build/forgecad_view_tests --bench-load File_Esempio/AP0730.prt 5
```

In una sessione grafica con OpenGL desktop, `forgecad_view_tests --gl` verifica anche il disegno dei buffer e confronta pixel per pixel il rendering VBO e quello immediato dello stesso corpo. Le impostazioni dei test sono temporanee. Il test comprende ridimensionamento, annullamento, Undo/Redo, vincoli sui riferimenti fissi, salvataggio e riapertura del documento, e invalidazione della cache durante il pan.


## Zoom comune a vista 3D e tavole (22 ottobre 2026)

Il modulo `forgeCad2026_zoom.h/.cpp` separa la normalizzazione degli eventi
Qt dal controller temporale e dagli adattatori della camera 3D/QGraphicsView.
Entrambi usano incrementi del logaritmo dell'ingrandimento: con sensibilità
100%, uno scatto angolare di120unità produce `exp(.03)` di ingrandimento
(la camera3D, espressa come estensione visibile, varia di `exp(-.03)`).
Le frazioni di scatto restano proporzionali;12eventi da10equivalgono a120.
Il percorso pixel usa un guadagno distinto di `.0015` per pixel. Non si
sommano angolo e pixel dello stesso evento e non si moltiplica per il DPR.
La direzione ricevuta segue le preferenze del sistema, senza doppia inversione.

In **Visualizza → Zoom → Impostazioni zoom…** si scelgono:

- **Automatico**: fallback animato per input incerti. Su X11/xcb si preferisce
  l'angolo perché Qt dichiara i pixel dipendenti dal driver e inaffidabili.
  Negli altri backend, tipoTouchPad, pixel e fasi di gesto congiunti possono
  selezionare la risposta diretta. Un primo evento quantizzato120 con almeno8pixel
  resta animato anche se dichiaratoTouchPad; questa è ancora un'euristica,
  non un riconoscimento universale dell'hardware.
- **Rotella animata**: ogni evento di scorrimento usa la durata scelta.
- **Touchpad diretto**: ogni evento di scorrimento viene applicato subito,
  preferendo i pixel quando presenti. È anche l'override per dispositivi
  che il backend non identifica correttamente.

Il percorso automatico resta stabile nella sequenza: viene riaperta dopo
180ms di inattività, cambio dispositivo o fine/inizio esplicito del gesto.
Un inizio/fine a delta nullo non genera zoom. Il pinch nativo è separato:
segue direttamente il fattore incrementaleQt `1 + value`, senza aggiungere
un'altra animazione; gli altri gesti nativi non vengono convertiti in zoom.

**Sensibilità** (5–1000%) e **Durata transizione** (0–10secondi) sono indipendenti.
Il default della durata è `.250` secondi, pari al sorgente presente all'inizio
di questa implementazione; per riprovare3secondi basta impostare `3.000 s`
nel dialogo. Zero applica immediatamente. Le chiaviQSettings condivise sono
`view/zoomMode`, `view/zoomSensitivity`, `view/zoomDuration`. La conferma aggiorna
anche i controller delle finestre già aperte e annulla gli eventuali residui.
Le tavole offrono lo stesso dialogo tramite **Impostazioni zoom…** nella barra.

Il residuo si distribuisce uniformemente nel logaritmo della scala, senza
easing. Eventi concordi si sommano; l'inversione scarta il residuo opposto.
Il timer preciso16ms usa il tempo effettivamente trascorso, anche dopo uno
stallo dell'UI. Clamp prima dell'esponenziale e limiti della singola vista
impediscono overflow o accumulo ai fine corsa. Gli adattatori mantengono
l'ancora al cursore; nelle tavole resta l'arrotondamento delle scrollbar intere.
Clic/pan, perdita focus e inquadramento eliminano il residuo; il3D mantiene
anche le interruzioni su cambio vista e disabilitazione della rotella.

Il test `forgecad_smooth_zoom_tests` comprende profiliQt sintetici per fallback
X11/pixel nativi, fasi, frazioni, mouse con pixel e mouse dichiaratoTouchPad,
più durata3secondi, sensibilità, override, pinch, dialogo e persistenza.
Il casoTouchPad con angolo-120/pixel-15 riproduce i dati fisici registrati
nella prova precedente dell'utente. `forgecad_drawing_view_edit_tests` verifica
anche animazione/ancora nelle tavole e interruzione su pan/Adatta foglio.
Questi test non sostituiscono un collaudo fisico su Windows, macOS e Wayland.
In sessione grafica `forgecad_view_tests --smooth-zoom --gl` verifica i
fotogrammi effettivamente presentati nel viewport vuoto.

Per registrare gli eventi senza modificare documenti:

```sh
FORGECAD_ZOOM_TRACE=1 QT_FORCE_STDERR_LOGGING=1 ./forgecad-release/forgecad 2>zoom-input.log
```

Le righe `ZOOM {…}` contengono JSON con backend, dispositivo/tipo, timestamp,
angolo, pixel, fase, inversione, incremento normalizzato, percorso e preferenze.
La diagnostica resta silenziosa quando la variabile non è impostata.

Riferimenti: [Qt QWheelEvent](https://doc.qt.io/qt-6/qwheelevent.html),
[Qt QNativeGestureEvent](https://doc.qt.io/qt-6/qnativegestureevent.html).
Il confronto iniziale con [SolidCraft v0.19.0](https://github.com/tangency-labs/solidcraft/blob/v0.19.0/crates/ui-egui/src/navigation.rs)
ha guidato lo studio della normalizzazione. Passo e avanzamento uniforme
attuali sono una taraturaForgeCAD, non una replica del filtroegui.

## Contesto durante la modifica di uno schizzo

In modifica schizzo la vista recupera gli stadi costruiti prima della prima feature che usa quel profilo nella storyboard, limitandosi anche al cursore di inserimento corrente. Uno schizzo nuovo o non ancora usato mostra il modello al cursore. La visibilità temporanea rispetta i corpi nascosti e i consumi delle booleane precedenti, senza modificare il documento, il cursore o la cronologia undo. Rendering, inquadratura e selezione dei riferimenti usano lo stesso contesto; uscendo torna la vista ordinaria.

Per il contesto trasparente un passaggio di sola profondità individua la superficie più vicina fra tutti i corpi, comprese le istanze. Il colore viene miscelato una sola volta su quella superficie: facce posteriori sovrapposte non aumentano l'opacità. Gli spigoli posteriori seguono l'opzione dedicata; lo schizzo rimane sovrapposto e leggibile. Questo è un contesto grafico per disegnare, senza simulazione fisica della trasmissione attraverso tutti gli strati.

Verifica nativa: `forgecad_view_tests --sketch-ghost-render` controlla opacità al35%,25strati,25istanze e corpi sovrapposti. `--render-sketch-context documento.prt indice immagine.png` permette una cattura su un documento senza salvarlo.

## Esperimento solver, mesh e GPU — branch solver-mesh-gpu

Il branch sperimentale conserva la geometria B-rep in double. Il solver usa
derivate analitiche per coincidenza, orizzontalità/verticalità, concentricità,
punto medio e target; gli altri residui conservano le differenze centrali.
Jacobiano e analisi dei gradi di libertà sfruttano le componenti indipendenti;
ogni componente del solve usa Cholesky locale con lo stesso parametro di
regolarizzazione globale del percorso precedente. Una componente grande e
connessa mantiene ancora una fattorizzazione densa: non è un solver sparso
generale con fattorizzazione simbolica riutilizzata.

La rigenerazione prepara i B-rep degli stadi intermedi senza tassellarli.
Scena, rollback, contesto schizzo e picker materializzano le mesh richieste;
le istantanee Undo conservano le proprie cache. Il cambio qualità riguarda
gli stadi visibili, gli altri vengono aggiornati quando richiesti.

Il nuovo filtro GPU delle booleane usa Metal su macOS e CUDA su Linux/NVIDIA.
I box double già espansi della tolleranza vengono convertiti in intervalli
float arrotondati verso l'esterno; il filtro non costruisce geometria e può
solo conservare candidati in eccesso. Tutti i candidati passano dal confronto
double prima delle intersezioni esatte. Errori, dati fuori campo o oltre il
budget di16MiB della maschera riportano al BVH CPU. Il filtro sperimentale
lavora su prodotti fra65.536e16.777.216coppie; il costo è quadratico, quindi
non sostituisce in generale il BVH e non è abilitato per default.

Avvio della build del branch, senza cambiare le preferenze salvate:

```sh
python3 tools/run_solver_mesh_gpu.py
python3 tools/run_solver_mesh_gpu.py --baseline
python3 tools/run_solver_mesh_gpu.py --gpu --profile
```

È possibile aggiungere il percorso di un documento. Per confrontare calcoli
effettivi, modificare uno schizzo o usare **Rigenera tutto**: le cache già
salvate possono evitare il ricalcolo alla sola apertura. L'opzione baseline
seleziona `FORGECAD_SOLVER_DENSE=1`, `FORGECAD_EAGER_MESH=1` e disabilita il
filtro GPU. `--gpu` abilita `FORGECAD_GPU_BROAD_PHASE=1`; la tassellazione CUDA
già esistente conserva il proprio interruttore separato.

Verifiche riproducibili: `forgecad_view_tests --solver-partition` confronta
soluzioni e DOF con il percorso denso; `--lazy-mesh` verifica volume analitico,
rollback, qualità e snapshot. `forgecad_gpu_broad_phase_tests --cpu-fallback`
controlla errori e cancellazione; `--require-gpu` richiede l'esecuzione reale.
Senza GPU il test CTest usa lo stato SKIP77, non un falso PASS.
Il modulo CUDA richiede una verifica separata su Linux/NVIDIA: la compilazione
del solo backend Metal non la sostituisce. Questo primo percorso non migra
intersezioni, raccordi, misure o topologia alla GPU.

Misure finali isolate del 25 ottobre, macOS arm64 Release: 100 vincoli orizzontali
indipendenti 2,75342 → 0,215334 ms (12,79×); 400 vincoli 205,498 → 0,849625 ms;
800 vincoli 1716,02 → 1,66217 ms. I valori sono **mediane di 5 chiamate alternate**
per versione nello stesso processo, con copie ripristinate dell'input.
Storia sintetica di 20 stadi di scala della sfera: 49,7512 → 2,96721 ms (16,77×),
mediana di 3 caricamenti alternati, 20 mesh contro 1. Non sono benchmark generali.
Sul controllo booleano dei prismi a 256 lati, una singola validazione Metal finale
misura 68,1387 ms CPU e 69,4297 ms GPU: nessun vantaggio dimostrato del filtro,
che resta opt-in. Non sommare questi fattori e non estenderli a ogni documento.

Per ripetere il benchmark solver usare `forgecad_view_tests --solver-partition --benchmark`
in ambiente Qt offscreen; senza `--benchmark` esegue soltanto le
regressioni. Protocollo, dati e limiti sono conservati in
[solver-mesh-gpu-2026-10-25.json](benchmarks/solver-mesh-gpu-2026-10-25.json).
