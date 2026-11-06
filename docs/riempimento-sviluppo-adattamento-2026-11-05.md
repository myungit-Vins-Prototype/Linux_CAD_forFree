# Riempimento, sviluppo e adattamento alle superfici

## Stato e richiesta

Prima dello sviluppo è stato pubblicato su `origin/main` il checkpoint `e1bde03`,
«Corregge loft, booleane e tagli NURBS e migliora i pannelli», dopo 13 CTest pertinenti PASS.
Le nuove funzioni descritte sotto sono implementate, compilate e verificate localmente.
Il successivo checkpoint locale comprende codice, test e README aggiornati;
queste funzioni non fanno parte di `e1bde03`. Il nuovo commit non viene pubblicato
con push in questo intervento.

Richiesta: ripetizioni di corpi/funzioni per riempire un contorno, con icone per scegliere
il pattern; distendere una superficie su un piano; adattare corpi a una superficie.
Chiarimenti dell'utente: **deformare** i corpi seguendo la superficie; sviluppo
**esclusivamente esatto**, senza approssimazioni di superfici a doppia curvatura.
Non sono stati modificati i CAD originali dell'utente.

## Comandi disponibili

| Menu | Comando e parametri |
| --- | --- |
| Funzioni → Ripetizione | **Riempimento automatico…**: corpo o funzione, contorno da schizzo chiuso o faccia piana, quattro schemi con icone, passi e rotazione. |
| Funzioni → Superfici | **Distendi superficie…**: faccia sorgente, piano destinazione, posizione e rotazione. |
| Funzioni → Superfici | **Deforma corpo su superficie…**: corpi/facce sorgenti, facce destinazione con punto di appoggio, piano sorgente, U/V, posizione, rotazione, distanza normale e conservazione della sorgente. |

Le azioni partecipano alla costruzione del ribbon; una configurazione personale salvata
può richiedere di aggiungerle al proprio layout. I menu rimangono disponibili.
Per usare la nuova build occorre riaprire `forgecad-release/forgecad`.

### Riempimento automatico

Schemi **rettangolare, sfalsato, esagonale e concentrico**, selezionabili con icone
vettoriali. Il passo esagonale fra righe è `passo X * sqrt(3) / 2`; nel concentrico
si impostano passo lungo la circonferenza e passo radiale.

Le copie mantengono l'orientamento della sorgente; la rotazione orienta la disposizione.
Il riempimento considera l'ingombro rettangolare conservativo della sorgente proiettato
sul piano del contorno: può lasciare spazi utilizzabili da sagome non rettangolari, ma
non inserisce intenzionalmente copie che oltrepassano il contorno o attraversano i fori.
Intersezioni e classificazione usano curve/B-rep in double, senza pixel o mesh.
Sono controllati anche fori interamente contenuti nell'ingombro e bordi coincidenti.

Ripetizione di corpi tramite la storia esistente con corpi derivati; ripetizione di feature
tramite il relativo strumento materiale, comprese aggiunte/rimozioni. Le feature booleane
con più strumenti sono esplicitamente rifiutate. Sono previsti massimo 500 copie e
1.000.000 di posizioni candidate, con cancellazione. Variazioni di quota ed esclusioni
numerate delle ripetizioni tradizionali non sono applicate a questa modalità.

### Sviluppo esatto

Il risultato è una nuova faccia B-rep piana; il modello tridimensionale resta conservato.

- Piani: trasformazione affine del riferimento UV.
- Cilindri: coordinate piane `(R*u, v)`, con contorni esatti.
- Coni: sviluppo isometrico del settore, per contorni isoparametrici.
- Cilindri/coni completi con due contorni semplici avvolti: apertura esplicita della
  cucitura in rettangolo/settore. Contorni periodici complessi richiedono prima una
  divisione lungo una generatrice.

Non viene effettuato un nuovo fit dei contorni. Lo sviluppo applica l'isometria alle
curve UV gia' rappresentate dal B-rep e trasporta i budget di SP-curve, bordi e vertici:
non promette una precisione maggiore di quella del taglio sorgente. I budget delle due
curve incidenti si sommano per la connessione sviluppata, prima della verifica; non si
allargano soglie in risposta a un fallimento. Sono rifiutati i tagli conici obliqui non
supportati, gli apici degeneri, le superfici a doppia curvatura e i carrier sviluppabili
generici non riconosciuti. Non è quindi un
riconoscitore universale di superfici sviluppabili NURBS.
Il riconoscimento isoparametrico verifica lo spostamento sull'intero intervallo, entro
l'arrotondamento macchina, evitando di scambiare un bordo lungo e obliquo per una generatrice.

### Deformazione sulla superficie

