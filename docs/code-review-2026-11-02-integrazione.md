# Code review del 2 novembre 2026 — prosecuzione e verifica locale

> **Aggiornamento:** le correzioni e la nuova estrusione sono descritte nel
> [rapporto conclusivo](code-review-2026-11-02-correzioni.md). I rilievi e gli stati
> aperti riportati sotto documentano la review prima di queste correzioni.

Stato: review di prosecuzione conclusa sullo stato attuale; cinque difetti funzionali confermati, ancora da correggere.
Recuperati i risultati finali della suite archiviata e ripetute le verifiche mirate. La build completa PASS appartiene
alla sessione precedente; in questa verifica ricompilate le sonde e usati i binari Release locali esistenti.
Base: `ab6fc71c8e639da620521a815b4b4101d2b7167b`, branch `main`.
Le modifiche documentali preesistenti ad `AGENTS.md`, `CLAUDE.md` e `docs/history/` sono preservate.
Nessuna correzione funzionale applicata durante la review.

La richiesta è proseguire la review lasciata da Claude al raggiungimento dei limiti giornalieri. Il rapporto
[precedente del 2 novembre](code-review-2026-11-02.md) descrive la base `7651724`: i suoi «ancora presente» e
«non ancora committate» sono stati superati dal commit `9321643`. Lo spostamento dei confronti OCCT è in `ab6fc71`.
Questo rapporto distingue difetti attuali, punti già corretti e ipotesi ancora senza riproduzione.

## Rilievi confermati, in ordine di priorità

### R1 — P1: un annidamento STEP profondo manda ancora in crash l'importazione

**Sede:** `kernel/fk_step.cpp:671`, `688–697` (`StepFile::value` / `list`).

La guardia introdotta per N2 protegge la costruzione di curve e superfici, ma il parser sintattico viene eseguito prima.
Ogni `(` richiama `list()`, che richiama `value()`, senza un limite. Non serve una geometria valida o un ciclo fra entità:
anche un'entità sconosciuta viene analizzata ricorsivamente prima di essere ignorata.

Riproduttore minimale:

```python
n = 100_000
content = 'ISO-10303-21;\nDATA;\n#1=IGNORED(' + '(' * n + '0' + ')' * n
content += ');\nENDSEC;\nEND-ISO-10303-21;\n'
# Passare content a ForgeCad::Kernel::readStep(content).
```

**Verificato:** 200.061 byte; processo Release terminato con segnale 11. ASan sul vero `fk_step.cpp` conferma
`stack-overflow`, stack alternato `list:688/697 → value:671`. Nella prima sessione a profondità 100 e 10.000
la sonda termina. Nella ripresa locale confermate liste a profondità 100 e 100.000 e anche **parametri tipizzati `T(T(...))`**: 300.061 byte a profondità 100.000,
segnale 11 in Release e `stack-overflow` ASan (`value:682 → list:697`); profondità 100 termina.
Il crash avviene anche se `readStep` è racchiuso in `catch(std::exception&)`; nell'app il thread non isola il processo.

**Proposta:** limite sintattico indipendente da quello geometrico, applicato sia alle liste sia ai parametri tipizzati;
errore controllato prima di costruire alberi profondi, considerando anche la distruzione ricorsiva dei `Param`.
Regressione richiesta: liste e parametri tipizzati profondi, senza terminazione anomala.

### R2 — P1: 38 byte di B-rep richiedono 2,4 GB prima del controllo di troncamento

**Sede:** `kernel/fk_body_io.cpp:411–424`, in particolare `417`.

Nel ramo `SurfaceType::BSpline` il lettore verifica soltanto `uCount * vCount ≤ 100.000.000`, quindi alloca tutti i poli.
Non verifica che i relativi 24 byte per polo esistano nell'input. Il primo controllo dei byte avviene nel ciclo successivo.
L'uso di `Reader::count` per nodi e altri record non protegge questo ramo.

