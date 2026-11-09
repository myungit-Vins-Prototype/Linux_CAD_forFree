# Poli NURBS su più facce — 9 novembre 2026

Richiesta: pubblicare il lavoro già verificato e poi estendere Modifica poli NURBS alle facce multiple.
Pubblicati su `origin/main`: checkpoint iniziale `ddf856a`, estensione multifaccia `b6c10df`. Le modifiche preesistenti a STL/OBJ e memoria sono state mantenute separate.

## Comportamento implementato

Il corpo sorgente può essere una lamina cucita o un solido con facce NURBS. La lista mostra le facce disponibili; **Tutte**, **Nessuna** e **Scegli facce nella vista** consentono la selezione cumulativa. Un secondo clic toglie una faccia. Le facce escluse restano nel risultato e gli spostamenti si conservano quando una faccia viene tolta e reinserita durante la sessione.

Tutte le gabbie selezionate compaiono contemporaneamente. La tabella virtuale aggiunge la colonna **Faccia** e conserva soltanto le celle modificate; Ctrl/Maiusc e selezione in tabella permettono di muovere insieme poli di facce diverse. Gli assi locali seguono la faccia del polo attivo; Esc ripristina l'intero gruppo. La sola gabbia di display viene campionata, con un budget complessivo di circa4096 poli più i selezionati; i controlli geometrici reali restano accessibili in tabella.

Gli spinner U/V (4–64) sono comuni alle facce selezionate. Il pannello riporta conteggi e scostamento iniziale per faccia e il massimo complessivo; il limite ammesso viene rispettato su ogni faccia. Cambiare conteggi azzera gli spostamenti, come indicato nel pannello.

La modifica geometrica è simultanea: ogni bordo interessato viene ricostruito una volta e controllato sulle due facce adiacenti, comprese quelle escluse. Topologia, rifilature UV, facce escluse e stato lamina/solido vengono conservati. Un giunto che si apre viene rifiutato con un messaggio che richiede lo spostamento dei poli corrispondenti delle facce adiacenti. Le tolleranze originali vengono rispettate senza allargarle per nascondere incompatibilità.

Il codec opzionale schema11 conserva riferimenti espliciti, basi NURBS e modifiche separate per faccia. I documenti precedenti a singola faccia continuano a funzionare. Anche la ricostruzione pura senza spostamenti viene salvata correttamente. Il limite è256 facce selezionate e4096 poli modificati complessivi per funzione.

## Hook

Usata la copia congelata `/tmp/forgecad-poles-Hook-input.prt`, senza salvare l'originale. Su **Cucitura8** sono state selezionate due facce da193×1483 poli ciascuna: **572438 righe virtuali**, **3680 poli di gabbia prima delle selezioni aggiunte**. Modifica di un polo interno per faccia, risultato B-rep, immutabilità della sorgente, Undo/Redo e codec PASS nella sonda applicativa.

La prima sonda ha rivelato un rallentamento: i nomi storici condivisi avviavano una ricerca geometrica costosa sui supporti molto densi. Il comando usa ora il nome univoco oppure l'identità esplicita nello stesso stadio, con verifica della base; non riassocia alla faccia più vicina. Le sonde iniziali scadute non sono incluse negli esiti positivi.

## Verifiche

**App completa e runner ricompilati; 7/7 CTest PASS sui sorgenti finali**: `forgecad_surface_poles_multiple_tests`, `forgecad_surface_poles_large_tests`, `forgecad_surface_tools_tests`, `forgecad_function_panels_tests`, `forgekernel_surface_tools_tests`, `forgecad_document_compression_tests`, `forgecad_feature_dependencies_tests` (11,54s totali). Prova Wayland del gruppo tra facce ed Esc PASS; corpo7 Hook: griglia193×1483, spinner automatici8×48, scarto0,0848055mm, modifica, B-rep, Undo/Redo, codec e riapertura PASS. Per caricare il nuovo eseguibile occorre riaprire l'app.

L'ultimo controllo su Hook ha intercettato una regressione introdotta nella generalizzazione: la chiusura dei vertici applicava soltanto il fit1e-6mm, ignorando tolleranze locali già presenti. Gli scarti1,49569e-6 e2,70738e-6mm rientrano rispettivamente nelle tolleranze originali1,51314e-6 e2,74017e-6mm. Il controllo usa ora il massimo dei budget di connessione già dichiarati, senza aumentarli; una regressione kernel verifica vertici/edge tolleranti e budget invariati. Sonda kernel8×48: ricostruzione pura e modifica, entrambe con0issue B-rep. Le sonde precedenti fallite/scadute sono escluse dagli esiti positivi.