Per coordinate locali della sorgente `(x,y,z)` la mappa segue
`S(u0 + x/|Su0|, v0 + y/|Sv0|) + (z + offset) * N`, oltre a traslazione e rotazione
nel piano sorgente. Tiene conto dell'orientamento della faccia selezionata.
Conserva la quota lungo la normale; **può introdurre stiramento tangenziale**.
Segue il carrier della faccia, senza ritagliare automaticamente al suo contorno.

Accetta superfici analitiche e NURBS regolari nel dominio utilizzato. Spigoli condivisi
mappati una volta; superfici ricostruite con NURBS bicubiche Hermite adattive e derivate
condivise ai nodi. B-rep e genealogia restano espliciti. Budget predefinito 1e-6 mm,
scarto misurato e verifiche di coerenza fra curve/superfici; nessuna mesh entra nel modello.
Si rifiutano domini non validi, valori non finiti, inversioni/collassi locali individuati
e fit che superano i limiti. Il controllo Jacobiano e quello dello scarto sono campionati:
non costituiscono una certificazione globale né una prova di assenza di auto-intersezioni.

## Integrazione e persistenza

Moduli dedicati `cad_fill_pattern.*`, `cad_surface_mapping.*`,
`kernel/fk_surface_mapping.*`, `forgeCad2026_dialogs_mapping.cpp`.
Riutilizzati `BodyFeature::Pattern` (kind 3–6) e `SurfaceTools` (mode 3–4), senza
rinumerare enum persistite o cambiare il formato principale `.prt` 66.

Gli extra SurfaceTools mantengono lo schema 1 per documenti con sole funzioni precedenti;
lo schema 2 conserva le prime mappature singole; lo schema 3 aggiunge le sorgenti
multiple faccia/corpo, lo schema 4 le destinazioni multiple come unico appoggio.
Lettura degli schemi 1/2/3 mantenuta, con validazione atomica;
le versioni precedenti rifiutano lo schema nuovo, evitando perdite silenziose.
Identità dei corpi, dipendenze, genealogia, anteprime, conferma e Undo/Redo seguono i
percorsi dell'app. Lo sviluppo genera sempre un corpo indipendente; la deformazione
può conservare o consumare la sorgente.

## Verifiche della prima consegna, 5 novembre 2026

- **Compilazione completa Linux**: app `forgecad`, runner Qt, runner kernel e ribbon preview PASS.
- **12/12 CTest PASS**, 11,49 s: ribbon preview/context, lettura documenti, posizionamento/
  risultati/variazioni/pannello/legacy delle ripetizioni, storia completa, SurfaceTools,
  nuovo test applicativo e nuovo test kernel.
- Kernel: area/perimetro cilindro, area piano ruotato e settore conico; rifiuto sfera;
  volume deformato atteso 8,2; target NURBS a doppia curvatura; genealogia, B-rep,
  round trip binario e STEP, tessellazione senza facce fallite; rifiuto inversione.
- Applicazione: quattro schemi, contorni con fori, caso coincidente 9x9/foro 3x3 con
  8 copie; 15 fori ripetuti da una feature e volume atteso; limite copie;
  sorgenti/identità/Undo/Redo; codec e lettura atomica; volume deformato atteso 8,4;
  apertura del pannello, quattro icone e Annulla.
- **Sessione grafica Wayland**: apertura, impostazione, anteprima e conferma dei tre
  comandi, salvataggio di tre documenti sintetici. Picking alimentato attraverso il
  callback standard del viewport; questa prova non simula il clic fisico sulla geometria.
- **Riapertura di tutti e tre i documenti senza cache B-rep/display**: numero di corpi,
  rigenerazione senza errori e validità topologica PASS.
- **Architettura**: 607 file, 8492 relazioni, 293 unità analizzate, nessuna diagnostica;
  controllo aggiornamento PASS, overview e sei viste di dettaglio renderizzate.
  Verificati collegamenti entranti/uscenti e include dei nuovi moduli separatamente;
  nessun nuovo arco `reference` presente in questi moduli (15 nel grafo complessivo).
- `git diff --check` PASS. HTML/JSON architetturali generati e artefatti QA ignorati da Git.

Nuovi test: `kernel/tests/test_surface_mapping.cpp` e `tests/test_surface_mapping.h`.
Riproduzione mirata: `ctest --test-dir forgecad-release --output-on-failure -R surface_mapping`.

Artefatti locali in `forgecad-release/mapping-qa/`: `build-verified.log`,
`ctest-verified.log`, `native-reopen.log`, script/runner di prova, `patterns.png`,
i sei screenshot pannello/viewport, `Riempimento.prt`, `Sviluppo.prt`, `Deformazione.prt`
e immagini della mappa in `architecture-render/`. Non sono fixture CAD dell'utente.

## Prosecuzione conclusa: Taglio 12 e selezioni multiple

