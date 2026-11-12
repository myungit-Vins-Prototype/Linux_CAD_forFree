# KnifeHandle — blocco dello schizzo su faccia

11 novembre 2026. Correzione completata, compilata e verificata sulla copia corrente di `KnifeHandle.prt`.

## Riproduzione e causa

Originale letto da `/home/myungit/Documenti/Cad_Example/KnifeHandle.prt`; copia congelata
in `/tmp/knifehandle-sketch-2026-11-11/KnifeHandle.prt`, 64.466.966 byte.
SHA256 di entrambi: `0de40d57703cfd40736b50b0f5f3c01a5e3139cd92955e4b32fcaab60622cc6b`.
Il checkout Linux e' `/home/myungit/Linux_CAD_forFree`; il percorso `/Users` della chat non esiste sull'host.

Il documento contiene 191 feature e 10 schizzi. Il B-rep finale di `Unione 189` (indice190)
ha 1.188 facce e 13.521 spigoli. La sonda apre il documento e crea lo schizzo sulla faccia piana F11.
La creazione termina in circa1,6–2s; la preparazione dei riferimenti automatici per il disegno
resta incompleta oltre45s nella versione precedente (sonda interrotta con timeout).

Lo stack del thread GUI attraversa `sketchReferenceCache`, `appendLinkedSketchReference`,
`projectLinkedReference`, `geometryRefPath`, `resolveEdgeReference` e `projectPoint` su B-spline.
Il solver dei vincoli non e' in questo stack: il costo e' la risoluzione topologica ripetuta
per ogni proiezione e contatto di spigoli appena enumerati dallo stesso B-rep.
Anche il calcolo della valenza di ogni vertice riscansionava tutti gli spigoli.

## Correzione

- `cad_sketch_refs.cpp/.h`: preparazione in blocco dei riferimenti automatici dal B-rep corrente;
  riusa gli stessi algoritmi esatti di proiezione/contatto e la creazione dei metadata associativi.
  Conserva featureId, nomi genealogici, ID, tipo e contesto su ogni link.
- `cad_topology_ref.cpp/.h`: riferimenti di tutti i vertici con valenze calcolate in un'unica
  scansione degli spigoli; stesso trattamento dei bordi di lamina e degli spigoli chiusi.
- `forgeCad2026_viewport_sketch.cpp`: precedenza storica verificata una volta per corpo;
  nessuna nuova risoluzione degli ID appena letti dalla geometria corrente.
- `tests/test_sketch_linked_refs.h`, `tests/test_viewport.cpp`, `CMakeLists.txt`: regressioni e
  percorso facoltativo sul documento reale. Catalogo architetturale aggiornato.

Il percorso ordinario di refresh dei riferimenti persistenti continua a risolvere le sorgenti
con la genealogia esistente. Nessuna modifica di tolleranze, solver o geometria del modello.

## Verifiche

- Sonda isolata: cache in338–376ms; 14.012 segmenti,12.003 curve,26.010 link.
  Tre movimenti dello strumento Punto:157–161ms/evento su questa macchina.
- Finestra completa offscreen: creazione1.985ms,cache401ms. Clic di disegno,chiusura,
  Undo/Redo e codec67 PASS. Tutti i puntatori B-rep delle191feature restano invariati:
  il nuovo schizzo non rigenera i solidi.
- Finestra completa Wayland/OpenGL reale: creazione1.895ms,cache363ms;
  disegno,Undo/Redo,codec e riapertura dello schizzo PASS. Immagine acquisita e ispezionata.
- Regressioni sintetiche PASS: parallelepipedo,cilindro e lamina,in due piani dello schizzo;
  geometria/contatti/metadata confrontati col percorso associativo ordinario,valenze e nomi
  confrontati coi riferimenti singoli. Nessuna risoluzione topologica durante la cache.

- Build completa Release/CUDA di `forgecad-release/forgecad` e del runner `forgecad_view_tests` PASS.
- 12/12 CTest pertinenti PASS: cache B-rep, riferimenti associativi/uso/workflow/visibilita',
  snap automatici/intersezioni, costruzione, editing, schizzi vuoti, storia e precisione documento.
- `git diff --check` PASS. Mappa rigenerata:681file,9804relazioni,334TU,0diagnostiche;
  `--check` PASS. Verificate le nuove chiamate viewport→cache e cache→riferimenti dei vertici,
  collegamenti entranti/uscenti dei tre moduli e tre viste HTML in Chrome headless,
  acquisite e ispezionate. JSON/HTML restano locali e ignorati da Git.

## Limiti e integrita'

La prova sul file reale copre la prima faccia piana F11 e la sessione grafica locale;
non certifica ogni faccia,configurazione grafica o documento.
Il modello resta pesante: gli snap sulle circa26.000 entita' possono ancora richiedere
circa0,16s per movimento. La prima creazione dello schizzo richiede ancora circa2s.
Il miglioramento misurato riguarda la preparazione dei riferimenti automatici.
Nessuna modifica al file CAD originale; hash nuovamente confrontati dopo le prove.
L'app gia' aperta deve essere riavviata dall'utente per caricare la nuova build.

Log,sonde,copia e screenshot: `/tmp/knifehandle-sketch-2026-11-11/`.
Per le precedenti correzioni di KnifeHandle (unione,loft7,tagli reciproci):
[cronologia](history/claude-prima-badge-2026-11-11.md).
