# Ricollegamento dei riferimenti delle quote 2D — 6 novembre 2026

## Richiesta e utilizzo

Ripristinare i collegamenti rotti delle estensioni delle quote usando gli snap della tavola.
Selezionare la quota sul foglio o nell’albero e scegliere **Quote → Ricollega quota…**.
Il comando è disponibile anche nel menu contestuale; le proprietà della quota includono
**Ricollega riferimenti…**. Se uno o più riferimenti non sono risolti, vengono richiesti solo quelli.
Per una quota valida il comando ridefinisce tutti i riferimenti; il menu contestuale permette
anche di scegliere esplicitamente il primo o il secondo.

Avvicinare il mouse alla geometria nella stessa vista: il marcatore e la curva evidenziata mostrano
lo snap corrente. Tab scorre le entità sovrapposte; Ctrl offre il punto medio, Maiusc la curva,
Alt il punto sulla curva. Centri, quadranti, estremi e assi usano gli agganci già disponibili.
Le quote radiali, angolari e su curva richiedono curve compatibili. Per le quote legacy su curva
si sostituisce il bordo che determina gli estremi o la lunghezza.

Dopo l’ultimo riferimento compare l’anteprima nella posizione memorizzata della quota.
**Invio o un altro clic confermano**, Backspace torna al riferimento precedente, Esc annulla
l’intera operazione. La sola lettura disabilita il comando e interrompe un ricollegamento in corso.

## Implementazione

- `cad_drawing_dimension_repair.h`: transazione temporanea, scelta dei riferimenti non risolti o
  singoli, sostituzioni senza mutazioni del documento, mantenimento degli agganci validi e delle quote legacy.
- `cad_drawing_editor.cpp/.h`: comando pubblico `beginDimensionRepair`, selezione con gli snap esistenti,
  vista vincolata, indicatori numerati, controllo della misura completa, anteprima con le tolleranze originali,
  conferma attraverso `updateDimension` e cancellazione dello stato transitorio.
- `cad_drawing_window.cpp`: comando nel menu Quote; `CMakeLists.txt`: modulo e runner dedicato.
- `tests/test_drawing_dimension_repair.cpp`: input mouse/tastiera e contratto di persistenza/storia.
- `tools/generate_architecture.py`: descrizioni del modulo e delle regressioni; HTML/JSON restano generati locali.

Gli agganci sono riferimenti topologici/semantici esatti scelti dall’utente. Nessun riaggancio
automatico per prossimità e nessuna misura ricavata dai pixel. Non cambia lo schema persistito:
restano invariati UUID, tipo, decimali, settore angolare, posizione memorizzata, presentazione,
tolleranze, prefisso/suffisso e assegnazioni dei badge. Un solo Undo ripristina anche lo stato
con i riferimenti rotti; nessuna modifica prima della conferma.

## Verifiche

Runner dedicato offscreen PASS sul codice corretto: aggancio singolo/doppio, snap e Tab/Ctrl, vista
vincolata, proprietà, annullamento parziale/completo, Backspace e conferma mouse/Invio; ID, tolleranze,
settore, posizione e badge, Undo/Redo byte per byte, codec e genealogia dopo variazione del modello;
quote radiali, angolari, ordinate e legacy, errori geometrici e sola lettura. Log:
`/tmp/dimension-repair-tests-final.log`. La stessa suite passa in sessione nativa Wayland
(`/tmp/dimension-repair-wayland.log`); anteprima `/tmp/dimension-repair-wayland.png` ispezionata:
estensioni agganciate ai punti numerati, tolleranze leggibili, selezione conservata e istruzioni di conferma.
Build completa finale di `forgecad`, `forgecad_view_tests`, runner dedicato e runner 2D interessati PASS.
**18/18 CTest PASS in 29,15 s** (`/tmp/dimension-repair-regressions-final.log`): comprende quote GUI e
geometriche, editor, scala, storia/layout, presentazione, assi, pagina, focus, controlli UI,
viste/proiezioni, file proiettati, revisioni, rilascio/ripristino e multifoglio. Build:
`/tmp/dimension-repair-app-build-final.log`, runner dedicato `/tmp/dimension-repair-test-build-final.log`.
Nessun warning nel nuovo test; restano warning preesistenti nell’editor su istruzioni sulla stessa riga.
Eseguibile abituale Linux aggiornato: `forgecad-release/forgecad`; l’app già aperta richiede riavvio.
Mappa rigenerata sui sorgenti definitivi: **628 file, 8.882 relazioni, 306 unità AST, zero errori**.
La seconda generazione riusa 304 unità e rianalizza editor e nuovo test dopo le correzioni;
`--check` PASS (`/tmp/dimension-repair-architecture-final.log`, `-check.log`). Verificate in entrambi
i versi le chiamate editor → transazione → valutazione quote e gli include dei test; conteggi righe coerenti.
HTML eseguito in Qt WebEngine: tre pannelli PASS, modulo/editor ispezionati visivamente
(`/tmp/dimension-repair-architecture-html.log`, `/tmp/dimension-repair-architecture-0.png` e `-1.png`).
HTML/JSON generati restano ignorati da Git. Anche `git diff --check` PASS.
I collegamenti desktop `forgecad.desktop` e `forgecad-2.desktop` puntano all’eseguibile compilato sopra.

Il primo controllo ha corretto la fixture di bordo eliminato: una feature estranea
alla vista produceva un documento non valido. Una regressione sulla posizione dell’anteprima
con tolleranze sovrapposte ha rilevato l’ordine errato fra font e layout dei testi; corretto
usando lo stesso ordine del renderer definitivo. La fixture della retta è stata corretta per usare
gli estremi della curva analitica: i campi `lineFirst/lineSecond` sono destinati alle proiezioni lineari.

Limiti: verifiche su fixture sintetiche Linux, senza nuovi collaudi macOS/Windows o su file CAD utente.
R profilo e assi associati riusano i risolutori esistenti senza nuove fixture di riparazione dedicate.

Non sono stati modificati documenti CAD originali.

Nel seguito l’utente ha richiesto commit e push su `main`. Prima del commit, `git fetch origin`
ha confermato `HEAD` e `origin/main` allineati, senza commit locali precedenti da pubblicare.
Controllo architettura e `git diff --check` nuovamente PASS; i 18 test restano quelli eseguiti sul codice invariato.

Pubblicazione completata: commit `56ae114` — **Ripristina i riferimenti delle quote 2D con snap** —
push su `origin/main` riuscito, da `93dff18` a `56ae114`. HEAD e riferimento remoto verificati coincidenti,
checkout pulito dopo il commit. Questo seguito aggiorna soltanto la registrazione della pubblicazione.