Copia congelata di KnifeHandle in `forgecad-release/mapping-multi-qa/input.prt`, SHA256
`c2a09e7820908898bf663b3b5f05eb8cdef1cc49c970b7f13e02ea1fc74262f2`.
Il file contiene 75 feature, incluso il riempimento di 57 corpi. L'utente conferma
che chiede sviluppo esatto di **Taglio 12**, non proiezione ortogonale.
L'hash dell'originale e' ancora identico a fine prove; nessuna scrittura sul CAD originale.

### Diagnosi e risultato sul modello reale

La sonda sul B-rep salvato misura K=0,002932 e 0,00293034 mm^-2 in punti interni
delle facce F0/F1: sono a doppia curvatura e lo sviluppo isometrico piano dell'intero
Taglio 12 non esiste. Il pannello ora identifica entrambe per nome/numero F, con il
valore misurato e il motivo del rifiuto. Per carrier non riconosciuti senza questa
prova di doppia curvatura, il messaggio dichiara solo il limite di supporto del kernel.

Le quattro facce cilindriche F2–F5 erano invece bloccate dal controllo
`pcurveTolerance == 0`: il B-rep del taglio possiede regolarmente i propri budget.
Ora vengono sviluppate esattamente rispetto ai contorni UV rappresentati, senza
un nuovo fit e conservando i budget del sorgente. Il risultato sul **Piano 4,
quota z=-18 mm**, e' composto da quattro isole separate con spazio di 1 mm:
non e' una cucitura continua dell'intero Taglio 12.
Area complessiva prima/dopo circa907,803 mm^2, differenza numerica2,08877e-7 mm^2.

### Selezioni e integrazione

- Sviluppo: lista di facce multiple con numero F, evidenziazione e rimozione;
  clic successivi nella vista/storyboard; secondo clic toglie la faccia;
  Esc o Termina selezione avvia l'anteprima.
- Deformazione: piu' corpi a spunte, Tutti/Nessuno e selezione dalla vista,
  piu' singole facce, anche mescolate ai corpi interi, verso una faccia destinazione.
  Le singole facce diventano lamine; i corpi interi restano solidi. La deformazione
  puo' cambiarne il volume e stirarli tangenzialmente. La successiva correzione descritta sotto estende anche la destinazione a piu' facce
  di un unico appoggio, con ancoraggio sulla prima faccia.
- `SurfaceToolParameters::sources` e famiglia `SurfaceSources` dedicata nel
  visitatore: indice operativo e featureId persistente, dipendenze, riordino e
  genealogia di tutte le sorgenti. Schema3 negli extra; formato principale66 invariato.
- Le sorgenti indicate come sole facce rimangono visibili. Disattivando Conserva
  sorgenti, vengono consumati solo i corpi interi selezionati; selezionare insieme
  un corpo e sue facce e' rifiutato come duplicazione.
- `Body::collect`, nel modulo `kernel/fk_body_collection.cpp`, raccoglie i risultati
  senza fusioni ne' riassociazioni per vicinanza. Conserva regioni solide/lamine,
  geometrie condivise, tolleranze e storia Same. La genealogia attraversa estrazione,
  deformazione e raccolta. Volume/inerzia usano solo le regioni solide; STEP scrive
  solidi e superfici con i rispettivi tipi in una rappresentazione mista.

### Verifiche finali della prosecuzione

- **App completa e runner ricompilati**, binario `forgecad-release/forgecad` aggiornato.
  Per usare le modifiche serve riaprire l'app; non e' stata chiusa alcuna app utente.
- **9/9 CTest PASS**, 3,80 s: ribbon_context, document_read, pattern_result,
  all_history, surface_tools, surface_mapping applicativo, geometry_refs,
  exchange_robustness e surface_mapping kernel.
- Nuove regressioni: due solidi piu' una faccia,13facce risultanti, validita' B-rep,
  volume dei soli solidi, tutti e tre i genitori genealogici con ordine di selezione
  differente, consumo corretto delle sorgenti, schema3 e rigenerazione senza cache,
  Undo/Redo; due isole piane distinte; lista GUI con clic ripetuti e rimozione.
- Kernel: sviluppo con precisione sorgente sui bordi, collezione mista con genealogia,
  codec B-rep e conservazione delle regioni; volume esclude la lamina; round trip STEP
  conserva un solido e una superficie distinti. Restano passate le prove precedenti
  piano/cilindro/cono, rifiuto doppia curvatura e deformazione NURBS.
- **Wayland sul KnifeHandle reale**: selezionate F2–F5 di Taglio12 e Piano4, anteprima
  pronta, Conferma,4facce valide, area, Undo/Redo, salvataggio/lettura e rigenerazione
  senza cache del risultato PASS. Gli altri stadi mantengono le cache del documento.
- **Wayland deformazione multipla**: due corpi e una faccia selezionati, Tutti/Nessuno,
  destinazione cilindrica, anteprima, Conferma,13facce valide e salvataggio schema3 PASS.
  Liste e pulsanti azionati programmaticamente; il picking passa dal callback standard,
  senza dichiarare simulato il clic fisico sulla geometria.
