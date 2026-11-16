PRE7 — prove private e audit indipendente K10, 16 novembre 2026

Archivio curato di20artefatti incluso questo testo, comandi e manifest.
Nessun eseguibile, oggetto, CAD, copia dei tre sorgenti privati K10 o log enorme.
I nomi sorgente terminano .txt per non entrare nelle compilazioni del repository.
SHA256SUMS usa esclusivamente i nomi effettivi dell'archivio.

PROBLEMA E PROPOSTA PRIVATA
PRE7(P2): mapping/fill conservavano risultato pronto dopo una modifica effettiva
alla selezione continua. Non viene contata l'apertura del picker senza mutazioni.
repro.cpp.txt e repro.log conservano la prima riproduzione contro oggetti app:
lista sorgenti vuota, geometria e vecchia chiave valide, messaggio pronta.

Produzione proposta: unico forgeCad2026_dialogs_mapping.cpp.txt, patch change.patch.txt.
Helper locale refreshSelectionPreview usa la chiave completa del builder.
All'apertura picker invariato conserva la policy corrente; dopo variazioni mentre
picking chiama clearPreview, cancella token, incrementa generazione, elimina chiave
geometria e visualizza attesa. Mapping cancella Esito; fine scelta riprende request.
Nessuna API common modificata e nessuna modifica surfaces richiesta.
Hash sorgente privato3075692e4f16b98eff7ee23831121aaeb9ea5381e6073aa78681d6189877c3e2.
Questa cartella documenta il prototipo; build e prove integrate sono responsabilita'
della consegna del coordinatore, non vengono anticipate qui.

REGRESSIONE PERMANENTE
Header test_preview_selection_state.h.txt da includere dentro ViewportInteractionTest.
Funzione static previewSelectionMutationReview; dispatch proposto --preview-selection-state,
CTest con TIMEOUT150. Sei casi, ciascuno watchdog20s: sviluppo sorgente, wrap
destinazione e Fill contorno, sia preview pronta sia job pendente.
Le asserzioni accumulano fallimenti e lanciano soltanto dopo eventloop e cleanup.
Il test non cambia QSettings. Solo il wrapper privato usa QTemporaryDir per settings.

ESITI FINALI REALI DELL'HEADER
private-header.log:79PASS/0FAIL, offscreen,exit0.
private-header-wayland.log:79PASS/0FAIL, Wayland,exit0.
baseline-header.log:48PASS/31FAIL, soli oggetti app correnti,exit1.
Non sono79difetti distinti. Entrambi i link sono conservati: il privato rimpiazza
il solo mapping.cpp.o, il negativo non sovrappone oggetti produzione privati.
Oggetti production mapping/common/preview successivi ai sorgenti: object-audit.json.

Nel caso pending viene modificato un numero UI dopo una preview pronta e avviato
subito il job reale. Token ancora falso prima della mutazione, vero dopo.
Controlli: generazione incrementata, chiave vuota, geometria nulla, stato attesa,
nessuna pubblicazione obsoleta dopo180ms, ripresa e cleanup picker/lock/callback.
I callback simulati eseguono prima cancelReferencePick, come il viewport reale.

PRIMO FIXTURE, CONSERVATO
private-initial-fixture.log:77PASS/2FAIL. Nei due casi Fill la callback diretta
ometteva il cancelReferencePick che il viewport reale esegue prima dell'esito;
la selezione terminava nella logica dialogo ma il fixture lasciava refPicking attivo.
Correzione solo del fixture, prototipo produzione identico. Il pending finale e'
anche stato reso deterministico tramite numero UI+avvio immediato del worker.
Il primo Wayland nel sandbox abortiva prima dei test senza produrre log; il replay
fuori sandbox autorizzato da auto-review e' quello finale conservato.

AUDIT K10 INDIPENDENTE
Audit READ-ONLY in audit-k10.txt: nessun nuovo difetto/bloccante trovato nei tre
sorgenti privati dichiarati e sigillati da hash nel rapporto. Revisionati proiezione
vs residuo mm, snapshot originali outward e owned, domini/wrapper, flag metrico
privato, nodi Bezier e multinsieme fin/budget dopo AssemblyMap.
Replay di binari altrui:6guardiePASS e10verifichePASS nei due log k10-*.
Questi replay sono distinti da una nuova campagna indipendente o Fraction.

LIMITI
Nessuna certificazione pixel OpenGL, tutti i temi/OS, tutti gli ingressi kernel o
assenza globale di leak. Nessuna nuova campagna sanitizer qui. Root eseguira' build
completa e test permanente Release; questi artefatti non dichiarano app aggiornata.
L'audit K10 non ricompila da zero gli altri binari ne' certifica fallback non migrati.
