# Trasferimenti UV Mapping/Quilt — consegna core V3

Copie private soltanto. Nessun edit del repository, documento originale, build condivisa, CLAUDE.md, commit, PR o worktree effettuato da questo agente.

## Baseline e consultazione

`baseline-sha256.json`: 321 file congelati prima degli edit, inclusi sorgenti/header kernel, test esistenti, libreria statica, oggetti Mapping/Quilt, comandi compilazione, grafico architetturale e istruzioni. Grafico consultato per descrizioni e archi Mapping/Quilt: il trasferimento `MappingFaceImage` di `mapBody` e' prodotto in produzione da `SurfaceQuiltMap::bend`; `offsetSurface` Plane ignora finestre UV. Letti AGENTS.md, CLAUDE.md, politica tolleranze e rapporto K10 punti aperti.

## Candidato finale

`candidate-v3.patch`, `candidate-v3-sha256.json`; sorgenti in `candidate/kernel`. V1 e V2 sono superati. V2 usava target fisico di fit 1e-9; intercettato prima dell'integrazione, V3 conserva target massimo 1e-10. Nessun aumento di tolleranze.

* Mapping Plane: `fitPCurveOnPlane` costruisce la SP-curve usando il bordo 3D effettivamente emesso, l'intervallo originale e il carrier originale. Provider continuo su coefficienti, scarto in mm, target `min(1e-10,.05*fitBudget)`, cap `.8*connectionBudget` fissato prima del fit. Fin memorizza il limite continuo ottenuto (minimo dichiarato 1e-14), non un massimo campionato.
* Dopo `Body::build/Body::buildSheet`, controllo della genealogia faccia/edge esplicitamente dichiarata, carrier pointer identico, stesso range, stessa fin/sense e stessa coppia SP-curve pointer/budget di BuildFin. `requirePCurveOnPlane` ricostruisce il provider dal bordo/supporto effettivi e ricontrolla ogni coefficiente della SP-curve finale contro il cap ed il budget dichiarato. Il ledger deve essere interamente consumato. Nessuna scansione closest o riassociazione per vicinanza.
* Quilt offset Plane: eliminato il fit UV della finestra. L'offset analitico e' una traslazione del carrier, conserva assi e parametro. Si passano le finestre chart finite gia' esistenti al ramo Plane che non le usa. Gli altri carrier restano nel ramo precedente.
* Pubblicazione history/deviation solo dopo check finale e cancellazione. Nessuna nuova API, header o modulo. Genealogia non costruita quando il chiamante non la richiede e non esistono obblighi Plane. Questo percorso non chiama unify; non si dichiara ricertificato alcun unify esterno successivo.

## Prove core realmente eseguite

Entrambe TU compilate su oggetti privati con comandi congelati; V3 surface con `-fno-fast-math` esplicito. Runner `kernel/tests/test_surface_mapping.cpp` esistente ricompilato e collegato con oggetti V3 prima della lib baseline: PASS. Log `surface-mapping-v3.log`; copre sviluppo piano/cilindro/cono, aree/lunghezze, mapping cilindrico/volume, NURBS, multifaccia, genealogia, B-rep/STEP/mesh. `git diff --no-index --check` delle due coppie baseline/candidato PASS senza output. Nessuna app ricompilata da questo agente.

L'agente test ha riproduttore pubblico Quilt Plane con mondo vicino origine e supporto riparametrizzato (`anchor U=10000`), sorgente razionale/trim, quota .2 e offset .3: due pose baseline rifiutate dal fit UV, V3 passa. Per conferma indipendente e risultati definitivi fare riferimento al suo export V3 e all'audit Fraction, non ai precedenti export V1/V2. L'esperimento privato `experiments/faruv-strict` ha verificato direttamente entrambe pose U=10000 PASS; U=100000/1000000 restano rifiutate.

## Limiti espliciti

Il certificato dimostra coerenza continua SP-curve/bordo 3D effettivo su Plane e conservazione del parametro; il bordo 3D di mapping e' ancora ottenuto dal fitter campionato preesistente. Non dimostra fedelta' continua del bordo alla mappa vera, iniettivita', appartenenza continua al trim o assenza di autointersezioni. Inverse Quilt con arc length/Newton, finestre UV non Plane, metriche e fit campionati dei fallback non sono migrati. Non completare globalmente K10 e non contare i due siti come due nuovi bug indipendenti. Il caso pubblico dimostra un rifiuto evitabile legato alla parametrizzazione Plane; la migrazione copre solo questa famiglia. Coordinate UV elevate possono ancora esaurire il limite di risoluzione/raffinamento e devono rifiutare atomicamente.
