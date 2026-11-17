# UI11 — sottomenu cache e overflow ribbon, 17 novembre 2026

Corretto il menu segnalato nello screenshot: **Opzioni → Memoria per la cache all’apertura** ora usa lo stesso componente degli altri menu. Il difetto è presente anche nei sottomenu dei gruppi nascosti della ribbon, corretti nello stesso intervento. Due chiamate con la stessa causa costituiscono un solo nuovo rilievo UI11: totale della review 34, K10 ancora parziale.

## Causa e correzione

`QMenu::addMenu(titolo)` crea un QMenu standard. Dentro CadMenu eredita il foglio stile trasparente; il solo CadMenu disegna fondale, ombra e vetro nel proprio paintEvent. Il menu standard rimane senza quel disegno e, nel caso mostrato dall’utente, appare nero.

Sostituite soltanto due costruzioni con il già esistente `addCadMenu`: cache in `forgeCad2026_gui.cpp` e gruppi overflow in `forgeCad2026_ribbon_preview.cpp`. Nessuna modifica al renderer comune, al controller globale del tema o alla logica della memoria. Le sei scelte, il gruppo esclusivo, la preferenza salvata e l’applicazione dalla prossima apertura restano identici. Aggiornate le descrizioni dei due moduli nel generatore della mappa.

## Verifiche effettuate

- Prova sul menu reale: baseline 17 controlli, 13 fallimenti; candidato 17/17 PASS. Il driver sostituisce il solo main e collega gli oggetti di produzione dell’app, senza copie di librerie né oggetti GUI modificati in privato.
- Cache: sei scelte; 512 MiB salvati selezionano una sola voce; scegliendo 2048 MiB si aggiorna la preferenza. Configurazione e recupero sono isolati in una directory temporanea, nessun CAD aperto.
- Chiaro → Scuro → Chiaro con menu già aperto: fondo `#f0f0f0` → `#111820` → `#f0f0f0`, alpha 255 con opacità di prova 100%. La baseline produce pixel alpha 0 e manca delle proprietà del componente comune.
- Ribbon: larghezza forzata 380 px sul widget distaccato dal viewport, tre gruppi overflow effettivamente creati; tutti usano CadMenu dopo la correzione. È una prova del percorso reale di costruzione, non della geometria completa della ribbon nel desktop.
- Build completa app e runner del tema PASS, 43,748 s, seriale. Test CTest `forgecad_application_theme_tests` 1/1 PASS, 0,109 s.
- Mappa 804 file/11741 relazioni/408 TU, zero errori; 406 unità da cache e 2 rianalizzate in 22,728 s. Hash sorgenti/configurazione, HTML/JSON, `--check` e chiamate a addCadMenu alle due righe modificate verificati nei due versi.

Verificati visivamente i due raster del menu: [chiaro](review-ui11-menu-cache-prove-2026-11-17/candidate-images/cache-0.png), [scuro](review-ui11-menu-cache-prove-2026-11-17/candidate-images/cache-1.png). Le prove sono Qt offscreen/Fusion; non si attribuisce una nuova certificazione a Wayland, macOS o a tutti i plugin desktop. Non aggiunto un test permanente per duplicare le due chiamate al factory: il driver mirato è conservato nelle prove.

Kernel, runner generale e runner viewport rimangono byte-identici alla baseline del presente intervento. Nessuna suite kernel o UI generale ripetuta. Eseguibile app aggiornato SHA256 `c9653b129b02d0903b40c41f9532209b24b9e81d9fc5ff74927ff7c4c214a80f`; eventuali processi utente aperti non sono stati riavviati. Rimossi 269855480 byte (257,4 MiB) di soli ELF temporanei conclusi; sorgenti, raster e log conservati.

## Stato e consegna

[Elenco consolidato dei punti ancora aperti](review-punti-aperti-2026-11-17.md), distinguendo K10, uniformazione residua e verifiche mancanti. [Prove con checksum](review-ui11-menu-cache-prove-2026-11-17/SHA256SUMS). [Memoria precedente](history/claude-prima-menu-cache-stato-review-2026-11-17.md).

Tre agenti Sol hanno controllato in sola lettura i limiti K10, gli altri punti e i costruttori dei menu; modifica, build e runtime sono verifiche dell’agente principale. HEAD/origin 47c7c19, indice vuoto; nessun commit o push, nessuna nuova CI, nessuna promessa di assenza globale di bug o leak.
