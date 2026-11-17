Provider tavola legacy — staging congelato, 16/11/2026

Baseline HEAD47c7c19c4faf490995e8ff64f10d83d8a6abb077. Nessuna repoedit,
mutazione Git, commit/push, build o esecuzione app/test da questo intervento.
Letti AGENTS, CLAUDE, PLAN legacy e mappa778/11373 prima del codice.

Patch minima in3file: cad_drawing_editor.h/.cpp e cad_drawing_window.cpp.
Rimossi setter setModelProvider, soli membri Impl::provider/updateAction,
creazione/callback/disabilitazione della action, aggiornamento della action in
setReadOnly, entry drawingUpdateModelAction dalla tabella icons e findChild
nel contenitore col commento del vecchio hide. Nessun null-check di ripiego:
la tabella non riferisce piu l'action che non viene creata. Gli altri comandi
e il loop icone non cambiano. Nessuna pulizia include/risorse aggiuntiva.

Preservati byte-identici updateModel, installPreparedModel, updateFromSource,
refreshEditAccess (hash funzioni in preservation.json). In setReadOnly cambia
solo il setEnabled del comando morto; guard computing/versione archiviata,
cancellazione input/drag, liste di azioni e mobilita restano identici.
Le righe drawingReloadSourceAction e DrawingWindow::updateAction_ restano
identiche: e il comando vivo Aggiorna dal .prt salvato. Identita/impronta,
fonti salvate e percorsi di ricaricamento non vengono modificati.

Ricerca prima: setter soltanto dichiarazione/definizione; action presente in
creazione, tabella icone e hide del contenitore. Nessun chiamante del setter
nel grafo AST. Ricerca successiva su overlay virtuale di772file tracciati
sorgente/UI/build: zero occorrenze di setter/action e zero memberuse residui
nell'editor. Repository fisico resta intatto; docs storiche escluse dal conteggio.

before/ e after/ contengono i tre file completi. Patch e manifest collegano
hash prima/dopo; prepare_staging.py.txt e sorgente inerte riproducibile sul
baseline pulito. graph-focus-before.json contiene descrizioni e collegamenti
nei due versi, distinti call/reference; graph-expected-delta.json e solo delta
atteso, NON grafo rigenerato. La callback morta perde anche3sites AST interni:
factory action, Impl::report e updateModel. Root rigenera/verifica HTML/JSON
all'integrazione e aggiorna la memoria condivisa.

Limiti: validazione statica e confronto esatto dei corpi conservati; nessun
binario aggiornato da questo staging. Analisi closed-world dei sorgenti, non
promessa di assenza consumer esterni; gli header non sono installati comeSDK,
ma la rimozione applicativa e autorizzata esplicitamente. Non e un nuovo bug
che incrementa il conteggio review. Nessun test speculare per API eliminate.

Verifiche esistenti affidate al coordinatore dopo integrazione: build app
completa, drawing_editor, drawing_layout_history, linked_drawing e
 drawing_multisheet; costruzione dell'editor verifica anche la coerenza icons.
Nessuno di questi esercita direttamente il setter orfano: proteggono i
contratti preservati. Nessuna verifica dichiarata PASS senza eseguirla.

Pacchetto solo testo UTF8/sorgenti/JSON/patch e SHA256SUMS; niente binari/CAD.
