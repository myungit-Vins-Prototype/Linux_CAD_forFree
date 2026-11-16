# Review: ricostruzioni UV degli offset e selezione anteprime — 16 novembre 2026

**Correzioni integrate e verificate sull’app e sui runner Release ricompilati.** Due ulteriori siti di K10 hanno ora un percorso continuo per Plane; PRE7 corregge le anteprime superate di Mapping e Riempimento pattern. Tre agenti GPT-6.1 Sol hanno svolto implementazione, regressioni e audit separati. Il totale dei rilievi confermati corretti sale a **28**; K10 rimane aperto, senza contare le sue singole famiglie come nuovi difetti.

## K10: associazione del bordo alla faccia

`offsetFaces` con rifilatura e `offsetJoinedFaces` ricostruivano le SP-curve tramite fit campionato; il controllo successivo usava65 parametri uniformi. Nel secondo percorso, un box regolare con un bordo rettilineo riparametrizzato da NURBS cubica a tratti perde un piccolo lobo del parametro tra0,1003 e0,1013. Bordo e SP-curve sorgenti conservano la stessa corrispondenza alla risoluzione del kernel. Con tutte le distanze di offset nulle, il bordo3D viene mantenuto mentre la SP-curve viene appiattita in una retta nel parametro.

A t=0,1008 il residuo **`||S(PC(t))-E(t)||` è circa7,500005e-6mm**, oltre il budget dichiarato della fin, **1e-6mm**. Input e output superano `checkBody`; la sorgente serializzata rimane invariata. È violato il contratto del medesimo parametro fra edge e fin: non si afferma una perdita di volume o uno spostamento dell’insieme geometrico del bordo.

Sei varianti pubbliche riproducono il difetto: polinomiale, razionale, intervallo traslato con trim, verso e ordine dei supporti invertiti, posa e scala1000. La prova indipendente Fraction trova dodici fin fuori budget, due per variante, con testimoni razionali e un limite inferiore sicuro superiore al budget. Le due prove di `offsetFaces(trimSeams)` erano già positive sulla baseline e restano controlli di compatibilità.

### Contratto della correzione

`detail::planePCurveErrorBound` conserva snapshot posseduti dei coefficienti originali del bordo e degli assi O/X/Y del Plane. Supporta Line e NURBS con pesi positivi, wrapper Trimmed/Transformed e intervalli originali. Produce due certificati distinti:

- **Proiezione sul piano in mm**, per guidare il fit delle coordinate UV al budget precedente0,1*tolleranza.
- **Residuo completo contro il bordo3D**, per rispettare il `pcurveTolerance` realmente assegnato alla fin, compresa l’eventuale distanza normale ereditata.

La normale viene racchiusa dagli assi originali, senza assumere un frame double perfettamente ortonormale. I poli UV candidati vengono sollevati con aritmetica outward e confrontati con gli originali omogenei in Bernstein. Non si usa una superficie campionata o una proiezione intermedia arrotondata come fonte del certificato. I nodi del bordo effettivo guidano la suddivisione, senza sostituire la prova continua.

Il nuovo helper privato `fitPlanePCurveCertified` accetta esclusivamente il limite fisico in mm. Le callback e i poli restano controllati per finitezza; la distanza campionata in UV non entra nel budget fisico. Le API pubbliche precedenti `fitCurve` e `fitCurveCertified` mantengono il percorso euclideo esistente. Una prova privata con asse X di scala1e-12 dimostra errore UV3725,32 e limite fisico9,93415e-9mm, correttamente accettato entro1e-8mm.

Nei due percorsi offset, i certificati vengono applicati prima dell’assemblaggio e ricreati dopo su **edge, Plane e SP-curve realmente emessi**. `AssemblyMap` fornisce la coppia faccia/edge; si controllano tutte le fin incidenti, presenza, numero, intervallo e multinsieme esatto dei budget. Nessun riaggancio per prossimità. Una guardia verifica l’intero schema dei nodi Bézier prima di interpretare i gruppi di quattro poli; una cubica a sette poli con nodi lisci viene rifiutata.