Architettura finale: **662file, 9482relazioni, 324unità AST, zero diagnostiche**. `--check` PASS; sei moduli controllati in entrata/uscita, quattro confini di chiamata e righe confrontate con i sorgenti finali. Overview e dettagli di sessione, kernel poli e pannello renderizzati e ispezionati; gabbie ad alto contrasto e pannello multifaccia ispezionati in Wayland. Log e immagini in `/tmp/forgecad-poles-multiface/`.

SHA256 dell'originale e della copia dopo le prove: `13883a7873687759cc18c108ba3d0786117d4d07f848a58961284cf086c96f64`.

La suite multifaccia comprende selezione da lista e callback di picking, tre gabbie iniziali e due selezionate, conservazione degli spostamenti dopo deselezione, gruppo su facce diverse, una funzione/Undo, codec e record duplicato rifiutato, riapertura e rigenerazione senza cache. La ricostruzione pura6×8 su due facce viene confermata e salvata senza spostamenti manuali. Le prove kernel includono due facce connesse, spostamento coerente del giunto, spostamento incoerente rifiutato, faccia esclusa invariata e due facce di un solido con le altre analitiche conservate.

## Limiti

Le facce devono appartenere allo stesso corpo sorgente. La ricostruzione indipendente può produrre bordi incompatibili tra pezze curve: viene rifiutata se non rispetta i giunti, anche quando lo scarto iniziale di ciascuna faccia è ammesso. Non si promette una ricostruzione vincolata G1/G2 tra pezze.

Scarto, regolarità e confronto delle SP-curve sono controlli campionati: non sono una certificazione globale delle distanze, della continuità o dell'assenza di auto-intersezioni. Gli spostamenti intenzionali dei poli non fanno parte dello scarto iniziale del refit. La rigenerazione senza cache è verificata sulla fixture; non su tutta la storia Hook. Nessun originale CAD modificato e nessun processo utente chiuso.

## Moduli

`cad_surface_poles_session.*` gestisce stato/cache/raccolta delle modifiche; `cad_surface_poles_model.*` gestisce la tabella e le gabbie multifaccia; `kernel/fk_surface_poles.cpp` applica la modifica simultanea preservando le connessioni. Aggiornati codec/coordinamento, parametri, chiavi di anteprima, pannello, viewport, test, CMake, README e catalogo dell'architettura. HTML/JSON della mappa restano generati e ignorati da Git.

## Interazione mouse e riquadro — seguito

Richiesta successiva: un rilascio del mouse perso, soprattutto dopo la spunta di approssimazione, sospendeva gli aggiornamenti dell'anteprima; aggiungere la selezione rettangolare.

Il precedente gestore ascoltava soltanto gli eventi della vista e chiudeva il trascinamento soltanto su rilascio/Esc ricevuti dalla vista. La perdita di cattura non azzerava lo stato; il cambio U/V restava rinviato dal controllo di trascinamento attivo. Il nuovo modulo `cad_surface_poles_interaction.*` ascolta il ciclo di interazione nell'applicazione: rilascio anche su un altro widget, movimento senza pulsante, perdita della cattura e disattivazione. Cattura e stato vengono azzerati prima di richiamare i callback; la perdita di cattura annulla lo spostamento e riattiva l'anteprima. Il caricamento della griglia interrompe e annulla l'interazione prima di sostituire il modello, evitando rinvii indefiniti dopo il cambio di approssimazione.

Trascinando dallo spazio vuoto si selezionano con un riquadro i poli visualizzati, anche su più facce: selezione normale sostitutiva, Maiusc additiva, Ctrl/Cmd alternata; Esc conserva la selezione iniziale. Il limite resta4096 poli selezionati per funzione. Il riquadro segue la gabbia visualizzata: per griglie campionate gli altri poli restano selezionabili dalla tabella.

Verifiche finali: **app completa e runner compilati, 7/7 CTest PASS** (11,79s), prova multifaccia Wayland/OpenGL PASS. Riquadro su32nodi/due facce, Ctrl/Maiusc/Esc, spunta approssimazione durante cattura residua, rilascio su checkbox dopo refit, UngrabMouse con ripristino, movimento senza pulsante e ritorno alla griglia originale PASS. Il test controlla che il rilascio su un altro widget faccia avanzare la generazione dell'anteprima. Anche il workflow completo su corpo7 Hook (griglia grande, refit8×48, modifica, Undo/Redo, codec e riapertura) PASS con il gestore definitivo. Sonda su due facce di Hook e ispezione del riquadro attivo in finestra reale PASS; sorgente invariata.

Mappa finale del seguito: **664file, 9496relazioni, 325TU, zero diagnostiche**, `--check` PASS. Sette moduli verificati in entrata/uscita, sei confini di chiamata e righe dei sorgenti confermati. Quattro viste HTML rigenerate e ispezionate, incluso il nuovo gestore di interazione. Log finali: `/tmp/forgecad-poles-multiface/ctest-mouse-validated.log`, `mouse-Wayland-validated.log`, `Hook7-mouse-validated.log`, `Hook-box.log`; immagine `Hook-box.png`.

