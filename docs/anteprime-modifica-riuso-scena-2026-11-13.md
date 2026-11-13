# Riapertura editor: riuso della scena — 13 novembre 2026

Richiesta: tutte le funzioni in modifica devono riutilizzare il risultato già presente, senza ricalcolare l'anteprima all'apertura. Grafico architetturale consultato prima degli interventi per seguire pannelli → PreviewScope → requestPreview → worker/builder e aggiornamento del documento.

## Cause e implementazione

- Il pannello della ripetizione richiede l'aggregato (`patternComponent=-1`), mentre la madre conserva una sola componente (`0`) e le altre sono già nella scena come risultati derivati. Il confronto precedente considerava diverse le definizioni e avviava la ricostruzione.
- Anche quando il B-rep veniva riutilizzato, il percorso precedente avviava sempre il worker per preparare la visualizzazione, estrarre patch e generare U/V. Ora `requestPreview` adotta immediatamente i buffer già disponibili; per le ripetizioni conserva separati i buffer delle istanze, con relativo disegno, senza costruire aggregati o fondere mesh.
- L'anteprima invariata mostra il risultato completo esistente in ambra. Le anteprime dopo modifiche effettive mantengono il comportamento precedente, incluse patch locali. Gli operandi consumati vengono nascosti per non sovrapporli al risultato completo.
- Se manca una visualizzazione, oppure cambia la qualità, il worker prepara soltanto la mesh dal risultato già calcolato. Non ricostruisce B-rep o curve. La conferma invariata conserva tutti i risultati runtime, anche curve, piani e componenti delle ripetizioni.
- I campi numerici arrotondavano il valore interno alle cifre visibili, producendo falsi cambiamenti. `ExpressionSpinBox` conserva ora la precisione double separatamente dalla presentazione; Invio/focus senza digitazione conserva il valore. Campi percentuali loft e coordinate della scala usano lo stesso controllo.
- Anche curve e piani riconoscono le definizioni invariate. Lo schizzo 3D non cancella l'anteprima pronta entrando nell'inserimento punti.

File principali: `forgeCad2026_viewport_preview.cpp/.h`, `forgeCad2026_feature_preview.cpp/.h`, `forgeCad2026_viewport_document.cpp`, `forgeCad2026_viewport_render.cpp`, `forgeCad2026_viewport_sketch.cpp`, `cad_expression.cpp/.h` e pannelli solidi/superfici/comuni. Regressioni in `tests/test_edit_preview_reuse.h`, `tests/test_expression.cpp` e aspettative aggiornate in `tests/test_viewport.cpp`.

## Verifiche finali

- Applicazione Linux completa e runner compilati dai sorgenti finali. Build completa prima della consegna, inclusa la conservazione dei valori molto piccoli. L'app già aperta deve essere riavviata per usare il nuovo eseguibile.
- **23/23 CTest PASS**: riuso in modifica, prestazioni/cache anteprime, stadi nascosti, precisione/parser e unità, variazioni/pannello, maniglie, pannelli comuni, selezione/rifinitura/raccordi avanzati, storia raccordi/estrusione/rivoluzione, riapertura raccordi, curve/schizzo3D, superfici/mappature, cache estrusione a limite, documenti e regressioni generali.
- Nuova regressione `forgecad_edit_preview_reuse_tests`: ripetute aperture senza timer/worker e con buffer identici per primitiva, raccordo, estrusione, rivoluzione, scala, superficie, curva3D; piano esatto riusato. Ripetizioni di corpi e funzioni, esclusioni e tutte le componenti. Un cambiamento effettivo avvia il worker; ritornare all'originale riusa subito la scena e il worker superato non la sostituisce. Provato il percorso reale Modifica e Annulla tramite `PdfWindow`.
- Precisione numerica: valore double completo conservato dopo impostazione, focus/Invio, cambio decimali; modifiche reali e frecce funzionanti. Locale C e italiano. Quota positiva inferiore alle cifre visibili conservata, invece di essere portata al minimo.
- **Wayland/OpenGL PASS** sulle due copie della girante: modello corrente con ripetizione di funzione e precedente con tre corpi separati. Pannelli rivoluzione/estrusione/ripetizione: zero worker, stessi B-rep. Framebuffer controllato, tutte le istanze visibili; rimuovere temporaneamente le visualizzazioni aggiuntive cambia l'immagine nel caso multirisultato.
- Grafico consultato prima delle modifiche e aggiornato nelle descrizioni; rigenerazione **689file/9924relazioni/337AST, zero diagnostiche**, controllo `--check` PASS. Browser: ricerca/dettagli/collegamenti nei due versi per viewport preview, builder, campi numerici e regressione, senza errori JavaScript. Artefatti HTML/JSON locali ignorati da Git.
- `git diff --check` PASS. Nessuna modifica a formati persistiti, kernel o tolleranze. Nessun commit/push. Nessuna certificazione macOS o CI remota.

## Copie ed evidenze

Il file è cambiato rispetto all'indagine precedente sulle variazioni; per questa richiesta è stata congelata anche la versione corrente. Sorgente `/home/myungit/Documenti/Cad_Example/girante.prt`; copia e prove `/tmp/edit-preview-reuse-2026-11-13/`.

Snapshot corrente: 2081234 byte, SHA256 `ae839fb04f2ec28cd550cbcb59b299378be8df58e63b073dc9bae1ceba1bd6fb`. Snapshot precedente: `/tmp/girante-variazioni-2026-11-13/girante-originale.prt`, SHA256 `ce0f8839e276fa129197b65a6970f9d99aaf3d63de757002d7c29d0c59d85030`. File originale corrente verificato invariato al termine; nessun salvataggio sul CAD originale o chiusura di processi utente.

Log: `build-precision-final.log`, `ctest-final.log`, `girante-wayland-final.log`, `girante-separate-wayland-final.log`, `architecture-final.log`, `architecture-check.log`, `architecture-browser.log`. Acquisizioni in `qa-current/` e `qa-separate/`.