**Verificato con sonda che intercetta `operator new`:** header nativo `FKBD`, versione 1, byte order `0x01020304`,
record superficie 3, tipo BSpline 7, gradi 1/1, vettori nodi vuoti, conteggi 10.000/10.000, nessun dato successivo.
Input totale **38 byte**, richiesta intercettata **2.400.000.000 byte**. La sonda la rifiuta prima dell'allocazione
e riceve `std::bad_alloc`. Nessuna allocazione di 2,4 GB realmente eseguita e nessun OOM dell'app dichiarato.

Il percorso è usato anche per la cache dei documenti nativi (`cad_document_io.cpp:1176`). Catturare un'eccezione dopo
l'allocazione non impedisce il consumo della memoria o l'intervento del sistema operativo.

**Proposta:** controllare dimensioni/gradi/nodi prima dell'allocazione, usare moltiplicazioni controllate e confrontare
i byte dei poli con quelli rimasti. Se sono presenti pesi, applicare lo stesso criterio. Regressione richiesta:
record troncato rifiutato senza richieste di memoria proporzionali ai conteggi non presenti.

### R3 — P1: poche cifre nelle molteplicità STEP causano allocazioni non proporzionate al file

**Sedi:** `kernel/fk_step.cpp:847–885`, `935–966`; `kernel/fk_bspline.cpp:222–226`.

Le molteplicità sono convertite con `int(number(m))` e passate a `expandKnots`, che esegue `vector::insert` per ciascuna
ripetizione. La validazione del costruttore B-spline viene dopo l'espansione. Perciò un numero enorme di nodi viene
materializzato anche se è già incompatibile con grado e numero dei poli.

**Verificato:** file cubo prodotto da `writeStep`, un edge rimandato a una curva grado 1 con due poli:

```text
#190=B_SPLINE_CURVE_WITH_KNOTS('',1,(#10,#12),.UNSPECIFIED.,.F.,.F.,
                              (100000000,2),(0.,1.),.UNSPECIFIED.);
```

La sonda intercetta una richiesta di **800.000.000 byte**. Dopo il rifiuto artificiale dell'allocazione,
`readStep` restituisce zero corpi e una nota `non ricostruito (std::bad_alloc)`. Il percorso superfici usa lo stesso
helper, ma non è stato sottoposto a una seconda sonda dedicata.

**Proposta:** validare interi finiti/rappresentabili/integrali, positività delle molteplicità, massimo rispetto al grado
e somma attesa `poli + grado + 1` prima dell'espansione. Controllare anche il grado dei nodi impliciti. Un limite
arbitrario elevato sulla sola memoria lascia il difetto; il record è geometricamente invalido già prima dell'allocazione.

### R4 — P2: letture concorrenti del catalogo generano falsi errori e possono far perdere l'accesso in modifica

**Sedi:** `cad_revision_store.cpp:188–190`, `209–210`, `RevisionStore::documents/history`;
`forgeCad2026_gui.cpp:3977–3992`.

È **R3 della review del 25 ottobre**, ancora presente. La pubblicazione aggiorna prima `head.json`, poi `catalog.json`;
i lettori non prendono il lock della transazione e possono confrontare due generazioni diverse. L'errore di integrità
è quindi possibile durante una scrittura regolare, senza dati corrotti.

**Nuova riproduzione sui sorgenti correnti:** archivio temporaneo, 300 cicli `checkout → cancelCheckout` in un thread,
`documents()` ripetuto nell'altro: **345 falsi errori su 2.350 letture** nella prima sessione; nuova esecuzione nella ripresa:
**181 errori su 2.258 letture**; verifica di chiusura: **294 errori su 2.279 letture**,
writer senza errori e lettura finale riuscita in entrambi i casi.
Il conteggio dipende dalla schedulazione e non è una percentuale garantita.

