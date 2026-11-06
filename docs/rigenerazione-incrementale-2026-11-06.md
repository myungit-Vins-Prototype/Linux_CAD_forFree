# Rigenerazione incrementale della storia — 6 novembre 2026

Richiesta: eliminazione e modifica di una funzione dalla storyboard devono
conservare i risultati precedenti validi e aggiornare solo i consumatori.

Cause individuate: deleteFeatures chiamava regenerateAll dopo removeBodies;
removeBodies aggiornava indiscriminatamente tutti i riferimenti degli schizzi;
regenerateAfter faceva lo stesso quando cambiava un datum. La soppressione di
un gruppo usava anch'essa la rigenerazione completa.

Implementazione iniziale: conservare gli ID dei consumatori prima di ricollegare
le catene ed eliminare/rimappare gli indici; usarli come semi espliciti del grafo
di invalidazione. I riferimenti rimasti senza sorgente vengono diagnosticati;
nessun riaggancio per vicinanza. Aggiornamenti dei datum limitati ai riferimenti
che usano quel datum e alla cascata di schizzi dipendenti. Le verifiche e la
compilazione completa sono ancora in corso; nessun miglioramento temporale
e' ancora attestato su KnifeHandle.


## Prime verifiche

App completa e runner compilati. Sulla copia KnifeHandle151/131copie, Wayland:
eliminazione ultima deformazione1ms con150risultati precedenti identici;
conferma senza modifiche1ms/151risultati identici; inversione normale Piano5
senza consumatori5ms/151B-rep identici; soppressione del gruppo riempimento2ms,
eliminazione3ms, prefisso0-17 conservato e consumatore mancante diagnosticato.
Undo/Redo ripristinano le istantanee. Non e' misurato un fattore di accelerazione
rispetto al vecchio algoritmo: e' verificata l'assenza di ricostruzione dei B-rep.
Log /tmp/forgecad-incremental-native.log; originale non scritto dalle prove.

Prima suite27/29PASS. Due problemi delle verifiche: la normalizzazione puo'
clonare il contenitore immutabile di un riferimento senza cambiare la sorgente
ne' rigenerare geometria (sonda: base e ramo indipendente identici); il test
ora controlla l'identita' semantica del riferimento oltre ai puntatori B-rep.
La vecchia asserzione della cache richiedeva versione12, mentre il writer usa
13 da un intervento precedente: aggiornata la sola aspettativa della prova,
lasciando invariata la copertura delle fixture legacy e dei dati corrotti.


## Esito finale

- App completa forgecad-release/forgecad e runner ricompilati; riaprire l'app
  per caricare la nuova build. Nessuna app utente chiusa durante le prove.
- 29/29CTest PASS,8,03s: storyboard, soppressione, datum, posizione storica,
  genealogia/cache, booleane/raccordi, estrusione/rivoluzione, riferimenti
  collegati, Undo/Redo, pattern e nuove mappature. Log finale
  /tmp/forgecad-incremental-final-tests.log.
- Prove nuove: predecessori e rami successivi indipendenti conservano gli
  stessi puntatori B-rep; la catena ricollegata dopo eliminazione produce i
  nuovi volumi corretti; conferma invariata senza ricostruzione; datum aggiorna
  il proprio schizzo e consumatore mantenendo il ramo indipendente. Undo/Redo PASS.
- Wayland sul KnifeHandle congelato151feature: prove descritte sopra PASS.
  Tempi1-5ms della mutazione del modello e pubblicazione dello stato; il
  rendering asincrono successivo non e' un benchmark end-to-end dell'interfaccia.
  Sono conservati150risultati eliminando l'ultima deformazione e il prefisso
  di18feature sopprimendo/eliminando il gruppo131copie. Nessun CAD originale scritto.
- Mappa613file/8625relazioni/296TU/zero diagnostiche; controllo aggiornamento
  PASS, overview e13dettagli HTML renderizzati, modulo documento ispezionato.
  Verificate chiamate, include e assenza di reference negli archi interessati,
  in entrambi i versi. HTML/JSON locali ignorati da Git.
- git diff --check PASS. Nessun commit, push o CI remota in questo intervento.

La correzione evita calcoli geometrici di stadi non invalidati. Non elimina il
costo intrinseco delle funzioni modificate o realmente dipendenti, della loro
tessellazione o del disegno. Rigenerazione completa esplicita, apertura con
cache e percorsi di riordino/inserimento conservano le rispettive politiche;
non e' dichiarata un'accelerazione globale di tutti i comandi.
