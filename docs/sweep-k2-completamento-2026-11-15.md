# K2 — controllo globale degli sweep, 15 novembre 2026

**K2 completato e integrato nel kernel di produzione.** Build completa di app e runner, regressioni e mappa architetturale verificate. Il prototipo precedente è superato da questa implementazione.

Su richiesta dell'utente, K2 ha precedenza sugli altri approfondimenti della review. Due agenti GPT-6.1 Sol hanno seguito rispettivamente l'atlante laterale e le teste/il ponte analitico, con revisione indipendente e integrazione del coordinatore. Base locale HEAD `ce03a6d5941840886416aafe055763fb98209e94`, comprese le correzioni precedenti non ancora committate.

## Difetto e comportamento

Lo sweep accettava spire o regioni sovrapposte come un solido valido: il riproduttore iniziale restituiva volume 19,7417 pur essendo contenuto in un box di volume 12,6. Il checker topologico e il controllo locale delle pieghe non bastavano a escludere queste intersezioni.

Prima di restituire il corpo, lo sweep ora controlla le superfici laterali effettive, le teste ritagliate con i loro fori e il nesting delle regioni. Se non riesce a dimostrare la separazione o la compatibilità delle cuciture entro le tolleranze dichiarate, restituisce `std::domain_error`; un budget esaurito non equivale mai a geometria valida. Gli alias non vengono pubblicati in caso di fallimento e la cancellazione resta propagata.

## Moduli integrati

- `fk_sweep_surface_validation`: estrazione Bernstein razionale con arrotondamento verso l'esterno, gerarchia di box, separazione e iniettività su rettangoli dell'atlante. Comprende tutte le celle intermedie, controlla anche intersezioni sulla stessa faccia e chiusure periodiche. Le cuciture sono dichiarate dalla genealogia e hanno un limite fisico esplicito sul contatto ammesso.
- `fk_sweep_caps`: classificazione conservativa dei trim piani, fori e isole; controlli testa/testa e testa/lato. Cerchi ed ellissi usano coefficienti intervallari ricavati da Taylor con resto esplicito. Le query sui lati riusano un'estrazione preparata una volta per il carrier.
- `fk_sweep_analytic_bridge`: collega l'atlante razionale al carrier analitico realmente scelto, includendo errori sui poli e sui pesi. Le promozioni a cilindro/toro restano quando supportate; altrimenti la faccia usa direttamente il carrier razionale validato.
- `fk_sweep_atlas`: costruzione dei chart di traslazione/rotazione, collegamento fra teste e famiglie e nesting del materiale.
- `fk_sweep.cpp`: integrazione dei controlli e SP-curve longitudinali coerenti con la parametrizzazione dei carrier razionali. Rimossa la promozione approssimata a piano dei carrier generali, che avrebbe separato la geometria verificata da quella restituita.

I validatori intervallari sono compilati senza fast-math e senza contrazione FMA implicita. Non si usano mesh, campioni di display o OpenCASCADE per decidere la validità. CMake aggiunge `forgekernel_sweep_validation_tests`, indipendente da Qt e OCCT, con 30 regressioni.

## Geometrie valide preservate e prestazioni

Il prototipo precedente riempiva troppo spazio fra i profili e rifiutava una lamina curva regolare e due tubi intercalati disgiunti. La verifica delle superfici effettive risolve entrambi i casi. Restano validi un'isola dentro il foro di un'altra regione, il toro anulare in quattro rappresentazioni, le eliche da 15/16/17 giri e le 18 combinazioni K4 di scala, posa e rappresentazione; i veri piegamenti continuano a essere rifiutati.