- Pannelli e viewport dei due casi ispezionati. La prima sonda Undo aveva contato
  anche i successivi comandi Mostra/Nascondi; il controllo e' stato spostato subito
  dopo Conferma, per misurare l'Undo della creazione.
- **Mappa608file/8526relazioni/294TU/zero diagnostiche**, controllo aggiornamento PASS;
  overview e7dettagli renderizzati. Verificati call/include/reference nei due versi:
  il nuovo collettore ha chiamanti applicativo e test; `cad_surface_tools.cpp` conserva
  un arco reference, gli altri moduli verificati non ne hanno. HTML/JSON ignorati da Git.
- `git diff --check` PASS. Nessun commit o push aggiuntivo in questa prosecuzione.

Artefatti locali in `forgecad-release/mapping-multi-qa/`: `inspect.log` (diagnosi iniziale),
`build-final.log`, `ctest-final.log`, `native-verified.log`, screenshot `unroll-*` e `wrap-*`,
`KnifeHandle-sviluppo-cilindri.prt` (circa19MB), `Deformazione-multipla.prt` (circa14KB),
sonde/script e viste architetturali. Sono copie QA ignorate da Git.

## Correzione conclusa: destinazioni multiple come unico appoggio

La richiesta precedente era stata implementata solo per le sorgenti: la destinazione
restava singola. L'utente richiede esplicitamente un appoggio comune su piu' facce,
con attraversamento delle giunzioni, senza copie separate per destinazione.

### Pannello e riferimenti

Lista **Superfici destinazione**, con nome del corpo e numero F, aggiunta continua,
secondo clic per rimozione, selezione multipla e Rimuovi. Esc o Termina selezione
conclude la scelta e aggiorna l'anteprima. La prima faccia, marcata «appoggio», conserva
l'ancoraggio U/V quando si aggiungono le successive. Rimuovendola, il riferimento di
appoggio passa alla successiva. La riapertura della funzione mantiene tutte le facce.

Durante la prova con i nomi reali e' emerso un secondo difetto: F0 e F1 condividono
un nome genealogico di origine. Il confronto basato sul solo nome le interpretava
come la medesima selezione e toglieva la prima. Ora, sul medesimo B-rep immutabile
mentre il pannello e' aperto, il confronto usa il numero della faccia; il ripiego
senza numero richiede anche il punto. Il nome persistente resta conservato e la
risoluzione genealogica generale non e' modificata. Regressione dedicata su due
facce distinte con lo stesso nome, oltre alla prova reale su Taglio12.

### Calcolo, limiti e persistenza

Il kernel riusa l'engine di fit B-rep per una mappa comune sulle facce rifilate.
Con piu' destinazioni, le coordinate tangenziali sono proiettate lungo la normale
all'appoggio iniziale definito dalla prima faccia; la quota dalla sorgente viene
mantenuta lungo le normali orientate locali. Le facce fanno parte di un unico
appoggio: nessuna copia della sorgente per destinazione. La mappa UV delle vecchie
funzioni con destinazione singola rimane invariata.

Raggi e classificazione lavorano su superfici e curve esatte; si riusano le gerarchie
delle pezze NURBS. Controlli su copertura, falde sovrapposte, giunzioni non tangenti,
parametri non finiti e inversioni. Budget di fit invariato; nessuna mesh nel calcolo.
Sono supportati appoggi regolari visibili lungo la normale iniziale, non una
parametrizzazione universale di insiemi ripiegati o con vuoti. Le falde orientate
dalla parte opposta sono escluse dal grafico dell'appoggio; se resta un punto senza
appoggio il calcolo fallisce esplicitamente. Le guardie e il fit restano campionati,
senza certificazione globale di assenza di auto-intersezioni.

Extra schema4: `targets[0]` e' la prima faccia, `targets[1]` il piano sorgente,
`targets[2..]` le altre destinazioni. Formato principale66 invariato; schemi1–3 letti.
Le vecchie versioni rifiutano lo schema4. Il visitatore SurfaceTargets esistente
mantiene tutte le dipendenze; la genealogia del materiale conserva le sorgenti.

### Verifiche di questa correzione

- App completa `forgecad-release/forgecad` e runner ricompilati. **6/6 CTest PASS**,
  3,10 s: ribbon_context, document_read, all_history, surface_tools, surface_mapping
  applicativo e surface_mapping kernel. `ctest-selection.log` e' l'esito finale.
- Kernel: un solo solido attraversa due facce piane e due NURBS tangenti; confronto
  con la mappa analitica `z=0,025*x^2` e relativo offset normale; sei facce risultanti,
  B-rep valido, nessuna duplicazione. Un vuoto fra le destinazioni viene rifiutato.
- App: schema4, dipendenze di entrambe le destinazioni, rigenerazione senza cache,
  Undo/Redo, rifiuto atomico di un downgrade a schema3; lista con clic ripetuti,
  rimozione, riapertura e nomi genealogici condivisi.