Il flusso GUI traduce uno stato revisioni non verificato in sola lettura e rilascia `documentLock_`. Al ritorno del
catalogo coerente non riacquisisce automaticamente l'esclusiva. Questa conseguenza è verificata nel codice,
**senza prova integrata con due finestre**; non è una prova di perdita di contenuto già salvato.

**Proposta:** snapshot coerente sotto lock o protocollo di lettura con ritentativo e riconoscimento della transazione
in corso. Separare stato transitorio da corruzione persistente prima di invalidare la sessione di modifica.

### R5 — P2: la deduplicazione STEP elimina occorrenze con trasformazioni distinte

**Sede:** `kernel/fk_step.cpp:1432–1436`.

La chiave della trasformazione usa `std::to_string(double)`, con sei cifre decimali nella toolchain C++17 usata.
Trasformazioni distinte diventano la stessa chiave prima della ricostruzione dei corpi; la seconda occorrenza viene
saltata senza nota. La politica geometrica in double e le tolleranze del kernel non vengono consultate.

**Verificato:** cubo 1×2×3 generato dal writer, istanziato due volte tramite `NEXT_ASSEMBLY_USAGE_OCCURRENCE`,
`CONTEXT_DEPENDENT_SHAPE_REPRESENTATION` e `ITEM_DEFINED_TRANSFORMATION`:

| Traslazioni X delle due occorrenze, mm | Attesi | Importati | Note |
| --- | ---: | ---: | ---: |
| 0 e 0,0000004 | 2 | **1** | 0 |
| 0 e 0,000002 | 2 | 2 | 0 |

Nella ripresa sono confermate entrambe le prove; aggiunte due occorrenze **intenzionalmente coincidenti**:
anch’esse ridotte a un solo corpo senza nota. La distinzione deve quindi includere l’identità dell’occorrenza,
non soltanto più cifre nella trasformazione. La seconda prova della tabella differisce soltanto per la traslazione.
Volume di ogni corpo 6 mm³; baricentro della seconda occorrenza nella controprova `0,50000200000000006, 1, 1,5`.

**Proposta:** chiave sui valori double o rappresentazione round-trip senza arrotondamento; distinguere identità
dell'occorrenza e percorsi di visita duplicati, includendo il contesto quando rilevante. Le rotazioni ravvicinate restano da provare; la perdita di occorrenze
intenzionalmente coincidenti è ora riprodotta.

## Difetti nei test e fallimenti da chiarire

### T1 — P2: l’asserzione sul colore dei pannelli blocca sempre la suite generale

**Sedi:** `tests/test_viewport.cpp:8490–8494`; `forgeCad2026_dialogs_common.cpp:1074–1075`.
`forgecad_view_tests` e `forgecad_orbit_pivot_tests` falliscono entrambi su «testo leggibile nella selezione dei menu
a discesa». Il test pretende `Highlight.red() > 230`, ma `FunctionDialogPanel::updatePalette` assegna esplicitamente
`#c6e4f5` nel tema chiaro (rosso 198) e `#305472` nello scuro (rosso 48). Nessuno dei due può soddisfare l’asserzione;
non è un difetto di leggibilità dimostrato, bensì un’attesa rimasta al precedente colore arancione. Entrambi i runner
eseguono il percorso generale: il primo fallimento impedisce di raggiungere i controlli successivi.

**Proposta:** allineare la regressione al contratto attuale, verificando contrasto e palette nei due temi, senza imporre
il vecchio accento. Non disattivare il controllo e non attribuire a questi run copertura delle sezioni successive.

### T2 — P3: storyboard richiede una cartella temporanea creata fuori dal test

**Sede:** `tests/test_viewport.cpp:2607`.
`forgecad_sketch_story_tests` tenta di salvare `/tmp/sketch-story/tree.png` senza creare la directory.
In un ambiente pulito fallisce su «cattura storyboard con schizzo autonomo» dopo i controlli del modello.
Ripetuto senza modificare i sorgenti, creando soltanto la cartella: **PASS** (0,11 s).
Il risultato dipende quindi da residui di precedenti sessioni. Il percorso fisso può anche collidere fra runner.