Il raffinamento iniziale consumava quasi tutte le suddivisioni lungo il profilo per le pezze molto allungate, lasciando poco raffinato il percorso. L'euristica finale alterna gli assi quando le norme delle derivate hanno rapporto fuori da [1/4,4]; negli altri casi divide la direzione fisicamente più lunga. Cambia l'ordine della ricerca, non la condizione di accettazione, la tolleranza o il budget. Nel gate mirato di staging, due varianti K4 sono passate da circa 22,5 a 1,3 secondi; la NURBS del toro divisa in due tratti, prima non conclusa entro 500.000 visite, passa in circa 18,8 secondi. Tempi diagnostici della macchina locale, non garanzie generali.

## Verifiche finali

- **Build completa PASS**, app `forgecad`, runner e test dell'interfaccia. La prima build ha segnalato due helper mancanti nel nuovo test autonomo: corretti con costruttori locali, mantenendo le stesse geometrie e nessuna dipendenza OCCT. Il rilancio completo è PASS. Restano warning in codice preesistente non modificato da K2.
- **54 test kernel distinti PASS**: 30 nel nuovo target (117,39s), 19 nel filtro Sweep storico (171,75s), più BodyIORoundTrip, BooleanParallelMatchesSequential, OffsetFacesSpring, BlendSweptTubeCap e StepWriteReadByOcct. Il controllo STEP copre tre schemi, forma e volume letti da OCCT.
- **8/8 CTest PASS**: pannelli delle funzioni, anteprime/storia, riferimenti sweep, strumenti superfici, unità, storia completa, lettura documenti e workspace. Ambiente Qt offscreen/Fusion con cartelle XDG temporanee; non è una prova del renderer OpenGL.
- **22/22 test ASan/UBSan/LeakSanitizer PASS**, esecuzione nativa con rilevamento leak abilitato. Strumentati i nuovi moduli, l'integrazione sweep e i test; la libreria restante del kernel è quella normale, quindi questo esito non certifica tutta l'app.
- **Audit indipendente degli agenti PASS**: 14 prove dirette core, 7 avversariali, 58.050 inclusioni campionate delle derivate propagate e ulteriori 3.168 su 48 reti. Copie/cache, cancellazione e fallimenti conservativi verificati.
- **Architettura PASS**: 736 file, 10.570 relazioni, 361 unità C++, zero unità con diagnostica; `--check`, audit mirato dei moduli, chiamanti/dipendenze nei due versi e dati incorporati nell'HTML coerenti con JSON.
- Hash dei sorgenti iniziali/finali e dei binari conservati. Nessuna variazione inattesa nei file versionati al confronto; modifiche precedenti preservate. `git diff --check` PASS.

[Log, script e audit](sweep-k2-validazione-2026-11-15/README.md). I tempi riportati sono misure durante verifiche concorrenti sulla macchina locale.

## Limiti espliciti

La risposta `Certified` vale per la geometria dei carrier e per la tolleranza fisica dichiarata: alle cuciture tolleranti non significa uguaglianza IEEE esatta di superfici costruite separatamente. Le prove con campioni delle derivate verificano la regressione dell'algebra; non sono il criterio di accettazione della geometria.

I limiti predefiniti sono 500.000 visite, 200.000 celle, 2.000.000 coefficienti, 128 celle per verifica locale e profondità 24. Sono budget algoritmici, non un limite assoluto di memoria del processo o di tempo reale. Estrazione, indici e preparazione consumano lavoro e memoria; sono cancellabili. Geometrie valide molto difficili o non supportate possono ancora restituire un errore esplicito di controllo non concluso.

Il fallback razionale può modificare il tipo del supporto restituito per alcuni moti, pur conservando la geometria entro i contratti verificati; per questo sono incluse regressioni su SP-curve, massa, persistenza e consumatori dello sweep. Nessun nuovo test visuale OpenGL, nessuna ricertificazione macOS/Metal, Windows o CUDA. Nessun CAD originale modificato e nessun processo utente chiuso.

La chiusura di K2 porta a 19/19 i difetti confermati della review iniziale corretti; non certifica l'assenza di altri difetti nell'intero repository. Le 22 funzioni senza chiamanti restano candidati manutentivi separati.
