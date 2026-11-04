# Svuotamento: polo B-spline non finito — 4 novembre 2026

## Caso riprodotto

Documento `SurfaceModellingExample219.prt`, copia congelata in `forgecad-release/shell-nonfinite-2026-11-04-qa/input-current.prt`. SHA256:

`a501f4fddff8768ae9855a7443ba00502096549ca3095cbcdd90f561412c8f57`

Originale mai scritto; hash riconfermato al termine delle prove geometriche. Loft 15 ora ha una condizione trasversale tangente a F0 di Loft 13 sulla prima guida e normale a un piano sulla seconda. Raccordo 25 ha 31 facce e 68 bordi, rispetto alle 33 facce della prova precedente. Svuota 1 conserva spessore 1 mm, aperture F0/F14 e corpo base 24. Il corpo sorgente supera `checkBody` con geometria e incroci dei loop.

La chiamata `shellBody(source,{F0,F14},1)` riproduce `BSplineCurve: polo non finito` in 3,7 s. Debugger: `splitOffsetFaces → sewSheets(1319 parti) → assembleBody → translatedCurve<2>`, con traslazione UV su un dominio illimitato. Non è un polo NaN già salvato nel documento: lo genera la cucitura dopo una suddivisione eccessiva.

## Correzioni

- **Offset.** Separare solo le vere linee C0 con salto di normale, tramite `splitAtC0Knots`, mantenendo insieme le campate lisce. Poli, pesi e parametri dei pezzi restano esatti. Sul corpo iniziale la preparazione passa da 1319 a 13 parti; ciò elimina il NaN ma, da solo, non basta a svuotare il modello.
- **Raccordi.** Punto comune ai tratti e jet unilaterali calcolati dalle posizioni di tre sezioni interne. Evita di valutare il Jacobiano proprio sul nodo e di amplificarne il rumore in microcampate di parametro 1,86e-9. Gli eventi includono gli estremi; una radice a un ULP da un nodo esistente usa lo stesso parametro solo se soddisfa il residuo della ricerca. Nodi distinti non vengono fusi per semplice vicinanza.
- **Limite numerico del fit.** Il nominale resta 1e-9 mm. La stima del limite laterale viene ripetuta dimezzando il passo: soltanto uno scarto stabile e sotto la risoluzione sorgente (1e-7 mm) entra come incertezza locale. L'errore di troncamento deve continuare a raffinarsi. Questa componente non riceve il fattore 16 già usato per il rumore del solve interno. Restano attivi i controlli sulle vere discontinuità, i contorni, l'inversione dei bordi e i budget del B-rep; non sono stati aumentati i default di precisione o le tolleranze delle entità.
- **Loft G1.** Correggere una sola riga interna concentra la curvatura al crescere del grado U. Il lift cubico di Hermite distribuisce la correzione sull'intero dominio, rappresentato nella base corrente tramite momenti simmetrici dei nodi. Conserva sezioni, guide, pesi e jet dell'estremo opposto. La forma diventa indipendente dall'elevazione esatta del grado.
- **Valutazione NURBS.** Numeratori e derivate si sommano nelle coordinate locali del blocco attivo, ripristinando l'origine soltanto sul punto. Riduce la cancellazione numerica su gradi elevati/nodi stretti senza modificare poli o pesi.
- **Cuciture quasi tangenti.** Richiamo al seme fisso lungo ciascun bordo. Le connessioni con budget maggiore di quello base conservano il peso originale; sui bordi precisi con piccolo diedro il peso è più debole, permettendo l'intersezione reale. Se il passo oscilla nell'ultimo bit, si conserva soltanto una soluzione con residuo 3D verificato entro il budget richiesto. La variante che indeboliva indiscriminatamente il richiamo regrediva `ShellStitchedTolerantTangencies`: esclusa.
- **Cache 12, formato documento 62 invariato.** Le cache di lavoro precedenti con loft/raccordi attivi vengono scartate atomicamente insieme ai derivati. Definizioni e riferimenti restano disponibili per la rigenerazione. Check-in immutabili, feature soppresse e documenti senza queste operazioni mantengono gli snapshot portabili. Aggiornati anche due test che richiedevano letteralmente la vecchia versione 11.

## Verifiche geometriche e applicative