**Proposta:** `QTemporaryDir` per le catture, oppure directory diagnostica configurabile creata e controllata dal test.

### Fallimento numerico da chiarire — sformo ripetuto del cono

**Sede:** `kernel/tests/test_draft.cpp:134`, `DraftCurvedAndConcaveSolids`.
Il log completo archiviato contiene **457/458 test del kernel superati**, con questo unico fallimento. Ripetuto
isolatamente anche nella verifica di chiusura: volume `253.5270716475676`, atteso `253.5270734420086`, differenza
`1,794441e-6 mm³` contro una tolleranza assoluta di `1e-6 mm³`. L'errore relativo è circa `7,08e-9`.
È un fallimento riproducibile in Release; non va liquidato come un effetto dei sanitizer o di OCCT 7.6.
La causa fra geometria dello sformo, quadratura della massa e contratto della regressione resta da isolare.
Non è conteggiato nei cinque difetti funzionali confermati e non sono state cambiate tolleranze o asserzioni.

### Tema e focus — distinguere ambiente e applicazione

Nella suite archiviata i test di espressioni, focus e tema fallivano con il tema di piattaforma ereditato. Le ripetizioni
allora registrate recuperavano espressioni e focus, mentre tema andava in timeout nella creazione di Breeze, riprodotta
anche da una sonda soltanto Qt. Nella verifica corrente, con backend offscreen, Fusion, tema di piattaforma vuoto e
cartelle XDG temporanee, **tutti e tre passano senza modificare il codice**. Il tema riporta zero richieste native e
cinque segnali simulati: non è una prova del cambio tema nella sessione grafica dell'utente e non isola quale differenza
dell'ambiente elimini il vecchio timeout. Nessun nuovo difetto funzionale del tema dichiarato.

## Stato del lavoro di Claude e degli altri rapporti

| Punti precedenti | Stato attuale verificato |
| --- | --- |
| N1/N2: cicli STEP | Guardie geometriche presenti in `9321643`; R1 riguarda il parser, un percorso differente. |
| B1/B2/B3/B3b: conteggi, spline e ricorsione IGES | Controlli e regressioni presenti; non ripresentati come difetti aperti. |
| B4/B5: espressioni ambigue/profonde | Prodotto implicito ristretto e limite 256 presenti; suite Expression locale PASS. |
| B6: risorse GL | `glassCompositor_.clearResources()` aggiunto al segnale del contesto; test di ricreazione GL ancora da eseguire. |
| B7: Qt 6.4 | Guardia per API Qt 6.5 presente; ambiente attuale Qt 6.12, quindi non è una nuova prova Qt 6.4. |
| B10 e N4 | Controllo normale degenere e catture compatibili C++17 presenti. |
| N3 / separazione OCCT | Ricerca confinata a `kernel/occt_tests`; app/kernel senza `HAVE_TK` nei flag esaminati. Build completa PASS; assenza di librerie OCCT in `ldd forgecad` e nei flag/link app/kernel verificata nella sessione archiviata. Esiti finali della suite recuperati sotto. |
| B8: riferimento a `extrusions_` durante loop annidato | Meccanismo copy-on-write ancora possibile; nessun percorso reale riprodotto. Rimane ipotesi, esclusa dai cinque difetti confermati. |
| R3 del 25/10 | Riprodotto nuovamente come R4 sopra; non era una correzione già completata. |
| R1/R2/R4/R5/R6/R7/R8 del 25/10 | Modifiche presenti; verifiche correnti e limiti vanno distinti dagli esiti storici. |

