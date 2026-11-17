# Review locale — sette API legacy, 16 novembre 2026

Richiesta: proseguire con le correzioni e accumulare modifiche locali, **senza commit o push**. Base47c7c19, con CI precedente verde sul codice958ed68. Quella CI non viene attribuita alle modifiche locali nuove; non è stata avviata una CI remota per questo blocco. Tre agenti GPT-6.1 Sol hanno preparato delta separati, controllati poi da root con build e regressioni integrate. Mappa778/11373/390TU consultata prima delle modifiche.

## Pulizia implementata

| Blocco | Rimozione | Percorso conservato |
| --- | --- | --- |
| Estendi | setExtendPickCallback, setEdgePickLinear, resumeExtendPick, membro extendPickFinished_ e consegna callback morta | beginExtendPick e stato Linear/Extend; picker/preview e callback changed/key del pannello. finishEdgePick pulisce e termina il ramo Estendi prima dei callback raccordo/modifica. |
| Tavola | setModelProvider, provider/azione morti dell'editor, azione nella tabella icone e findChild che la nascondeva nel contenitore, enable inutilizzato di setReadOnly | Aggiorna dal .prt salvato del contenitore; updateFromSource, updateModel, installPreparedModel; accessi, identità/impronta, annullamento, Undo e sorgenti salvate. DrawingWindow::updateAction_ e l'icona condivisa restano. |
| Utility | forgeIntersectRay, nearestBodyEdge, appendFixedReferenceLine: dichiarazioni/definizioni e commenti propri | Picking esatto forgePickFace/firstRayHit, projectPoint e appendSegment/makeConstraint; Fix, proiezioni e riferimenti associativi con genealogia. Nessuna riassociazione per vicinanza. |

Sono sette ingressi applicativi senza usi osservati, non sette nuovi bug. I metodi pubblici sono ordinari, non virtuali/slot/Q_INVOKABLE; gli header non sono installati come SDK. La rimozione è una decisione di manutenzione dell'app; non si promette compatibilità sorgente/ABI con consumatori esterni. Non sono stati cancellati strumenti, formati, enum persistite o dati; nessuna pulizia automatica degli include.

Delle22candidate iniziali: **16rimosse** (5private+4wrapper precedenti+7legacy in questo blocco), **6query/cataloghi conservati intenzionalmente**, zero candidate iniziali ancora da classificare. Questo non prova l'assenza di ogni altra orfana nell'intero progetto. **Totale33rilievi funzionali corretti invariato.**

## Verifiche realmente eseguite

- Ricerca attiva e grafo: sette ingressi senza call/reference osservati sulla base; dopo integrazione nomi API/callback/azione morti assenti. Corpi e percorsi mantenuti confrontati; **35/35 controlli statici** di audit incrociato PASS. Non sono test runtime.
- **Build completa app e test PASS**, 591.49s; eseguibile aggiornato. Avvio Wayland isolato5s con percorso/hash verificati, nessun CAD o processo utente toccato. Avvio non equivale a nuova prova pixel/OpenGL.
- **10/10 CTest pertinenti PASS**, 6.54s, zero SKIP: Estendi, anteprima Smusso, strumenti superficie, editor tavola, storia/layout, disegni collegati, multifoglio, display/picking, riferimenti associativi e cache. Test esistenti: nessun nuovo test speculare alle funzioni rimosse.
- Mappa **778/11372/390TU**, zero errori; catalogo chiarisce il coordinamento Estendi e l'aggiornamento transazionale della tavola. Hash/digest compileDB, HTML/JSON, simboli rimossi e collegamenti call/include/reference nei due versi PASS.
- Kernel, runner generale e compile_commands rimangono byte-identici alla base: nessun test kernel generale/sanitizer ripetuto senza una modifica che lo giustifichi. Nessuna nuova garanzia di assenza di leak globale, nessuna ricertificazione nativa macOS o dell'intera UI Wayland.

## Prosecuzione K10 in sola lettura

Individuato il prossimo confine concreto: le fin non ISO emesse dai giunti Newton. L'attuale provider su BSpline riconosce UV ISO affini; una pcurve cubica generale rimane unsupported e il ledger finale copre soltanto followSeam. Il prossimo delta proposto verifica E−S(Q) sui coefficienti finali e con cap originario, seguendo la genealogia dopo build/unify. Primo perimetro: edge cubica, pcurve lineare/cubica non razionale e carrier bicubico polinomiale nativo. [Piano, righe e limiti](review-legacy-api-prove-2026-11-16/k10/PLAN.md).

**Nessuna modifica kernel, nessun nuovo bug o certificato K10 in questo blocco.** Newton, branch selection, topologia, carrier globale e famiglie non supportate restano aperti/campionati. Le vecchie prove Fraction sono soltanto consultate, non rieseguite o attribuite al lavoro nuovo.

## Conservazione e consegna

Baseline5565file e22sigilli congelati. Fonte, delta, audit e log curati in [prove](review-legacy-api-prove-2026-11-16/README.txt); nessun ELF/oggetto/libreria/CAD archiviato. Memoria precedente in [history](history/claude-prima-legacy-api-2026-11-16.md). HEAD/origin/main47c7c19 e indice Git invariati: **nessun commit, nessun push**, tutte le modifiche restano nel working tree.
