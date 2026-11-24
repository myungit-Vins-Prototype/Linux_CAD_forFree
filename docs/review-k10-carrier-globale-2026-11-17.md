# K10 — interno del carrier, 17 novembre 2026

Prosecuzione del filone2 della review, dopo underflow e arresto della quadratura. Il conteggio rimane34 rilievi corretti; K10 resta parziale. Prima delle modifiche consultati descrizioni e archi tipizzati bidirezionali della mappa: chiamante applicativo `buildSurfaceMapping`, `wrapBodyOnSurface`/`mapBody`, fit condiviso, percorso di conservazione del carrier e verifiche delle fin. Tre agenti Sol: costruzione del provider, audit indipendente e regressioni; compilazioni seriali.

## Riproduzione pubblica confermata

Baseline locale di inizio intervento: lavoro numerico precedente già presente, nuovo provider ancora assente; hash sorgenti/libreria e output conservati. I file del mapping erano ancora quelli del checkpoint pubblicato.

`wrapBodyOnSurface` su box1×1×0,001mm, anchor(0,0), tolleranza1e-6mm. Target NURBS cubico3/3,9×9 poli, pesi unitari, nodi per entrambe le direzioni `{0×4,46/256,47/256,48/256,49/256,50/256,1×4}`; XY dei poli sono coordinate Greville, Z zero salvo polo(4,4) alto2,25e-4mm. Il supporto del rilievo è interno ai bordi e lontano dalle sonde del fit.

Oracolo indipendente: a `c=3/16`, la base cubica centrale vale2/3 e la sua derivata è zero. Il picco tensoriale è `(4/9)*z44≈1e-4mm`, normale verticale. Il fondo deve contenere quel rilievo; la cima aggiunge0,001mm.

La baseline pubblica emette6 facce, `checkBody` non segnala problemi e l'input resta invariato. Dichiara deviazione1,221797765e-10mm, ma fondo/cima rimangono piatti con residuo1e-4mm: cento volte la tolleranza dell'operazione e5000 volte il budget locale di fit2e-8mm. Non è un esempio costruito soltanto per una callback interna. Il controllo continuo delle fin certifica i bordi, non l'interno del carrier.

Quattro gruppi pubblici sulla baseline: **3/4 PASS**; fallisce precisamente il rifiuto del solido prima della pubblicazione (esito e sentinelle). Passano l'oracolo, il controllo piatto con gli stessi nodi e una lamina a quota zero che conserva già il carrier originale e il rilievo.

## Correzione integrata e perimetro

Aggiunto `fk_surface_mapping_bounds.{h,cpp}`. Riconosce facce Plane a quota effettiva esattamente zero, UV affini separabili (anche assi scambiati/negativi), target B-spline nativo nonperiodico di grado2/3 per direzione e candidata bicubica nativa. I pesi devono essere originali, positivi ed esattamente costanti: un gauge comune diverso da1 descrive ancora una superficie polinomiale. Non si usano poli di istanze trasformate o conversioni Bezier arrotondate.

Il provider costruisce i polinomi Cox dalle ancore nodali originali con intervalli outward. Confronta i coefficienti tensoriali Bernstein del carrier emesso e della mappa richiesta, centrati rispetto alla stessa origine; il limite euclideo dei coefficienti racchiude il residuo su tutto il rettangolo. Partiziona sui nodi della candidata e considera **tutte le celle chiuse target intersecate**, incluso il lato sinistro ai nodi ripetuti. I polinomi di queste celle sono verificati anche sulla loro estensione all'intero rettangolo sorgente: è conservativo, senza preimmagini nodali arrotondate usate per scegliere una sola cella.

Quota zero e separabilità richiedono coefficienti dimostrati esattamente nulli, senza epsilon; quasi-zero non viene promosso a zero. Il cap rimane quello deciso prima del fit, `.2*min(options.tolerance,linearResolution())`. Limiti di lavoro indipendenti dalla tolleranza:100000 poli per superficie,16384 visite complessive,256 celle per asse/prodotto, profondità24, controlli di cancellazione. Dati/dominio non dimostrati, aritmetica inconclusiva o esaurimento diventano `Failed`, mai un ritorno alle sonde per una famiglia riconosciuta.

`mapBody` applica il controllo prima di pubblicare la faccia; `Failed` solleva un errore specifico sul residuo globale del carrier. Il registro finale controlla identità del carrier e genealogia anche senza history utente o fin certificate. La pubblicazione di history/deviazione resta successiva all'assemblaggio e alle verifiche. Il solido del riproduttore viene **rifiutato conservativamente**, non ricostruito: il limite non è nascosto da una tolleranza maggiore. La lamina già conservata esattamente continua a riuscire.

