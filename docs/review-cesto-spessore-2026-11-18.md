# Cesto.prt — diagnosi di Spessore a 2 mm

Verifica sulla copia di `Cesto.prt` SHA-256 `35b1e982e1e534c7096f0e7f7e6759213757b4baa621839097c6e3202f50d0bf`. Precisione 1e-7 mm. Il file contiene undici lavorazioni e nessuna lavorazione Spessore salvata. Sono stati provati i due risultati visibili a valle della costruzione, Cucitura 11 e Taglio 10. Il documento sorgente è stato soltanto letto.

Sulla lamina intera, Spessore lungo la normale si ferma prima dell'offset delle pelli, in circa 6–10 ms. Falliscono +2, −2 e metà per parte su entrambe le lamine. Su Cucitura 11 il primo spigolo rifiutato è E48. Anche 0,1 mm dà lo stesso rifiuto. Su Taglio 10 il bordo corrispondente è E43. Il controllo B-rep in ingresso non segnala problemi e le SP-curve sono presenti.

E48 corre da (15,5; 72; 3) a (−15,5; 72; 3). Separa F11, un piccolo gradino verticale, e F19, la faccia curva del fondo. L'angolo fra le normali misurato nei tre punti usati dal controllo è 84,884°, 82,077° e 84,884°. Il percorso normale passa alla gestione degli spigoli vivi oltre 2° soltanto quando può costruire chiusure planari sulle aperture. Su entrambe le lamine i tre contorni delle asole sono non planari e il percorso alternativo è escluso. Su Cucitura 11 le direzioni X, Y e Z vengono anch'esse rifiutate per cambio di verso della normale sul corpo intero.

**Verificato a 2 mm:** selezionando solo il fondo forato F19 di Cucitura 11, sia la normale sia l'asse Z producono un solido con 22 facce e zero bordi liberi, zero problemi al checker e 22 facce con genealogia. La prova lungo la normale impiega circa 4,25 s e lungo Z 2,75 s. La misura lungo Z è uno spessore direzionale. Nella GUI scegliere `Cucitura 11 → Scegli facce`, poi soltanto il fondo con le tre asole. L'operazione agisce sulla faccia selezionata.

Passano anche le prove separate su Superficie planare 1 e Riempimento 1, con rispettivamente 18 e 10 facce di uscita e nessun problema al checker. Queste prove non ispessiscono la lamina completa. Nessuna modifica al software o alle tolleranze per questa diagnosi.


## Solido dell’intero cesto

Su richiesta successiva è stato costruito il solido completo sulla stessa copia. Sostituito nella Cucitura 11 l’operando Taglio 10 con Raccordo 7, che conserva il fondo integro. Sweep 9 rimane nella cucitura, con la stessa tolleranza di 1e-5 mm. La lavorazione nativa Spessore lungo la normale verso l’interno, 2 mm, riesce in circa 17 s e produce 73 facce in un unico solido chiuso.

La sottrazione diretta dei tre prismi di Schizzo 6 incontra un rifiuto del tracciamento dell’intersezione sulla pelle interna F55. Le due asole laterali sottratte separatamente passano. Quella centrale fallisce anche con altezze diverse e dopo una sottrazione laterale. Per il risultato consegnato, i tre rettangoli XY da 50 × 5 mm di Schizzo 6 sono proiettati sulle due pelli F19 e F55 e poi collegati con dodici facce planari verticali. Il duplicato centrale dello schizzo non genera una quarta apertura. Il taglio proiettato rimuove tre regioni su ciascuna pelle. Scarti dichiarati 9,73e-9 e 9,95e-9 mm. Cucitura finale al budget esistente di 1e-6 mm.

Il risultato ha una sola shell, 85 facce, zero bordi liberi e nessun problema al checker. Nove punti interni al materiale nel solido senza asole risultano esterni dopo il taglio, tre per apertura. Tre punti adiacenti rimangono interni. Il volume passa da 53.457,64 mm³ a 51.945,86 mm³. Pareti delle asole lungo Z. Spessore di fondo, pareti e Sweep ottenuto lungo le normali.

La copia nativa ha UUID separato e mostra soltanto il solido finale con le tre asole. Conserva sei schizzi e le lavorazioni editabili fino allo spessore. L'ultimo corpo con i fori è salvato come geometria importata con definizione STEP incorporata. Verificate la rigenerazione di quella definizione senza cache, con le tre asole e il materiale adiacente, e la rilettura della copia con B-rep identico a quello controllato. Copia consegnata: `Cesto-solido-2mm.prt`, SHA-256 `21e8fe9fc9fc715ce08ca9c8409c4c014fb46ef3b40b0bdc5461040d25439a16`. L'originale ha lo stesso hash riportato all'inizio.


## Correzione nel kernel sulla Cucitura 11 originale

