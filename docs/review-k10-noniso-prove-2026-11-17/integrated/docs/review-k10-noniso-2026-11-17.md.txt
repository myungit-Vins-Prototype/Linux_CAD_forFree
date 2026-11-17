# K10: residuo continuo delle fin Newton non ISO — 16–17 novembre 2026

Prosecuzione locale della review con gli stessi tre agenti GPT-6.1 Sol: provider, test e audit indipendente. **Nessun commit o push.** HEAD e origin/main restano `47c7c19`; le precedenti rimozioni API locali sono preservate. **33 bug funzionali corretti invariati:** questo delta estende la copertura di K10, non aggiunge un nuovo difetto indipendente al conteggio.

## Cambiamento implementato

Dopo la costruzione Newton, `extendSheet` verifica continuamente il residuo posizionale delle fin effettivamente emesse:

`sup(t nel range edge) |E(t) − S(Q(t))| ≤ cap`.

Il cap è il valore di `tolerance` fissato prima del fit (`max(1e-7,1e-9*scale)`), conservato come `joinTolerance`. Non deriva dalla deviazione misurata del fit, dalla somma delle tolleranze o da un ampliamento successivo. Non cambia alcuna tolleranza globale.

- `fk_surface_curve_bounds.h/.cpp`: provider mirato, con tre esiti distinti `Certified`, `Failed`, `Unsupported`. Famiglia: bordo BSpline cubico nativo, pcurve Line2 nativa oppure BSpline2 grado1/3, superficie BSpline bicubica nativa nonperiodica. Pesi originali esattamente costanti e positivi restano polynomiali. Sottorange originali ammessi; istanze, wrapper, pesi variabili e altri gradi restano fuori perimetro.
- Coefficienti originali IEEE, estrazione polar de Boor e aritmetica intervallare outward, partizione comune dei nodi E/Q, immagine UV dell'intero intervallo, composizione Bernstein di **tutte** le celle nodali intersecate, incluse quelle adiacenti. Nessuna cache Bézier arrotondata o campione è assunto come prova. Il residuo viene ricontrollato anche sul risultato dell'unificazione.
- `fk_sheet_join_validation.h/.cpp`: ledger common/extra con indici delle facce realmente riscritte, curva/range e cap originali. `BlendModel::build` restituisce la mappa esplicita degli edge compattati. Si richiede un'incidenza univoca; fin mancanti, metadati cambiati e prove riconosciute fallite sono terminali. Per fin certificate, `pcurveTolerance` diventa il cap prefissato, non la deviazione campionata. La tolleranza dell'edge non viene aumentata.
- Dopo `unifySameDomain`, il ledger segue i discendenti dichiarati dall'operazione, anche quando il chiamante non chiede history. Controlla identità curva/pcurve, range, sense, cap e residuo sulla superficie finale. Non usa prossimità geometrica.

Rispetto al piano precedente, gli helper outward sono privati nel nuovo modulo: non è stato rifattorizzato trasversalmente il provider esistente. Questo limita il delta; le nuove operazioni sono state sottoposte all'audit matematico separato.

## Verifiche eseguite sul delta finale

| Verifica | Esito e perimetro |
| --- | --- |
| Build completa | PASS, app e runner ricompilati; circa325s. Warnings in file non modificati (parametri offset e inizializzatori test viewport), nessun errore di compilazione. |
| CTest pertinenti | 22/22 PASS, zero skip; nuovi contratti, giunti ISO/nonISO, fit, offset, raccordi, history ed estensione UI. JUnit e nomi esatti nel pacchetto. |
| Kernel generale | 483/483 PASS sul runner ricompilato, incluso `OffsetLoftExtendedSeamsTrimBoth` originale. |
| Driver mirato | 55/55 PASS:22 nuovi casi provider/ledger e33 regressioni giunti immutate, comprese4 varianti pubbliche nonISO scala/posa. |
| ASan/UBSan/LSan mirati | 55/55 PASS; tre TU modificati (provider, ledger, sheet) e test/main strumentati; resto kernel Release. Calibrazione LSan rileva19byte intenzionalmente persi, exit23 fuori ptrace. Nessuna certificazione di assenza di leak dell'intera app. |
| Operazione pubblica e genealogia | Due fin finali, entrambe `Certified`, capedge=capfin=1e-7mm. Seconda chiamata senza history utente: output byte-identico. |
| Verifica indipendente esatta | Fraction sugli originali IEEE delle due fin appena esportate:120 campate comuni,128 rami carrier, massimo1,543930915e-8mm < cap1e-7mm. Runtime upper conservativo massimo2,446602079e-8mm. Preflight clamped esplicito per questo verificatore offline. |
| Identità dell'estrazione | 462 uguaglianze esatte di coefficienti Cox/monomi/Bernstein contro polar de Boor, inclusi unclamped e intervalli1ULP; supporta l'audit, non è prova esaustiva di tutti gli input IEEE. |
| Avvio app Wayland | PASS5s, configurazione temporanea, eseguibile aggiornato verificato, nessun documento aperto o processo utente toccato. Non è certificazione dei pixel/GL. |
| Mappa architetturale | 784 nodi /11.437 relazioni /394 TU, zero diagnostiche AST; hash, configurazione, HTML e archi call/include/reference nei due versi verificati. |
| Conservazione | Baseline5677 file incluse modifiche non tracciate; nove sorgenti API locali, regressioni originali e23 pacchetti precedenti preservati. Index vuoto, nessun commit/push. |

I nuovi test coprono positivi obliqui, linee native con direzione effettiva normalizzata, pesi costanti, nodi comuni e celle attraversate, sottorange1ULP e curve unclamped; negativi per picchi del bordo/carrier fra campioni, escursioni UV fuori dominio, budget/input invalidi, limite risorse, riferimenti mancanti/duplicati e modifiche post-unify. La prima bozza di un test positivo assumeva una direzione Line non normalizzata: è stata corretta la fixture, insieme ai negativi che avrebbero avuto un gap non pertinente. Nessun allargamento del cap o indebolimento dei test esistenti.

## Limiti ancora aperti

**K10 resta parziale.** Il controllo certifica la posizione finale di questa famiglia di fin, non convergenza o selezione del ramo Newton, appartenenza globale ai trim, topologia, autointersezioni o preservazione globale dei carrier. Fit Newton e inviluppo UV a65 punti restano algoritmi di costruzione campionati.

`Unsupported` conserva il percorso legacy senza certificato; una prova riconosciuta `Failed` non viene recuperata da un controllo campionato. Unify che ricostruisca una pcurve, cambi parametro o fonda range senza una mappa supportata può essere rifiutata conservativamente. I limiti fissi (100.000 poli E/Q, maxScalars2.000.000,16.384 visite,256 celle per nodo,profondità24,grado composto18) possono rifiutare geometrie valide; cancellazione propagata, nessuna deadline temporale rigida.

Non sono stati riverificati macOS/Metal o la sessione completa Wayland schede ancora aperta nel rapporto precedente. La CI verde precedente riguarda sorgenti precedenti: queste modifiche locali non hanno una nuova CI, perché commit e push sono rinviati. Restano anche i limiti geometrici già noti di Superfici.prt scalato/ruotato e della chiamata kernel inversa Flacone3.

[Prove, sorgenti inerti, ricette e manifest](review-k10-noniso-prove-2026-11-17/README.txt).
