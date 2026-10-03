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

La GPU già eseguiva trasformazioni, illuminazione della pipeline fissa, rasterizzazione, profondità, stencil e antialiasing OpenGL. Ora riusa anche la geometria residente nei VBO: si riducono chiamate CPU e invii ripetuti per ogni fotogramma. Il profilo compatibility resta necessario.

Anche la sfocatura dei pannelli funzione usa ora OpenGL quando sono disponibili GLSL 1.20 e il blit tra framebuffer. La scena viene copiata una volta per fotogramma, sfocata con due passaggi separabili al 75% della risoluzione del viewport e ricomposta soltanto nelle aree arrotondate dei pannelli. Il 75% riduce la pixelatura rispetto alla precedente metà risoluzione, con 2,25 volte i frammenti del filtro ma ancora meno lavoro del formato pieno. Questo elimina la lettura sincrona del framebuffer e i filtri d'immagine sul thread dell'interfaccia. Se il percorso shader non è disponibile, rimane attivo il precedente fallback CPU condiviso e limitato a circa 15 aggiornamenti al secondo. Questa accelerazione non dipende da CUDA.

**CUDA valuta ora le superfici B-spline/NURBS della tassellazione di display.** Il raffinamento di `fk_tessellate` raccoglie a ogni giro i punti medi dei lati da provare e li valuta in un lotto: con un acceleratore (`SurfaceBatchEvaluator`, nell'app `cad_cuda_tessellation` + `cuda_support.cu`) i lotti di almeno 512 punti su una `BSplineSurface` vanno alla GPU (un thread per punto, in double, stessi algoritmi del kernel: scarto dalla CPU 7e-16 relativo sui punti, 3e-14 sulle normali, verificato da `forgecad_cuda_tests`), gli altri alla CPU. Il guadagno misurato e' piccolo, perche' dopo le ottimizzazioni la valutazione e' circa il 15% del tempo: su AP0730-REV00.STEP a qualita' alta la tassellazione passa da 1,9 s a 0,24 s per il lavoro sulla CPU (facce in parallelo, decisioni sui lati prese una volta, tabelle dei lati a indirizzamento aperto, punto valutato riusato come vertice) e a 0,20 s con CUDA; con un thread solo la GPU non conviene (latenza di circa 60 microsecondi per lotto, con la scheda in P8 a batteria). Booleane, misure, raccordi e la geometria esatta restano sulla CPU; si spegne con *Opzioni → Tassellazione con CUDA*.

Roadmap GPU: mantenere sempre il backend CPU e aggiungere CUDA come acceleratore opzionale, scelto a runtime. I primi candidati sono operazioni in grandi batch con dati regolari: valutazione/tassellazione di curve e superfici, classificazione preliminare di raggi e coppie di facce, analisi di mesh e anteprime. La topologia B-rep esatta, le booleane e la costruzione dei raccordi restano inizialmente sulla CPU: hanno molte diramazioni, strutture dinamiche e casi degeneri, quindi trasferirle per prime aumenterebbe la complessità senza garantire un guadagno. Risultati CUDA e CPU dovranno avere gli stessi controlli di tolleranza e il fallback CPU dovrà funzionare anche quando il progetto è compilato senza `nvcc`.

Gli impieghi GPU più promettenti per ForgeCAD sono la tassellazione di molte facce, il campionamento in parallelo di curve e superfici, la selezione preliminare tramite un buffer di identificatori e l'istanziazione grafica delle ripetizioni. La GPU può anche preparare coppie candidate di facce o box per intersezioni, lasciando al kernel CPU la verifica esatta. Le booleane B-rep finali, la modifica della topologia e gran parte del risolutore dei vincoli hanno molti rami, strutture dinamiche e dipendenze sequenziali; inoltre usano `double`. Sulla Quadro RTX 3000 il vantaggio CUDA in doppia precisione è molto inferiore a quello in `float`, quindi trasferirli integralmente rischierebbe di aumentare latenza e complessità senza un guadagno stabile.

Un ulteriore intervento possibile è una selezione GPU con identificatori di facce/oggetti in un framebuffer e successiva verifica geometrica esatta. Richiede gestire correttamente trasparenze, sezione e lettura asincrona dei risultati. Per anticipare il risultato delle operazioni geometriche servirebbe invece profilare singolarmente i moduli del kernel: spostare il calcolo esatto su CUDA è un intervento distinto, da validare numericamente.

## Valutazione del passaggio a OpenGL 3

Il passaggio al solo contesto OpenGL 3 non produce automaticamente un aumento
di prestazioni. Il lavoro utile consiste nel sostituire la pipeline fissa e i
percorsi `glBegin/glEnd` residui con shader, VAO/VBO persistenti e disegno
aggregato, e nel separare la selezione dal calcolo geometrico esatto tramite un
buffer GPU di identificatori. Una migrazione completa e verificata richiede
indicativamente **2-4 settimane**: nuovo renderer, materiali e luci, overlay e
anteprime, picking, fallback/diagnostica e test su driver diversi. La
compatibilita' OpenGL 3.3 rende inoltre piu' lineare il futuro porting Windows e
macOS, dove il compatibility profile non e' una base affidabile.

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

In una sessione grafica con OpenGL desktop, `forgecad_view_tests --gl` verifica anche il disegno dei buffer e confronta pixel per pixel il rendering VBO e quello immediato dello stesso corpo. Le impostazioni dei test sono temporanee. Il test comprende ridimensionamento, annullamento, Undo/Redo, vincoli sui riferimenti fissi, salvataggio e riapertura del documento, e invalidazione della cache durante il pan.
