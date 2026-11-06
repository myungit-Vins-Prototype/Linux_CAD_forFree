# Booleane: selezione a rettangolo — 6 novembre 2026

Richiesta: scegliere più corpi con un rettangolo nei pannelli delle booleane.
Implementato per gli strumenti di Unione, Intersezione e Differenza. A resta
una scelta singola. Il rettangolo aggiunge i corpi alle spunte esistenti;
non toglie quelli già scelti. Il clic singolo continua ad aggiungere/togliere.
Sinistra → destra include soltanto corpi interamente dentro il riquadro;
destra → sinistra include anche i corpi attraversati, compreso un riquadro
interno a una faccia senza intersezione con i suoi bordi. Esc termina il
picking mantenendo le scelte; Annulla non modifica la geometria.

Modulo dedicato `forgeCad2026_viewport_body_box.cpp`: selezione sulle mesh
visualizzate, filtro degli operandi disponibili e controllo della posizione
nella storia. Nessun uso delle mesh per la booleana: kernel e dati persistenti
restano invariati. `viewport_interaction` distingue clic/trascinamento;
`viewport_selection` inoltra il gruppo al pannello. `booleanDialog` aggiorna
le spunte una sola volta e mostra le evidenze di A e strumenti. Nessuna
ricostruzione geometrica per ogni corpo durante il trascinamento.

Verifiche completate:

- App `forgecad-release/forgecad` e runner `forgecad_view_tests` ricompilati.
- 9 CTest mirati PASS: boolean_box, boolean_history, fillet_selection_review,
  function_panels, storyboard_panel_selection, offset_face_pick,
  reference_list_highlight, solid_trim, workflow_ui.
- Fixture sintetica a quattro blocchi: rettangolo ripetuto cumulativo,
  finestra/attraversamento, esclusione di A e delle feature successive,
  corpi soppressi, clic singolo, Esc durante il trascinamento, Annulla.
  Conferma dell’unione: volume 32, un solo Undo, Redo e codec conservano
  tutti gli strumenti. Intersezione/Differenza: selezione e Annulla verificati.
- Esecuzione nativa Wayland e XCB PASS, con oggetti reali dell’app e formato
  OpenGL 3.3 Core. Framebuffer ispezionato: rettangolo azzurro ed evidenze
  gialle corrette. La cattura QWidget dell’intera finestra non conserva gli
  overlay GL: per verificarli è stata usata `grabFramebuffer`, senza cambiare
  il rendering dell’app. Artefatti locali ignorati in
  `forgecad-release/boolean-box-qa/`.
- Mappa rigenerata e `--check` PASS: 623 file, 8797 relazioni, 302 unità,
  nessuna diagnostica AST. Controllati collegamenti in entrata e uscita del
  modulo, distinti call/include/reference; HTML renderizzato, panoramica e
  quattro dettagli verificati.
- README italiano/inglese e memoria aggiornati; nessun originale CAD
  modificato, nessun commit/push in questo intervento.

Le evidenze usano il riferimento di visualizzazione al contorno del corpo
(kind 9), come gli altri pannelli; non creano riferimenti associativi nel
modello. La nuova selezione riguarda i corpi interi disponibili nel pannello,
non i singoli solidi interni a un corpo composto. Le prestazioni geometriche
delle booleane restano quelle del kernel esistente.


## Correzione successiva: il trascinamento ruotava la scena

Senza un corpo preselezionato l'apertura attivava il selettore singolo di A,
sebbene la casella mostrasse già il primo corpo. Il rettangolo era attivo
soltanto nel selettore degli strumenti. Inoltre il pulsante attivo conservava
l'etichetta «Strumenti dalla vista»: premerlo disattivava il picking.

L'apertura di una nuova booleana ora attiva sempre gli strumenti usando A
indicato dalla casella; A può essere cambiato separatamente. Il pulsante attivo
mostra «Termina selezione». La regressione usa ora la vera PdfWindow e verifica
anche il caso senza preselezione di A, la camera immutata dopo il trascinamento
e il passaggio esplicito A → strumenti. Le prime prove sopra non coprivano
questa apertura senza preselezione. README italiano e inglese corretti.

Per la successiva correzione del kernel e l'unione dei 129 strumenti reali,
vedere [HoneyRepeat](honeyrepeat-taglio-tra-superfici-2026-11-06.md).

Verifica della correzione: app e runner completi ricompilati, gli stessi 9
CTest ripetuti PASS; `--boolean-box-pick` eseguito anche su Wayland PASS con
PdfWindow reale e senza A preselezionato. Nessun commit o push richiesto.
