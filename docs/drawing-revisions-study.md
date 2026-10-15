# Tavole 2D con sezioni associative e gestione delle revisioni

Studio del 15 ottobre 2026. I tre vincoli sono: quotare le sezioni anche di superfici aperte, valutare dove conservare modello e tavole, gestire check-out e tutte le copie di check-in. L'utente ha scelto file tavola separati dal modello: .drw leggeri che riferiscono il .prt, senza copiarne la geometria. I moduli geometrici, il backend locale delle revisioni, i comandi GUI delle revisioni del modello e il primo editor delle tavole sono descritti nelle ultime sezioni. Le parti non ancora implementate restano indicate come proposte.

Per il confronto dei file si assume che «stesso file del 2D» significhi «stesso file del modello 3D»; il chiarimento e' aperto. La gestione concorrente viene considerata per non precludere l'uso futuro su cartella condivisa; il numero di utenti resta da confermare.

## Quote sulle curve di sezione

Ogni vista deve avere un sistema di riferimento geometrico nel modello: origine, assi del piano e normale. La sola direzione di osservazione non determina il piano di sezione: serve anche la sua posizione. La posizione della vista sul foglio e la scala di stampa sono dati separati.

La geometria quotabile comprende sia gli spigoli proiettati sia le curve ottenute intersecando il piano della vista con le facce limitate del modello. La sezione puo' attraversare l'interno di una superficie aperta senza incontrare alcuno spigolo preesistente. Non deve essere necessario ispessire o chiudere la superficie. Una vista ordinaria deve poter esporre queste curve come riferimenti di quotatura, anche quando non visualizza un taglio con tratteggio.

Il calcolo deve rispettare i contorni e i fori delle facce. Intersecare il supporto matematico infinito della superficie, senza limitarlo alla faccia, produrrebbe quote su geometria inesistente. Le curve analitiche e NURBS sono la base delle misure; mesh e pixel servono a visualizzazione e selezione. Scarti di intersezione e approssimazione devono seguire la [politica delle tolleranze](tolerance-policy.md).

Una quota su una sezione deve riferirsi almeno a:

- identita' del documento modello e stato di origine, feature/corpo e nomi topologici delle facce sorgenti;
- identita' della vista e definizione del piano, con eventuale legame a un datum del modello;
- ramo della sezione e ancoraggio geometrico, per esempio estremo, centro, intersezione o punto parametrico;
- tipo di misura e riferimenti coinvolti, separati dalla posizione del testo e dallo stile sul foglio.

L'indice della curva appena generata e' insufficiente: l'ordine puo' cambiare a ogni ricalcolo. Anche una coordinata o un parametro numerico da soli non identificano stabilmente un ramo. La risoluzione deve combinare genealogia delle facce, provenienza della sezione, continuita' del ramo e ancoraggio. In caso di divisione, fusione o candidati equivalenti la quota va segnalata come ambigua o da riassociare, senza accettare silenziosamente la curva piu' vicina.

La misura geometrica resta nelle unita' del modello. Scala della vista, zoom e arrotondamento del testo non devono modificarla. Per geometrie esterne al piano, quote proiettate e misure spaziali richiedono tipi distinti. Una sezione ellittica di un cilindro, per esempio, non va trattata automaticamente come un cerchio di cui quotare il diametro.

Le tangenze isolate possono produrre punti; facce coincidenti con il piano producono regioni di cui eventualmente utilizzare i bordi, non una curva d'intersezione arbitraria. Vanno inoltre distinti rami multipli, curve chiuse senza un estremo privilegiato e duplicati sulle giunzioni fra facce. La visibilita' della curva sul foglio non deve cancellarne l'associazione alla quota.

### Componenti riutilizzabili

[`cad_sketch_refs.h`](../cad_sketch_refs.h) e [`cad_sketch_refs.cpp`](../cad_sketch_refs.cpp) contengono proiezioni di curve esatte e sezioni di solidi/superfici nel piano dello schizzo. Nel percorso delle lamine viene gia' chiamato `bodySectionCurves` fra piano e superficie.

Prima del prototipo, [`kernel/fk_boolean.h`](../kernel/fk_boolean.h) restituiva `BodySectionCurve` con sola curva e intervallo. Il nuovo percorso `bodySectionCurvesWithOrigins` aggiunge le facce sorgenti e conserva i tratti per coppia di facce, senza dipendere da `SketchObject`.

[`kernel/fk_intersect.h`](../kernel/fk_intersect.h) distingue gia' intersezioni piano/superficie, coincidenze e tangenze. [`cad_topology_ref.h`](../cad_topology_ref.h) offre risoluzione dei nomi e diagnostica dei riferimenti mancanti/ambigui. Questi elementi non costituiscono ancora un sistema di quote associative sulle sezioni.

## Modello e tavole nello stesso file oppure separati

| Soluzione | Vantaggi | Costi per revisioni e collaborazione |
| --- | --- | --- |
| Un file con modello e tavole | Trasporto semplice; un solo salvataggio puo' conservare uno stato coerente | Blocco e revisione dell'intero documento; una modifica al cartiglio tocca anche il contenitore del modello; collaborazione indipendente piu' difficile |
| Documenti separati con riferimenti espliciti | Revisioni e check-out indipendenti; piu' tavole possono usare lo stesso modello | Servono risoluzione delle dipendenze e pubblicazione coerente; copiare solo la tavola puo' perdere il modello |
| Documenti separati piu' pacchetto di consegna | Indipendenza durante il lavoro e consegna di una combinazione precisa | Occorre un manifesto del pacchetto e una funzione di raccolta delle dipendenze |