- Copia aggiornata di KnifeHandle congelata in `mapping-targets-qa/input.prt`, SHA256
  `334a56cda4de051490df467902278f1e889e9e42ca221dfbd1251028d20534f9`.
  Il file originale sul disco e' cambiato durante il lavoro (hash successivo
  `e33330ab07dee0d5ac62b53ad48d91e7d84fc0e79bbf3203d38889ddb32d557d`);
  queste prove si riferiscono alla copia congelata, senza nostre scritture sull'originale.
- **Wayland sul file reale**: aggiunta di un piccolo solido sintetico e selezione di
  F0/F1 di Taglio12, con punti interni e nomi persistenti come nel picking ordinario.
  Appoggio U=0,V=0,5 sulla prima faccia: un solido di6facce attraversa la giunzione,
  checkBody PASS, scarto B-rep misurato **2,07856e-7 mm**. Anteprima e Conferma,
  modifica con entrambe le destinazioni presenti, Rimuovi/Annulla, Undo/Redo,
  salvataggio/lettura e rigenerazione senza cache del risultato PASS.
- Clic di lista e pulsanti azionati programmaticamente; picking alimentato tramite il
  callback standard, senza dichiarare simulato il clic fisico nella geometria.
  Pannello e viewport ispezionati. La prima sonda usava punti sulla giunzione senza
  nomi; quella con nomi ha riprodotto il difetto di deduplicazione. L'esito finale e'
  `native-selection.log`, con nomi reali e punti interni, senza avvisi di ambiguita'.
- Mappa608file/8533relazioni/294TU/zero diagnostiche, controllo aggiornamento PASS;
  panoramica e4dettagli renderizzati; verificati
  call/include/reference nei due versi per kernel, costruttore applicativo, pannello
  e codec. Gli archi reference restano distinti (uno in uscita da cad_surface_tools).
- `git diff --check` PASS. Modifiche locali; nessun commit/push aggiuntivo.

Artefatti in `forgecad-release/mapping-targets-qa/`: input congelato, `probe-bottom.log`,
`build-selection.log`, `ctest-selection.log`, `native-selection.log`, script/sonde,
`panel.png`, `viewport.png`, `KnifeHandle-appoggio-multifaccia.prt` e viste architetturali.
La copia risultante aggiunge una sorgente sintetica per la prova, non modifica la
definizione dei corpi originali del documento congelato.


## Riempimento19 su B15:F6 — correzione e verifica completa

Copia congelata `knifehandle-pattern-wrap-qa/input.prt`, SHA256
`e5ad799d91d086e399872b4c024fb34af63f941e654f033002ce967ec0b742c7`.
La revisione contiene40copie di Riempimento19 (indici18–57), spessore0,1mm,
sul piano Z=-18. B15:F6 indica la faccia6 della feature15, Solido cucito16;
non il corpo logico15. La deformazione salvata riguarda invece una sola copia
sulla faccia11 con pianoYZ; non coincide con il tentativo descritto dall'utente.

Riproduzione: Piano4, U=V=0,5. Senza rotazione il lato lungo del pattern viene
mandato nella direzione corta U e il dominio viene superato. Con rotazione90°
il calcolo raggiunge un difetto indipendente: bordi/superfici erano fittati
ciascuno a0,2*1e-6mm, mentre checkBody richiede1e-7mm per la connessione edge/faccia.
Il fit ora usa0,2*min(tolleranza,kLinearResolution), senza aumentare budget B-rep.
Il maggiore raffinamento ha evidenziato il passo fisso delle derivate del fit:
restava legato alla campata iniziale e attraversava dettagli della destinazione.
Ora segue la cella raffinata, con differenze centrate sul punto per ridurre
la cancellazione numerica. `probe-adaptive.log`: **40/40copie PASS**,7facce ciascuna.
La verifica diretta riguarda geometrie reali. Seguono gli esiti completi nell’app.


### Allineamento e completamento del fit

Il centro UV effettivo della faccia rifilata e' U≈0,733987,V=0,5, non U=0,5:
quest'ultimo punto usato nella prima sonda appartiene al carrier e non alla zona
centrale del trim selezionato. La prova grafica finale usa il centro del trim.
Qui il vecchio limite18 interrompeva alcuni fit ancora convergenti (residuo
2,16074e-8mm contro budget2e-8mm). Il limite ora e'24, con budget di celle/poli e
precisione invariati. Nessuna deroga a checkBody o alle tolleranze dei bordi.

