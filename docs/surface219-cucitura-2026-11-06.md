# Cucitura Corpo 9/10 di SurfaceModellingExample219 — 6 novembre 2026

## Richiesta e riproduzione

Copia congelata di `/home/myungit/Documenti/Cad_Example/SurfaceModellingExample219.prt`,
SHA256 `bd93ef3191b523304b805c01989f94ad2e326883df22bf60a6113091af03a4df`.
L'originale non viene modificato. Sonde e log in `/tmp/surface219-sew-20261106`.
39 feature: Corpo 9 = Offset 33 (indice32,2facce); Corpo 10 = Cucitura39 (indice38,89facce).
Gli input salvati passano `checkBody` anche con verifiche geometriche complete.
La lettura diagnostica usa il budget cache esplicito4GiB per recuperare gli snapshot;
il budget applicativo1GiB scarta questa cache. Nessuna modifica dei limiti di produzione.
Il ricalcolo completo diagnostico e' stato interrotto; non e' una verifica della rigenerazione integrale.

## Causa e correzione

L'ordine Corpo9/Corpo10 fallisce in `sewnPCurve` / `fitCurve`, con messaggio
«offset: curva a distanza non approssimabile entro il limite di raffinamento».
Il prefisso offset proviene dal fitter condiviso, non da una nuova operazione di offset.
I bordi coincidenti hanno scarto campionato circa1,9e-12mm, ma parametri diversi.
Una SP-curve di Cucitura39 contiene52.882poli lineari; il fit richiesto per convertirla
nel parametro di Offset33 supera32.768campate iniziali, prima di qualsiasi raffinamento.
Nell'ordine inverso la cucitura riesce gia':91facce,1shell,0bordi liberi.

`fk_exchange.cpp`: per ogni gruppo di estremi il bordo rappresentativo conserva le
SP-curve piu costose (somma dei poli; ordine stabile a parita'). Si rimappano le
curve piu semplici, lasciando invariati controlli di coincidenza, tolleranze,
limite di raffinamento e ordine delle facce. Nessuna decimazione delle geometrie.
Il test sintetico in `test_offset.cpp` usa33.000campate e parametri non uniformi:
fallisce prima e passa con la correzione; verifica entrambi gli ordini, pcurve
originale conservata per identita', validita' geometrica e input immutati.

## Stato delle verifiche

Prototipo kernel su geometrie salvate:91facce/1shell/0bordi liberi, controlli completi PASS,
circa7secondi inclusa verifica degli input e del risultato. Regressione sintetica prima FAIL/dopo PASS.
Verifiche finali sulla correzione integrata:

- Build completa `cmake --build forgecad-release --parallel 2`: PASS; applicazione e runner aggiornati.
- Filtri kernel Sew12/12, Offset26/26, Step22/22, Iges9/9, History38/38: PASS.
  Sono107esecuzioni, con possibili sovrapposizioni fra filtri; non una suite generale completa.
- CTest11/11 pertinenti: exchange_robustness,history,tolerance,surface_tools,
  offset_split_crease,surface_instances,solid_trim; UI surface_tools,
  declared_history_cache,function_panels,workflow_ui. I4test UI sono ripetuti
  dopo il completamento della compilazione del runner:4/4PASS.
- GUI nativa Wayland: `createBody` usa gli stessi parametri del pannello,
  Corpo9poi10,tolleranza1e-5mm,Crea un solido. Risultato in29,667s,
  91facce/1shell/0bordi liberi; checkBody completo PASS; entrambe le origini
  dichiarate, input byte-identici, Undo/Redo, riuso cache input e codec con
  snapshot PASS. La lettura QA conserva il budget esplicito4GiB indicato sopra.
- Mappa624file/8822relazioni/303TUs/zero diagnostica, `--check` PASS.
  HTML e dettagli di fk_exchange/test_offset verificati su XCB, con chiamate,
  include e riferimenti distinti e collegamenti in entrambi i versi.
  Il visualizzatore Python/QtWebEngine su Wayland e' andato in crash;
  la GUI CAD nativa Wayland ha completato la prova normalmente.
- SHA256 dell'originale verificato identico al termine; nessun CAD utente modificato.
- `git diff --check` PASS. Commit/push richiesti dall’utente il 6 novembre; revisione finale e allineamento con origin/main verificati.

La scelta evita una ricostruzione inutile della SP-curve: non riduce i poli
salvati dal modello e non risolve in generale i costi del ricalcolo integrale.
Sono state verificate le geometrie salvate e la nuova funzione; la rigenerazione
completa senza cache resta esclusa dalle verifiche di questo intervento.
