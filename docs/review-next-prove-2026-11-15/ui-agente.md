# UI2/UI3/UI4/UI6 — staging, 15 novembre 2026

Artefatti: `fix.patch`, snapshot originali/hash, due sorgenti finali,
`tests.snippet.h`, harness `repro.cpp`, `build.py`, `build.log`, `repro.log`.
Nessuna modifica al repository. Snapshot successivo alle correzioni UI1/UI5
parent, preservate. AGENTS/CLAUDE/review e grafo descrizioni/archi consultati.

## Correzioni

- UI2: alternativa circoscritta autorizzata dal parent. La guardia centrale
  runModeless consente **un comando modeless alla volta fra documenti**.
  QPointer di workspace/dialogo impediscono un secondo loop cross-document.
  Il secondo pannello, anche già costruito con preview/picker, viene
  annullato: picker/callback/marks e preview vengono cancellati prima del
  ritorno, finished viene emesso, messaggio nella statusbar del secondo
  workspace. Il primo comando resta attivo e la consultazione/cambio scheda
  resta possibile. Terminato il primo, il secondo workspace può aprire il
  comando. Annidamento nello stesso workspace preservato con ripristino
  RAII dello stato precedente.
- Non introdotto unlock su finished a stack sospeso: ciò lascerebbe i vecchi
  distruttori/cleanup post-run liberi di cancellare risorse di un nuovo
  comando nello stesso viewport. La restrizione evita questa condizione
  senza migrare tutti i chiamanti a lifecycle asincrono.
- UI3: fillPatternDialog e surfaceMappingDialog hanno cleanup idempotente
  collegato a finished e usato al ritorno: prima stop/cancelReferencePick,
  poi reset callback e marks. Non dipende da BodyPicker.active().
- UI4: SurfaceEndControls annulla il picker quando cambia il modo/ruolo
  attivo, rimuove la callback obsoleta e azzera i pulsanti checked. Il
  pulsante può riavviare esplicitamente la nuova scelta. Dopo sync, rearm
  controlla ancora active_>=0. Nessuna modifica di parametri geometrici.
- UI6: selezioni multiple e current sono salvati separatamente, riassociati
  per identità dopo refresh/reorder e ripristinati con NoUpdate per current.
  Highlight riceve tutti i riferimenti selezionati sopravvissuti. Le
  facce/edge con indice topologico conservano l'identità anche se cambia
  il punto di appoggio; punti liberi richiedono ancora le coordinate.

## Verifiche effettive

Compilati solo common.cpp/mapping.cpp staged e harness, ricollegati agli
object correnti di forgecad_view_tests. Build PASS. Harness con impostazioni
isolate, Qt offscreen/Fusion: **34 verifiche PASS, failures=0**, exit0:

- G1facce→G0, piano→facce, riapertura picker nuova modalità;
- multiselezione invariata, reorder, current non selezionato separato,
  riferimento rimosso, punto aggiornato della stessa faccia;
- fillPatternDialog reale, picker attivo e Annulla;
- surfaceMappingDialog reale in mapping e sviluppo, cancelPicker osservato
  sincronicamente dentro finished, poi callback/lock ripuliti;
- due workspace: secondo runner rifiutato con picker e preview già avviati,
  previewPending=false/picker=false/nessun lock del secondo; primo attivo;
  messaggio statusbar; chiusura primo e apertura secondo successiva;
- DocumentTabsWindow reale: cambio scheda, rifiuto secondo runner,
  chiusura primo con interactionLocked=false;
- annidamento nello stesso workspace: lock esterno preservato dopo inner,
  poi ripristinato dopo outer.

`tests.snippet.h` fornisce helper da integrare e invocare nella suite
functionPanelsReview; il relativo corpo corrisponde al harness verificato.
Nessuna build app completa/CTest, verifica desktop/OpenGL/Wayland/macOS o
certificazione generale leak/thread. previewPending=false prova la
cancellazione logica della richiesta; non misura l'istante di uscita fisica
del worker già avviato. Semantica apply/hide non modificata; test esistenti
apply del parent restano da rieseguire nel runner integrato.
