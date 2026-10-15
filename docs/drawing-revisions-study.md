# Tavole 2D con sezioni associative e gestione delle revisioni

Studio del 15 ottobre 2026. I tre vincoli sono: quotare le sezioni anche di superfici aperte, valutare dove conservare modello e tavole, gestire check-out e tutte le copie di check-in. Le scelte generali restano una proposta architetturale; i prototipi geometrici e il backend locale delle revisioni implementati sono descritti nelle ultime sezioni. I comandi GUI non sono ancora disponibili.

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

La proposta preferita e' mantenere modello e disegno come documenti logicamente distinti, con un disegno che puo' contenere piu' fogli, e prevedere un pacchetto unico per consegna/archiviazione. La preferenza dipende dall'esigenza di revisioni e check-out indipendenti e va confermata con l'uso previsto. Se si scegliera' un unico file operativo, conviene conservare questa separazione logica interna.

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

Restano da concordare il contenitore operativo, l'uso multiutente, la convenzione delle revisioni e l'eventuale flusso di approvazione. Nessuno di questi punti richiede ancora la modifica del formato parametrico 49 o della cache 7.

## Primo prototipo implementato

Il modulo [`cad_drawing.h`](../cad_drawing.h) e [`cad_drawing.cpp`](../cad_drawing.cpp) implementa sezioni e prime quote senza dipendenze dalla GUI. Il chiamante fornisce uno snapshot immutabile dei corpi scelti, i loro nomi genealogici e l'identita' del documento/versione. La selezione dei corpi da mostrare e la gestione dell'identita' del modello nel documento `.prt` restano da collegare all'applicazione.

Le misure disponibili sono lunghezza del tratto e distanza fra i suoi estremi. La lunghezza di una curva chiusa e' distinta dalla distanza nulla fra i suoi estremi coincidenti. Il prototipo non comprende ancora quote fra due entita', quote radiali/angolari, impaginazione, rendering delle viste o interfaccia di quotatura.

Un nome genealogico identifica le facce candidate. Se la sezione ha piu' rami, il testimone nel piano deve appartenere a uno solo: altrimenti la quota risulta ambigua. La regola viene salvata e impedisce anche di associare un ramo eliminato all'unico superstite. Per i rami multipli questa prima politica e' conservativa: spostamenti che portano il testimone fuori dalla curva richiedono riassociazione; non e' ancora implementata una continuita' dei rami basata sui bordi sorgenti. Per un solo ramo l'associazione segue il nome della faccia. Un errore restituisce uno stato esplicito e una misura non valida, senza conservare numeri precedenti apparentemente aggiornati.

Il codec autonomo JSON schema 1 conserva identita' del disegno, viste e quote, piano completo, scala, tolleranza, feature con ID a 64 bit e riferimenti a documenti/check-in. Non sceglie il contenitore operativo finale e non cambia il formato `.prt` 49/cache 7. Il modello si aggiorna verso un altro check-in soltanto con un comando esplicito; il comando rifiuta tavole marcate come archiviate. Il controllo degli accessi resta da realizzare; il backend locale descritto sotto conserva record e contenuti storici tramite un protocollo cooperativo.

I test standalone in [`tests/test_drawing.cpp`](../tests/test_drawing.cpp) verificano superfici aperte, cilindro e tangenza, vista obliqua ellittica, patch NURBS razionale, foro con piu' rami, coincidenze, associazioni perse, dimensioni e scala, riapertura del B-rep e della tavola, versioni fissate e rifiuto atomico dei dati incoerenti. La prova riguarda il modulo geometrico e il codec, non un flusso interattivo di tavola gia' disponibile nella GUI.

## Archivio locale delle revisioni implementato

Il modulo [`cad_revision_store.h`](../cad_revision_store.h) e [`cad_revision_store.cpp`](../cad_revision_store.cpp) fornisce un archivio su filesystem locale, indipendente da GUI, kernel e contenitore dei documenti. [`cad_document_version.h`](../cad_document_version.h) definisce l'identita' condivisa da tavole e archivio. L'archivio assegna un UUID persistente a ogni documento logico; nomi dei file e percorsi non sono identificativi. Il formato `.prt` resta 49/cache 7: l'integrazione di questa identita' nel file nativo deve ancora essere realizzata.

Il catalogo JSON schema 1 conserva documenti, tutte le copie di check-in e prenotazioni. Ogni check-in ha un UUID scelto prima del tentativo, predecessore, numero progressivo, revisione tecnica, autore dichiarato, data UTC, commento, impronta SHA256 e dipendenze a check-in precisi dello stesso archivio. Due check-in possono avere la medesima revisione tecnica e condividere gli stessi byte, conservando entrambi i record. Il contenuto binario viene conservato in `objects` con nome derivato dalla sua impronta; nessuna API elimina lo storico o riscrive una copia precedente.

Il check-out resta nel catalogo dopo la chiusura del processo. Il chiamante riceve un token da conservare con la copia di lavoro e puo' riprendere la prenotazione tramite quel token. Al check-in devono coincidere token, titolare dichiarato e versione di base. Soltanto la pubblicazione riuscita rilascia la prenotazione. La lettura di un check-in non richiede check-out e rimane disponibile mentre il catalogo e' prenotato per una scrittura. Il backend non crea ancora copie di lavoro sulla scrivania dell'utente: restituisce dati e metadati al futuro comando applicativo.

Le modifiche al catalogo usano un lock breve `QLockFile` e pubblicazione tramite `QSaveFile`. Il contenuto viene completato prima del catalogo che lo rende visibile. Ripetere lo stesso check-in restituisce il record originale; riusarne l'UUID per un'operazione diversa viene rifiutato. Un contenuto preparato prima di una pubblicazione interrotta puo' essere riusato dopo verifica dell'impronta. Lettura e ritentativi segnalano contenuti corrotti. Se il catalogo manca ma la cartella contiene gia' dati, l'inizializzazione si arresta senza creare un archivio vuoto.

Le dipendenze devono riferire check-in gia' archiviati e integri, mai una copia di lavoro o l'ultimo file trovato per nome. Una tavola puo' quindi rimanere collegata al primo check-in del modello anche dopo che il modello ha raggiunto revisioni successive. Un ripristino crea un nuovo discendente usando i contenuti vecchi; le copie originali rimangono leggibili. Spostare l'intera cartella dell'archivio mantiene identita' e riferimenti.

I test [`tests/test_revision_store.cpp`](../tests/test_revision_store.cpp) verificano conservazione binaria, prenotazioni persistenti, basi obsolete, risposte perse, errori di scrittura, contenuti preparati, ripristino, dipendenze, corruzioni e contesa tra due processi locali. Non sono ancora verificati filesystem di rete, perdita fisica di connessione/alimentazione e arresto forzato in ogni stadio della pubblicazione. Restano da implementare autenticazione e permessi applicativi, comandi GUI, gestione locale della copia di lavoro e del token, rilascio coordinato tramite manifesto di insieme e integrazione dei codec `.prt`/tavola per garantire snapshot completi. Il chiamante fornisce per ora il contenuto binario da conservare.

I limiti iniziali sono 16 MiB per catalogo, 256 MiB per singolo contenuto, 10000 documenti, 100000 check-in e 10000 dipendenze per check-in, con il limite del catalogo che puo' essere raggiunto prima degli altri. Il superamento viene rifiutato senza troncare lo storico. Questi limiti sono del primo backend, da riesaminare con misure su archivi rappresentativi.
