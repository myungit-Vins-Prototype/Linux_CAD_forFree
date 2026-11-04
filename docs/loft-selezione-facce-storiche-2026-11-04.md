# Loft: selezione delle facce di appoggio, 4 novembre 2026

## Segnalazione e diagnosi

Il pulsante «Scegli facce adiacenti» si attiva ma il clic acquisisce una faccia sottostante invece della faccia attesa. Caso indicato: Loft 13, F7 adiacente a E25, `SurfaceModellingExample219.prt`.

La copia corrente del documento è congelata in `forgecad-release/loft-guide-face-pick-qa/input-user-current.prt`; l'originale resta intatto. Nel documento salvato Loft 13 è la feature indice 11 (ID 22), con una faccia F0. Prima di essa l'unico corpo superficie è Loft 11 (indice 7). F7 con E25 compare negli stadi Raccordo 24, Raccordo 25 e Svuota 1, successivi a Loft 13. Il nome genealogico di F7 è `f28:side.S0.P0` (ID28: Cucitura 18). Non è lecito usare direttamente questi risultati successivi come riferimenti di Loft 13: il corpo finale dipende dalla loft stessa. La sola posizione successiva di una superficie indipendente, invece, è un limite di ordinamento e non implica di per sé un ciclo.

È stata individuata e riprodotta un'incoerenza effettiva della UI: `requestPreview` ripristina anche operandi di feature successive, mentre `beginReferencePick` consente solo il contesto precedente alla feature modificata. `isBodyVisibleInScene` mostrava le facce ripristinate, anche se `referenceBodyEligible` le escludeva. Una base selezionabile poteva inoltre essere nascosta dalla lista `preview.replaced`. La superficie in anteprima poteva coprire gli appoggi precedenti.

## Correzione implementata

- In scelta riferimenti storici, rendering e picker condividono il contesto precedente alla feature, senza alterare i flag Mostra/Nascondi salvati.
- La superficie in anteprima viene sospesa durante la scelta delle facce storiche, lasciando esposti gli appoggi selezionabili; torna dopo la scelta.
- Il pannello indica esplicitamente il limite storico della scelta G1 e la necessità di riordinare gli appoggi indipendenti.
- Nessuna rimappatura per prossimità, modifica al kernel, ai dati geometrici, al formato o alle tolleranze.

## Appoggio originale di F7 e riordino

F7 di Estrusione 23 e degli stadi successivi usa una NURBS di grado U34/V5, 2177×448 poli. Tutti i poli coincidono esattamente con quelli di F0 della Loft 15 originaria (massimo scarto 0; nessuna riflessione). La guida condivisa fra Loft 13 e Loft 15 è Curva estratta 3D 5. Loft 15 è indipendente da Loft 13, anche considerando i riferimenti attraverso gli schizzi: è valido spostare Loft 13 dopo Loft 15.

La UI impediva però qualsiasi spostamento del creatore di un corpo. Ora consente il riordino se il corpo ha un solo stadio; restano il controllo di tutti i riferimenti, i controlli sugli schizzi e il blocco dei creatori di catene con più stadi. Lo spostamento mantiene il normale Undo/Redo. I flag di trascinamento nell’albero reale seguono lo stesso criterio: creatori autonomi trascinabili, creatori di catene fissi.

Procedura per questo documento: portare Loft 13 dopo Loft 15 e prima di Cucitura 16; modificare Loft 13, scegliere la guida Curva estratta 3D 5 e la tangenza alle facce adiacenti, quindi F0 di Loft 15. La F7 del corpo finale rimane un risultato successivo e non va usata come riferimento di Loft 13. Nessuna correzione per sola vicinanza geometrica viene applicata agli agganci.


## Verifiche

- Il riproduttore prima della correzione mostra base visibile=0/selezionabile=1, stadio futuro visibile=1/selezionabile=0 (fallimento atteso, exit2); dopo la correzione 1/1 e 0/0, PASS.
- Clic reali sul pannello e sulla faccia curva, fuori dall'area coperta dal pannello, PASS su Wayland e XCB. Sul documento originale è disponibile F0 di Loft 11; dopo il riordino il clic sulla vera superficie di appoggio acquisisce F0 di Loft 15 e la associa alla guida Curva estratta 3D 5.
- Il riordino tramite `moveFeatureTo(11,13)` rigenera tutte le 26 funzioni senza errori, incluso Svuota 1 (circa 173s per lo svuotamento). Salvata e riletta con cache accettata la copia `forgecad-release/loft-guide-face-pick-qa/SurfaceModellingExample219-appoggio-prima-loft13.prt`, 88.710.772 byte. La copia conserva i parametri originali della loft: la condizione G1 non è confermata nel documento consegnabile.
- App completa e runner aggiornati; suite finali 11/11 PASS, inclusi clic reali, flag di trascinamento nell'albero reale, riordino, Undo/Redo e rifiuto degli ordini non validi.
- Verifica indipendente della nuova Loft 13 con appoggio G1 su F0 di Loft 15 PASS: `forgeReferencedLoft` riesce, `checkBody` senza errori; lungo la guida condivisa massimo seno fra normali 7,84024e-10 e distanza dalle superfici 1,89646e-10 mm. Questa prova costruisce la singola loft e non certifica i raccordi/svuotamento dopo la conferma della nuova condizione. La rigenerazione completa sopra riguarda il solo riordino, con i parametri geometrici originali.
- Mappa: 574 file/7962 relazioni, 274 TU, zero diagnostiche AST; controllo dei collegamenti entranti/uscenti e rendering di sei dettagli aggiornati PASS.

I test precedenti del pannello iniettavano il callback dei riferimenti e non verificavano la corrispondenza fra visibilità e selezionabilità. La nuova regressione invia eventi del mouse e copre anche riordino, Undo/Redo e rifiuto di catene non valide.

Il primo harness minimale incontrava un crash Qt nell'animazione della geometria della finestra durante il configure Wayland, prima del clic. L'harness definitivo usa una finestra già stabilizzata e animazioni dei dock disabilitate; le prove native successive passano. Nessuna modifica a Qt o alle impostazioni della sessione utente.

Copia di ingresso SHA256: `73581cce2b6716418fa0bce443145d38254a4d41f30cf77452637ffdb55c3907`; artefatti e log locali in `forgecad-release/loft-guide-face-pick-qa/`, esclusi da Git. Originale mai scritto.

