# Onion: ripetizione, cucitura, mesh e raccordi — 7 novembre 2026

## Richieste e dati

Calcolare gli spicchi necessari per far combaciare i bordi; correggere la cucitura e gli artefatti dopo la ripetizione;
aggiungere Seleziona tutte/Deseleziona tutte sotto Dalla vista nel pannello Cuci superfici.
Prove su copie congelate, mai scrittura del CAD utente.
Copia iniziale: SHA256 `aff8531c4e560cc145ab16200f96f8fd801e68af527ca189bf9a9254dc432c64`, 20 feature,
Loft3 e ripetizione17. Il file utente e' stato aggiornato esternamente a18 durante l'intervento;
seconda copia congelata: SHA256 `e2aca112d654d55730163fee2ea7948a1c3436b6a693c23961591d9c89f41730`, 21 feature.
Sonde, log e immagini in `/tmp/onion-qa/`; copie revisionabili in `output/`.

## Cause e interventi

- **Conteggio:** lo spicchio e' ampio20°, quindi18 istanze totali, originale compreso. Prima17 produceva un passo
  360/17 e giunti aperti. Il pulsante Calcola dai bordi cerca un intero2..500 e verifica l'intera coppia di curve
  sui Bezier razionali mediante un limite sui coefficienti di Bernstein, entro la risoluzione kernel1e-7mm.
  Le valutazioni puntuali propongono il conteggio; non certificano la coincidenza. Il comando imposta360°/distribuzione,
  conserva esclusioni e verso, senza modificare lo spicchio. Parametri espliciti: premere di nuovo dopo cambi al loft.
  Curve non convertibili o segmentazioni non compatibili possono dare un rifiuto conservativo.
- **Mesh:** `transformBody` azzerava tutte le SP-curve anche nelle pose NURBS che conservanoU/V.
  La reinversione di un bordo collassato sceglie un parametro arbitrario: una copia aveva un contorno UV che
  tornava indietro lungo la punta,1465 triangoli invertiti e circa83,98mm² di area invertita.
  Ora conserva la SP-curve quando il supporto NURBS mantieneU/V e la curva3D mantiene il parametro;
  scala il budget3D della SP-curve insieme alla posa. Gli altri percorsi ricalcolano come prima.
- **Cucitura:** i18 bordi puntuali della cima venivano fusi in un edge con18 fin, violando la topologia:
  `Body::build: l'edge 1 non ha due fin opposte`, anche con tolleranze1e-7/1e-6/1e-5mm.
  Prima di fonderli, `sewModel` elimina dai contorni i soli bordi costanti provati sui poli esatti entro
  32epsilon*scala delle coordinate. Non usa la tolleranza di cucitura per cancellare bordi piccoli distinti.
  I cammini UV singolari restano ricostruiti dai percorsi gia' presenti di tassellazione/massa.
  Risultato atteso:18 facce,36 edge,1 shell e18 bordi liberi alla base. Il fondo e' aperto:
  cucire come superficie, oppure aggiungere un fondo prima di chiedere un solido. Messaggio aggiornato.
- **Pannello:** due pulsanti in colonna sotto Dalla vista, selezione atomica dell'elenco e una sola richiesta
  di anteprima dopo il cambio collettivo; i corpi non vengono alterati dai pulsanti.
- **Cache14:** le copie di lavoro con ripetizioni, pose o cuciture precedenti rigenerano la geometria e i derivati;
  i check-in restano snapshot immutabili. DefinizionePRT66 invariata.

## Verifiche consolidate e stato

- Conteggio integrato: app e runner compilati;8CTest pertinenti PASS; Wayland sul vero Onion:
  pulsante18/20°,21feature, tutti18 i giunti (ultimo-primo incluso) entro1,53e-14mm,
  B-rep/Undo/Redo/codec con e senza cache/GL PASS.
- Prototipo delle due correzioni sulle geometrie Onion:18pose senza inversioni significative, cucitura1shell/18facce/18bordi base,
  controlli B-rep PASS in entrambi gli ordini.
- Regressioni kernel indipendenti da OCCT:10/10 SurfaceInstance PASS, inclusi54 pose/scale,
  identita' delle SP-curve, conservazione budget, mesh orientate, area esatta dopo cucitura,
  due ordini di cucitura e chiusura con fondo in un solido19facce.
- Build finale app/runner/kernel runner PASS.14CTest selezionati PASS (13app+SurfaceInstance), inclusi migrazione cache,
  genealogia, revisioni, pannelli e pulsanti. Sew12/Transform10/Tessellate11/Step22/Iges9/Mass11/History38:113kernel PASS;
  i10SurfaceInstance portano a123 le esecuzioni kernel pertinenti.
- Wayland su Onion18: due pulsanti, cucitura18facce/36edge/1shell/18bordi liberi alla base, controlli B-rep,
  zero inversioni mesh significative, Undo/Redo/codec14/no-cache/rigenerazione/GL PASS.
  Salvate `output/Onion-bordi-combacianti.prt` e `output/Onion-cucito.prt` con identita' indipendenti.