Scelta confermata dall'utente: file operativi separati, modello .prt e tavole .drw. Piu' tavole possono riferire lo stesso modello senza ripeterne B-rep o mesh. Il primo editor gestisce un foglio per file, con piu' sezioni; piu' fogli nello stesso disegno e un pacchetto unico per consegna/archiviazione restano sviluppi successivi.

Ogni documento deve avere un'identita' stabile indipendente dal percorso. La tavola deve identificare il modello e lo specifico check-in usato. Una cartella rinominata non deve trasformare il documento in un altro modello. «Copia di lavoro dello stesso documento» e «nuovo documento derivato da una copia» sono operazioni diverse e devono gestire esplicitamente l'identita'.

Durante il lavoro e' utile poter aggiornare una tavola verso una nuova versione del modello e mostrare le differenze o i riferimenti da riparare. Una tavola archiviata deve invece rimanere collegata alla versione registrata: aprire il file non deve portarla automaticamente all'ultimo modello disponibile.

La consegna di un modello e delle sue tavole deve avere un manifesto che elenchi gli identificativi esatti di tutti i check-in, le loro dipendenze e le impronte dei contenuti. Anche un'archiviazione con file unico deve conservare le dipendenze esterne. La riproduzione storica richiede anche gli snapshot geometrici e della tavola; rigenerare anni dopo con un kernel diverso puo' cambiare le curve entro le tolleranze. Versione del kernel e opzioni di calcolo vanno registrate.

## Revisioni e copie di check-in

Un salvataggio della copia di lavoro, un check-in e una revisione tecnica sono eventi distinti. Si propone di numerare ogni check-in in modo immutabile; piu' check-in possono appartenere alla medesima revisione di lavoro. La promozione a revisione rilasciata e' un'azione esplicita. Eventuali approvazioni e ruoli sono una scelta successiva del flusso operativo.

Ogni check-in conserva un'identita', il documento, il predecessore, la revisione tecnica, l'autore dichiarato, la data, il commento, l'impronta del contenuto e le dipendenze precise. La denominazione alfabetica o numerica delle revisioni resta configurabile. Senza un sistema di autenticazione, il nome dell'autore e' metadato dichiarato e non un'identita' verificata.

Tutti i check-in devono restare accessibili, compresi quelli intermedi. Si puo' condividere fisicamente un contenuto identico per ridurre lo spazio, mantenendo ogni evento e la sua copia logicamente completa e ricostruibile. La deduplicazione iniziale non deve diventare un requisito che rallenta il primo rilascio. Non prevedere cancellazione o pulizia automatica dei check-in.

Il ripristino di una vecchia versione deve creare una nuova copia di lavoro o un nuovo check-in discendente; non deve sovrascrivere o rinumerare l'archivio. Il ritorno nella storia parametrica del modello e l'apertura di un check-in storico restano operazioni diverse.

### Flusso proposto

1. **Consulta:** apertura in sola lettura dell'ultimo check-in o di uno storico, senza richiedere il check-out del documento.
2. **Check-out:** riserva di modifica del documento logico e creazione della copia di lavoro, registrando check-in di base e identificativo univoco della riserva. La riserva deve sopravvivere alla chiusura dell'applicazione.
3. **Salva:** aggiornamento della sola copia di lavoro. Eventuali recuperi automatici non sono nuovi check-in e non modificano le revisioni archiviate.
4. **Check-in:** controllo della riserva e della versione di base, validazione delle dipendenze, conservazione dei contenuti e pubblicazione di un nuovo record immutabile. Soltanto dopo il successo si puo' rilasciare o mantenere il check-out secondo il comando scelto.
5. **Annulla check-out:** rilascio della riserva, distinguendo esplicitamente la conservazione locale della copia di lavoro dalla sua eliminazione. Nessuna modifica all'archivio dei check-in.

L'attuale [`DocumentFileLock`](../cad_document_io.h) protegge un percorso con `QLockFile`; il salvataggio usa `QSaveFile` e verifica il possesso del blocco. La GUI oggi richiede il blocco esclusivo anche all'apertura. Si tratta di una base per la scrittura del file, da tenere distinta dalla riserva persistente del documento logico e dall'accesso in sola lettura.

### Concorrenza e interruzioni

Il check-in deve confrontare la versione di base con quella corrente sotto una riserva di scrittura dell'archivio. Una copia di lavoro obsoleta non puo' sostituire silenziosamente un check-in piu' recente. Ogni tentativo di check-in deve avere un identificativo riutilizzabile al nuovo tentativo, cosi' una risposta persa non produce due registrazioni della stessa operazione.

La pubblicazione proposta procede per stadi: scrivere e verificare contenuti e record in un'area temporanea, conservarli con nomi immutabili, quindi rendere visibile il nuovo stato aggiornando un solo manifesto. Se si interrompe prima dell'ultimo passaggio, i lettori continuano a vedere lo stato precedente. Eventuali file preparati ma non pubblicati devono essere recuperabili o classificati senza presentare check-in incompleti. `QSaveFile` su un file non rende atomica l'intera operazione su piu' documenti.

Una pubblicazione coordinata di modello e tavole deve rendere visibile un manifesto di insieme soltanto quando tutti i check-in referenziati esistono. I singoli documenti possono avere revisioni indipendenti; il manifesto fissa la combinazione consegnata.