Lo spessore lungo la normale ora ammette, nel percorso per le lamine a spigoli vivi, i contorni interni compatibili su supporti B-spline nativi. Il kernel esclude provvisoriamente questi contorni durante il calcolo delle giunzioni delle pelli. Ripristina i fori nelle coordinate UV originali e unisce ciascuna coppia di bordi con una parete rigata. Conserva esattamente il bordo dal lato a quota zero e verifica a intervallo intero la distanza fra bordo e supporto anche dopo la cucitura. Le facce delle pareti riportano come generatore il bordo originale. Il budget di connessione di Cesto rimane 1e-6 mm.

Prova diretta di `thickenSheet` con l'intera B-rep di Cucitura 11, 2 mm nel verso opposto della normale. Un solo solido con una shell, 85 facce, 206 bordi, 126 vertici, zero bordi liberi e zero problemi al controllo esteso. Tutte le 36 facce di ingresso hanno discendenti. I dodici bordi originali delle tre asole generano dodici pareti. Il test sintetico della lamina bicubica con tre asole supera anche la classificazione del passaggio e del materiale adiacente.

Nel verso della normale e con metà spessore per parte, l'offset dei supporti a 2 mm totali segnala l'inversione di un bordo della cavità. Il controllo opera prima della ricostruzione delle asole e rimane attivo. Il risultato verificato usa **Tutta la superficie**, **Normale della superficie**, **Nel verso opposto**, **2 mm**.

La build Linux dell'applicazione è aggiornata. `CadViewport::createBody` ha applicato la funzione nativa al corpo intero in 18,1 s. La geometria serializzata di Cucitura 11 è identica all'ingresso e rimane tale dopo l'operazione. Il risultato conserva lo stesso corpo logico. Nove verifiche di classificazione, a tre profondità per asola, trovano il vuoto. I tre punti adiacenti sono nel materiale. La tassellazione a deflessione di 0,01 mm non perde facce.

La copia `Cesto-Spessore-nativo-2mm.prt` ha un'identità separata e dodici lavorazioni. L'ultima è **Spessore 2 mm**, collegata direttamente a Cucitura 11. Verificate riapertura con B-rep identico e rigenerazione della lavorazione dopo rimozione della sua geometria in cache. SHA-256 della copia `91b057a1bef161621c59e26c5d6db6fe6afff1416bd8a0429cf33f7f9a305059`. SHA-256 di `Cesto.prt` confermato invariato.

Delle otto suite CTest mirate, sette passano. Nella suite open-offset passano il nuovo caso con tre asole e i casi gola, continuazione e bordo in piega. I due casi storici Shower Head si fermano con esaurimento della profondità di quadratura. I due rifiuti sono riprodotti anche relinkando le versioni di `fk_thicken.cpp` e `fk_open_offset.cpp` precedenti alla correzione Cesto con la stessa quadratura locale.

La strumentazione localizza il rifiuto nella quadratura a 14 componenti del contorno delle facce B-spline durante il calcolo del segno del volume in `sewSheets`. Per l'esterno si osservano le facce 2 e 3, per l'interno 16 e 15. Le fin hanno una SP-curve dichiarata a 1e-6 mm, superiore al limite di uso diretto di 1e-8 mm del calcolo massa. Il percorso inverte il supporto a ogni nodo di quadratura. Sul primo caso il pannello del parametro si restringe a circa 3,7e-15 e la stima del solo errore sul volume, circa 6,6e-13 mm³, supera il budget locale di circa 5,5e-17 mm³. Usare come seme la SP-curve a ogni nodo, continuando l'inversione, riproduce ancora il rifiuto esterno.

Mappa rigenerata e verificata su 810 file, 11.860 relazioni e 413 unità di traduzione. Controllati i collegamenti dalla funzione dell'app a `thickenSheet`, da questa al percorso per lamine aperte e da qui a offset cucito, cucitura e certificato continuo dei bordi.

Controprova Shower Head su binario diagnostico. L'ammissione esplicita delle SP-curve certificate al budget di cucitura di 1e-6 mm, con suddivisione sui loro nodi, consente di completare separatamente entrambi i test storici. I limiti continui sulle quattro fin osservate sono compresi fra 8,67e-7 e 9,66e-7 mm. Il certificato al budget di massa da 1e-8 mm fallisce sulle due fin esterne. Questa variante rimane sperimentale. Il passo successivo è confinare la scelta del budget alla determinazione del verso del solido e verificare le proprietà di massa pubbliche e i solidi sottili.


### Correzione Shower Head in produzione

`sewSheets` usa `detail::sewnVolumeEstimate` soltanto per la stima preliminare del verso. Una SP-curve oltre il limite di uso diretto della massa entra nella quadratura solo se la dichiarazione e il limite continuo E/S/Q rispettano `min(tolleranza di cucitura, 1e-5 × diagonale)`. La quadratura di quella fin include anche i nodi della SP-curve. Una prova assente o inconclusiva lascia la fin al percorso di inversione, che usa l'edge come riferimento. Il calcolo fine conserva la soglia relativa già usata per gusci sottili e cancellazioni. L'ingresso pubblico `massProperties` continua a usare il suo budget.