Rimangono `Unsupported` in questo nuovo provider: quote/offset normali non nulli, facce non piane, UV obliqui o non dimostrati separabili, pesi variabili, istanze, periodicità, altri gradi e altre famiglie. I certificati già esistenti delle fin per razionali/istanze restano disponibili; non vanno confusi con questo nuovo controllo dell'interno. Non si certificano regolarità delle normali, iniettività, autointersezioni, trim o l'intero top del solido. Per queste famiglie resta il percorso precedente e K10 rimane aperto.

## Verifiche

Candidata privata:10/10 gruppi Release PASS in0,168s. Include oracolo del picco, controllo piatto con stessi nodi, rifiuto specifico e sentinelle immutate, lamina esatta/history nulla; provider2/3, gauge costanti, picco sopra/sotto cap con oracolo convex hull, curve polinomiali equivalenti, UV negativi/scambiati, input invalidi, famiglia razionale variabile esclusa e cancellazione distinta. Aggiunto poi il controllo simmetrico dei nodi stretti nella candidata emessa; verifica integrata10/10 PASS.

Audit statico indipendente Sol sul provider e sull'integrazione: nessun blocker numerico/pubblicazione individuato nel perimetro dichiarato. Oracolo Python Fraction indipendente conferma B4=2/3, derivata0, XY=3/16 esatti e altezza esatta del picco `4150517416584649/41505174165846491136`mm per il polo letterale2,25e-4 del driver iniziale. Nei test il prodotto `(9/4)*1e-4` differisce nel polo di un ULP, con picco esatto `2767011611056433/27670116110564327424`mm: entrambi superano circa100 volte il budget richiesto; non si rivendica identità binaria fra queste due fixture. Build completa app PASS, compilazione seriale. Dopo la rigenerazione il primo comando multi-target ha completato l’app e si è fermato sul nome del nuovo target, caricato dal Makefile precedente; ripreso sul Makefile aggiornato, runner PASS in5,893s. Nessun errore del compilatore. **7/7 CTest PASS**,6,66s: surface_mapping, mapping_carrier, mapping_uv, mapping_diagonal, surface_curve_bounds, mapping_nonplane e mapping_global. Nuovi gruppi integrati10/10 PASS. **10/10 ASan/UBSan PASS**,2,308s, leak detection abilitata; strumentati provider, chiamante, main e due TU di regressione, resto kernel Release. Non è una verifica LSan dell’intera app. Mappa rigenerata in serie: **808 file/11796 relazioni/411 TU, zero errori**,772,612s; `--check` PASS. Il nuovo header ha richiesto la rianalisi completa. Verificati hash/configurazione, HTML e collegamenti tipizzati nei due versi: applicazione→wrap→provider e test→API/provider. Non ripetuti UI, avvio app o suite generale483.

## CI del checkpoint precedente

La CI44142011489 sul software78d7268 è conclusa: Linux CPU121/121 CTest PASS, macOS CPU/Metal124/124 PASS; compilazioni complete PASS. Tempi CTest1270,96s e1049,41s. Questo esito remoto non include le successive correzioni numeriche locali né il lavoro carrier descritto qui. [Run GitHub](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/44142011489).

## Passo successivo delimitato, non implementato

Per estendere la prova al top del box serve confrontare il carrier con `T+hN`, non conservare semplicemente `T`: lì h=0,001mm. Analisi Sol, nessun codice o runtime nuovo: con `D=F-T`, normale orientata `n=C/||C||`, bound `B≥||D×n||`, `R≥| ||D||²-h² |` e segno `h(D·C)≥0` dimostrati nel continuo, per `|h|≥hmin>0` si ottiene `e≤sqrt(B²+((R+B²)/hmin)²)`. Occorrono anche denominatore positivo e lower della normale strettamente positivo su tutte le porzioni attive. È una proposta per quota costante nonzero; non viene attribuito questo certificato alla correzione corrente. Quote variabili, zero attraversato e costi dei prodotti tensoriali restano da delimitare e verificare.

Pulizia dello staging concluso: rimossi7 ELF/oggetti ricreabili,331470992byte (316,1MiB); conservati sorgenti/log e tutti i42 pacchetti precedenti,6058 payload verificati invariati. Nessun CAD originale modificato o processo utente riavviato.

Correzione nuova e lavoro numerico precedente ancora **locali**, senza nuovi commit/push. Eseguibile app completo SHA256 `6774b6c487d8676a0325d6dd26ee9f43eff7bb56e9e8b6c9f28d227d1cdab57c`; non è stata riavviata l’app dell’utente. [Prove testuali e checksum](review-k10-carrier-globale-prove-2026-11-17/SHA256SUMS).
