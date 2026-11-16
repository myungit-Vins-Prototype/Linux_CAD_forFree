# Runner modeless, messaggi di anteprima e footer — implementazione privata

## Esito

La modifica privata è congelata in sei file: `forgeCad2026_dialogs_common.cpp`, `forgeCad2026_dialogs.h`, `forgeCad2026_dialogs_solids.cpp`, `forgeCad2026_dialogs_surfaces.cpp`, `forgeCad2026_dialogs_sketch.cpp`, `forgeCad2026_dialogs_analysis.cpp`.

Cinque translation unit sono state compilate nella cartella privata senza usare la build condivisa. Common, solids, surfaces e analysis non producono diagnostica; sketch mantiene due warning `misleading-indentation` in funzioni non modificate. I cinque oggetti finali sono stati collegati ai probe insieme ai restanti oggetti dell'app.

Il probe locale comune passa: conferma e annullamento sincroni in onShown, finished emesso durante showEvent, guardia prima del callback del secondo documento, pulizia immediata del primo e compatibilità del puntatore a funzione a tre argomenti.

L'agente di test indipendente ha comunicato **115 PASS / 0 FAIL**, con **11 / 11 pannelli realmente aperti e annullati**. Le 36 nuove asserzioni sui footer passano sui nove runner con PreviewScope; Datum e Measure sono esplicitamente esclusi perché non hanno tale anteprima. La variante senza helper footer fallisce quelle 36 asserzioni, perciò il controllo negativo distingue la regressione. I test comprendono Shell con worker pronto -> spessore zero -> recupero, Thread con una faccia cilindrica reale e cambio corpo/standard, guardia fra documenti, stesso documento, callback sincroni, storyboard/dock di Pattern e scorrimento/hide-show dei footer. I dettagli e le fonti delle regressioni sono conservati dall'agente di test nel proprio archivio.

## Causa e soluzione UI2

Undici dialoghi mantenevano un proprio WindowLock e un QEventLoop, senza passare dalla guardia globale del runner comune. Un comando nel documento B poteva perciò aprire un altro ciclo annidato mentre il comando del documento A era ancora attivo. La chiusura del pannello A emetteva finished, ma il suo stack, le anteprime e il lock restavano vivi fino al ritorno del loop B.

Tutti gli undici runner passano ora da `runModeless` con setup differito: quattro nei solidi, cinque nelle superfici, uno nei piani e uno nella misura. La guardia fra documenti precede creazione di PreviewScope, installazione dei picker, callback del viewport e prime richieste di anteprima.

L'API conserva l'overload a tre argomenti, che inoltra al nuovo overload a quattro argomenti con callback vuoto. Questo preserva sia i chiamanti sorgenti, sia il simbolo e i puntatori a funzione esistenti; i probe possono collegare oggetti dell'app precedenti senza shim divergenti dal codice finale.

Il runner collega finished **prima** di show e del callback, registra un flag e avvia il loop soltanto se il dialogo non è già terminato. Un accept/reject sincrono durante showEvent o onShown termina regolarmente. Non vengono usati reject accodati per aggirare il problema.

Il runner modale `runUntilApplied` resta distinto: il suo eventuale QEventLoop gestisce un dialogo Qt modale e il caso hide durante conferma; non è uno degli undici runner manuali migrati.

## Picker, Pattern e durata dei dati

I callback di selezione vengono dichiarati nelle funzioni originali ma installati dentro onShown, dopo il lock e show. Tutte le cancellazioni e la pulizia di riferimenti, marker e anteprime restano al ritorno dei runner originali.

Pattern abilita il dock e installa il callback della storyboard dopo il lock comune; dopo il ritorno rimuove il callback e ripristina lo stato enabled originale del dock. Non forza il dock disabilitato dopo che il runner comune ha ripristinato la finestra. Thread inizializza lettura della faccia, misure e prima scelta dopo show, senza il precedente timer di avvio.

Fill ha un callback speciale dell'anteprima. Il suo puntatore previewLabel viene acquisito soltanto dopo la creazione differita di PreviewScope e catturato per valore dal callback. Il refresh del contorno vuoto accede allo scope solo nella propria lambda. La review ha individuato e corretto un accesso anticipato nella prima bozza privata prima di ogni integrazione; il freeze finale e le prove coprono la versione corretta.

## Coerenza del footer

Creare PreviewScope dopo showEvent richiede lo stesso trasferimento nel footer usato dalle anteprime create prima dell'apertura. L'helper anonimo `movePreviewMessageToFooter(QDialog&, QFormLayout*)`, interno a common.cpp, è condiviso da showEvent e dal costruttore di PreviewScope quando il pannello è già visibile, è un FunctionDialogPanel e non usa un layout esterno.

L'helper conserva etichetta e messaggio, rimuove soltanto i QLayoutItem dalla riga originaria, li riassocia al footer e inserisce il contenitore subito prima dei pulsanti. Mantiene i margini storici 0/10/0/0. Se il messaggio non appartiene più a una riga del form, non fa nulla: hide/show non duplica né footer, né messaggio, né pulsanti. Il messaggio resta fermo quando il form scorre. Non occorre una nuova API pubblica o un settimo file/header.

## Messaggi PRE5 e PRE6

Sono importate le due correzioni indipendenti del test agent:

- Shell, spessore zero: il messaggio dell'anteprima passa ad attesa; al ritorno a uno spessore valido viene cancellato l'errore persistente prima della richiesta.
- Thread, scelta o faccia assente/non valida: il messaggio dell'anteprima indica esplicitamente l'attesa della faccia, invece di mantenere una precedente dicitura pronta.

La cancellazione delle anteprime già presente nei rispettivi rami resta attiva.

## Verifiche statiche e identità

- `scope-access-audit.json`: tutti i 28 accessi agli scope differiti sono dentro lambda.
- `setup-access-audit.json`: tutte le 109 operazioni iniziali di picker, viewport, refresh e anteprima dei runner selezionati sono differite in lambda; nessuna è eseguita prima del guard comune.
- Inventario finale dei quattro coordinatori: zero WindowLock manuali e zero QEventLoop, undici callback al runner comune (4 + 5 + 1 + 1).
- `baseline-sha256.json` e `freeze-sha256.json` identificano i sei sorgenti prima/dopo; baseline completa conservata e confrontata prima dell'integrazione.
- `architecture-consult.json` conserva descrizioni e connessioni del grafo consultate; sono state lette anche memoria e politica delle tolleranze. Nessun parametro geometrico o soglia è cambiato.

## Limiti e responsabilità residue

Questa verifica usa Qt offscreen e controlla pannelli, picker, stato dei widget e worker. Non certifica il rendering OpenGL o tutte le combinazioni di piattaforma/tema. I runner restano sincroni sul proprio stack con un ciclo eventi comune: la politica intenzionale resta un comando modeless attivo fra documenti, con consultazione degli altri documenti consentita.

Produzione, catalogo, grafo e memoria non sono stati modificati da questo agente. L'integrazione, la build completa, l'aggiornamento architetturale e la documentazione complessiva sono responsabilità del coordinatore. L'archivio di questo agente è limitato alla cartella core autorizzata e contiene esclusivamente fonti testuali, diff, JSON, Markdown e log; nessun eseguibile o oggetto.