Nel pannello **Copie** seleziona solo i risultati della ripetizione evidenziata.
**Centra e allinea dalla sorgente** risale alla funzione sorgente, recupera il
piano dello schizzo quando disponibile, centra l'ingombro delle curve esatte e
sceglie la direzione parametrica piu' vicina all'asseX sorgente. Su questo modello:
Piano4, rotazione90°, X≈0,199322mm,Y≈-1,81702mm. Coordinate/rotazione restano
modificabili; il comando non altera automaticamente le definizioni precedenti.
La prima versione proiettava l'asse sul piano tangente obliquo, mescolando U/V:
la prova reale l'ha rifiutata; e' stata corretta prima della consegna.
Non cambiano schema o formato: i valori espliciti usano i parametri gia' persistiti.

### Risultati sulla prima revisione (40copie)

- App completa e runner ricompilati (`build-app.log`, `build-final.log`,
  `build-refinement.log`). **8/8CTest PASS**,7,80s, `ctest-final.log`:
  ribbon_context, document_read, all_history, surface_tools, surface_mapping;
  kernel tolerance, surface_tools, surface_mapping.
- Regressioni: carrierNURBS con asse lungoV e dettagli locali, piu' copie e
  controllo di non inflazione dei budget edge; selezione di tutte e sole le copie
  nel pannello e centratura. Conservati test cilindro/NURBS, appoggi multipli,
  collezioni solide/lamine, genealogia e scambio/persistenza.
- **Wayland sul file reale**, `native-refinement.log`:40sorgenti di Riempimento19
  su B15:F6, **40regioni solide e280facce**, checkBody geometrico/topologico PASS.
  Scarto B-rep misurato **2,66225e-8mm**, volume40,5629mm³. Anteprima, Conferma,
  modifica/Annulla, Undo/Redo, salvataggio/lettura e rigenerazione del risultato
  senza cache PASS. La selezione usa pulsanti/lista reali e callback standard con
  riferimento genealogico e punto interno, senza simulazione del clic fisico.
- `viewport.png` ispezionata: disposizione sul fianco inferiore sagomato; rimane
  il numero e la forma delle40copie del documento, non il numero dei tasselli della
  fotografia di riferimento. La mappa UV puo' stirare tangenzialmente; non e'
  sviluppo isometrico e non si certifica l'assenza globale di autointersezioni.
- Copia pronta `knifehandle-pattern-wrap-qa/KnifeHandle-Riempimento19-F6.prt`,
  **36.301.325byte**: manico e risultato visibili, sorgenti nascoste e conservate;
  rilettura con B-rep identico prima/dopo la sola modifica di visibilita' (`delivery.log`).
- Mappa **608file/8546relazioni/294TU/zero diagnostiche**, `--check` PASS;
  panoramica e4dettagli renderizzati/ispezionati. Verificati call/include/reference
  nei due versi per kernel, pannello e modello storia (`architecture-links.json`).
  `git diff --check` PASS. Correzioni locali, nessun nuovo commit/push.


### Ultimo salvataggio utente:96copie, verifica finale

Il file originale e' cambiato durante il lavoro:59→115feature e40→96copie.
Seconda copia congelata `input-current.prt`, SHA256
`441eace4da5d476d17989b1f883132f5495d053d6e582d11c8acb8641dbeee96`.
Il confronto binario dei B-rep conferma la destinazione15 invariata e i risultati
del riempimento modificati; la prima sonda non veniva quindi attribuita al nuovo
salvataggio. Nessuna nostra scrittura sul CAD originale.

Ripetuta la prova completa Wayland (`native-current.cpp`, `native-current.log`):
**96solidi/672facce**, stessa B15:F6, Piano4, U≈0,733987,V=0,5, rotazione90°,
X≈0,223794mm,Y≈0,135235mm. Scarto misurato **4,06682e-8mm**, volume54,631mm³.
CheckBody, Copie/Allinea, anteprima/Conferma/modifica, Undo/Redo, codec e
rigenerazione del risultato senza cache **PASS**. Nessun ulteriore cambiamento
ai sorgenti necessario; valgono8/8CTest e mappa finali indicati sopra.
`viewport-current.png` ispezionata: le96copie seguono il fianco e le ondulazioni.

**Copia di consegna aggiornata**:
`knifehandle-pattern-wrap-qa/KnifeHandle-Riempimento19-F6-aggiornato.prt`,
**67.478.073byte**, salvata dopo aver reso visibili manico e risultato e nascosto
le sorgenti; tutte le definizioni restano nel documento. La precedente copia a
40rimane solo come evidenza della prima prova. Non si attribuiscono al file
aggiornato il conteggio40 o il confronto binario di sola visibilita' della prima
consegna: il file96 e' verificato separatamente con rilettura e rigenerazione.


## Checkpoint locale e README bilingui

Richiesta successiva: aggiornare prima i README inglese e italiano con tutte le
nuove funzionalita', quindi creare il commit. Aggiornati `README.md` (inglese
principale), `README.it.md` e l'indice `README.en.md`, mantenendo una sola guida
inglese completa. Copertura allineata: snap Intersezione, riferimenti automatici e
costruzione, schizzi vuoti, curve proiettate, loft con punti/piani/corrispondenze,
condizioni trasversali e unione delle pezze, filettature ISO, controlli ribbon,
variazioni delle ripetizioni, riempimento, sviluppo esatto e deformazione multifaccia.
Documentati comandi, workflow Copie/Allinea, formato66 e limiti geometrici effettivi.

