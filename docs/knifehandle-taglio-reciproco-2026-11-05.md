# KnifeHandle: taglio reciproco di Corpo 4 e Corpo 5

## Riproduzione

Copia congelata in `forgecad-release/knifehandle-trim-qa/input.prt`; gli originali non vengono modificati dalle prove. Corpo4 è Offset10 (feature indice9, ID11), due superfici NURBS bicubiche208×1615 poli,137902 pezze ciascuna. Corpo5 è Estrusione11 (indice10, ID12), lamina con tre facce cilindriche. Entrambi passano checkBody.

La suddivisione nei due versi falliva con «intersectSurfaces: suddivisione per i semi troppo profonda». Il limite400000 veniva consumato dal prodotto cartesiano delle griglie, incluse coppie di pezze subito scartate perché disgiunte.

## Correzione

`fk_marching.cpp` filtra i box conservativi dei poli delle pezze prima della chiamata ricorsiva, riusando i box del secondo operando. Anche nei livelli ricorsivi il contatore viene incrementato solo dopo lo scarto dei box disgiunti. Il budget resta400000 nodi candidati e il limite di profondità resta40; tolleranze, Newton e tracciamento non cambiano. Il solo filtro iniziale non bastava sul caso reale: il problema si ripresentava nei figli disgiunti. Le iterazioni del filtro sono cancellabili. Nessuna modifica alle superfici o ai dati CAD.

## Verifiche concluse

- Copia congelata SHA256 `a0a3e3d9fcaf592603fd85f48f092f80ab4841b2a1b6f1d958a5761474ecad73`, uguale all'originale a fine prove. Nessuna scrittura sul CAD originale.
- Build completa di `forgecad-release/forgecad`, runner viewport e nuovo test indipendente conclusa.
- Documento reale: due regioni per Corpo4 e due per Corpo5; tutte e quattro le combinazioni ricucite con checkBody (compresi incroci dei loop) superato. Risultato confermato mantenendo Parte2 di ciascun corpo.
- Pannello reale Wayland: checkbox reciproco attiva, liste2+2, cambio delle porzioni e Conferma PASS. Le liste e Conferma sono azionate programmaticamente; non viene dichiarata una prova di clic fisico sulle regioni 3D. Anteprima pronta; screenshot del pannello e viewport finale ispezionati.
- Feature risultante valida, mesh completa e visibilità effettiva dei due operandi consumati verificata. I flag di preferenza Mostra/Nascondi dei corpi possono restare accesi nella storia; la scena esclude gli operandi consumati. Il primo harness pretendeva erroneamente che anche questi flag fossero spenti, poi è stato corretto per verificare la visibilità effettiva.
- Undo/Redo ripristina lo stesso B-rep binario; salvataggio della copia e riapertura PASS.
- 4/4 CTest PASS: ricerca semi fitta, strumenti superfici applicativi, genealogia completa e lettura documenti. Runner kernel: 10/10 Marching e40/40 Sheet PASS.
- Regressione indipendente: superficie piana NURBS con100701 pezze contro un cilindro. Il kernel precedente fallisce il budget; quello corretto trova il cerchio analitico completo senza semi iniziali, nei due ordini, con verifica delle SP-curve. Non viene usata una fixture CAD dell'utente.
- Mappa: 598 file,8332 relazioni,288 unità analizzate,zero diagnostiche. Check, collegamenti entranti/uscenti e rendering di panoramica più due dettagli PASS. Chiamate/include distinti; nessun arco reference per i due file modificati. `git diff --check` PASS.

## Riproducibilità e limiti

Artefatti locali ignorati in `forgecad-release/knifehandle-trim-qa/`: input, B-rep dei corpi, sonde, log, `KnifeHandle-trim-both-qa.prt`, `panel.png` e `viewport.png`. Le prove non certificano ogni contatto tangente o griglia arbitraria: il limite400000 continua a respingere una ricerca realmente troppo complessa. Nessun aumento di tolleranza, nessun uso di mesh per la geometria esatta e nessun cambio di formato/cache necessario.

La prima variante con solo filtro delle coppie iniziali passava la regressione sintetica ma falliva ancora sul documento: il conteggio dei figli disgiunti è stato corretto prima della build e delle prove finali. Sono validi i log con suffisso `final`; gli esperimenti precedenti restano diagnostici.


## Prosecuzione: Corpo 1 con Corpo 4

