# Anteprime nella storia: contesto GL e mesh differite

Corretto sul branch `solver-mesh-gpu` il rendering intermittente modificando uno stadio nascosto della storia. Caso reale: `mouse2.prt`, Superficie planare 1 (indice 28), con Specchio 28 (27) ripristinato come base e gli stadi successivi nascosti.

## Causa riprodotta

Il B-rep era disponibile e le mesh CPU valide, ma `drawExtrusions` preparava le cache mancanti durante la passata grafica. `ensureBodyDisplays` mantiene la GUI responsiva tramite un event loop; il compositore Qt rendeva corrente un altro contesto OpenGL. Traccia nativa: contesto cambiato e framebuffer della scena MSAA da 2 a 0 nel mezzo del disegno.

Il codice proseguiva creando risorse GL nel contesto errato. Nessun errore GL rilevato: il solo controllo di glGetError non rilevava il difetto. La cache conservava poi le risorse sbagliate. Ripulendola nel contesto della vista, la stessa geometria ricompariva completa, senza rifare il B-rep.

Riprodotta la scomparsa del corpo con il solo contorno dell'anteprima visibile. Il grande triangolo della schermata utente non è stato riprodotto con identica forma; il contesto errato spiega la possibilità di geometria grafica spuria. Le immagini dopo la correzione sono complete e concordano con il riferimento.

## Correzione

`paintGL` prepara ora le mesh necessarie prima di iniziare il frame della cache e tutte le passate grafiche. Include visibili, operandi ripristinati/sostituiti e corpi necessari alla selezione. I paint rientranti durante il lavoro responsivo conservano il fotogramma precedente. Terminata la preparazione, ripristina contesto/FBO del widget e viewport; solo allora avvia la scena e la passata MSAA.

`drawExtrusions` usa i display già pronti. Restano il parallelismo della tassellazione, le mesh differite e il rilascio delle risorse non usate a fine frame. Nessuna modifica a geometria, parametri, B-rep, solver, CUDA o Metal; nessuno svuotamento generalizzato della cache come soluzione produttiva.

## Verifiche

La nuova regressione usa una fixture con superfici, planare e cucitura, più la modalità esplicita sul file reale. Ripete tre aperture/chiusure, verifica il riuso del B-rep, la superficie presente e gli operandi ripristinati; alla fine tutti i puntatori B-rep restano identici.

La variante nativa confronta la prima immagine con una seconda ottenuta ricreando la sola cache GL. Soglia fissata: massimo 32 pixel con differenza di canale maggiore di 2 su 1000×760 pixel. Stessa regressione e stessi oggetti finali con il solo renderer precedente ricollegato: **fallisce**, 310.731 pixel diversi, scarto massimo 246.

| Scenario corretto | Riaperture | Pixel diversi prima/dopo ricreazione cache | Errori GL |
|---|---:|---:|---:|
| mouse2 con cache del documento | 3 | 0 in tutte | 0 |
| mouse2 dopo ricostruzione completa | 3 | 0 in tutte | 0 |
| Fixture sintetica nativa | 3 | 0 in tutte | 0 |

Immagini native ispezionate: corpo completo già dalla prima apertura, senza triangoli anomali nelle prove. Flacone: superficie più U/V ancora presenti; suite nativa anteprime e controllo pixel PASS.

App e runner viewport ricompilati. **32/32 CTest PASS**, zero FAIL/SKIP, 11.94 s: anteprime/storia, mesh differite, selezione riferimenti, analisi grafica, export, CUDA e correzione RAM. Prima build della nuova fixture interrotta da una parentesi mancante nel test, corretta prima della verifica; nessun errore produttivo di compilazione.

Originale `mouse2.prt` invariato, SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617`. Verifiche native su Linux/XCB/OpenGL; nessun nuovo esito macOS/Windows e nessuna misura di accelerazione.

## Riproduzione

```sh
cmake --build forgecad-linux-solver --target forgecad forgecad_view_tests -j4
ctest --test-dir forgecad-linux-solver -R forgecad_history_preview_mesh_tests --output-on-failure
QT_QPA_PLATFORM=xcb forgecad-linux-solver/forgecad_view_tests --history-preview-mesh --gl
QT_QPA_PLATFORM=xcb forgecad-linux-solver/forgecad_view_tests --history-preview-mesh /percorso/mouse2.prt --gl
QT_QPA_PLATFORM=xcb FORGECAD_HISTORY_PREVIEW_REBUILD=1 forgecad-linux-solver/forgecad_view_tests --history-preview-mesh /percorso/mouse2.prt --gl
```

Per salvare le immagini impostare `FORGECAD_HISTORY_PREVIEW_QA_DIR` a una directory esistente. CTest ordinario esegue la fixture CPU; la modalità nativa richiede esplicitamente OpenGL. I test leggono soltanto il documento.

[Dati, hash e verifiche](history-preview-mouse2-2026-10-26.json). Artefatti locali: `forgecad-linux-solver/benchmarks/history-preview-mouse2-2026-10-26`, con immagini prima/dopo, trace, renderer e runner conservati, riferimento ricollegato e log. Mappa aggiornata: 492 file, 6.675 relazioni, 234 unità senza diagnostiche; --check PASS, collegamenti entranti/uscenti e righe verificati. La chiamata alla preparazione è ora in paintGL; la vista generata resta ignorata da Git.

Eseguibile aggiornato: `forgecad-linux-solver/forgecad`; occorre riavviare l'app per caricare il codice e ricreare tutte le risorse GL. Installazione desktop precedente non sostituita; nessun commit/push in questo intervento.