Verifiche del checkpoint: **128collegamenti locali e frammenti Markdown validi**,
blocchi di codice bilanciati, **15/15CTest PASS** in9,95s su Linux, comprendendo
ribbon, lettura, storia, SurfaceTools/mappature, ripetizioni, riferimenti e
precisione del kernel. Log `/tmp/forgecad-readme-checkpoint-tests.log`.
`git diff --check` e mappa608/8546 aggiornata PASS. L'app era gia' compilata nella
precedente verifica completa; in questo passaggio sono cambiati soltanto i README
e i registri. Il checkpoint include i moduli applicativi/kernel, test e
rapporti della funzionalita'; CAD originali, copie QA, binari e HTML/JSON generati
restano esclusi. Nessun push richiesto per questo checkpoint.

## 05/11 — Posa rigida e appoggio F5/F6

L'utente ha chiesto un'alternativa alla deformazione: conservare ciascun solido
sorgente e orientarlo secondo la **normale locale** della destinazione. Il
selettore **Metodo → Posiziona senza deformare** e' nello stesso pannello.
Ogni sorgente e' ruotata/traslata separatamente; il centro dell'ingombro esatto
nel piano sorgente, a quota zero, diventa il suo punto di appoggio. X segue la
disposizione nel piano tangente, Z la normale orientata; la distanza normale
resta regolabile. La base rimane rigida: non viene piegata per farla aderire.
Il posizionamento usa trasformazioni B-rep esatte, conserva carrier analitici,
volume, distanze, nomi/genealogia e regioni separate. Nessun nuovo fit.

Il nuovo booleano `mappingRigid` e' salvato nelle estensioni superfici schema5,
solo quando attivo; gli schemi1–4 continuano a essere letti come deformazione.
Un downgrade dello schema viene rifiutato atomicamente. Restano invariati i
valori persistiti delle modalita' e il formato nativo66.

La centratura precedente considerava soltanto la prima faccia. Per F5/F6 di
Solido cucito16 la sua normale laterale lasciava la seconda falda fuori dal
grafico di proiezione. **Centra e allinea dalla sorgente** ora usa E20, giunzione
topologica realmente condivisa, e l'ingombro proiettato dell'insieme delle facce.
Sul file congelato gia' usato nelle prove precedenti (SHA256
`441eace4da5d476d17989b1f883132f5495d053d6e582d11c8acb8641dbeee96`)
calcola appoggio U=0, V=0,521818, Piano4, rotazione90 gradi. Non cambia
silenziosamente i parametri gia' salvati: il comando va premuto dopo la scelta
delle destinazioni. Per facce senza una giunzione topologica condivisa con la
prima conserva la centratura sulla prima faccia.

Il pannello evidenzia i bordi di **tutti i corpi spuntati** e il piano scelto,
anche quando le sorgenti non sono visibili normalmente. I marcatori sono
transitori e si rimuovono uscendo dal pannello. Il riferimento visuale dei
bordi e' kind9; non viene confuso con kind11, usato per le sorgenti intere nel
contesto della mappatura ma riservato ai punti medi nei riferimenti di misura.

Prove effettive:

- kernel: volume e distanze rigide, carrier piani invariati, normale locale su
  NURBS multifaccia, rifiuto sovrapposizioni; regressioni dello sviluppo e
  deformazione preesistenti. Classificazione UV interno/esterno/bordo.
- applicazione: schema5/compatibilita'/downgrade atomico, rigenerazione senza
  cache, Undo/Redo, riapertura Metodo e due destinazioni, marcatori corpi/piano.
- **15/15 CTest PASS**,11,71s; compilati app e runner Linux.
- **Wayland KnifeHandle:96 solidi/672 facce su F5+F6 PASS**, volume51,3571mm3,
  distanze di ogni coppia di vertici di ciascuna sorgente conservate entro1e-9mm,
  volume totale entro1e-8mm3, checkBody, anteprima/Conferma/modifica,
  salvataggio/lettura/rigenerazione dell'ultima funzione senza cache, Undo/Redo.
  Marcatori verificati anche nel viewport. Evidenze nella cartella ignorata
  `forgecad-release/knifehandle-wrap-twofaces-qa/`: `native-rigid-final.log`,
  `panel-current-initial.png`, `viewport-highlights.png`, `viewport-current.png`,
  `KnifeHandle-Riempimento19-F5-F6-rigido.prt`. Originale non modificato.

La deformazione continua delle96copie sulle due facce dense rimane una prova
separata dalla posa rigida: sono in corso profilazione e verifiche delle
intersezioni ripetute. Le prove rigide non certificano quel percorso.