Nuova copia congelata in `forgecad-release/knifehandle-trim-1-4-qa/input.prt`, SHA256 `41cbffc5742347709e64df2d30ba0f059f908ff80b34c1fea97b8548bd3d5e4c`. Corpo1 è Estrusione9 (indice8, ID10), solido con6facce; Corpo4 è ora Taglio12 (indice11, ID13), il risultato del taglio reciproco precedente, lamina con6facce. I due carrier NURBS dell'offset mantengono137902 pezze ciascuno, contro788 per i principali carrier originali.

Il kernel precedente impiegava molto tempo a verificare la coincidenza e poi falliva ancora con «intersectSurfaces: suddivisione per i semi troppo profonda». Il semplice filtro dei box assiali non bastava: una superficie offset separata puo' restare dentro i box del carrier originale, soprattutto confrontando griglie di densita' molto diversa.

### Correzione integrata

`fk_marching.cpp` aggiunge un filtro conservativo per coppie di carrier B-spline, prima della coincidenza e della ricerca dei semi. Confronta le gerarchie dei box Bezier e suddivide anche una foglia grande prima di confrontarla con tutte le foglie piccole dell'altro operando. Dove servono, gli intervalli di proiezione dei poli su assi scelti dalle corde dimostrano la separazione degli inviluppi convessi. I pesi positivi richiesti dal costruttore NURBS garantiscono che le superfici stiano negli inviluppi; le normali delle corde scelgono solo gli assi, senza decidere la geometria da campioni.

Il margine numerico rende incerti i casi vicini alla soglia. Il filtro ha un proprio limite400000 visite e12 suddivisioni per foglia: quando non dimostra la disgiunzione restituisce il controllo al percorso completo precedente. Non aumenta il budget della ricerca dei semi, non modifica tolleranze, coincidenze, tracciamento, superfici, mesh di calcolo o formato dei documenti. Le prove sperimentali che modificavano anche la coincidenza non sono integrate.

### Verifiche finali

- Build completa di `forgecad`, runner viewport e kernel conclusa. Gli avvisi preesistenti sugli inizializzatori di LoftContact restano; nessun errore di build.
- Due regioni da Corpo1 e tre da Corpo4. Tutte le sei combinazioni ricucibili superano checkBody, inclusi gli incroci dei loop.
- `forgeTrimBoth` diretto e pannello reale Wayland: mantenute **Parte2 di Corpo1 e Parte1 di Corpo4**, Conferma, geometria e tassellazione PASS. Liste e pulsanti azionati programmaticamente; non si dichiara una prova di clic fisico sulle regioni 3D. Pannello a tutta altezza e viewport ispezionati.
- Risultato: **una componente connessa,12facce,zero bordi liberi**, conservata come lamina chiusa secondo il comportamento esistente di Taglia superfici. Non viene promossa automaticamente a solido volumetrico. Gli operandi consumati sono esclusi dalla scena; le preferenze storiche Mostra/Nascondi possono restare accese.
- Undo/Redo con identita' del B-rep binario e salvataggio/riapertura PASS. Copia risultante: `forgecad-release/knifehandle-trim-1-4-qa/KnifeHandle-taglio-corpi-1-4.prt` (circa19MB). Hash dell'originale uguale all'input congelato a fine prove.
- Cinque CTest distinti PASS, eseguiti in due gruppi (uno ripetuto): document_read, all_history, surface_tools, marching_seed_grid e boolean_seam. Runner kernel: **10Marching+40Sheet+38Boolean+42Loft=130/130 PASS**.
- Regressione estesa: piani razionali paralleli distinti con1 e100701pezze, nei due ordini; intersezione trasversale NURBS verificata sulla retta analitica e sulle SP-curve; coincidenza esatta e entro la tolleranza conservata. Il runner senza il nuovo filtro supera il timeout60s; quello finale completa l'intero test in circa1,04s. Sul documento reale il kernel precedente ha anche riprodotto l'errore di budget: il timeout sintetico non viene presentato come lo stesso messaggio.
- Mappa **598file/8332relazioni/288unità/zero diagnostiche**, check e rendering di panoramica e due dettagli PASS. Verificati collegamenti entranti/uscenti call/include; nessun arco reference nei due file interessati. Descrizioni aggiornate nel generatore. `git diff --check` PASS.

La correzione accelera le esclusioni dimostrate; i casi incerti possono mantenere costi e limiti precedenti. Nessuna riduzione dei poli del CAD e nessuna soluzione universale per intersezioni arbitrarie dichiarata. Artefatti, sonde e log restano nella directory QA ignorata; nessuna modifica al CAD originale, nessun commit/push eseguito in questa prosecuzione.