Per B8 ho ricontrollato i chiamanti con riferimento interno, i percorsi `rebuildParallel`, i timer di preview/revisioni,
le guardie di paint/autorecupero e la consegna asincrona. `rebuildParallel` sul singolo indice riassegna già il risultato
per indice, mentre `rebuildBody` conserva il riferimento. Una sonda che forzasse appositamente una scrittura durante
il loop dimostrerebbe il rischio del pattern, ma non proverebbe un evento raggiungibile dall'interfaccia normale.

## Orfane e semplificazioni

Ricontrollate le occorrenze nei sorgenti C++/header/CUDA/Objective-C++ versionati, inclusi i test. Restano con sola
dichiarazione/definizione: `setSelectedPointPosition`, `setEdgePickLinear`, `setExtendPickCallback`, `resumeExtendPick`,
`constraintsVisible`, `sectionEnabled`, getter `geometrySnap/gridSnap`, `extrusionNames`, `ensureBodyDisplay`,
`StoryboardTree::firstFeature`, `DrawingSheetEditor::setModelProvider`, `forgeIntersectRay`, `modelBodyFeatures`,
`usesSketch`, `commandIconNames`, `nearestBodyEdge`, `appendFixedReferenceLine`, `Kernel::transformSurface`.
I wrapper viewport `basisMatrix/normalizedViewAngle` restano inutilizzati; gli omonimi della camera sono usati.

Questa è una conferma testuale dei candidati precedenti, **non una nuova prova linker né autorizzazione a rimuovere API**.
`drawSketchDimensions` ha un chiamante nei test; `resumeEdgePick` è chiamata dal pannello raccordi: non vanno eliminate
come orfane. Callback Qt, override e consumatori esterni richiedono verifica prima di qualunque rimozione.

Le semplificazioni precedenti restano proposte. Ordine utile dopo i difetti:

1. Unificare il ramo Cholesky denso con `ConstraintLinear::minimumNormStep`, conservando ordine aritmetico e criteri
   di arresto; confrontare solver e schizzi difficili prima di cambiare il percorso numerico.
2. Riutilizzare la cache delle lunghezze in `blendPathPoint`: oggi esegue 48 integrazioni da inizio bordo per stazione.
   Il costo è visibile nel codice; nessun miglioramento percentuale misurato in questa review.
3. Separare `RibbonPreview::runSelfTest` dai sorgenti dell'app, lasciandolo al runner; è ancora nello stesso oggetto
   compilato in produzione. Non è un difetto geometrico.
4. Consolidare prima uguaglianza/versioni/UUID; evitare un unico helper generico per funzioni con contratti geometrici
   diversi soltanto perché hanno lo stesso nome. Helper banali ripetuti hanno priorità inferiore ai difetti sopra.

## Verifiche, copertura e limiti

Inventario precedente: **501 file C++/header/CUDA/Objective-C++, 159.407 righe**. Analisi trasversale con letture mirate,
ricerca dei chiamanti, build e riproduttori; non lettura riga per riga né certificazione di assenza di altri bug.
Base corrente invariata: `ab6fc71`, sorgenti funzionali senza modifiche durante la review.

### Esiti recuperati dalla sessione precedente

L'[archivio dei riproduttori](history/code-review-2026-11-02-reproducers.zip) conserva sorgenti delle sonde, input,
`summary.json`, `ctest.log`, diagnostica ASan e log delle ripetizioni. Il rapporto era rimasto con lo stato «in corso»,
ma i log contengono già l'esito finale:

- Build completa Linux di app e runner **PASS** nella sessione archiviata; non ripetuta nella verifica di chiusura.
- CTest: **133 PASS, 7 FAIL, 6 SKIP su 146**, 375,96 s. Dopo le ripetizioni registrate allora:
  **136 PASS, 4 FAIL, 6 SKIP**. Rimanevano i due runner generali bloccati da T1, tema e kernel/sformo.