Il furto o il recupero di una riserva deve invalidare l'identificativo precedente: un'applicazione rimasta aperta non deve poter pubblicare con la vecchia autorizzazione. Non rilasciare automaticamente una riserva perche' un computer e' temporaneamente irraggiungibile. Senza connessione si puo' lavorare sulla copia locale, ma la pubblicazione resta sospesa fino alla verifica della versione corrente.

Per una cartella condivisa, mutua esclusione e visibilita' atomica vanno verificate sui filesystem effettivamente usati. Un protocollo cooperativo fra applicazioni non protegge l'archivio da scritture esterne o dalla modifica manuale delle copie storiche: occorrono anche permessi coerenti. Se il filesystem non consente di sostenere il protocollo, oppure servono identita' verificate e accessi da sedi diverse, l'autorita' del check-in deve essere un servizio dedicato. L'interfaccia dell'archivio va separata dall'uso di percorsi locali per poter aggiungere tale servizio.

## Prestazioni e ordine di implementazione

La cache della geometria di vista deve dipendere dalla geometria sorgente, dal piano, dalla direzione, dalle tolleranze e dalle opzioni geometriche. Scala sul foglio, testo delle quote e cartiglio richiedono aggiornamenti di presentazione; non devono rigenerare il 3D. Cambiare una vista non deve ricostruire tutte le altre. Il ritorno a un corpo gia' disponibile deve poter riusare i risultati senza ricostruirne la genealogia.

Il lavoro di proiezione/sezione deve avvenire fuori dal thread della UI; un risultato precedente deve essere scartato se nel frattempo cambia lo stato richiesto. Le cache devono avere un limite di memoria. Impronte, compressione e archivio dei check-in vanno misurati separatamente dall'interazione grafica, con un documento di check-in coerente e immutabile mentre il salvataggio procede.

La sequenza proposta e': definire identita' e dipendenze dei documenti; provare la sezione con provenienza su solidi e superfici; dimostrare una quota associativa che sopravvive a rigenerazione e riapertura; scegliere il contenitore operativo; realizzare archivio e riserva persistente con recupero dagli errori; estendere poi fogli, viste, quote e rilascio coordinato. I riferimenti alle revisioni devono essere presenti gia' nei primi prototipi, anche se l'archivio completo arriva dopo.

## Criteri di accettazione

| Area | Verifica richiesta |
| --- | --- |
| Sezioni | Solido forato, lamina aperta curva, superficie NURBS rifilata con foro, sezione obliqua, piu' rami, tangenza, coincidenza e assenza di intersezione |
| Quote | Associazione al ramo corretto dopo variazione dei parametri/piano e riapertura; split o cancellazione con segnalazione esplicita; misura invariata al cambio di scala |
| Riferimenti ai file | Spostamento cartella, modello assente, tavola storica aperta dopo nuovi check-in, copia come documento nuovo |
| Archivio | Due richieste concorrenti, base obsoleta, risposta persa e nuovo tentativo, arresto in ogni stadio del check-in, perdita rete, riserva recuperata con rifiuto del vecchio titolare |
| Conservazione | Riapertura di ogni check-in con i contenuti e le dipendenze originali; ripristino senza sovrascrivere il passato; insieme modello/tavole sempre coerente |
| Prestazioni | Tempi e memoria di sezione, proiezione, aggiornamento di una quota, rollback e check-in su modelli rappresentativi |

Il contenitore operativo e' stato scelto: .prt e .drw separati. Restano da concordare l'uso multiutente, la convenzione delle revisioni e l'eventuale flusso di approvazione. L'utente ha approvato il formato parametrico 50 per incorporare l'identita' del modello; la cache geometrica resta 7. Per questa fase ha scelto un workspace locale nella cartella del primo salvataggio, con metadati nascosti.

## Primo prototipo implementato

Il modulo [`cad_drawing.h`](../cad_drawing.h) e [`cad_drawing.cpp`](../cad_drawing.cpp) implementa sezioni e prime quote senza dipendenze dalla GUI. Il chiamante fornisce uno snapshot immutabile dei corpi scelti, i loro nomi genealogici e l'identita' del documento/versione. Il ponte nativo descritto sotto collega l'identita' del `.prt` e prepara questi snapshot per una selezione esplicita di ID feature; l'editor delle tavole offre ora la scelta interattiva dei corpi finali disponibili.

Le misure disponibili sono lunghezza del tratto e distanza fra i suoi estremi. La lunghezza di una curva chiusa e' distinta dalla distanza nulla fra i suoi estremi coincidenti. Il modulo geometrico non comprende quote fra due entita' o quote radiali/angolari. Il primo editor descritto sotto aggiunge foglio, sezioni selezionabili e quotatura interattiva.

Un nome genealogico identifica le facce candidate. Se la sezione ha piu' rami, il testimone nel piano deve appartenere a uno solo: altrimenti la quota risulta ambigua. La regola viene salvata e impedisce anche di associare un ramo eliminato all'unico superstite. Per i rami multipli questa prima politica e' conservativa: spostamenti che portano il testimone fuori dalla curva richiedono riassociazione; non e' ancora implementata una continuita' dei rami basata sui bordi sorgenti. Per un solo ramo l'associazione segue il nome della faccia. Un errore restituisce uno stato esplicito e una misura non valida, senza conservare numeri precedenti apparentemente aggiornati.

Il codec autonomo JSON schema 1 conserva identita' del disegno, viste e quote, piano completo, scala, tolleranza, feature con ID a 64 bit e riferimenti a documenti/check-in. Non sceglie il contenitore operativo finale; l'identita' nativa del modello viene salvata separatamente nel `.prt` 50/cache 7. Il modello si aggiorna verso un altro check-in soltanto con un comando esplicito; il comando rifiuta tavole marcate come archiviate. Il controllo degli accessi resta da realizzare; il backend locale descritto sotto conserva record e contenuti storici tramite un protocollo cooperativo.