### File aggiornato:155 copie e correzione dell'anteprima

Il file salvato durante le verifiche contiene173funzioni/155copie, non le115
funzioni/96copie della prima fotografia congelata. Copia di prova
`input-7f89e509.prt`,19401127byte, SHA256
`7f89e50938bb8639f47b01e450069b399874e0ac499490c69249f79fa396798b`.

Sul nuovo file il solo centraggio dei bounding box lasciava il centro della
copia29 fuori dall'appoggio. Il comando ora verifica gli input e cerca una
piccola traslazione entro mezzo passo minimo della ripetizione, scegliendo la
prima valida in ordine di distanza. Non scala i corpi e non cambia le distanze
fra le copie. I valori corretti restano visibili: nel caso verificato
X=0,473794mm,Y=0,510235mm, U=0,V=0,521818,angolo90gradi,Piano4.
La ricerca finita non prova l'impossibilita' quando non trova una traslazione.
Per facce singole vengono usati soltanto i vertici delle facce selezionate.

C'era anche una causa indipendente: la chiave di anteprima ometteva sorgenti,
U/V,X/Y,angolo,offset,conservazione sorgenti e metodo. Il viewport poteva
riusare un errore o un B-rep precedente, e la modifica poteva apparire invariata.
La chiave ora contiene tutti questi dati e i nomi genealogici dei riferimenti.
Test reale errore→cambio posizione→anteprima valida→nuovo spostamento geometrico,
oltre alle regressioni su persistenza, Undo/Redo e riapertura dei pannelli.

**Posa rigida del file aggiornato: PASS Wayland**,155solidi/1085facce,
volume82,9202mm3 circa. Controllate tutte le distanze fra coppie di vertici
all'interno di ciascuna copia (1e-9mm), volume complessivo (1e-8mm3),
appartenenza di ciascun centro a F5/F6 e corrispondenza di ciascun asse Z con la
normale orientata locale (1e-8). Conferma, modifica, Undo/Redo, codec e
rigenerazione dell'ultima funzione senza cache PASS. Evidenze:
`native155-final.log`, `KnifeHandle-155-F5-F6-rigido.prt`, screenshot nella stessa
cartella QA. Il CAD originale e' rimasto invariato.

Il percorso del fit distingue ora il materiale sorgente dalla finestra
rettangolare del suo carrier: triangoli e poligoni possono avere angoli del
rettangolo che non appartengono alla faccia. Le guardie sul Jacobiano si
applicano al materiale; il fit puo' usare una continuazione matematica fuori
dal trim soltanto per quella parte inutilizzata del carrier. Vertici e curve
di bordo reali restano controllati sull'appoggio rifilato. Un test triangolare
copre la correzione; il negativo con vuoto fra le destinazioni continua a
fallire. Il limite200000 e' applicato alla cache dei contatti, che viene
svuotata, senza dichiarare una geometria invalida per aver superato un numero
di accessi; restano i limiti di fit e la cancellazione.

Il classificatore UV evita una nuova proiezione globale di parametri gia' noti.
Le intersezioni retta/NURBS usano il raggio nel BVH e un percorso rapido sulle
pezze polinomiali soltanto quando i poli delle derivate limitano la norma di
I-AJ: unicita' della radice e/o esclusione intervallare. Negli altri casi resta
la suddivisione completa; test con superficie ripiegata a due radici e
sovrapposizioni conservano i rifiuti precedenti. Nessuna tolleranza aumentata.

**Limite ancora aperto:** la deformazione completa delle155copie non e'
risolta. Con la mappa normale attuale il primo solido fallisce al punto del
piano sorgente X=-25,8149464709,Y=-9,70279121999,Z=0,1mm. Ora il messaggio
riporta anche queste coordinate. L'esito rigido non certifica la deformazione.
Prototipi di trasporto UV/arco e appoggio locale sono rimasti nella cartella
QA ignorata; le prove di deformazione non hanno prodotto una convalida entro
300s e non sono state integrate. Non e' stata estesa silenziosamente la
superficie oltre il trim per far accettare punti del materiale reale.

Validazione finale della build consegnata: **18/18 CTest PASS**,11,00s, con
rilink dei binari kernel interessati. Inclusi anche parametrizzazione superfici,
loft con guide proiettate e booleane sulle cuciture. App e runner ricompilati.
`native155-delivery.log` conferma nuovamente la posa rigida, tutte le normali,
Undo/Redo e rigenerazione dopo la pulizia degli esperimenti numerici; il file
risultante e'19686894byte. Mappa609file/8565relazioni/294TU/0diagnostiche,
`--check` PASS; verificati call/include nei due versi e overview+7dettagli HTML.
Tolleranze di query e passo originario delle derivate conservati nella build
finale; le variazioni provate durante l'indagine non sono state mantenute.