- Mappa631file/8932relazioni/307TUs/zero diagnostica, check e HTML (panoramica+8dettagli) PASS.

## Raccordi alla cuspide: integrati e verificati

Riprodotto sul corpo cucito:18spigoli interni incidenti nella punta; raggio costante0,3mm e variabile0,3→1e-6mm
fallivano per il limite4spigoli. Il raggio0 era impedito dalla validazione kernel, dal minimo UI1e-6mm e dal codecPRT.

- `kernel/fk_blend.cpp`, `fk_blend_surface.cpp/.h`: zero ammesso solo agli estremi di percorsi aperti, almeno una
  stazione positiva; restano invalidi raggi negativi/non finiti, zero interno, legge tutta zero e zero su un ciclo.
  Le catene si fermano agli estremi a zero, anche con piu' di4spigoli incidenti. Sezioni interne regolari forniscono
  il limite unilaterale dei contatti e dei jet, poi il bordo terminale e' imposto esattamente nel vertice originale;
  il fit dei tratti conserva i normali controlli geometrici. Nessun raggio minimo sostitutivo, mesh geometrica o
  aumento delle tolleranze. Condivisione del vertice ammessa solo fra estremi tutti a zero.
- `forgeCad2026_dialogs_fillet.cpp`, `forgeCad2026_fillet_handles.cpp`: tabella e trascinamento raggiungono0
  agli estremi aperti; stazioni interne e cicli mantengono il minimo positivo. Cambio percorso conserva lo zero.
- `cad_document_io.cpp`: lettura/scrittura ammettono zero terminale con una stazione positiva, respingono valori
  interni zero e negativi/non finiti. Il kernel controlla apertura e associazione topologica anche senza cache.
  Nessun cambiamento del layoutPRT66 necessario.
- Regressione permanente `SurfaceInstanceZeroRadiusAtHighValenceCusp`:18raccordi con leggi esplicitamente invertite,
  36facce,1shell, B-rep immutabile sorgente, limite alla punta, raggio in lunghezza d'arco, contatti, tangenza,
  mesh e codecB-rep. `test_fillet_refinements.h`: zero nel pannello, minima distinti, cambio percorso, salvataggio
  con/senza cache e ricostruzione di un raccordo effettivamente terminante a zero.

Prove sul vero Onion (`/tmp/onion-fillet/`):

- Tutti18spigoli, R0,3mm alla base→0mm alla punta,36facce/72edge, controllo B-rep0errori.
  Mesh a deflessione0,03mm:301710triangoli,0facce perse,0inversioni significative.
- Distanza massima della sezione terminale dalla punta5,33e-15mm; errore raggio1,44e-9mm;
  distanza contatto dall'appoggio5,20e-11mm; angolo tangente2,11e-8rad. Tolleranze edge/vertici0, invariate.
  Misure a20/50/80% del parametro dei raccordi e11punti sulle sezioni terminali.
- QA Wayland nel pannello reale: tutti18percorsi, zero vero conservato, conferma36facce,
  Undo/Redo, salvataggio/riapertura con e senza cache, rebuild integrale e OpenGL PASS.
  Copia autonoma `output/Onion-raccordato.prt` (8,31MB), originale mai scritto.
- Prove aggiuntive sul box:12combinazioni (zero iniziale/finale/entrambi, lineare/morbido, verso diretto/inverso) PASS.
  Onion ruotato e traslato, scale0,5/2, leggi lineari/morbide e verso invertito:4/4PASS,
  distanza alla punta<=3,06e-14mm. Zero su un ciclo rifiutato come previsto.
- Build definitiva app/runner/kernel runner PASS;21/21CTest selezionati PASS, incluso salvataggio di zero
  con/senza cache, migrazioni cache, revisioni, pannelli, ripetizioni e strumenti superfici.
  Runner kernel completo filtroBlend:62/62PASS. Sonde box12/12 e Onion trasformato4/4PASS.
- Mappa finale631file/8939relazioni/307TUs,0diagnostica; checkPASS.
  HTML verificato su XCB: panoramica+15dettagli, collegamenti entrata/uscita e distinzione call/include/referencePASS.
  Log conclusivi: `/tmp/onion-fillet/{build-codec,tests-final,kernel-blend,native,measure-polished,transformed,architecture-check,architecture-render}.log`.
  Eseguibile `forgecad-release/forgecad` aggiornato; le sessioni gia' aperte richiedono riavvio manuale.
  Nessun commit o push eseguito.

Per usarlo: riaprire la nuova build; sul corpo cucito selezionare i giunti interni, Raggio variabile, due stazioni
per percorso, raggio desiderato alla base e0alla punta. Il verso puo' cambiare fra percorsi: verificare il marker
prima di assegnare lo zero. La copia fornita contiene gia'18tabelle con verso corretto.
Il raggio positivo nella cuspide multipla resta non gestito: richiederebbe una pezza d'angolo diversa.
Il fondo dell'Onion resta aperto: il risultato e' una lamina raccordata, non un solido chiuso.