I test standalone in [`tests/test_drawing.cpp`](../tests/test_drawing.cpp) verificano superfici aperte, cilindro e tangenza, vista obliqua ellittica, patch NURBS razionale, foro con piu' rami, coincidenze, associazioni perse, dimensioni e scala, riapertura del B-rep e della tavola, versioni fissate e rifiuto atomico dei dati incoerenti. Questa suite riguarda il modulo geometrico e il codec; le prove del nuovo flusso interattivo sono descritte sotto.

## Archivio locale delle revisioni implementato

Il modulo [`cad_revision_store.h`](../cad_revision_store.h) e [`cad_revision_store.cpp`](../cad_revision_store.cpp) fornisce un archivio su filesystem locale, indipendente da GUI, kernel e contenitore dei documenti. [`cad_document_version.h`](../cad_document_version.h) definisce l'identita' condivisa da tavole e archivio. L'archivio assegna un UUID persistente a ogni documento logico; nomi dei file e percorsi non sono identificativi. Il formato `.prt` 50 incorpora questa identita', il check-in archiviato e la base della copia di lavoro; la cache resta 7. I file precedenti sono leggibili e ricevono un'identita' da conservare al successivo salvataggio.

Il catalogo JSON schema 1 conserva documenti, tutte le copie di check-in e prenotazioni. Ogni check-in ha un UUID scelto prima del tentativo, predecessore, numero progressivo, revisione tecnica, autore dichiarato, data UTC, commento, impronta SHA256 e dipendenze a check-in precisi dello stesso archivio. Due check-in possono avere la medesima revisione tecnica e condividere gli stessi byte, conservando entrambi i record. Il contenuto binario viene conservato in `objects` con nome derivato dalla sua impronta; nessuna API elimina lo storico o riscrive una copia precedente.

Il check-out resta nel catalogo dopo la chiusura del processo. Il chiamante riceve un token da conservare con la copia di lavoro e puo' riprendere la prenotazione tramite quel token. Al check-in devono coincidere token, titolare dichiarato e versione di base. Soltanto la pubblicazione riuscita rilascia la prenotazione. La lettura di un check-in non richiede check-out e rimane disponibile mentre il catalogo e' prenotato per una scrittura. Il backend restituisce byte e metadati; il client nativo collega la prenotazione alla copia `.prt` gia' salvata nel workspace e mantiene il token su disco.

Le modifiche al catalogo usano un lock breve `QLockFile` e pubblicazione tramite `QSaveFile`. Il contenuto viene completato prima del catalogo che lo rende visibile. Ripetere lo stesso check-in restituisce il record originale; riusarne l'UUID per un'operazione diversa viene rifiutato. Un contenuto preparato prima di una pubblicazione interrotta puo' essere riusato dopo verifica dell'impronta. Lettura e ritentativi segnalano contenuti corrotti. Se il catalogo manca ma la cartella contiene gia' dati, l'inizializzazione si arresta senza creare un archivio vuoto.

Le dipendenze devono riferire check-in gia' archiviati e integri, mai una copia di lavoro o l'ultimo file trovato per nome. Una tavola puo' quindi rimanere collegata al primo check-in del modello anche dopo che il modello ha raggiunto revisioni successive. Un ripristino crea un nuovo discendente usando i contenuti vecchi; le copie originali rimangono leggibili. Spostare l'intera cartella dell'archivio mantiene identita' e riferimenti.

I test [`tests/test_revision_store.cpp`](../tests/test_revision_store.cpp) verificano conservazione binaria, prenotazioni persistenti, basi obsolete, risposte perse, errori di scrittura, contenuti preparati, ripristino, dipendenze, corruzioni e contesa tra due processi locali. Non sono ancora verificati filesystem di rete, perdita fisica di connessione/alimentazione e arresto forzato in ogni stadio della pubblicazione. Restano da implementare autenticazione e permessi applicativi, apertura/ripristino delle versioni storiche dalla GUI e rilascio coordinato tramite manifesto di insieme. I primi comandi GUI e la persistenza locale della sessione sono descritti sotto. Il backend generico riceve byte dal chiamante; il ponte nativo descritto sotto verifica identita' e presenza della geometria degli snapshot `.prt`.

I limiti iniziali sono 16 MiB per catalogo, 256 MiB per singolo contenuto, 10000 documenti, 100000 check-in e 10000 dipendenze per check-in, con il limite del catalogo che puo' essere raggiunto prima degli altri. Il superamento viene rifiutato senza troncare lo storico. Questi limiti sono del primo backend, da riesaminare con misure su archivi rappresentativi.

## Workspace e collegamento ai documenti nativi

Il modulo [`cad_workspace.h`](../cad_workspace.h) e [`cad_workspace.cpp`](../cad_workspace.cpp) individua il workspace piu' vicino risalendo dalla cartella del documento. Se non esiste, il primo Salva usa la cartella di destinazione. Le sottocartelle riusano il workspace antenato; un workspace gia' presente piu' vicino prevale. La ricerca e l'apertura del documento non creano file. La struttura viene preparata durante il salvataggio, prima della sostituzione atomica del `.prt`:

```text
workspace/
  particolare.prt
  .forgecad/
    workspace.json
    revisions/
      catalog.json
      objects/
    sessions/
    pending/
```

