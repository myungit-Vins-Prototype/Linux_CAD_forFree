# K10: mapping nonPlane e controllo delle fin finali — 17 novembre 2026

Corretto un falso successo nel trasferimento delle curve sulle superfici non piane: il vecchio controllo campionato accettava un picco fuori budget fra due campioni. Per la famiglia NURBS riconosciuta il mapping usa ora il controllo continuo sui coefficienti e ricontrolla le fin effettivamente assemblate, anche senza history richiesta dal chiamante. **Nessuna tolleranza di accettazione aumentata; K10 generale resta parziale,33rilievi funzionali invariati. Nessun commit o push.**

Su richiesta dell’utente sono stati eseguiti solo test kernel pertinenti. Nessun test o avvio UI, nessuna ripetizione della suite generale483 e nessuna nuova CI. Le precedenti modifiche locali sono conservate.

## Difetto riprodotto

L’API interna esistente `detail::mapBodyByFunction` permette di fornire l’immagine di una faccia sul carrier originale. Nel riproduttore una lamina unitaria viene mappata con la funzione identità, associandole deliberatamente un carrier bicubico incoerente: il carrier ha un rilievo Z confinato a U∈[0,1003;0,1013]. Non è una nuova riproduzione di un documento CAD pubblico.

Prima della correzione il risultato era accettato, `checkBody` con controllo dei loop non segnalava problemi e la fin dichiarava1e-14mm. Il residuo effettivo della prima fin al parametro IEEE corrispondente a0,1008 è però circa0,00075mm, **circa9.375volte** il cap prefissato8e-8mm. La prova indipendente Fraction usa i coefficienti IEEE originali e conferma un testimone fuori budget; la positività dei pesi e l’inviluppo dei poli Q provano inoltre il contenimento dell’intera Q nel dominio. Non è soltanto un rifiuto per coordinate UV fuori dominio.

Il precedente `pcurveDeviation` suddivide ai nodi E/Q e verifica33punti per campata, senza le preimmagini dei nodi del carrier S. Le33valutazioni esatte della vecchia griglia di questa fin sono entro cap e spiegano il punto cieco; non sono usate come prova globale. Il nuovo percorso rifiuta il candidato, conservando input e valori esterni di history/deviation.

## Correzione integrata

In `fk_surface_mapping.cpp` il cap resta `.8*min(tolerance,linearResolution())`, fissato prima dei fit; nei tre casi pubblici verificati è8e-8mm. Si prepara una Q privata e si effettua la prova continua `|E(t)−S(Q(t))|` sul range originale. Un risultato riconosciuto `Failed` è terminale; soltanto `Unsupported` conserva la Q originale e il controllo legacy campionato. La candidata è assegnata solo dopo `Certified`.

La costruzione della candidata, già presente per le estensioni, è stata spostata in `surfaceCurvePCurveCandidate`, condivisa dal mapping e dal ledger delle estensioni. Non cambia l’algoritmo del certificatore: tutta la funzione `surfaceCurveResidualBound` e il resto del suo file successivo alla firma sono byte-identici alla baseline. La logica del ledger Sheet successiva al vecchio helper cambia soltanto il nome/argomenti della chiamata condivisa.

Per la famiglia supportata, una Line UV diventa una BSpline di grado1 con poli agli estremi del range edge; si usa la velocità originale, senza rinormalizzarla. La Q BSpline conserva grado/nodi/pesi. I poli candidati sono limitati al rettangolo naturale prima della prova, senza epsilon UV. Questo è costruzione, non accettazione: un errore fisico reale deve comunque essere rifiutato. Q condivisa, E, S, range e cap restano invariati.

La distinzione evita falsi rifiuti numerici. Nei due box pubblici di baseline,8/24fin ciascuno erano `Failed` interrogando direttamente il certificatore con QLine, pur avendo l’intera Line esattamente nel dominio. Su una di queste fin Fraction dimostra già nella baseline un limite continuo≈1,072e-8mm<cap. Non erano superamenti UV reali: la rappresentazione endpoint-Bernstein permette alla prova di riconoscere il contenimento senza indebolire il predicato.

Il registro finale usa la genealogia dichiarata delle facce e degli edge e controlla tutte le incidenze pianificate quando c’è una fin certificata. Dopo `build`/`buildSheet` verifica carrier e identità E/Q, range, sense delle fin, tolleranze edge/fin e cardinalità; ricertifica le fin native/Plane effettive. Non usa corrispondenze per prossimità e opera anche con history utente nulla. History e deviation esterne sono pubblicate solo dopo i controlli. Qui non c’è un successivo `unify` da certificare.

Per le fin native certificate `pcurveTolerance` dichiara ora il cap originario, anziché una misura campionata moltiplicata per1,01. Il metadato può quindi essere maggiore di prima; **il cap di accettazione non aumenta** e l’edge non viene reso più tollerante. I tre risultati pubblici hanno tutti edgeTolerance=0 e finTolerance=8e-8mm.

