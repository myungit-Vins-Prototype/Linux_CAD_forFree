# Riferimenti associativi e costruzione negli schizzi — 3 novembre 2026

Richiesta: creare solo i riferimenti associativi effettivamente utilizzati, liberarli quando scompare l'ultimo utilizzo, escludere la costruzione dalle sezioni loft e nasconderla fuori dalla modifica dello schizzo.

## Implementazione

I clic su geometria esterna in Seleziona e Quota creavano immediatamente cache associative con `recordUndo` e `documentChanged`, pur senza confermare una geometria o un vincolo. Anche la scelta di piani/assi salvava subito queste cache. La rimozione delle entità rinumerava i vincoli, ma non liberava le cache rimaste senza consumatori.

Ora la scelta esterna mantiene una cache di lavoro transitoria: selezione e annullamento non registrano un Undo né segnalano una modifica del documento. Alla conferma di geometria o vincoli resta il normale Undo. Gli snapshot per cronologia/persistenza escludono i riferimenti automatici inutilizzati; deselezione, modifica e uscita dallo schizzo ripuliscono anche la cache di lavoro. Il caricamento elimina le cache automatiche preesistenti prive di consumatori prima della rigenerazione.

`cad_sketch_reference_usage.*` applica una pulizia conservativa. Ogni vincolo esistente conta come utilizzo, incluse quote e i riferimenti annidati di Offset/Pattern. Sono sempre conservati conversioni esplicite, geometria di profilo convertita e riferimenti provenienti da altre funzioni/schizzi, percorsi e assi di rivoluzione. Un gruppo di cache resta intero se ne serve una parte, conservando il contratto di refresh con la sorgente. Dopo la rimozione si aggiornano gli indici locali, i consumatori e le selezioni; anche l'eliminazione esplicita di parti dello schizzo rimappa ora tutte le sorgenti delle estrazioni multiple; le copie condivise degli snapshot restano immutate. Un errore proveniente esclusivamente dalla cache rimossa viene liberato senza eliminare le diagnostiche delle sorgenti ancora utilizzate. In presenza di vincoli gia malformati o incompleti la potatura si ferma, conservando il caso diagnosticabile invece di perdere vincoli durante la rinumerazione.

La nuova policy comune `isSketchEntityVisible` rende visibili segmenti, curve, punti e assi di costruzione soltanto nello schizzo in modifica. Le cache automatiche rimangono nascoste. La stessa policy governa rendering/highlight/errori, picking di punti/curve/schizzi, selezione rettangolare, storyboard e limiti dell'inquadratura. Gli snap non importano più le cache nascoste o la costruzione di altri schizzi non in modifica.

Il profilo dello schizzo intero escludeva già la costruzione. Il problema rimaneva nei picker, che offrivano anche le singole entità di costruzione. Queste sono ora escluse, e `forgeReferencedLoft` rifiuta anche un riferimento di costruzione già salvato come sezione, indicando sezione e nome per la diagnostica esistente. Le sezioni intere legacy e associative continuano a usare il solo profilo.

Nessun nuovo formato, enum persistito, algoritmo di fit, tolleranza o geometria del kernel. Gli originali CAD non vengono scritti dalle prove.

## Approvazione della modifica alla cronologia

Il controllo automatico ha inizialmente respinto due integrazioni nei percorsi Undo/documentChanged e di rimappatura, indicando rischio di perdita del tracciamento o dei riferimenti. È stato preparato e verificato un prototipo separato, poi spiegato il rischio concreto degli indici che cambiano dopo una rimozione. L'utente ha approvato esplicitamente: «Sì, applica e verifica la pulizia». L'integrazione conservativa è stata applicata dopo questa approvazione. Una richiesta di esecuzione dei test di costruzione ha avuto un timeout del controllo automatico; l'unico retry consentito è riuscito.

## Prove sul file dell'utente

La copia congelata di `SurfaceModellingExample219.prt` contiene 6.216 byte, SHA-256 `9c4a0a5335ac62d7d614f7c257bf4407b01ee2490a2531b69805aadfc4df4705`; metadati nel pacchetto di verifica. I riferimenti passano da **49 a 33**, eliminandone 16 inutilizzati. Tutti i vincoli sono conservati; numero dei tratti dei profili e 20 valutazioni per tratto coincidono esattamente prima/dopo la pulizia. Nessuna funzione viene rimossa. Ricostruzione completa, salvataggio/riapertura con e senza cache e nuova ricostruzione: PASS sulle copie.

## Verifiche

Le prime prove mirate PASS coprono selezione esterna senza falso Undo/dirty, snapshot senza selezioni temporanee, Escape e quota annullata, conferma del disegno, eliminazione dell'ultimo utilizzo, Undo/Redo, vincoli annidati, consumatori loft, conversioni esplicite e codec. Passano anche le regressioni esistenti di riferimenti associativi e visibilità. La fixture storica di promozione delle conversioni ora collega le cache a vere quote: il test continua così a verificare promozione e Undo senza richiedere la sopravvivenza di cache inutilizzate.

Wayland e XCB: costruzione visibile in modifica e assente fuori; il framebuffer esterno coincide con quello ottenuto rimuovendo materialmente la costruzione dalla sola fixture. Passano conversioni, riferimenti invisibili e Undo nelle prove OpenGL. Screenshot ispezionati.

App e runner compilati completamente in Release, incluso l'ultimo aggiornamento delle fixture. Sono stati eseguiti tutti i 170 CTest: risultato consolidato **169 PASS, 1 SKIP OpenGL offscreen, 0 FAIL residui**. Il gruppo generale del kernel PASS (346,72 s). Due fixture richiedevano il vecchio comportamento: la quotatura ora ancora il punto iniziale al centro dell'asse con un vero vincolo Midpoint; il flusso generale verifica che selezione/deselezione non crei Undo e che solo la geometria confermata conservi il riferimento. Le asserzioni geometriche, la quote guidante a 18 mm, gli otto casi di quotatura e Undo/Redo restano verificati. Dopo gli aggiornamenti entrambe le esecuzioni dirette PASS e la riesecuzione CTest delle due fixture PASS 2/2. Il consolidato combina la prima suite completa e questa riesecuzione mirata; non e stata ripetuta inutilmente tutta la suite. Il controllo finale ha inoltre trovato una sorgente multipla delle curve estratte omessa dalla rimappatura della cancellazione esplicita: aggiunta e verificata con una regressione dedicata. Dopo questo ultimo fix sono stati rieseguiti i 17 test pertinenti: 17/17 PASS.

Sei prove native PASS sul runner completo: costruzione con OpenGL, visibilita dei riferimenti con OpenGL e uso/cancellazione/Undo dei riferimenti, ciascuna su Wayland e XCB. Preferenze isolate. I probe aggiuntivi conservano riproduttori per errore della cache orfana e vincolo gia malformato.

Mappa aggiornata: **566 file, 7.824 relazioni, 268 TU**, zero diagnostiche AST; `--check` PASS. Verificati 11 confini e i collegamenti in entrata/uscita di 11 moduli, mantenendo distinte chiamate/include/riferimenti. Due viste HTML renderizzate e ispezionate. Gli artefatti HTML/JSON generati restano ignorati da Git.

Le verifiche sono locali Linux/GCC/Qt6; macOS e una nuova CI GitHub non sono stati eseguiti in questa sessione. Log, riproduttori, screenshot e hash degli artefatti sono nel [pacchetto di verifica](history/schizzi-riferimenti-costruzione-2026-11-03-verifica.zip). Gli originali CAD, i file di preferenze e i binari sono esclusi dal pacchetto.