Correzione del mouse e selezione rettangolare pubblicate su `origin/main` nel commit `9f455a9`.


## Rotazione e scostamento vincolante — correzione successiva

L'utente ha confermato che lo scarto deve impedire la conferma e che occorre aumentare i poli per rispettarlo. Lo screenshot riportava0,185802mm rispetto a0,001mm. Individuate due cause distinte: il riquadro intercettava tutti i clic sullo sfondo, impedendo la normale rotazione; dopo il rilascio, il rifiuto per scarto lasciava il vecchio messaggio di attesa del mouse. Il filtro globale da solo non correggeva questo messaggio.

Implementati e verificati: il trascinamento sullo sfondo ruota la vista anche con selezione multipla; il riquadro richiede Alt oppure il pulsante esplicito. Rilascio non-client, nuovo clic e controllo dello stato globale Qt del pulsante durante gesti nativi recuperano catture residue. Il fallimento della validazione aggiorna anche lo stato dell'anteprima.

Esteso il refit a4–256poli per direzione; il pulsante **Adatta i poli allo scostamento** cerca U/V in un worker cancellabile, con consegna GUI soltanto per la richiesta corrente. Prova separatamente l'incremento di U e V, scegliendo il miglioramento dello scarto peggiore delle facce. Avvio automatico se necessario quando si abilita l'approssimazione o cambia il limite; modifiche manuali, cambio facce/sorgente e chiusura invalidano il risultato in corso. Il limite non viene aumentato. Codec12 soltanto per conteggi oltre64; lettura degli schemi precedenti invariata.

Sonda preliminare kernel sul corpo7 originale congelato:64×64 produce0,0277352mm,32×192 produce0,000306304mm. Nel secondo caso sia refit sia modifica interna+.01mm passano il controllo B-rep. Il limite precedente64×64 rendeva quindi impossibile raggiungere0,001mm. Il pannello sullo stesso corpo7, partendo da8×48 e limite0,001mm, trova18×162 con scarto campionato0,00070033mm: modifica interna+.01mm, conferma, B-rep, Undo/Redo, codec12 e riapertura PASS. Da16×20 la ricerca trova16×150 con scarto0,000910393mm; un limite1e-7mm irraggiungibile restituisce256×256 con0,000154881mm e non autorizza la conferma.

App completa e runner compilati;7/7CTest PASS in11,50s. Wayland/OpenGL: multifaccia con rotazione e selezione conservata, Alt/riquadro, rilascio, gruppo, applicazione e fixture senza cache PASS; workflow completo sul corpo7 Hook PASS. Una sonda con QtTest attraverso QWindow/QPA (eventi simulati del sistema finestre, non un gesto manuale sul dispositivo) verifica rotazione con più poli, rilascio deliberatamente soppresso da un filtro dopo l'aggiornamento dello stato Qt, recupero dal timer e spunta di approssimazione durante la ricerca. Heartbeat GUI attivo durante il calcolo; cancellazione senza consegna tardiva;2400poli ancora visibili e selezionabili dopo ripartenza PASS. Il controllo visivo della gabbia usa il framebuffer OpenGL oltre alla cattura della finestra. Due errori nei nuovi test grafici sono stati corretti prima di questi esiti: un cast a QLabel su FunctionMessage e un punto per la rotazione che poteva colpire il gizmo del gruppo.

Log e catture: `/tmp/forgecad-poles-recovery/ctest-final.log`, `multiple-final.log`, `hook-final.log`, `native-final2.log`, `Hook-frame.png`. Hash dell'originale confermato invariato. Worker allineato alla precisione numerica del documento; app e runner ricompilati completamente. Verifiche ripetute sui sorgenti definitivi: **7/7CTest PASS (12,22s)**, multifaccia Wayland, Hook7 con adattamento/modifica/Undo/codec/riapertura e sonda QPA PASS. Log definitivi: `ctest-validated.log`, `multiple-validated.log`, `hook-validated.log`, `native-validated.log` nella stessa directory.

Mappa finale: **664file,9509relazioni,325TU,zero diagnostiche**; `--check` PASS. Otto moduli controllati in entrata/uscita e sette confini di chiamata, distinti da include/riferimenti; conteggi delle righe confrontati con i sorgenti. Cinque viste HTML renderizzate e ispezionate: overview, interazione, sessione, pannello e refit kernel. HTML/JSON generati restano ignorati. Per caricare l'app aggiornata occorre riaprirla. Commit/push del seguito in preparazione. Restano i vincoli dei giunti tra facce curve e il carattere campionato dello scarto, come sopra.
