# Onion2: sezioni sui piani 1 e 2 — 7 novembre 2026

## Riproduzione e causa

Richiesta: correggere il comando «Sezione di solidi e superfici nel piano dello schizzo» su `Onion2.prt`, per Piano 1 e Piano 2.

Nel file iniziale ci sono due schizzi, `Rivoluzione 1` e i due piani di costruzione. La rivoluzione è una lamina a una faccia, non un solido chiuso; il comando supporta comunque le superfici. I due piani contengono l'asse della rivoluzione, inclinati simmetricamente di 10° rispetto al piano mediano.

Prima della correzione, creando uno schizzo su ciascun piano e invocando `addSectionReferences`, entrambi fallivano con:

```
Rivoluzione 1: Sezione non riuscita: intersectSurfaces: tracciamento dell'intersezione non riuscito
```

Mancava il dispatch analitico piano/rivoluzione per la sezione meridiana. Il marching generico arrivava alla singolarità della punta e falliva.

## Implementazione

- `kernel/fk_intersect.h/.cpp`: `revolutionMeridianAngle` riconosce piano assiale e meridiano piano dai poli delle pezze Bezier, con i budget lineare/angolare esistenti. Le due intersezioni sono copie ruotate del meridiano, con geometria NURBS conservata. Rotazioni e pose annidate vengono materializzate nei poli/assi, compatibili con le SP-curve planari; gli intervalli seguono l'eventuale scala del parametro delle rette.
- `kernel/fk_boolean.cpp`: usa queste intersezioni esatte; il ritaglio ai bordi delle facce resta nel percorso esistente. Le sezioni non assiali e i meridiani sghembi continuano al percorso generico.
- `kernel/tests/test_sheet.cpp`: regressione con punta, meridiani polinomiali/razionali, piani ±10° e 0,7 rad, modello traslato/ruotato, ordine degli operandi, strumento finito e rivoluzione parziale; rifiuto esplicito dei casi non meridiani.
- `tests/test_viewport.cpp`: regressione del comando per riferimenti associativi, Undo/Redo e codec con/senza cache.
- `tools/generate_architecture.py`: descrizioni aggiornate per riconoscimento meridiano e dispatch.

Non sono state ampliate tolleranze né utilizzate mesh per calcolare le sezioni.

## Verifiche già concluse

Sonde e log in `/tmp/onion-section/`.

- Riproduttore sul B-rep reale: due curve per ciascuno dei due piani.
- Kernel di prova con gli oggetti corretti: filtro Sheet **41/41 PASS**, Revolution **18/18 PASS**, Boolean **38/38 PASS**. I filtri hanno alcuni casi condivisi; i conteggi non vanno sommati come test unici.
- QA nativo Wayland sul file iniziale e su una seconda copia salvata esternamente durante l'indagine: due curve visibili associate per piano; Undo/Redo PASS; codec con/senza cache, riapertura e rigenerazione completa PASS; contesto OpenGL valido. Massimo scarto campionato delle sezioni dalla superficie **7,62e-15 mm**.
- Copia indipendente `output/Onion2-sezioni.prt`, con gli schizzi `Sezione Piano 1` e `Sezione Piano 2`; identità documento distinta e originale mai scritto dal QA. La copia finale deriva dal secondo snapshot congelato.

## Verifica conclusiva

- Build completa Linux: `forgecad`, `forgecad_view_tests`, `forgekernel_tests` PASS (`build-final.log`). Eseguibile aggiornato: `forgecad-release/forgecad`.
- Runner kernel ufficiale: nuova regressione `SheetRevolutionMeridianSection` PASS (`kernel-final.log`). I precedenti filtri Sheet/Revolution/Boolean con gli stessi sorgenti corretti sono 41/41, 18/18 e 38/38 PASS (`kernel-sheet.log`, `kernel-revolution.log`, `kernel-boolean.log`); il primo passaggio usava una libreria QA con gli oggetti corretti, poi la build definitiva ha ricompilato kernel e runner.
- **8/8 CTest PASS**: workflow riferimenti schizzo (include la nuova regressione), riferimenti associativi, visibilità dei riferimenti, uso dei riferimenti, costruzioni, storia rivoluzioni, storia booleane e accesso documenti (`ctest.log`).
- QA Wayland conclusivo collegato agli oggetti della build definitiva: due curve su ciascun piano, Undo/Redo, codec/cache/no-cache/rebuild/GL PASS (`native-final.log`), scarto campionato 7,62e-15 mm.
- Mappa: **631 file, 8947 relazioni, nessuna TU con diagnostica**; `--check` PASS. HTML visualizzato: panoramica e cinque dettagli PASS; collegamenti call/include/reference in ingresso/uscita verificati, inclusa la chiamata `fk_boolean.cpp → fk_intersect.cpp::revolutionMeridianAngle`. HTML/JSON restano ignorati da Git.
- `git diff --check` PASS. Le modifiche preesistenti delle altre chat sono conservate.

Una prima prova di rendering HTML nel sandbox non disponeva dei servizi necessari; la verifica conclusiva nella sessione grafica XCB è passata. I warning di compilazione preesistenti non sono stati trattati come errori. Nessuna certificazione macOS eseguita in questo intervento.

## Provenienza e limiti

File utente: `/home/myungit/Documenti/Cad_Example/Onion2.prt`. Snapshot iniziale SHA-256 `4fb4a1ab4f9fd4517f1646f0ff72b921d95d904b8225fce29e9501bc45932f3d`; secondo snapshot `87a3f185debbda5a7b55993670619093e4c1085f7107c5e3fbfab2cee308d25b`. L'originale è stato aggiornato da un processo esterno durante le prove: la differenza di hash non è una scrittura del QA.

Questa correzione riguarda il caso meridiano esatto; non certifica tutte le sezioni oblique di superfici libere. Il risultato conserva la lamina originale e aggiunge i riferimenti di sezione. Per caricare il nuovo eseguibile occorre riaprire ForgeCAD; nessun processo utente è stato chiuso.
