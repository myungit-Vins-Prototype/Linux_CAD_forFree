# Mesh viewport: triangoli degeneri dopo conversione float

Il filtro comune della visualizzazione rimuove le faccette che collassano
nelle coordinate float inviate al renderer. Su `mouse2.prt` i triangoli
passano da **49.853 a 49.279**, con **zero degeneri** dopo il filtro in tutti
sei i processi: due CPU, due CUDA generici e due con specializzazioni abilitate.
Ogni processo CUDA valuta effettivamente 90.675 punti. Le specializzazioni
restano sperimentali e disabilitate per impostazione predefinita.

## Comportamento

`cad_forge.cpp` usa lo stesso helper interno per scena, anteprime locali
appena tassellate e patch riusate dalle cache precedenti. Le facce conservate
sotto un'anteprima seguono lo stesso filtro. Scarta vertici non finiti o
ripetuti e triangoli collineari; l'area è verificata in double sulle
coordinate float finali. La distinzione esplicita dei vertici evita il
residuo possibile di `cross(v,v)` con FMA su ARM. Nessuna soglia di area
elimina triangoli sottili validi e nessuna tolleranza del kernel cambia.

Vertici, normali e mappa triangolo→FaceId sono aggiunti insieme soltanto per
triangoli validi. Le etichette vengono ricavate da triangoli conservati; una
faccia completamente collassata non genera etichette spurie. Polilinee,
ID topologici e indice di picking esatto restano separati dalla mesh float.
Il B-rep, le superfici esatte e il formato dei documenti non cambiano.
Non si riscrivono cache persistenti o documenti originali: per confrontare
una mesh salvata precedente usare **Rigenera tutto** (`Ctrl+B`).

## Verifica sul documento reale

Documento: 31 feature, 13 schizzi, 16 B-rep, 19 facce nel risultato visibile.
SHA256 originale invariato:
`e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617`.

Il riferimento è lo [snapshot numerico precedente](mouse2-mesh-cpu-cuda-2026-10-26.md).
Il file produttivo `cad_forge.cpp` era identico fra quel checkpoint e
`e3db5f7`, base di questo intervento. Un controllo indipendente elimina dal
riferimento soltanto i 574 triangoli con prodotto vettoriale esattamente
nullo sulle coordinate float. Tutti gli array risultanti coincidono
**nei bit** con quelli attuali nella rispettiva modalità: vertici e normali
validi, ordine, appartenenza alle facce, polilinee, etichette e metadati.
Tutti i 16 hash B-rep, visibilità ed errori restano identici.

Fra CPU e CUDA rimangono le stesse 23 normali diverse, massimo
7,4505805969e-9; vertici identici nei bit, nessuna inversione o degenerazione.
Questo conferma la rimozione delle sole faccette identificate, senza una
promessa di equivalenza universale. Non misura chiusura o volume della mesh.

## Regressioni e riproduzione

Nuovo `forgecad_display_mesh_tests`: box parzialmente/completamente collassati
a coordinate grandi, tre qualità, conservazione di tutti i triangoli sottili
del riferimento double, anteprime fresche e parti conservate, patch da cache
precedenti con degeneri/non finiti, conservazione di un triangolo di area
circa 1e-50, immutabilità della cache e picking concorde con FaceId.
Il test fallisce sul codice precedente e passa con la correzione.

**27/27 CTest PASS**, compresi anteprime differite, etichette, storie,
lettore, solver ed export CPU/CUDA generico/specializzato. QA nativa
XCB/OpenGL delle anteprime PASS; `preview-real.png` del flacone Linux locale
ispezionata, superficie e U/V presenti. Flacone originale invariato; è un
file diverso da quello Mac. Applicazione e runner ricompilati in
`forgecad-linux-solver`; collegamento desktop invariato.

```sh
ctest --test-dir forgecad-linux-solver -R forgecad_display_mesh_tests --output-on-failure
python3 tools/compare_document_mesh_backends.py /percorso/mouse2.prt \
  --build-dir forgecad-linux-solver --output /tmp/mouse-mesh-filtrata \
  --repeats 2 --specialized
```

[Rapporto JSON](viewport-degenerate-mesh-2026-10-26.json). Artefatti locali in
`forgecad-linux-solver/benchmarks/display-mesh-filter-2026-10-26`: snapshot,
sonda CUDA ricollegata, confronto prima/dopo, log, immagini e manifest.
I tempi diagnostici non sono benchmark: generatore/test erano concorrenti.

La modifica è comune alle piattaforme e non tocca i backend Metal/CUDA.
La verifica Mac è stata successivamente completata nel [merge del lavoro Metal](metal-box-pool-2026-10-26.md#integrazione-del-lavoro-linux-concorrente): casi sintetici del filtro, 21 CTest e QA Cocoa/OpenGL sul flacone Mac PASS. Il confronto numerico mouse2 riportato qui rimane specifico Linux.
Restano aperti altre GPU, budget globale dei lavori e componenti grandi
connesse del solver. Questo passaggio corregge la visualizzazione; non
trasferisce altri calcoli sulla GPU e non dichiara nuova accelerazione.