Il prefisso punto nasconde la cartella sui sistemi UNIX-like; su Windows viene impostato anche l'attributo Hidden. I dati storici restano nell'archivio `revisions`; `sessions` conserva la sessione di check-out per documento e `pending` la richiesta esatta di check-in fino alla sua conferma e all'aggiornamento del file locale. Questi contenuti vengono creati dai comandi delle revisioni, mentre il primo Salva prepara soltanto le cartelle. Il normale Salva non esegue check-in e non prenota il documento. La copia di lavoro `.prt` resta nella cartella scelta dall'utente.

`workspace.json` conserva un UUID stabile per il workspace; l'archivio conserva il proprio UUID e i documenti i rispettivi ID. Spostare l'intero workspace mantiene le identita'. `workspace.lock` esiste solo durante la preparazione della struttura, distinto da `revisions/catalog.lock` e dal check-out persistente nel catalogo. Le cartelle interne non sono una destinazione valida per il normale Salva. Un marker corrotto, dati sconosciuti o un archivio incompleto causano un errore senza sostituire il `.prt` precedente. Un'inizializzazione interrotta puo' essere ripresa preservando l'archivio gia' creato; se la preparazione riesce ma il commit del `.prt` fallisce, puo' restare il workspace inizializzato senza quel documento. Le cartelle non vengono eliminate automaticamente.

I salvataggi successivi verificano marker e struttura senza rileggere l'intero storico; le operazioni di revisione verificano catalogo e contenuti. Il workspace non viene consultato da Undo/Redo o dalla rigenerazione geometrica. Nessun database viene introdotto. La cartella nascosta conserva dati necessari, non soltanto cache eliminabili: per trasferire o salvaguardare lo storico occorre includerla. La replica DFS non costituisce un'autorita' unica per le prenotazioni.

Il ponte [`cad_native_revisions.h`](../cad_native_revisions.h) e [`cad_native_revisions.cpp`](../cad_native_revisions.cpp) registra l'UUID nativo nell'archivio, riserva soltanto una versione di base ancora corrente, legge snapshot storici e prepara/pubblica check-in coerenti. I check-in richiedono geometria disponibile per le feature geometriche attive. Un'operazione preparata conserva UUID e byte originali in un contenitore verificato tramite SHA256, per ritentare dopo una risposta persa. Una modifica parametrica intervenuta dopo la preparazione impedisce di applicare la ricevuta come se il documento fosse gia' aggiornato. Le dipendenze native esterne non sono ancora supportate dal ponte; le tavole usano le dipendenze esatte del backend generico.

Salva con nome su un percorso diverso crea un documento indipendente; soltanto il salvataggio riuscito cambia l'identita' della sessione. Undo/Redo e navigazione della storia preservano l'identita' corrente. Il normale Salva rifiuta uno snapshot marcato come archiviato; la copia indipendente azzera check-in e base. Il ponte per le tavole riusa B-rep e nomi genealogici senza ricostruirli.

Le verifiche di questa fase comprendono [`tests/test_workspace.cpp`](../tests/test_workspace.cpp), [`tests/test_native_revisions.h`](../tests/test_native_revisions.h) e il flusso Salva con nome nei test di accesso al documento. Gli esiti effettivi di compilazione e test sono riportati nel registro del progetto; la presenza del codice per Windows non equivale a una verifica su quel sistema.

## Client e primi comandi GUI delle revisioni

Il modulo [`cad_revision_client.h`](../cad_revision_client.h) e [`cad_revision_client.cpp`](../cad_revision_client.cpp) collega il backend e il codec nativo al workspace. Il menu **File -> Revisioni** offre:

- **Esegui check-out**: salva la copia di lavoro e richiede un proprietario dichiarato; una sessione gia' presente per quel proprietario viene ripresa con lo stesso token.
- **Esegui check-in**: richiede etichetta libera di revisione tecnica e commento, salva il modello e pubblica uno snapshot completo tramite la prenotazione esistente.
- **Completa check-in pendente**: ritenta l'operazione originale conservata su disco, senza preparare una nuova versione.
- **Annulla check-out**: conferma la liberazione della prenotazione conservando le modifiche locali; rifiuta l'annullamento quando un check-in e' pendente.
- **Storico dei check-in**: mostra sequenza, revisione, autore, data e commento in una tabella non modificabile. Non apre o ripristina ancora uno snapshot storico nella GUI.

La sessione `sessions/<documentId>.json`, schema 1, conserva ID archivio, token, proprietario, base, data, percorso relativo della copia di lavoro e fase `reserving`, `active` o `canceling`. Il token viene generato e scritto prima di prenotare l'archivio; una richiesta identica ripetuta mentre la prenotazione e' attiva ne restituisce lo stesso stato. L'annullamento salva prima la fase locale, cosi' il recupero completa la liberazione senza iniziare una nuova prenotazione. Chiusura, Nuovo e apertura di altri documenti non annullano automaticamente il check-out. Salva con nome crea un UUID indipendente e lascia recuperabile la sessione del documento originale.

La richiesta `pending/<documentId>.op` conserva snapshot, identificativo e metadati originali. Il client la scrive prima di pubblicare il check-in, applica la ricevuta alla base della copia `.prt`, salva il file e rimuove prima la sessione e infine il pendente. Una risposta persa o l'interruzione durante la pulizia non generano un'altra copia di check-in. Se l'archiviazione riesce ma il salvataggio locale fallisce, il pendente resta disponibile e la GUI mantiene la nuova base in memoria marcando il documento da salvare. L'API puo' quindi aggiornare base e ricevuta anche quando restituisce un errore successivo alla pubblicazione; il chiamante deve conservarle per il recupero.