Passano entrambi i test nativi Shower Head, interno ed esterno, nella suite open-offset. La suite supera tutti e sei i gruppi, inclusa la lamina bicubica con tre asole. Cinque suite CTest su cinque per offset aperto, spessore planare, cucitura, isolinee e quadratura. Il runner OCCT passa i gruppi Massa 11/11 e Spessore 6/6. Due scatole con orientamento invertito, dimensioni 10 × 5 × 0,001 mm e 10 × 5 × 3 mm, sono cucite con volume positivo 0,05 e 150 mm³. Verificata anche la B-rep originale di Cucitura 11 a 2 mm verso l'interno. Risultato con una shell, 85 facce, 12 pareti delle asole e controllo esteso senza problemi.

La stessa build supera anche Sew 12/12 e Shell 18/18, compreso il test di orientamento di un solido sottile. I moduli cucitura e massa strumentati con ASan/UBSan hanno ripetuto la prova analitica delle due scatole con leak detection attiva. La regressione Cesto di questo giro usa la B-rep congelata descritta nel rapporto. Il file di lavoro corrente ha un hash differente, 810b9b3666633eed16f5ca227098a4b7fe8ae35632ff3c95b4b70d33ad939b1d.

Il B-rep rigenerato da quella copia congelata coincide byte per byte con il risultato prima della correzione Shower. I due corpi hanno SHA-256 `be08d04b6536b02450715d526e624e514d152802bbcd34665dcc2fc15c40bc48`.


## Micro-geometrie al giunto del fondo e di Raccordo 7

Prova sulla versione di `Cesto.prt` SHA-256 `810b9b3666633eed16f5ca227098a4b7fe8ae35632ff3c95b4b70d33ad939b1d`, con dodici lavorazioni e Spessore 1 a 2 mm. La B-rep salvata e quella caricata coincidono nei cinque passaggi controllati, da Cucitura 6 a Spessore 1.

La parete interna di Estrusione 4 va da Z=0 a Z=3 mm. Il fondo Riempimento 1 incontra il bordo superiore a Z=3 mm. Raccordo 7 usa raggio costante 2,95 mm sulle due catene tangenti della corona inferiore. Porta il contatto dei fianchi a Z=2,95 mm e lascia otto fasce alte 0,05 mm. Un esempio è F9 di Taglio 10, larga 0,05 mm su 31 mm di lunghezza. E40 va da (−15,5; 72; 2,95) a (−15,5; 72; 3). La corona planare residua è larga 0,10 mm, pari a 6−2×2,95. Tutti gli otto spigoli corti hanno due facce e tolleranza locale di default. Taglio 10 aggiunge tre loop di quattro edge a F17. Le fasce, il bordo esterno del fondo e le loro adiacenze coincidono con Raccordo 7. Cucitura 11 cambia gli ID senza eliminare le fasce.

| Prova su copia | Esito |
| --- | --- |
| Raccordo 7 a 2,90 mm | Otto fasce, altezza 0,10 mm. B-rep valida. |
| Raccordo 7 a 3,00 mm | Rifiuto al limite di consumo simultaneo. Parametro 3,000000 all'estremo della parete. |
| Raccordo del giunto da 1 mm, dopo Taglio 10 | Rifiuto su E40 con intervallo 2,95–3,00. Scelto E43, segmento anteriore del perimetro del fondo, con propagazione. |
| Raccordo del giunto da 0,02 mm, dopo Taglio 10 | Riesce sull'intero contorno di otto bordi. Cucitura con Sweep 9 e Spessore 2 mm verso l'interno riescono. Un solido con 102 facce, nessun bordo libero o problema al checker, tutte le facce tassellate. Nove punti confermano il passaggio nelle asole e tre punti adiacenti confermano il materiale. |
| Raccordo 7 a 1,75 mm e nuovo raccordo del giunto da 1 mm | Il raccordo del giunto riesce. Dopo cucitura, lo spessore interno da 2 mm supera il raggio di curvatura 1,75 mm sulla faccia 21 e provoca il ripiegamento. |

L'app riporta le micro-geometrie rilevate come dimensioni o angoli da verificare. I messaggi in `cad_forge.cpp` e in Analisi → Micro-geometrie sono stati aggiornati e provati nell'app sulla copia, con l'errore del raggio da 1 mm e il pannello di analisi di Taglio 10. L'originale è stato soltanto letto.

Il flusso Raccordo 0,02 → Cucitura → Spessore è stato ripetuto anche con `CadViewport::createBody` sulla copia. La Cucitura usa il flag del documento per una superficie cucita, con 14 bordi liberi prima dello spessore. Sono confermati una shell solida, 102 facce, zero bordi liberi, zero problemi al checker e tutte e tre le asole passanti.
