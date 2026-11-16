# K10 — mapping obliquo su estrusioni originali, 15 novembre 2026

Prosecuzione autorizzata della revisione con tre agenti GPT-6.1 Sol: implementazione, regressioni e audit indipendente. Il blocco estende il [mapping con conservazione del supporto](curve-fit-k10-mapping-2026-11-15.md); non conclude K10 né la revisione complessiva.

## Problema e ambito

La baseline congelata accetta `native_rotation_shift` (rotazione 0,29 rad, shift 0,03/0,02, anchor 0,05/0,1), dichiara deviazione circa 4,25e-11 ma perde un rilievo di **0,001 mm**, a tolleranza 1e-6. Il generatore originale è polinomiale di grado 6, con rilievo stretto tra i nodi 0,1 e 0,101; la regolarità del supporto era stata verificata nel blocco precedente. Il percorso precedente ricostruiva un supporto campionato piatto.

L'estensione riguarda superfici `ExtrusionSurface` originali con generatore Line/NURBS supportato e SP-curve UV affini: linee, spline di grado uno con pesi uguali sul tratto, e relativi trim. L'operazione conserva il supporto originale per lamine piane e quota entro il budget già esistente. Non aumenta tolleranze.

## Metodo

Il nuovo modulo `fk_affine_surface_fit` riconosce i tratti UV dai coefficienti originali e conserva gli intervalli nodali originari anche per query parziali. Per S(u,v)=C(u)+vD, il certificato confronta C(u(t)) con il candidato cubico Q(t) meno v(t)D. Sottrazione, inversione affine e cambio di parametro sono racchiusi da aritmetica intervallare outward; il confronto usa il generatore originale, inclusi pesi e nodi. Le preimmagini nominali dei nodi guidano il raffinamento, senza sostituire la copertura intervallare.

Il controllo isoparametrico precedente resta attivo. Il mapping conserva il precedente percorso isoparametrico quando la rimozione della componente trasversale è interamente coperta dal limite continuo e dal budget; altrimenti preserva entrambe le coordinate UV per le estrusioni riconosciute. Mantiene i controlli già presenti su corridoio UV completo, quota dell'intera regione, trim e vertici originali, regolarità campionata iniziale e pubblicazione atomica di storia/deviazione. Un controllo inconclusivo su una famiglia riconosciuta deve fallire esplicitamente.

La selezione isoparametrica è basata sul limite metrico dello spostamento completo, non su un epsilon applicato alla direzione UV. Le regressioni dirette includono variazioni U di 1e-16 con variazione geometrica significativa; il certificato non le azzera. Il mantenimento del percorso isoparametrico verificato evita una regressione osservata durante lo sviluppo sulle pose a scala 1000.

## Correzione della ripartizione del budget

Il caso trasformato a scala 1000 ha evidenziato un rifiuto evitabile: il fit riservava solo l'errore del bordo sorgente (circa 2,47e-11), mentre il controllo successivo del trim richiedeva 4,11e-11. Con fitError=1,996365667e-8, la somma superava di poco il budget immutato di 2e-8. Il controllo finale era corretto; la riserva iniziale era insufficiente.

Il lavoro è stato riordinato in tre fasi: costruzione delle UV e del trasferimento dei bordi, calcolo del massimo errore di tutti i trim originali, infine fit e ricertificazione entro il budget residuo effettivo. Nessuna tolleranza o condizione di accettazione è stata rilassata.

## Verifiche finali

- Build completa Release di app, runner e nuovo target: PASS. Restano 18 avvisi in file invariati in questo intervento; nessuno nei moduli nuovi o modificati qui. Eseguibile `forgecad-release/forgecad` aggiornato; riaprire l'app per usare il nuovo binario.
- **7/7 nuovi gruppi, 25 scenari**: 21 successi obbligatori (15 operazioni e 6 controlli diretti), 2 fallimenti/cancellazione atomici, 2 famiglie esplicitamente non supportate. Scale 0,01/1/1000, pesi razionali, pose 3D, bordi inversi, trim annidati, fori, storia e codec. **9/9 gruppi precedenti** invariati: PASS.
- Riproduttore iniziale corretto: errore bordo **1,0235892837e-8 mm**, deviazione dichiarata **1,6641748144e-8 mm**, supporto originale conservato, nessun errore di controllo del corpo. Prima: errore **0,001 mm** accettato. La baseline completa è congelata; il confronto della nuova suite con il vecchio adapter usa il provider nuovo per i test diretti, dichiaratamente.
- Kernel: **129 casi distinti / 145 esecuzioni** pertinenti PASS; **38/38 suite CTest** PASS, comprese verifiche Qt offscreen, ciclo di vita, storia, mapping, composizione, offset e pannelli.
- ASan/UBSan/LSan: **7/7 gruppi PASS**, senza diagnostiche. Strumentati nuovo test e tre unità di produzione modificate (`fk_affine_surface_fit`, `fk_curve_fit_bounds`, `fk_mapping_carrier`); il mapping pubblico invariato e il resto dell'archivio sono Release. È una verifica mirata, non un controllo integrale dell'app.
- Audit indipendente finale: **183/183 controlli PASS** (107 precedenti e 76 nuovi). Include coefficienti originali, query sui nodi nascosti, direzioni U piccolissime, intervalli invalidi, nodi condivisi chiusi, lifetime e famiglie escluse.
- Mappa: **752 file / 10845 archi / 373 unità C++**, diagnostica vuota; `--check`, uguaglianza HTML/JSON e audit degli archi entranti/uscenti di chiamata, include e riferimento PASS.
- Conservati byte per byte **2060 dei 2069 file preesistenti**; i 9 modificati appartengono allo scope dichiarato. Conservazione delle modifiche pregresse, hash dei sorgenti/binari, flag FP rigorosi, link kernel senza Qt/OCCT, `git diff --check`, memoria entro 16 KiB e manifest delle prove verificati nel rapporto di audit finale.


## Limiti e stato complessivo

K10 rimane aperto: **8/24 punti di chiamata iniziali parzialmente trattati, 16 interamente da migrare**, oltre alle famiglie che usano ancora il percorso precedente. Questa estensione appartiene allo stesso punto di mapping già contato. Restano esclusi i mapping diagonali su superfici NURBS tensoriali generali, le SP-curve non affini e i casi con offset normale significativo. I controlli di regolarità campionati preesistenti non costituiscono una prova globale di iniettività; casi geometricamente validi possono essere rifiutati da limiti conservativi o budget finiti.

Il totale dei **24 rilievi precedenti corretti** non aumenta. **K2 è in produzione**; restano **22 funzioni candidate orfane** da valutare e l'uniformazione completa di anteprime, pannelli e stili. Nessuna nuova certificazione globale dei leak o nuova prova del renderer/OpenGL in questo blocco. CAD originali e modifiche pregresse preservati.

Le [prove riproducibili e il manifest](curve-fit-k10-diagonal-prove-2026-11-15/README.md) distinguono baseline, integrazione e strumenti; script e sorgenti delle prove sono salvati con suffisso `.txt` per non alterare il catalogo C++.


## Prosecuzione successiva

Il [blocco appiattimento piano](curve-fit-k10-flatten-2026-11-15.md) tratta una famiglia di un nuovo sito: conservazione dei contorni originali su una sola Plane. Il conteggio passa a 9/24 siti parzialmente trattati, 15 interamente da migrare; i risultati di questo rapporto restano storici e K10 rimane aperto.
