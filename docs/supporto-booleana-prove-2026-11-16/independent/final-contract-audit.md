# Audit mirato finale — retry, entrypoint e loop chiuso

16 novembre 2026. Nessuna modifica al software del repository, nessuna compilazione aggiuntiva. Ispezione del sorgente privato finale `kernel/fk_boolean.cpp` SHA256 `3b8e21c9133627c99cf51df712d8b638cc466f7d2d6442f2c43c1ba47f1feda2`; helper/header invariati rispetto alla revisione matematica precedente. Tutti e tre i valori di final-freeze.json sono stati ricalcolati e coincidono.

## Perimetro della verifica

La copia bytewise della revisione intermedia aacb50 non è conservata, come confermato dall'autore. Questa è pertanto una nuova ispezione mirata del codice finale sulle tre modifiche semantiche dichiarate, non un confronto bytewise con aacb50. Il confronto completo rispetto alla produzione è disponibile separatamente in stage/delta.diff.txt.

## Attivazione del percorso

Il nuovo argomento del costruttore BooleanBuilder ha default false. Il wrapper pubblico booleanOperation è l'unico chiamante che passa true. bodySectionCurves, bodySectionCurvesWithOrigins, clipSolidByFace, uniteDisjointBodies, splitSheet e imprintCurves costruiscono il builder senza attivazione. Anche l'estrazione degli anchor in pairArcs è condizionata dal flag. collectCuts non invoca boundaryChain quando il flag è false. Rimangono le guardie contains su entrambe le facce prima di ogni dereferenziazione.

## Transazione e retry

usedBoundary_ parte false e diventa true solo dopo avere inserito i Piece della catena in cuts. Il wrapper azzera history all'ingresso; costruisce un primo builder su copie degli stessi operandi const; chiama run. Cattura OperationCancelled esclusivamente per rilanciarla, senza retry. Cattura std::domain_error e rilancia immediatamente se nessuna sostituzione è stata usata. Altrimenti azzera di nuovo history e costruisce un nuovo builder con gli stessi a, b, operation, options e attivazione false. Il secondo run non richiama ricorsivamente il wrapper: un solo tentativo storico. Un'eccezione del secondo tentativo esce direttamente dal catch, senza ricattura da parte del primo handler.

Non vi è catch generico nel wrapper. std::logic_error diretto, bad_alloc e altre eccezioni non geometriche propagano. Precisazione della gerarchia C++: domain_error deriva a sua volta da logic_error, ma è la sola sottoclasse esplicitamente recuperata; un logic_error diretto non viene intercettato. La costruzione iniziale del builder è fuori dal try: un suo fallimento precede qualunque sostituzione e non richiede retry. Il costruttore copia i Body prima di preparare le pcurve; non modifica gli operandi originali.

Le assegnazioni a history dentro run/assemble avvengono sui percorsi di ritorno riuscito. Il reset prima del retry impedisce comunque di riutilizzare uno stato eventualmente parziale del primo tentativo. Tolleranze, precisione corrente, operazione e opzioni non vengono cambiate.

## Attraversamento di un loop chiuso

Il ritorno al primo fin è ammesso una sola volta con wrapped. Serve al caso in cui entrambi gli anchor appartengono allo stesso fin ma il secondo parametro è dietro al primo nel verso scelto. Il parametro viene riavviato all'estremo originale del fin, e la seconda visita può terminare all'anchor. Il secondo ritorno al primo fin interrompe il cammino. Restano controlli di incidenza esplicita fra vertici, intervalli originali e famiglia Ellipse; ogni pezzo deve mantenere progressione stretta dei parametri del segmento e superare il certificato di monotonia.

Un loop intero che ripassa sullo stesso punto non può essere autorizzato dalla sola chiusura topologica: la proiezione sulla retta deve essere monotona su tutti i pezzi, con ta<tb. I limiti di4096 passi e16384 unità di lavoro condivise, oltre alle4096 coppie di anchor e ai cancellationPoint, impediscono traversamenti indefiniti. I casi ambigui o inconclusivi tornano al percorso precedente; non aumentano il budget.

## Esito e limiti

Nessun ulteriore problema bloccante individuato sulle tre variazioni richieste. Questo esito riguarda il sorgente congelato privato e non dichiara integrazione, aggiornamento dell'app o nuova validazione runtime eseguita da questo agente. Le prove runtime finali sono eseguite dagli altri agenti e devono essere archiviate con i loro log/hash. La revisione matematica e la prova Fraction indipendente restano nel rapporto app-path-audit.md e negli artefatti cap-*-fraction.