Se il documento e' stato modificato dopo la preparazione del pendente, il completamento pubblica esattamente lo snapshot originale e conserva le modifiche correnti nella copia di lavoro, aggiornandone soltanto la base. L'esito segnala che queste modifiche successive non appartengono a quel check-in. Il comando normale di check-in rifiuta di sovrascrivere una richiesta pendente. La lettura/scrittura del token non costituisce autenticazione; valgono i limiti del backend cooperativo locale.

Il percorso relativo e l'UUID dell'archivio consentono il recupero dopo lo spostamento dell'intero workspace. Una copia manuale del `.prt` con lo stesso UUID su un percorso diverso non eredita la sessione originale: per creare un documento indipendente si usa Salva con nome. Un lock breve per documento protegge gli aggiornamenti del client. Le operazioni dell'archivio restano distinte dal blocco esclusivo del file aperto e dalla cronologia geometrica.

Le prove in [`tests/test_workspace_revisions.h`](../tests/test_workspace_revisions.h) e nel runner viewport verificano ripresa delle sessioni, richieste di check-out ritentate, conservazione delle modifiche all'annullamento, tutti i check-in, recupero di risposte perse e richieste corrotte, errori di pubblicazione/salvataggio locale, spostamento del workspace e dialoghi GUI effettivi di check-in, storico e annullamento. I log e gli esiti reali della fase sono registrati in `CLAUDE.md`. Restano da verificare arresti forzati a ogni stadio, filesystem di rete, Windows, carichi rappresentativi e autorizzazioni multiutente.


## Primo editor GUI e file .drw separato

**File -> Tavole 2D -> Nuova tavola dal modello** salva il .prt e apre una finestra autonoma. **Apri tavola** apre un .drw senza sostituire il documento 3D corrente. Il foglio iniziale A4 orizzontale consente piu' sezioni XY/XZ/YZ, posizione del piano, scala e scelta dei corpi/superfici. Selezionare una curva e usare **Quota estremi** o **Lunghezza curva**; il trascinamento dei testi/titoli cambia la disposizione sul foglio. Le curve sono campionate per la visualizzazione, mentre le misure usano le curve esatte del kernel.

Il codec [`cad_drawing_file.h`](../cad_drawing_file.h) / [`cad_drawing_file.cpp`](../cad_drawing_file.cpp) usa JSON schema 1 con metadati del foglio, viste, quote, UUID della tavola e base di revisione, UUID/versione del modello, percorso relativo e SHA256 della sua definizione parametrica normalizzata. Esclude cache geometriche, orientamento di visualizzazione e unita' del documento dall'impronta; le misure del prototipo sono espresse in mm. L'impronta puo' segnalare anche modifiche conservative ai metadati della definizione. B-rep, mesh, schizzi e storia 3D restano nel .prt 50/cache 7, che non cambia formato. Ogni .drw contiene un foglio; piu' .drw possono riferire lo stesso .prt.

[`cad_drawing_window.h`](../cad_drawing_window.h) / [`cad_drawing_window.cpp`](../cad_drawing_window.cpp) gestisce apertura e salvataggio, blocco esclusivo del percorso .drw, pubblicazione atomica e workspace al primo Salva. **Salva con nome** su un altro percorso crea un UUID indipendente solo dopo il salvataggio riuscito e conserva il riferimento al modello. La chiusura della finestra principale rispetta Annulla sulle tavole con modifiche. Il blocco del file aperto e' distinto dal check-out persistente dell'archivio.

Spostare insieme cartella, .prt e .drw conserva il collegamento relativo. **Individua modello** consente di scegliere il .prt spostato verificandone l'UUID: un modello diverso, anche con lo stesso nome, viene rifiutato. All'apertura una definizione cambiata non aggiorna automaticamente la tavola. Il foglio resta consultabile in sola lettura, con quote da verificare; **Aggiorna dal .prt salvato** rilegge il modello, ricalcola sezioni/quote e marca la tavola modificata. Le modifiche 3D non salvate non vengono lette. Se il modello manca, si conservano disposizione e riferimenti, senza presentare misure precedenti come valide; non e' ancora salvata un'anteprima geometrica nel .drw.

Un riferimento a un check-in preciso legge quello snapshot dal .prt o dall'archivio indicato, verificando UUID dell'archivio, versione e impronta; non ripiega sul modello corrente. La tavola marcata come archiviata e' in sola lettura e richiede una copia indipendente per essere modificata. Il menu della finestra tavola dispone ora dei comandi descritti sotto. I comandi **File -> Revisioni** della finestra 3D continuano a riguardare il .prt. Il rilascio coordinato modello/tavole resta da implementare.

[`tests/test_drawing_editor.cpp`](../tests/test_drawing_editor.cpp) verifica inserimento/selezione/quotatura su una superficie aperta e aggiornamento associativo. [`tests/test_linked_drawing.h`](../tests/test_linked_drawing.h) verifica persistenza leggera, doppia apertura, Salva con nome, errori di salvataggio, modello senza cache, sorgente assente/diversa/modificata, rilocalizzazione, spostamento della cartella, check-in preciso e comandi GUI. Gli esiti effettivi sono nel registro CLAUDE.md.

Il primo editor gestiva sole sezioni; l'estensione alle proiezioni dei bordi e' descritta sotto. Restano proiezione completa con silhouette/spigoli nascosti, tratteggi, cartiglio tecnico ed esportazione di tavole. I calcoli di sezione avvengono ancora nel thread GUI e vanno spostati su worker con cancellazione per modelli complessi. Le curve di sezione vengono riusate durante lo spostamento dei testi; l'aggiornamento ricalcola le viste e le quote. Nessun aggiornamento automatico delle tavole e nessun accesso al workspace vengono aggiunti a Undo/Redo o alla navigazione della storia 3D. Non sono ancora disponibili benchmark rappresentativi di tempo/memoria, apertura o dimensioni su superfici complesse.