Factory invalide/singolari lanciano eccezione; query inconclusive restituiscono infinito e non autorizzano un fit. Solo famiglie non supportate restituiscono un certificato vuoto e conservano il percorso precedente. Restano limiti finiti di coefficienti e raffinamento, cancellazione e fallimenti atomici. I budget delle fin e le tolleranze impostate non aumentano. In `offsetJoinedFaces` il Plane offset definitivo viene costruito prima delle UV e poi riusato identico.

## PRE7: anteprima superata durante selezione continua

In Mapping e Riempimento pattern, modificare i riferimenti e rimanere nella selezione continua sospendeva soltanto nuove richieste. Togliendo l’unica faccia in Mapping, lista vuota e vecchio B-rep rimanevano etichettati «pronta», con chiave e generazione invariate. Lo stesso meccanismo coinvolgeva i contorni del Riempimento pattern e poteva lasciare un worker precedente ancora attivo.

Il nuovo helper locale `refreshSelectionPreview` confronta la definizione con l’ultima richiesta. La sola apertura del picker conserva il comportamento corrente; una mutazione degli input invalida immediatamente chiave e geometria, annulla il worker, incrementa la generazione e indica **«Termina la scelta dei riferimenti.»**. Mapping elimina anche il vecchio esito. Il calcolo riprende alla fine della selezione. La pubblicazione tardiva del vecchio worker viene respinta dal meccanismo di generazione esistente.

La correzione riguarda un solo file di produzione, `forgeCad2026_dialogs_mapping.cpp`, senza nuove API comuni. La regressione permanente copre sorgente sviluppo, destinazione deformazione e contorno Riempimento pattern, ciascuno con geometria pronta e worker pendente. Nei casi pendenti, il token reale è verificato falso prima della mutazione e vero dopo; sono controllati generazione, assenza di pubblicazione tardiva, recupero e cleanup. I callback del fixture seguono il normale annullamento del picking prima dell’esito. Il nuovo comando del runner isola le preferenze in una directory temporanea.

## Verifiche finali e attribuzione

| Verifica | Risultato |
| --- | --- |
| Build completa | **App, kernel e runner PASS**, binari successivi ai sorgenti |
| CTest pertinenti | **30/30 PASS**, inclusi i due nuovi target e le regressioni precedenti di curve, offset, booleane, storia, durata documenti e pannelli |
| Nuovo target UV | **10/10 gruppi PASS**; baseline pubblica sei fallimenti attesi e tre gruppi compatibili |
| Regressioni generali Offset/Shell | **27/27 e18/18 PASS**; Offset anche prima delle modifiche27/27 |
| PRE7 permanente | **79/79 asserzioni PASS** offscreen e su **Wayland nativo**, usando il runner finale senza overlay |
| Negativo PRE7 | Stesso header su oggetti app precedenti: **48 PASS /31 FAIL**, exit1 |
| ASan/UBSan/LSan mirati | **10/10 PASS**, due TU kernel e driver strumentati, resto kernel Release; nessuna diagnostica o soppressione |
| Guardie private | **7/7 PASS**, inclusi nodi, metrica fisica, modifica di un polo finale e alterazione del budget |
| Attribuzione Release | Exporter del target permanente collegato all’archivio finale: **8/8 JSON byte-identici** ai dati V2 già verificati |
| Certificazione indipendente | **192 fin finali,248 campate complete**, tutte entro il budget pubblicato e il budget del fit proiettato;192 fin sorgenti entro risoluzione esplicita1e-7mm |
| Limite indipendente massimo | **≤2,427e-8mm** su tutti i casi; senza scala1000 **≤4,015e-10mm** |
| Negativo del verificatore | Un polo UV finale spostato di1e-4mm viene rifiutato |
| Architettura | **767 file /11085 archi /383 TU**, zero diagnostica; check, hash, HTML/JSON e633 archi incidenti verificati nei due versi |
| Conservazione | **3378/3388 file preesistenti invariati**; dieci file esistenti modificati nello scope, due nuove regressioni e documentazione/prove inventariate |

