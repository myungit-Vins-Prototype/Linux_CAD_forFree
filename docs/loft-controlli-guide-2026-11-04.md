# Loft: condizioni trasversali sulle guide — 4 novembre 2026

Richiesta: aggiungere alle guide delle loft di superfici il controllo normale al profilo con inversione del verso e la tangenza alle facce adiacenti.

## Comportamento implementato

Nel pannello Loft, scegliere una guida e usare **Condizione trasversale**:

- **Libero** conserva il comportamento precedente.
- **G1 — normale al piano del profilo** impone la direzione trasversale alla guida secondo la normale del piano dello schizzo. **Inverti normale** ne cambia il verso. Per una curva 3D, estratta o proiettata, si sceglie un piano esplicito; non viene inventato un piano approssimato per una curva spaziale.
- **G1 — tangente alle facce adiacenti** usa i piani tangenti delle facce selezionate lungo il bordo coincidente con la guida. Si possono selezionare più facce, se compatibili sul loro tratto comune. Un secondo clic rimuove l'appoggio.

Questa condizione trasversale è distinta da G0/G1/G2 lungo la curva guida: seguire la tangente di una guida non assicura la tangenza a una faccia che la contiene. Le guide con condizione trasversale seguono almeno G1; i controlli sono disabilitati per una loft rigata.

Il significato è coerente con i controlli laterali del [Loft PropertyManager di SolidWorks](https://help.solidworks.com/2021/english/SolidWorks/sldworks/HIDD_DVE_FEAT_LOFT.htm?id=26.6.27.7): la normale si riferisce al piano della guida e la tangenza alle facce richiede un bordo della geometria esistente. Conserviamo i budget e le sezioni esatte del nostro kernel.

Ogni guida conserva modalità, verso e appoggi quando viene riordinata. Preview, applicazione alla feature, Undo/Redo e salvataggio usano gli stessi parametri. La storyboard mostra anche la condizione trasversale. Gli appoggi diventano dipendenze della loft senza consumare o nascondere i corpi di supporto.

## Geometria e vincoli

`kernel/fk_loft_guide_constraints.*` interviene sulle righe di controllo adiacenti alle guide, lasciando invariata la riga del bordo. Colloca una correzione omogenea nella base B-spline V esistente usando una matrice a banda. I giunti completi del campo di correzione vengono ricuciti **C2 in coordinate omogenee**, mantenendo fermo il punto comune: la sola collocazione avrebbe consentito giunti C0 e nuove creste interne. I veri spigoli presenti nelle guide sorgenti restano nella riga di bordo originale e non vengono lisciati. Raffina la base soltanto quando il controllo angolare richiede altre campate; il raffinamento è esatto e conserva le curve sorgenti.

Sono controllati sia i bordi guida sia le isoparametriche di tutte le sezioni, incluse quelle intermedie. La correzione non modifica i jet V già imposti ai profili G1/G2. Un vincolo trasversale incompatibile con la tangente di una sezione viene rifiutato indicando guida e sezione, invece di muovere il profilo o ignorare una delle condizioni. Anche l'inversione può produrre questa incompatibilità: i versi delle tangenti delle sezioni esatte non possono essere cambiati arbitrariamente dalla guida.

I budget restano quelli dei percorsi loft esistenti: controllo delle sezioni entro `1e-9 mm`, controllo G1 con seno angolare `1e-4`, connessione geometrica `1e-6 × scala`. Nessuna tolleranza globale è stata aumentata. Il controllo del fit è adattivo e campionato; non costituisce una prova globale di assenza di auto-intersezioni.

## Persistenza e riferimenti

Il formato `.prt` passa da 61 a **62** per i parametri e i riferimenti delle condizioni trasversali. Le enum esistenti non cambiano valore. I documenti precedenti mantengono tutte le guide libere trasversalmente. I nomi genealogici delle facce sono conservati insieme ai nuovi appoggi; visitatori strutturali, grafo delle dipendenze, migrazione e rimappatura degli schizzi comprendono i nuovi riferimenti.

Il confronto delle impronte delle tavole può riprodurre anche la definizione del formato 61; i vecchi hash non scartano silenziosamente una nuova condizione delle guide.

## Verifiche

- Nuovo runner indipendente da OCCT `forgekernel_loft_guide_constraint_tests`: normale al piano, inversione con piano orientato al contrario, errore per guida non planare rispetto al piano scelto, corretta identificazione della seconda guida, G1 a faccia con piano tangente variabile, sezioni iniziale/intermedia/finale conservate, rifiuto di jet G2 incompatibili e di facce non incidenti.
- Nel caso sintetico con faccia adiacente curva: seno angolare massimo **1,06952e-9**. Il controllo negativo senza la nuova condizione viola G1 con seno maggiore di `0,1`.
- Giunti interni verificati direttamente sui due lati delle pezze di Bezier, senza differenze finite: errore relativo massimo dei jet euclidei fino all'ordine 2 **2,82e-12**, omogenei **1,33e-11**, errore assoluto dei jet dei pesi **9,10e-13**. Inclusi profili razionali. Confronto delle normali a `v ± 1e-8`: massimo seno **1,43e-8**; spigolo sorgente intenzionale conservato.
- CTest pertinenti: **10/10 PASS** (controlli guida, forma loft, guide/sezioni proiettate, codec, revisioni native, pannelli, workflow loft, riferimenti e dipendenze). Includono la migrazione atomica delle cache9→10 per istanze, aggiunta nell'intervento sugli specchi.
- Verifiche delle dipendenze: i supporti sono enumerati senza duplicazioni e non vengono nascosti; i riferimenti della copia Undo restano indipendenti.

Il workflow GUI guida, riordino, Undo/Redo e persistenza è passato su **XCB e Wayland**. La review delle immagini ha individuato un testo multilinea degli appoggi tagliato e un confronto che confondeva facce distinte dello stesso corpo: entrambi corretti, con controllo dell'altezza e selezione multipla nel test GUI. Compilazione completa dell'app e del runner conclusa, immagini rigenerate e ispezionate su entrambe le piattaforme: normale/inversione e supporti multipli leggibili, controllo automatico dell'altezza PASS. L'ultimo CTest mirato è **10/10 PASS** in 3,35 s. Le nuove condizioni sono state verificate con fixture sintetiche; non è stata modificata né ricalibrata con queste opzioni la loft del documento originale dell'utente. Log e immagini: `forgecad-release/loft-guide-controls-qa/` e `forgecad-release/guide-controls-*.log` (ignorati da Git). Nessun documento CAD originale modificato.