## Revisioni delle tavole e scelta del modello sorgente

Nella finestra .drw, **File -> Modello -> Collega revisione del modello** elenca i check-in del modello disponibili nello stesso workspace e ricalcola viste e quote sulla versione scelta. Non archivia automaticamente il 3D. **Usa copia di lavoro del modello** torna esplicitamente al .prt salvato. Il normale Salva consente entrambi gli stati; il check-in della tavola richiede una sorgente fissata a un check-in esistente, con UUID dell'archivio e impronta corrispondenti. Un collegamento alla sola copia di lavoro non e' una dipendenza storica valida. Il primo archivio gestisce dipendenze nello stesso workspace; collegamenti fra archivi diversi restano da sviluppare.

**File -> Revisioni tavola** offre check-out, check-in con revisione tecnica/commento, completamento del pendente, annullamento e storico. La prenotazione della tavola e' indipendente da quella del modello. Il protocollo delle sessioni e' condiviso con il client .prt tramite [`cad_revision_client_internal.h`](../cad_revision_client_internal.h), mantenendo lo schema 1 delle sessioni e tutti i formati esistenti. Ogni documento ha il proprio UUID, token, base e percorso relativo della copia di lavoro. La prenotazione persiste alla chiusura; annullarla mantiene le modifiche locali e viene rifiutato se esiste un check-in pendente.

[`cad_drawing_revisions.h`](../cad_drawing_revisions.h) / [`cad_drawing_revisions.cpp`](../cad_drawing_revisions.cpp) pubblica i metadati della tavola e registra come dipendenza il check-in esatto del modello. Non aggiunge B-rep o mesh al .drw o al suo snapshot: la geometria rimane nel check-in del modello referenziato. Prima della pubblicazione verifica identita', definizione e presenza del modello archiviato. Il normale Salva non crea copie storiche; ogni check-in crea un record immutabile e tutte le copie rimangono nell'archivio.

Il pendente `.forgecad/pending/<drawingId>.op` contiene l'operazione originale, con firma di formato FCDROP01, percorso relativo della copia di lavoro, token, base, identificativo di check-in, etichetta/commento e byte esatti del .drw archiviato. Un SHA256 dell'intero contenitore verifica anche i metadati. Viene scritto prima della pubblicazione; dopo la ricevuta si aggiorna la base e si salva il .drw locale, poi si rimuovono sessione e pendente. Un errore dopo la pubblicazione lascia il pendente recuperabile. Il completamento ritenta gli stessi byte e lo stesso UUID, preserva eventuali modifiche locali successive e segnala che queste non appartengono al check-in appena completato. Spostare insieme il workspace conserva il recupero; una copia manuale con lo stesso UUID su un altro percorso non eredita l'operazione.

Lo **Storico dei check-in** mostra ogni registrazione e permette **Apri copia storica**. La nuova finestra legge la tavola e il relativo modello dai rispettivi snapshot, senza prenotare il file della copia di lavoro, e impedisce modifiche/salvataggi sul documento originale. **Salva con nome** genera una tavola indipendente, conservando la versione precisa del modello. Questa operazione non costituisce un ripristino discendente della tavola originale; tale comando resta da sviluppare. La GUI e' implementata in [`cad_drawing_revision_ui.cpp`](../cad_drawing_revision_ui.cpp).

La suite [`tests/test_drawing_revisions.h`](../tests/test_drawing_revisions.h) verifica sessione persistente, base obsoleta, dipendenze precise, fonte non fissata/errata, ritentativi, modifica dopo preparazione, risposta persa, corruzione del pendente, errore di salvataggio locale, spostamento del workspace, conservazione di tutti i check-in e dialoghi GUI effettivi. Le verifiche realmente eseguite sono riportate in CLAUDE.md. Restano i limiti del backend cooperativo locale: autenticazione/permessi, filesystem di rete e Windows da validare, recupero del catalogo e rilascio coordinato tramite manifesto.


## Proiezioni dei bordi e quote nel piano della vista

L'editor offre **Inserisci vista** con orientamenti superiore, frontale, destra, inferiore, posteriore, sinistra e isometrico. Questa prima modalita' proietta tutti i bordi dei corpi scelti, inclusi quelli posteriori, senza classificazione visibile/nascosto e senza aggiungere le silhouette delle superfici curve. Il dialogo e il foglio esplicitano questo limite. Le viste di sezione gia' presenti restano disponibili con **Inserisci sezione**.

La geometria proiettata deriva dalle curve esatte dei bordi. Rette, coniche e NURBS usano l'immagine piana del kernel; le coniche oblique diventano NURBS razionali esatte. Per curve generiche, trasformate o limitate, una curva valutata proietta posizione e derivate senza interpolare una polilinea. I bordi ridotti a punti vengono omessi. Le quote misurano l'immagine nel piano della vista, senza dipendere da scala o zoom: non sono misure spaziali del bordo originale. Le nuove **Quota orizzontale** e **Quota verticale** misurano le componenti della distanza fra gli estremi lungo gli assi locali della vista; restano lunghezza della curva e distanza fra gli estremi. Per una curva chiusa la distanza fra gli estremi puo' essere nulla e non equivale a un diametro.

