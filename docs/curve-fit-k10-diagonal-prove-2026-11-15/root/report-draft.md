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

__VERIFICATION__

## Limiti e stato complessivo

K10 rimane aperto: **8/24 punti di chiamata iniziali parzialmente trattati, 16 interamente da migrare**, oltre alle famiglie che usano ancora il percorso precedente. Questa estensione appartiene allo stesso punto di mapping già contato. Restano esclusi i mapping diagonali su superfici NURBS tensoriali generali, le SP-curve non affini e i casi con offset normale significativo. I controlli di regolarità campionati preesistenti non costituiscono una prova globale di iniettività; casi geometricamente validi possono essere rifiutati da limiti conservativi o budget finiti.

Il totale dei **24 rilievi precedenti corretti** non aumenta. **K2 è in produzione**; restano **22 funzioni candidate orfane** da valutare e l'uniformazione completa di anteprime, pannelli e stili. Nessuna nuova certificazione globale dei leak o nuova prova del renderer/OpenGL in questo blocco. CAD originali e modifiche pregresse preservati.

Le [prove riproducibili e il manifest](curve-fit-k10-diagonal-prove-2026-11-15/README.md) distinguono baseline, integrazione e strumenti; script e sorgenti delle prove sono salvati con suffisso `.txt` per non alterare il catalogo C++.