La verifica Fraction non chiama il provider C++ né usa i suoi coefficienti estratti. Parte dai double esportati in esadecimale: inserimento nodi e restrizione razionali, sollevamento affine del Plane originale, residuo omogeneo e inviluppo Bernstein con pesi positivi. Le radici hanno limiti razionali e i riepiloghi floating-point sono arrotondati verso l’esterno. Verifica separatamente l’intera corrispondenza3D e la proiezione usata dal fit. I coefficienti sorgenti costruiti in double possono avere roundoff anche quando il budget fin iniziale è zero: questo controllo usa dichiaratamente la risoluzione1e-7mm, senza fingere uguaglianza algebrica esatta.

Il sanitizer V2 riguarda `fk_offset.cpp`, `fk_curve_fit_bounds.cpp` e il main della regressione; il resto è l’archivio Release precedente, dichiarato nei metadati. Il primo tentativo V1 fermato da LSan/ptrace è conservato e distinto dal V2 riuscito unrestricted. Una versione privata iniziale mescolava ancora errori UV e mm: è stata corretta prima dell’integrazione. La modifica finale del commento dell’header chiarisce `throw` della factory e infinito delle query; i token C++ coincidono con la versione strumentata. Il test permanente coincide byte per byte con l’output di clang-format del candidato, con gli stessi gruppi attivi; il formatter può dividere stringhe in letterali adiacenti. Le prove di guardia private non sono sette ulteriori difetti o sette corpi.

Il primo fixture UI77PASS/2FAIL non emulava l’annullamento del picking prima del callback; è conservato e corretto soltanto nel test. Il replay finale usa l’header effettivamente integrato. Le prove Wayland attestano comportamento dei pannelli e dei worker; non sono una nuova certificazione del renderer OpenGL.

## Punti ancora aperti

**K10:17/24 siti iniziali parzialmente trattati,7 interamente da migrare**, oltre ai fallback. Restano due giunti/continuazioni dei raccordi, due trasferimenti UV mapping/quilt, la rimappatura SP-curve nella cucitura, la giunzione delle superfici estese e le curve procedurali proiettate nello schizzo. Le famiglie non Plane dei due siti appena trattati conservano il percorso campionato.

Questa correzione certifica la corrispondenza delle fin piane riconosciute: non dimostra globalmente la correttezza della superficie offset, del bordo3D generato a monte, della topologia, dei volumi o l’assenza di autointersezioni. Risorse e intervalli conservativi possono rifiutare dati validi. L’uniformazione completa delle anteprime, dei pannelli e degli stili resta aperta; patch/corpo completo e avvio manuale/automatico non sono conteggiati automaticamente come bug. L’ultimo inventario delle orfane rimane separato:5 private/locali candidate e17 API pubbliche senza usi interni, nessuna rimozione in questo blocco.

CAD originali e processi utente non sono modificati o chiusi. Nessun commit/push. I quattro manifest precedenti sono ricontrollati e immutati: ellisse95 artefatti, offset bordi146, pannelli172, Supporto267. Riaprire ForgeCAD per usare la build aggiornata.

[Indice delle prove](review-k10-uv-preview-prove-2026-11-16/README.md), [audit finale](review-k10-uv-preview-prove-2026-11-16/root/final-audit.json), [verifica razionale](review-k10-uv-preview-prove-2026-11-16/root/fraction-summary.json), [memoria precedente](history/claude-prima-k10-uv-preview-2026-11-16.md). [Registro della review](code-review-2026-11-15.md).