- Il runner kernel esegue **457/458 test con successo**; unico fallimento `DraftCurvedAndConcaveSolids`, descritto sopra.
- Per quella suite il manifest CTest usava copie congelate di `Mouse.prt`, `Applicatore.prt` e `prova con loft.prt`,
  con hash in `fixtures.json`. Queste sono prove archiviate; nella verifica corrente non riaperti i CAD originali.
- I sei skip riguardano broad phase GPU, CUDA, pool CUDA, i due export CUDA e overlay OpenGL: non sono PASS.
- ASan/UBSan archiviati confermano stack overflow sia per liste sia per parametri STEP tipizzati profondi.
  Solo `fk_step.cpp` e la sonda erano strumentati; non era una build completa dell'app con sanitizer.

### Verifica di chiusura eseguita in questa sessione

Linux, GCC 16.2.1 e Qt 6.12.0; libreria e runner della build Release locale esistente. Sonde ricompilate dai sorgenti
archiviati. Esiti, ambiente e log conservati nell'[archivio di verifica](history/code-review-2026-11-02-verifica.zip).
Le prove sono eseguite in `/tmp/forgecad-review-audit/`, con impostazioni Qt/XDG isolate per i test grafici.

- **R1:** liste e parametri tipizzati a profondità 100 terminano; entrambi a 100.000 causano segnale 11 in un subprocesso
  senza core dump. Il normale cubo STEP viene importato correttamente.
- **R2/R3:** richieste intercettate rispettivamente di **2.400.000.000** e **800.000.000 byte**, con la guardia prima di
  `malloc` sopra 64 MB. Nessuna grande allocazione eseguita. Curva valida con molteplicità 2 importata correttamente.
- **R4:** 300 cicli checkout/cancel concorrenti, **294 falsi errori su 2.279 letture**; zero errori del writer, lettura
  finale riuscita. La conseguenza GUI resta verificata nel codice, senza una prova integrata con due finestre.
- **R5:** due occorrenze coincidenti o distanti 0,0000004 mm ridotte a una senza note; a 0,000002 mm sono importate entrambe.
- **11 suite autonome PASS:** vincoli lineari, espressioni, compressione, chunk, revision store, recupero catalogo,
  dipendenze feature, riferimenti geometrici, workspace, focus e tema. Quest'ultimo esercita segnali simulati.
- Kernel mirato: **IGES 9/9**, **BodyIO 2/2**, **STEP 5/5** (due guardie cicli, due controlli numerici, assembly).
  Il filtro ampio `Step` è stato interrotto dal limite locale della sonda a 30 s dopo `OcctImportStepRoundTrip`:
  non è conteggiato come PASS né come prova di blocco del prodotto. I singoli cinque casi mirati sono poi passati.
- Fallimenti confermati dei due runner generali sulla palette T1. Storyboard T2 fallisce con directory assente e passa
  dopo averla creata, senza modificare sorgenti. Sformo fallisce isolatamente con lo stesso valore archiviato.
- `python3 tools/generate_architecture.py --check` **PASS**, 524 file e 7.176 relazioni. Nessuna modifica strutturale
  introdotta e nessuna rigenerazione necessaria. `git diff --check` **PASS**.

Nessuna nuova build completa, esecuzione macOS/Metal, Qt 6.4/OCCT 7.6, prova manuale GL o campagna fuzzing estesa.
I risultati precedenti restano distinti da quelli appena eseguiti; le suite verdi non coprono automaticamente
le condizioni dei cinque riproduttori. B8 rimane plausibile e non dimostrato; orfane e semplificazioni sono proposte.

La review non applica fix, rimozioni, refactoring, commit o push. Sono stati aggiornati soltanto rapporto, memoria e
archivio degli esiti. Il lavoro successivo è correggere R1–R5 con regressioni, sistemare T1/T2, isolare lo scarto dello
sformo e validare B8, scenari grafici e piattaforme non coperte. Questo lavoro di implementazione resta aperto.