- Variante completa sul documento congelato: **62 facce, solido valido, zero issue** con geometria/incroci dei loop. Prima prova diretta riuscita in circa 217 s.
- QA nativa **Wayland**: migrazione cache, rigenerazione delle 25 feature a monte, anteprima dal pannello, conferma con riuso dello stesso B-rep, Undo/Redo, salvataggio e riapertura con cache e con sola definizione PASS. Il replay senza cache rigenera anche lo svuotamento. Eseguibile completo compilato; la sessione già aperta dell'utente non viene sostituita.
- Audit del risultato salvato: **31 facce interne, 662 punti**, errore massimo rispetto a `S - N × 1 mm` **5,8546061531e-8 mm**. Aperture F0 e F14 classificate libere, genealogia valida, un guscio chiuso senza bordi laminari e tessellazione senza facce fallite.
- Copia finale con cache: 84.289.327 byte; sola definizione della prova Wayland: 16.814 byte. La copia con cache viene riletta anche con i limiti standard nella prova XCB. Nessun CAD/B-rep originale grande entra nei commit.
- QA **XCB sulla build finale**: lettura con limiti standard, anteprima, OK con riuso, Undo/Redo e round trip con cache PASS. Il replay senza cache è la prova Wayland separata; l'ultima etichetta originaria della sonda XCB era troppo ampia, come annotato nel file QA `native-final-xcb-scope.txt`.
- Copia nominata `SurfaceModellingExample219-svuotamento-corretto.prt`: salvataggio e rilettura standard PASS, **26 nomi/identità/operazioni/corpi base conservati**, numero schizzi e camera originale conservati. File nella cartella QA locale, escluso da Git.

## Regressioni e controlli negativi

- `forgekernel_offset_split_crease_tests`, indipendente da OCCT: inserimento esatto di nodi V lisci anche distanti 1e-10 nella fixture con vere pieghe. ±1 mm, cucitura sì/no: numero facce, topologia, distanze nelle campate originali interne, genealogia e immutabilità PASS. Vecchio kernel: fallimento atteso, edge senza due fin opposte.
- `forgekernel_blend_contact_break_tests`: **5/5 PASS**, assi/appoggi/versi, estremi, nodo esistente a un ULP, nodi distinti, rifiuto di creste reali e raggio R2/offset 1 mm; inclusa una perturbazione sorgente di 1e-7 mm.
- Loft guide: invarianza grado 3/34 **2,89e-14 mm**; controllo negativo sul vecchio lift **0,43026 mm**. G1 sintetico **2,65e-10**, sezioni e jet C2 omogenei/euclidei PASS.
- `forgekernel_plane_jets_tests`: piano X=200000 mm, gradi 3/34, razionale/polinomiale, ordini 0..3 e intervalli UV stretti: coordinate costanti e normali PASS. Controllo negativo con il precedente valutatore: FAIL atteso sulla coordinata costante del piano.
- **11/11 CTest pertinenti PASS**, inclusi migrazione cache, storia dichiarata e revisioni immutabili. Build finale: **Blend 62/62, Shell 18/18, Offset 26/26 PASS**. Il filtro B-spline ha passato anche 23/23 durante il controllo del valutatore locale.
- Una prima compilazione di consolidamento era fallita per diagnostica temporanea rimossa solo in parte; lo script aveva poi eseguito il runner precedente. Quei PASS sono stati esclusi dalle prove finali. Le pipeline successive si interrompono al primo errore di build. Nessun test geometrico è stato indebolito.

## Limiti e tentativi esclusi

Scarto e validità geometrici sono verificati nei punti e nei domini coperti dai controlli descritti: non costituiscono una prova globale di assenza di ogni auto-intersezione. Il fit conserva il proprio limite numerico locale, distinto dal nominale.

Escluse le varianti con smorzamento variabile per campione, arresto anticipato su giunzioni mal determinate, sola derivata analitica sul nodo, derivata di ordine superiore priva della stima numerica, suddivisione preventiva di tutti i nodi a velocità discontinua. Non risolvevano l'intero caso o generavano una regressione.

Esperimenti aggiuntivi con loft sintetici quasi tangenti ruotati e traslati a 200000 mm mostrano ancora fallimenti di fit delle curve sia nel precedente sia nel nuovo solutore. Il test dei jet piani non certifica il successo universale degli svuotamenti lontano dall'origine. Questo limite resta separato dal caso utente verificato.

Sonde, log e immagini sono nella cartella QA ignorata da Git. Vista architetturale aggiornata: **576 file, 7987 relazioni, 276 unità AST, zero unità con diagnostica**. `--check` PASS; otto moduli verificati in entrata/uscita, quattro confini di chiamata controllati, overview e otto dettagli HTML resi e verificati. Chiamate C++ risolte, include e riferimenti restano distinti; non è una certificazione di ogni percorso dinamico. `index.html` e `graph.json` restano generati/ignorati, senza aggiunta forzata.

App completa e runner aggiornati. Tutti i controlli richiesti per questa correzione sono conclusi; codice e documentazione pronti per il commit.