L'opzione **Mostra sezioni del piano come riferimenti di quota** aggiunge, in verde, le curve ottenute intersecando il piano della vista con le facce, anche di superfici aperte. Queste quote conservano i riferimenti genealogici alle facce; quelle dei bordi proiettati conservano i nomi genealogici degli edge. Alias multipli vengono risolti soltanto con un testimone geometrico univoco; bordi mancanti o riferimenti ambigui non vengono sostituiti per indice o per vicinanza. Bordi geometricamente sovrapposti da profondita' diverse rimangono entita' distinte: la proiezione non e' un contorno unificato e la lunghezza di una curva che ripercorre un tratto ne segue il percorso parametrico.

Il codec geometrico interno passa allo schema 2 soltanto quando la tavola contiene proiezioni o quote orizzontali/verticali. Le tavole composte solo da sezioni e misure precedenti vengono ancora scritte nello schema 1, con gli stessi campi. Il lettore accetta entrambi; le vecchie app rifiutano le tavole con il nuovo schema. Un documento con nuovi campi dichiarato schema 1 viene rifiutato per evitare interpretazioni come se fosse una vecchia sezione. Il contenitore .drw, il foglio e i metadati delle revisioni restano schema 1; .prt 50/cache 7 invariati. Nessun B-rep, mesh o cache di proiezione viene aggiunto al file della tavola.

Le nuove prove in [`tests/test_projected_drawing.cpp`](../tests/test_projected_drawing.cpp) coprono proiezioni oblique, superfici aperte, ellissi, NURBS, curve generiche, quote nel piano, ambiguita', codec e dialogo GUI. [`tests/test_projected_drawing_file.h`](../tests/test_projected_drawing_file.h) collega le nuove viste a .prt, .drw e check-in precisi del modello, verificando che la tavola storica mantenga la misura originale. Esiti effettivi nel registro CLAUDE.md. Restano silhouette/HLR, allineamento associativo fra viste, quote fra entita' diverse e radiali/angolari, cartiglio/esportazione e lavoro asincrono su geometrie complesse.


### Interfaccia comune e modifica delle viste (2026-10-15)

Le tavole riusano CadMenu, le icone vettoriali e la configurazione delle barre da 24 px della modellazione. Il tema applicato dalla finestra 3D resta globale. L'albero agganciabile elenca viste e quote e sincronizza la selezione col foglio; i menu Modifica, Inserisci, Quote e Visualizza richiamano gli stessi comandi delle barre.

Modifica vista cambia titolo, scala, piano e sorgenti conservando gli ID e le posizioni dei testi. La generazione interessa la sola vista modificata. Una sorgente ancora quotata o le sezioni del piano con quote non possono essere rimosse prima delle relative quote. Spostare un piano fuori dal corpo conserva la quota, segnalata mancante senza valore numerico obsoleto. Una rinomina conserva la precisione originale di frame e scala, anche su piani arbitrari. Non sono richieste modifiche ai formati o all'archivio; l'aggiornamento del modello resta esplicito.


Verifica della fase: build Release app/runner/standalone senza warning; 44/44 regressioni superate, comprese modifica della vista, quote conservate, piano arbitrario senza perdita di precisione, persistenza e storico/copia indipendente. Anteprima completa controllata dopo il contenimento delle barre di stato su una riga; testo completo nei tooltip. Prove GUI offscreen, senza benchmark rappresentativi o test manuali GPU/Windows/rete. HLR/silhouette, allineamento, quote piu' complete, cartiglio/esportazione, Undo/Redo del foglio e calcoli asincroni restano aperti.


### Layout e cronologia locale del foglio (2026-10-15)

Allinea vista posiziona una tantum i centri delle curve visibili sulla medesima riga o colonna; non crea vincoli permanenti tra viste. Spostare una vista porta con se' i suoi testi quota senza cambiare le misure. Elimina selezione agisce sui titoli delle viste o sui testi quota; un bordo selezionato da solo non cancella la vista. La cancellazione di una vista comprende le quote e viene annullata/ripristinata come un'unica operazione; la GUI chiede conferma se sono presenti quote.

La cronologia conserva esclusivamente i metadati JSON del foglio, con limite di 100 passi e 16 MiB, e non viene salvata nel .drw. Layout e Undo/Redo riusano geometria e risultati delle viste invariate; una vista reinserita o con piano/sorgenti modificati viene rigenerata. Un aggiornamento esplicito del modello azzera la cronologia locale, evitando il ripristino di riferimenti a una vecchia sorgente. Salvataggio e check-in segnano il checkpoint corrente senza annullare la cronologia; Salva con nome crea un editor indipendente. Le modifiche del contenitore, come hash e collegamento del modello, restano distinte dai passi annullabili del foglio. Nessuna modifica a formati, database o protocollo delle revisioni e nessun accesso al workspace da Undo/Redo.


Verifica della fase layout/cronologia: build Release app/runner e quattro editor standalone senza warning; suite dedicata 10/10 e regressioni estese 45/45 PASS. Coperte cancellazione e ripristino atomico, trascinamenti effettivi, allineamento fra viste a scale/orientamenti diversi, scorciatoie del canvas separate dall'Undo dei campi di testo, limite di 100 passi, checkpoint di salvataggio, cambio piano con quota mancante e cambio sorgente che conserva lo stato da salvare. Verifiche GUI offscreen e anteprime ispezionate; nessun benchmark rappresentativo o prova manuale GPU/Windows/rete. Restano HLR/silhouette, vincoli persistenti fra viste, quote piu' complete, cartiglio/esportazione e calcolo asincrono.