## Verifiche finali

| Verifica | Esito e perimetro |
| --- | --- |
| App e target kernel selezionati | PASS; app aggiornata dal target completo `forgecad`, otto runner kernel ricostruiti/collegati. |
| CTest pertinenti | 8/8PASS, zero skip, circa47,45s. Nessun target UI selezionato. |
| Nuove regressioni permanenti | 6/6PASS: box su cilindro, box su target quadratico, carrier razionale effettivo, picco nascosto, UV molto lontano non riparabile entro cap, cancellazione. |
| Driver mirato Release | 40/40PASS:6nuovi+12ledger+22provider esistenti; circa0,35s dopo build overlay. |
| ASan/UBSan/LSan | Stessi40/40PASS, circa3,94s; tre TU produzione e quattro TU test+main strumentati, dipendenze Release. LSan calibrato fuori ptrace con perdita reale19byte ed exit23. Non certifica l’intera app. |
| API pubbliche sulla libreria root finale | 52/52fin `Certified`, cap originario, genealogia/range/sense validi; input e target immutati. Con history nulla risultati byte-identici nei nuovi test; caso razionale ripetuto anche dall’auditor. |
| Fraction indipendente finale | 52/52PASS sui coefficienti effettivamente emessi dalla libreria root `a0c5530e331c08d86fc5c4fa29e4477e66fa1ff4c6c646df15d959f211e9531c`. |
| Architettura e conservazione | 787nodi/11.519relazioni/397TU, zero errori AST; hash, configurazione, HTML e chiamate nei due versi verificati.27pacchetti sigillati precedenti preservati. |

Le otto suite sono `forgekernel_mapping_nonplane_validation_tests`, `forgekernel_surface_mapping_tests`, `forgekernel_mapping_uv_validation_tests`, `forgekernel_mapping_carrier_validation_tests`, `forgekernel_mapping_diagonal_validation_tests`, `forgekernel_surface_curve_bounds_tests`, `forgekernel_sheet_join_validation_tests` e `forgekernel_tolerance_tests`. I40test mirati e i6nuovi non sono conteggi da sommare alle otto suite: descrivono livelli diversi della stessa verifica.

L’audit indipendente esporta24fin del box avvolto sul cilindro,24del box su target quadratico e4della lamina piegata tramite Quilt sul carrier razionale originale. I primi due emettono S native3/3 polinomiali; il nome del target quadratico non è una prova del grado emesso. Nel terzo sono effettivamente conservati il carrier nativo2/2 e i pesi variabili originali. Limiti Fraction massimi, rispettivamente:1,0718664751e-8,8,18049845e-10 e1,16330619e-10mm, tutti inferiori a8e-8mm. E/S/range finali sono identici alla baseline;48QLine diventano BSpline1, le4Q del carrier razionale restano identiche.

Il primo comando di build ha completato l’app ma GNU Make non conosceva ancora il nuovo target test nella propria lista già caricata, dopo la rigenerazione CMake. La seconda invocazione passa senza altre modifiche ai sorgenti. Log iniziale exit2 e finale exit0 restano distinti; nessun errore di compilazione è stato nascosto. App finale SHA256 `b7b899b8c6e019cdc7e6141716da474c502b5cdd54017b0f1d54c44f64ef932b`.

## Perimetro e limiti

Famiglia riconosciuta: E BSpline cubica nativa e Q Line/BSpline grado1/3, pesi E/Q assenti o costanti positivi; S nativa nonperiodica di grado2/3 per direzione, anche a pesi positivi variabili. E/Q razionali a pesi variabili, istanze/wrapper, altre famiglie e gradi restano `Unsupported`. In particolare i carrier quintici/analitici delle regressioni Quilt mantengono il percorso legacy, verificato dalla suite esistente.

Il certificato riguarda la posizione delle fin reali, **non** il fit del carrier rispetto all’intera mappa richiesta, l’iniettività globale, il ramo Newton, la classificazione trim o l’assenza di autointersezioni. I fit 3D/carrier e le altre famiglie restano campionati; i limiti di risorse del provider possono rifiutare conservativamente geometrie valide. Non è dichiarato chiuso tutto K10.

Agenti GPT-6.1Sol per riproduzione, test e audit; implementazione integrata e verifiche root distinte dalle prove in staging. La lettura iniziale di un ramo si è interrotta per capacità del modello; nessuna patch viene attribuita a quel ramo. Nessun CAD utente aperto o modificato. Liberati circa2GiB eliminando soltanto oggetti/eseguibili/librerie ricreabili di vecchi staging `/tmp` completati; sorgenti, log, CAD e pacchetti in repository conservati.

[Prove riproducibili, sorgenti inerti, log e manifest](review-k10-mapping-nonplane-prove-2026-11-17/README.txt). La CI verde precedente non certifica questo delta locale; commit/push restano rinviati.
