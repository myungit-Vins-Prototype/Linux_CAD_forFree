# Review trasversale del 20 novembre 2026

Working tree Linux su `6294aa3`, comprese le modifiche locali successive al commit. Il primo intervento della review corregge l'orientamento nello scambio STEP e IGES. Il secondo intervento corregge la cucitura di più gusci. Il terzo intervento corregge il falso successo K10 sui pesi variabili.

## Scambio STEP e IGES verificato

Il cubo inclinato da 1 mm con origine (36 000 000, −27 000 000, 51 900 000) mm spariva dal file STEP. Lo stesso cubo in IGES a superfici rientrava con volume negativo. Entrambi i percorsi ricavavano il verso da tetraedri costruiti sulla mesh rispetto all'origine globale. Il kernel ora integra il flusso sul B-rep di ogni guscio in coordinate locali. Ripete il calcolo con precisione più alta per i volumi sottili o con cancellazione fra facce. Se il segno rimane indeterminato produce un errore esplicito. [Calcolo del volume](../kernel/fk_mass.cpp), [lettore IGES](../kernel/fk_iges.cpp), [riproduttore](review-generale-prove-2026-11-20/scambio.cpp.txt).

Il test sulla cavità ha rivelato una seconda causa. Il writer applicava due volte l'inversione del guscio interno. Un cubo da 27 mm³ con un vuoto di 1 mm³ rientrava a 28 mm³. Adesso rientra a 26 mm³, oppure a 27 mm³ complessivi quando si aggiunge un secondo solido da 1 mm³. Il writer identifica l'esterno che contiene la cavità con il classificatore B-rep della singola shell, all'interno della regione di appartenenza. Segnala un guscio entrante isolato e una falsa cavità nella tacca di un solido concavo. Il controllo presuppone gusci validi privi di intersezioni reciproche. [Writer STEP](../kernel/fk_step.cpp), [classificatore](../kernel/fk_classify.cpp), [regressioni](../kernel/tests/test_exchange_robustness.cpp).

| Prova | Risultato |
| --- | --- |
| Cubo del difetto | Un solido, sei facce, zero note e checker geometrico pulito in entrambi i formati. Volume STEP 0,999999997401 mm³, IGES 0,999999997160 mm³. |
| Posa e scala | 32 round-trip su due pose, otto traslazioni e due formati. Errore massimo di volume 1,08 × 10⁻⁷ mm³ sul cubo da 1 mm³. Alla traslazione più grande (fattore 10⁹ mm) il cubo inclinato d'ingresso ha già 15 rilievi del checker. Verificata separatamente una parete planare spessa 0,001 mm all'origine e lontano dall'origine. |
| Suite | Undici test nativi di robustezza, compresa una shell chiusa di due facce coincidenti con volume esattamente zero e diagnosi esplicita. Il runner con Open CASCADE riporta 22/22 STEP, 9/9 IGES e 11/11 massa. Cinque test STEP prevedono file SolidWorks facoltativi assenti nel checkout. La cavità è stata riletta anche da Open CASCADE nei tre schemi AP203, AP214 e AP242, verificando validità e volume. |

**Round-trip STEP sulle copie dei documenti.** Sono stati esportati in memoria i corpi con geometria salvata e flag di visibilità attivo. In Supporto.prt rientrano tre solidi, rispettivamente di 3, 4 e 364 facce, senza note. La differenza massima di volume è inferiore a 0,00002 mm³. In Cesto.prt rientrano quattro lamine e «Spessore 1», un solido a una shell di 102 facce. Il suo volume con segno passa da 25.783,0619185 a 25.783,069272 mm³. Il lettore segnala tolleranze di bordo fino a 0,000010 e 0,000035 mm. La prova utilizza i B-rep memorizzati nelle copie e misura gli effetti dello scambio. La prova di rigenerazione della storia è riportata in fondo.

Ricompilati il kernel Release, i runner e l'eseguibile ForgeCAD. Il CTest dello scambio e il controllo mirato di classificazione sono passati. Al termine dell'intervento sullo scambio la mappa locale conteneva 804 file e 11.874 relazioni su 413 unità compilate. STEP e IGES chiamano entrambi il volume per shell. Il classificatore della singola shell è utilizzato dal writer STEP per le cavità.

## Cucitura di più gusci

Il segno del volume dell'intero corpo mascherava un errore di orientamento locale. Prima della correzione fallivano 44 delle 64 combinazioni di ordine e verso di due o tre gusci. Con cubi disgiunti da 8 e 1 mm³ il piccolo poteva essere sottratto. Con il cubo piccolo annidato e i due versi concordi la cavità poteva essere sommata. Il checker topologico accettava entrambi i risultati. [Riproduttore](review-generale-prove-2026-11-20/cucitura.cpp.txt).

Cucitura ora usa il volume con segno riferito a ogni singolo guscio. Ogni contorno che lo contiene inverte il lato del materiale. I gusci esterni e le isole dentro una cavità hanno normale uscente, le cavità hanno normale entrante. Il kernel usa le sezioni B-rep sulle coppie di facce con box sovrapposti. Per due sfere intere la distanza dei centri e i raggi distinguono anche le tangenze puntuali. Il riassemblaggio usa la corrispondenza tra le facce grezze e le facce emesse, conservando l'origine dichiarata di ogni faccia. [Cucitura](../kernel/fk_sew.cpp), [regressioni](../kernel/tests/test_sew_pcurve_validation.cpp).

Lo scambio e Cucitura condividono il calcolo del volume locale per guscio e il ciclo di quadratura a riduzione ordinata. La prima stima di Cucitura può usare le SP-curve certificate entro il budget di connessione e la scala locale. Per il calcolo fine vale il budget delle proprietà di massa. La verifica delle intersezioni eredita le famiglie supportate dalle sezioni del kernel. I contatti isolati su superfici libere restano fuori dalla copertura dimostrata. La rappresentazione topologica mantiene la convenzione esistente di più gusci nella regione solida del corpo cucito.

**Verifica di accettazione.** I cubi disgiunti tornano a 9 mm³ in entrambi gli ordini. Il caso annidato dà 7 mm³ anche partendo da due gusci uscenti, con il punto nella cavità classificato esterno. Con una terza isola il volume è 7,125 mm³. Il riproduttore originale dà zero bordi liberi e zero rilievi del checker. Il runner di Cucitura supera 90 casi, compresi i 64 ordini/versi, il cubo nella tacca della U, i prismi che si attraversano senza vertici interni e le due tangenze oblique fra sfere. Il controllo di genealogia verifica origine e discendente delle facce.

Build Release di ForgeCAD completata. Superati i 12 casi della suite kernel filtrata per Cucitura, gli 11 di proprietà di massa, gli 11 di robustezza dello scambio, i due Shower Head e il caso del fondo curvo con tre asole. Mappa finale verificata su 804 file, 11.888 relazioni e 413 unità compilate senza errori di analisi.

## K10 — Rilievo perso su superficie razionale

Prima della correzione, nel wrap di una lamina diagonale un rilievo di 0,0001 mm spariva su un target razionale. L'operazione accettava il risultato e dichiarava una deviazione di 6,70715 × 10⁻¹¹ mm con tolleranza 0,000001 mm. Il controllo continuo classificava il target a pesi variabili come famiglia esclusa, lasciando che il fit campionato decidesse l'esito. Il riproduttore storico applicato al kernel corretto rifiuta il rilievo e accetta il controllo piatto, sia per la superficie nativa sia per la sua istanza traslata. Sul piatto nativo dichiara un limite di 1,62 × 10⁻⁸ mm, con uno scarto al punto di controllo di circa 3,08 × 10⁻¹⁴ mm. [Variante storica](review-generale-prove-2026-11-20/pesi-variabili.patch.txt), [regressioni sul comando](../kernel/tests/test_mapping_normal_validation.cpp).

Il kernel lavora sui coefficienti omogenei della cella attiva. Verifica che il denominatore sia positivo su ciascun rettangolo valutato, inclusi i parametri di bordo. Limita il residuo di posizione e, se la quota è diversa da zero sul dominio, anche l'errore dovuto alla direzione della normale. Le derivate naturali vengono trasformate prima di determinarne il verso, compresa la riflessione di un'istanza. Se il limite richiesto resta indimostrato alla fine delle suddivisioni, la famiglia riconosciuta restituisce un rifiuto definitivo. Il comando conserva sorgente, genealogia e deviazione precedente in caso di rifiuto. [Implementazione](../kernel/fk_surface_mapping_bounds.cpp), [contratto](../kernel/fk_surface_mapping_bounds.h).

| Prova | Esito |
| --- | --- |
| Rilievo e controllo piatto | Esiti corretti per UV diagonali, oblique di rango due e oblique di rango incerto. Confermata anche l'istanza traslata. |
| Carrier realmente curvo | Con quota di 0,001 mm il limite continuo racchiude i residui misurati su 33 × 33 punti, bordi inclusi. Il limite richiesto di 10⁻⁹ mm viene rifiutato. |
| Denominatore e normale | Rifiutati sia il passaggio del denominatore per zero nell'estrapolazione di bordo sia la normale degenere a quota non nulla. Sull'istanza riflessa passa l'orientamento corretto anche quando la quota attraversa zero. |
| Perimetro | Sorgente piana a quota affine, target B-spline non periodico di grado 2 o 3 in ciascuna direzione con pesi positivi, immagine polinomiale bicubica. La prova riguarda il residuo geometrico continuo sul rettangolo UV. Il seguito estende la copertura ai target periodici e ad altri gradi. La verifica della topologia dopo il ritaglio resta aperta. |

[Controlli diretti di posizione e denominatore](../kernel/tests/test_surface_mapping_bounds.cpp) e [controlli di orientamento](../kernel/tests/test_surface_mapping_normal_bounds.cpp).

ForgeCAD Release ricompilato. Superate le sei suite CTest di mappatura, con 44/44 prove nel runner del controllo globale. Le stesse 44 prove sono passate con AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer sul certificatore e sulle unità di test, collegati al resto del kernel Release. La mappa locale è aggiornata su 804 file, 11.888 relazioni e 413 unità senza errori di analisi. Risolve le chiamate al certificatore dal comando e dai test. Libclang indicizza le funzioni del ramo razionale, ma la vista non espone tutte le chiamate interne dipendenti da template.

## K10 — Periodicità e gradi misti

Nel wrap a faccia singola la selezione del controllo continuo dipendeva dai flag di periodicità e dai gradi della superficie. Con la versione precedente un rilievo di 0,0001 mm poteva sparire con tolleranza di 0,000001 mm. Il difetto era riproducibile con periodicità in U o in V e con gradi 1×3, 4×3 e 5×3. Nei casi a gradi misti il comando ora rifiuta la mappatura prima di pubblicare corpo, genealogia e deviazione. Il controllo piatto passa, anche sul target razionale di grado 5×3 con mappa obliqua.

Per i target B-spline il kernel esamina i tratti di nodo nei periodi interessati dalla mappa. Per le normali dei target polinomiali sopra il terzo grado riusa il controllo di orientamento e apertura angolare già impiegato per i target razionali. Una prova indipendente verifica il massimo di una superficie di grado 5×4 con altezza proporzionale a u⁵. I polinomi di lavoro sono limitati al grado 20 in ciascuna coordinata sorgente, con al massimo 256 coppie di celle per rettangolo e 16.384 unità di lavoro pesate. Il superamento del budget interrompe la prova della famiglia riconosciuta. [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [prove sui gradi](../kernel/tests/test_surface_mapping_normal_bounds.cpp).

Il test integrato sul solido ripetuto su una B-spline 3×1 a 90° ha distinto due cause. Sulla seconda faccia del primo solido il residuo misurato superava 2,13 × 10⁻⁸ mm, con un limite interno di 2 × 10⁻⁸ mm. Il comando ora ritenta una sola volta il fit campionato con metà del budget, poi ricontrolla il risultato sul limite continuo originale. Se i coefficienti del secondo fit sono identici, riusa il rifiuto già dimostrato. Sulla prima faccia era la verifica a esaurire le visite. Sulle facce verticali con UV diagonali e quota che arriva a zero, la prova sfrutta la proiezione della normale naturale e dà precedenza alle suddivisioni nella direzione in cui questa varia. Per le mappe quasi separabili, il kernel mantiene in intervallo la componente trasversale su tutta la cella, riducendo i prodotti polinomiali. Una regressione controlla che una componente minuscola su una corsa molto lunga continui a produrre il rifiuto dovuto allo spostamento fisico. [Comando](../kernel/fk_surface_mapping.cpp), [prova sul riempimento](../kernel/tests/test_surface_mapping.cpp).

Trovato anche un blocco nel calcolo dei nodi periodici di una superficie di estrusione con profilo chiuso. A un indice di periodo pari a 2⁵³ l'incremento in double non faceva avanzare il ciclo. L'eseguibile precedente superava il timeout su un intervallo ridotto a un punto. Ora il kernel restituisce il punto, controlla gli indici e il numero di giri per gli intervalli estesi e ammette la cancellazione durante il ciclo. Verificati indici positivi e negativi, intervalli invertiti e nodi su più giri ordinari. [Implementazione](../kernel/fk_surface.cpp), [regressione](../kernel/tests/test_surface_parameterization.cpp).

**Verifiche finali.** Ricompilati kernel e applicazione Linux Release. Superate tutte le otto suite CTest di mappatura, strumenti superficie e parametrizzazione, comprese 51 prove del controllo globale. La giunzione curva regolare è accettata sui due assi, e un salto di 0,001 mm viene respinto. Superate 56 prove con AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer sui moduli di mappatura, verifica del residuo, superfici e relative unità di test, collegati al resto del kernel Release. Verificata la mappa su 804 file, 11.889 relazioni e 413 unità senza errori di analisi.

## K10 — Wrap su estrusione aperta

Il wrap di una lamina diagonale su una superficie di estrusione con generatrice B-spline cubica accettava la perdita di un rilievo di **0,0001 mm** con tolleranza di 0,000001 mm. Dichiarava 3,77446 × 10⁻¹¹ mm sia per il rilievo sia per il controllo piatto. La stessa perdita era riproducibile ritagliando la generatrice all’intervallo da 0 a 0,9. Il percorso di estrusione dei bordi di schizzo può conservare questo tipo di ritaglio. [Riproduttore pubblico](review-generale-prove-2026-11-20/estrusione-wrap.cpp.txt), [estrusione](../kernel/fk_extrude.cpp), [conversione delle curve dello schizzo](../kernel/fk_curve_ops.cpp).

Il certificatore ora valuta la curva dai suoi poli e pesi originali su tutti i tratti di nodo visitati e sottrae dall’immagine la traslazione lungo la direzione di estrusione. La coordinata longitudinale può essere negativa o maggiore di uno. Per le quote diverse da zero il kernel verifica anche la normale orientata usando la derivata della generatrice e la direzione di estrusione. Le facce estruse condividono con i target tensoriali la suddivisione della bicubica emessa, il controllo delle normali e i limiti di lavoro. I ritagli mantengono il parametro della curva base e il dominio esterno effettivo. [Calcolo del residuo](../kernel/fk_surface_mapping_bounds.cpp), [contratto](../kernel/fk_surface_mapping_bounds.h).

Il riproduttore ora rifiuta il risultato con il rilievo perso prima di pubblicare il corpo. Sul controllo piatto il limite dichiarato è 1,14320 × 10⁻⁸ mm e lo scarto al punto di verifica è 2,06 × 10⁻¹⁵ mm. La regressione controlla sorgente, generatrice, genealogia e deviazione, anche quando il chiamante omette la genealogia. Le prove dirette comprendono pesi variabili, normale e verso opposto, un residuo pari a un ULP dopo una traslazione di 2⁴⁰ mm e l’uscita dal dominio di un ritaglio annidato. Per una generatrice con curvatura realmente razionale, il limite continuo racchiude tutti i residui misurati su 33 × 33 punti. [Regressione del comando](../kernel/tests/test_mapping_normal_validation.cpp), [prove del certificatore](../kernel/tests/test_surface_mapping_bounds.cpp).

Ricompilata l’applicazione Linux Release. Superate le otto suite kernel di mappatura, parametrizzazione e strumenti superficie, incluse le 55 prove di validazione globale. Superati anche i sei test mirati eseguiti con AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer abilitati su mappatura, certificatore, superficie, curva B-spline e unità di test.

Il perimetro verificato riguarda facce sorgenti piane, immagini bicubiche polinomiali native ed estrusioni aperte con generatrice B-spline nativa, anche ritagliata. Il passo successivo sui profili chiusi è documentato qui sotto. Il caso delle generatrici con trasformazione affine è trattato più avanti. Restano le altre famiglie di curve e le mappature multifaccia e quilt. [Selezione dei percorsi](../cad_surface_mapping.cpp), [mappa quilt](../kernel/fk_quilt_mapping.cpp).

## K10 — Giunzione dei profili estrusi chiusi

Su una lamina che attraversa la giunzione di una generatrice B-spline chiusa, il comando di wrap accettava uno scarto di **0,0001 mm** con tolleranza di **0,000001 mm**. Dichiarava una deviazione di **5,74 × 10⁻¹¹ mm**. Il certificatore escludeva le estrusioni con U periodica e il controllo successivo per campioni mancava il rilievo a U = 3/16. Ora lo stesso comando rifiuta il risultato prima di aggiornare la genealogia o la deviazione. Sul controllo liscio accetta il passaggio della giunzione con limite di 1,86 × 10⁻⁸ mm e scarto locale misurato di 1,02 × 10⁻¹³ mm. Entrambi gli esiti sono verificati con e senza genealogia. [Regressione del comando pubblico](../kernel/tests/test_mapping_normal_validation.cpp).

Il kernel riutilizza il calcolo dei tratti di nodo delle superfici periodiche. Per le estrusioni applica il periodo del profilo esposto, compresi i ritagli, e racchiude gli arrotondamenti della riduzione usata da `ExtrusionSurface::wrap`. Conserva gli estremi nativi, incluso il caso in cui al nodo finale cambia la derivata. La suddivisione tiene conto della porzione effettivamente esposta di ciascun tratto. Le prove coprono più giri in entrambe le direzioni, offset lungo la normale, un estremo ritagliato al limite della tolleranza del costruttore e indici di periodo oltre la precisione utile. [Implementazione](../kernel/fk_surface_mapping_bounds.cpp), [prove dirette](../kernel/tests/test_surface_mapping_bounds.cpp).

Ricompilata l’applicazione Linux Release. Superate tutte le otto suite kernel interessate, compresi i 62 test di validazione globale. Gli 11 test mirati alle estrusioni sono passati anche con AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer attivi sulle unità di mappatura, superficie, B-spline e test. Il confronto riguarda il kernel Linux e la nuova regressione riproducibile.

## K10 — Estrusione con generatrice trasformata

Su una generatrice B-spline riflessa rispetto al piano X = 0,125 mm, il comando di wrap perdeva un rilievo di 0,0001 mm con tolleranza 0,000001 mm. Dichiarava una deviazione di 3,77 × 10⁻¹¹ mm. Il certificatore riconosceva la B-spline attraverso i ritagli, ma la presenza di una trasformazione lasciava al solo controllo per campioni la verifica dell’interno della lamina. Ora il comando rifiuta quel risultato prima della pubblicazione del corpo, della genealogia e della deviazione. Sul controllo liscio dichiara 1,14 × 10⁻⁸ mm, con scarto di 2,06 × 10⁻¹⁵ mm nel punto controllato. Esiti verificati con e senza genealogia. [Regressione sul comando](../kernel/tests/test_mapping_normal_validation.cpp).

La verifica continua segue ritagli e trasformazioni nell’ordine della loro applicazione. Calcola separatamente posizione e tangente della curva. La direzione dell’estrusione rimane quella definita nello spazio del modello. La normale deriva dal loro prodotto vettoriale. Una trasformazione singolare può quindi produrre una superficie con una normale valida. Le prove comprendono rotazione, riflessione, matrice di taglio, pesi variabili, offset sul lato opposto e ritagli annidati. La periodicità considera gli estremi dopo la trasformazione, sia quando una scala chiude un piccolo scarto sia quando lo riapre. [Implementazione](../kernel/fk_surface_mapping_bounds.cpp), [prove dirette](../kernel/tests/test_surface_mapping_bounds.cpp).

Il calcolo per intervalli copre anche gli arrotondamenti delle singole trasformazioni della posizione e della derivata. Nel test numerico con traslazioni successive di +2⁵² mm e −2⁵² mm, il punto interno a X = 0,25 mm viene valutato a X = 0 mm. Trattando le due traslazioni come esattamente inverse il controllo accettava la candidata. La regressione ora verifica il rifiuto. La prova di stress usa coordinate intermedie oltre la normale dimensione del modello. Restano applicabili i limiti di lavoro del certificatore. Una catena di oltre 32 trasformazioni viene rifiutata.

Ricompilata l’applicazione Linux Release. Superate le otto suite kernel di mappatura, parametrizzazione e strumenti di superficie. Dopo l’aggiunta del caso sulla chiusura degli estremi, la suite globale è passata con 68 prove su 68. Sedici test mirati sulle estrusioni sono passati con AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer sulle unità di mappatura, curve, trasformazioni, superfici e test. Il caso aggiuntivo sugli estremi è stato eseguito in Release.

## K10 — Estrusioni di cerchi ed ellissi

Nel wrap di due lamine su estrusioni di coniche, entrambe con tolleranza di 10⁻⁶ mm, il controllo per campioni pubblicava una deviazione inferiore allo scarto in un punto interno. Il risultato geometrico di entrambe le operazioni era entro la tolleranza. Il difetto riprodotto riguarda il limite comunicato al chiamante. Le geometrie attraversano la giunzione in U e i punti testimone si trovano all’interno delle rispettive facce. Le prove confrontano l’immagine con posizione e normale analitiche. [Regressioni pubbliche](../kernel/tests/test_mapping_normal_validation.cpp).

| Generatrice | Scarto nel punto testimone (mm) | Deviazione precedente (mm) | Deviazione dopo la correzione (mm) |
| --- | ---: | ---: | ---: |
| Cerchio di raggio 1 mm | 7,14194 × 10⁻¹¹ | 6,43890 × 10⁻¹¹ | 2,83068 × 10⁻¹⁰ |
| Ellisse di semiassi 1 e 0,7 mm | 6,61634 × 10⁻¹¹ | 6,16595 × 10⁻¹¹ | 2,03931 × 10⁻¹⁰ |

Il kernel racchiude seno e coseno con polinomi cubici locali e un resto valido su tutto il rettangolo sorgente. Il parametro rimane l’angolo originale. La verifica usa la tangente della conica e la direzione dell’estrusione per controllare il verso e la regolarità della normale, anche se la quota passa da zero. Gli archi conservano il dominio del ritaglio. Quando gli estremi chiudono entro la risoluzione lineare, il riporto usa il periodo effettivo del ritaglio. [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [prove dirette](../kernel/tests/test_surface_mapping_bounds.cpp).

Il test di integrazione costruisce due solidi con Estrusione a partire da un arco e dalla corda di chiusura, poi applica il wrap alla faccia laterale ottenuta. Per il cerchio la superficie è cilindrica. Per l’ellisse conserva una generatrice ritagliata. Il confronto tra le Release prima e dopo su altri 36 casi con semiassi, quote, ampiezze e direzioni diverse dà 26 successi e 10 rifiuti da entrambe le versioni. Nei due casi della tabella lo scarto misurato rimane identico con e senza genealogia. [Scelta della superficie laterale](../kernel/fk_extrude.cpp), [prove del comando](../kernel/tests/test_mapping_normal_validation.cpp).

La regressione del solido 4 × 2 × 1 mm avvolto sul cilindro di raggio 20 mm ha evidenziato un rifiuto indebito nella prima versione della verifica continua. Sulla parete in cui la quota passa da 0 a 1 mm le componenti della normale ruotano. Il kernel sceglie ora la lunghezza di riferimento direttamente dal polinomio del quadrato della norma e racchiude la differenza di lunghezza usando il limite inferiore di regolarità. Verificati il volume atteso di 8,2 mm³, i 24 usi dei bordi, la genealogia e la chiamata senza genealogia con la tolleranza originale di 10⁻⁶ mm. [Prova sul solido](../kernel/tests/test_surface_mapping.cpp), [controllo dei bordi](../kernel/tests/test_mapping_nonplane_validation.cpp).

La riduzione al quadrante e gli intervalli di seno e coseno sono condivisi con la proiezione delle eliche. Un confronto indipendente con MPFR a 256 bit ha verificato 1.944 valori, inclusi angoli negativi, quadranti lontani e intervalli a cavallo degli assi. Un indice oltre la precisione ammessa viene rifiutato. [Calcolo condiviso](../kernel/fk_angular_bounds.h).

Ricompilata l’applicazione Linux Release. Superate tutte le nove suite interessate, comprese le 77 prove di validazione globale e il test della proiezione delle eliche. I nove nuovi casi sulle coniche e il caso del solido cilindrico passano anche con AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer sulle unità di mappatura, curve, superfici e test. Mappa verificata su 805 file e 413 unità di compilazione, inclusi i collegamenti al calcolo trigonometrico condiviso.

Restano da verificare le coniche racchiuse in trasformazioni affini, le superfici di rivoluzione e il wrap su più facce. La prova di questo intervento riguarda la mappatura di facce piane con immagine bicubica polinomiale.

## K10 — Riflessione e scala non uniforme delle coniche estruse

Su due lamine piane con tolleranza di 10⁻⁶ mm, il wrap di una generatrice trasformata saltava il controllo continuo e pubblicava lo scarto ricavato dai campioni. Le misure nello stesso punto interno della faccia sono riproducibili sia passando la genealogia sia omettendola. In entrambi i casi la geometria ottenuta era entro la tolleranza.

| Generatrice | Scarto misurato (mm) | Deviazione prima (mm) | Deviazione dopo (mm) |
| --- | ---: | ---: | ---: |
| Cerchio riflesso nel piano x = 0,125 mm | 7,92220 × 10⁻¹¹ | 7,22271 × 10⁻¹¹ | 3,26602 × 10⁻¹⁰ |
| Ellisse con scala 1,5 lungo X | 1,53946 × 10⁻¹⁰ | 1,43952 × 10⁻¹⁰ | 4,83396 × 10⁻¹⁰ |

Il kernel legge una sola volta la sequenza dei ritagli e delle trasformazioni della generatrice. Cerchi, ellissi e B-spline condividono il calcolo degli intervalli di arrotondamento dei singoli passaggi. Le coniche conservano l’angolo originale. Per il verso dell’offset conta il prodotto vettoriale fra la tangente trasformata e la direzione di estrusione nello spazio del modello. [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni del comando](../kernel/tests/test_mapping_normal_validation.cpp).

Le prove dirette verificano il cambio di verso, il rifiuto dell’offset opposto, gli archi con ritagli annidati a cavallo di 2π e la chiusura fisica che cambia dopo la scala. Una proiezione singolare che conserva la normale passa. Se la tangente si annulla in un punto interno, il wrap viene rifiutato anche a quota zero. Due traslazioni opposte di 2³⁰ mm perdono una coordinata di 2⁻²⁴ mm nella valutazione intermedia. La verifica racchiude questo errore e rifiuta il budget di 10⁻⁸ mm. [Regressioni dirette](../kernel/tests/test_surface_mapping_bounds.cpp).

Su 108 combinazioni con cerchi riflessi ed ellissi scalate, il confronto nella stessa Release con il riconoscimento delle coniche trasformate attivo e disattivato dà gli stessi 93 successi e gli stessi 15 rifiuti per inversione o collasso. In ciascun successo sono stati confrontati 645 punti con un oracolo analitico. I casi con almeno un campione sopra la deviazione dichiarata passano da cinque a zero, quattro dei cinque superamenti iniziali erano entro 2,22 × 10⁻¹⁶ mm. Con il controllo continuo attivo lo scarto massimo misurato è 1,89 × 10⁻⁸ mm. Le 36 geometrie di controllo a generatrice nativa conservano separatamente i 26 successi e i 10 rifiuti precedenti.

L’applicazione Linux Release è stata ricompilata. Passano tutte le otto suite di mappatura e superfici selezionate, incluse le 83 prove del runner di validazione globale. Nelle due unità di test del comando e del certificatore passano 61 prove con AddressSanitizer, LeakSanitizer e UndefinedBehaviorSanitizer, strumentando anche le unità di mappatura, curve, superfici e trasformazioni. Mappa verificata su 805 file e 413 unità di compilazione, con controllo delle chiamate dal wrap e verso le curve e il calcolo trigonometrico.

Nel filone K10.3 restano le superfici di rivoluzione e il wrap multifaccia. La copertura qui verificata richiede una faccia sorgente piana e un’immagine bicubica polinomiale.

## K10.3 — Rivoluzioni e wrap su più facce

Nel wrap su una superficie di rivoluzione con meridiano rettilineo la deviazione pubblicata poteva essere inferiore allo scarto misurato all’interno della lamina. Nei due riproduttori la geometria era entro la tolleranza di 10⁻⁶ mm. Con e senza genealogia si ottengono le stesse misure. [Regressioni del comando](../kernel/tests/test_mapping_normal_validation.cpp).

| Meridiano | Scarto nel punto testimone (mm) | Deviazione prima (mm) | Deviazione dopo (mm) |
| --- | ---: | ---: | ---: |
| Parallelo all’asse | 1,21421 × 10⁻¹⁰ | 1,10147 × 10⁻¹⁰ | 4,03598 × 10⁻¹⁰ |
| Inclinato | 1,68591 × 10⁻¹⁰ | 1,52396 × 10⁻¹⁰ | 5,86767 × 10⁻¹⁰ |

Il controllo continuo ruota il meridiano nel suo parametro originale e usa le due derivate naturali per verificare l’offset. Comprende rette, cerchi, ellissi e B-spline a pesi positivi, con i ritagli e i collocamenti della curva. Le estrusioni e le rivoluzioni condividono il calcolo in coordinate omogenee della posizione e della tangente delle B-spline. Il kernel tratta anche cono, sfera e toro nelle parametrizzazioni canoniche effettivamente emesse da Rivoluzione. Le verifiche dirette includono la giunzione angolare, un polo, il ramo interno del toro fusiforme, una mappa obliqua a quota che attraversa lo zero e un rilievo stretto di 0,0001 mm sul meridiano. È coperta anche una faccia prodotta da Rivoluzione da un arco ellittico e dalla corda. [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni dirette](../kernel/tests/test_surface_mapping_bounds.cpp).

| Confronto delle configurazioni | Esiti identici prima e dopo | Successi con almeno uno scarto campionato superiore al dichiarato, prima → dopo |
| --- | ---: | ---: |
| Meridiano rettilineo, due pose e tre pendenze | 72 successi | 7 → 0 |
| Faccia generata dall’arco ellittico ritagliato | 9 successi, 3 rifiuti per dominio | 1 → 0 |
| Coni, sfere, tori regolari e fusiformi | 56 successi, 4 rifiuti per inversione o collasso | 3 → 0 |

I confronti usano 645 punti per risultato nei primi due gruppi e 1.285 nel terzo. Alcuni superamenti iniziali erano dell’ordine dell’arrotondamento. Nessuno scarto campionato superava la tolleranza operativa. Nella baseline del terzo gruppo il controllo del cono era già attivo.

Nel wrap per proiezione su più facce, il riferimento dell’appoggio usava entrambe le tangenti UV normalizzate come assi del piano fisico. Su due facce complanari con UV oblique, un parallelepipedo da 0,4 mm³ ne occupava 0,282843 e un centro esterno al ritaglio veniva accettato. Il secondo asse viene ora costruito dalla normale orientata e dal primo asse. Con entrambi i versi di faccia e i due segni di obliquità, la deformazione conserva il volume e la posizione è coerente con la posa rigida. [Appoggio multifaccia](../kernel/fk_surface_mapping.cpp), [prove del comando](../kernel/tests/test_surface_mapping.cpp).

Due appoggi complanari separati da una fessura di 0,00001 mm potevano inoltre sostenere un unico solido che attraversava il vuoto. Il checker topologico accettava il risultato e il comando dichiarava una deviazione di circa 7,21 × 10⁻¹¹ mm. Ora gli intervalli scoperti lungo gli assi dell’appoggio individuano dove cercare. Quando un singolo bordo sorgente ne attraversa uno, la verifica cerca un punto interno a quel bordo e controlla il contatto reale con le facce rifilate. Il solido viene rifiutato. Due solidi separati, uno su ciascun lato, passano con 12 facce e volume totale di 0,3 mm³.

Nel caso della fessura il punto di contatto mancante si trova a X = 0,312349319458 mm sul bordo della sorgente. Il rifiuto precede la pubblicazione della deviazione e della genealogia. La Release Linux dell’applicazione è stata ricompilata. Passano tutte le otto suite selezionate, incluse le 93 prove di validazione globale. Passano inoltre 71 prove con ASan, LeakSanitizer e UBSan sulle unità di mappatura, curve e superfici, oltre al runner di integrazione multifaccia con la stessa strumentazione. La mappa architetturale è verificata su 805 file e 413 unità di compilazione.

**Perimetro aperto.** Sulle rivoluzioni il controllo continuo riguarda facce sorgenti piane e immagini bicubiche polinomiali entro i limiti di calcolo. Per il seguito sui fori interni e sui bordi rientranti, inclusi i limiti della verifica, vedere la sezione successiva. La scelta del ramo della proiezione e il B-rep finale rimangono filoni distinti.

## K10.3 — Fori interni e bordi rientranti nel wrap multifaccia

Una lamina di area 4 mm² attraversava un foro rettangolare di 0,00008 × 0,00006 mm centrato in (0,312345, 0,172345) mm. Il vecchio wrap restituiva un corpo con deviazione dichiarata di 7,21 × 10⁻¹¹ mm. Il foro cadeva fra i campioni e i bordi della lamina avevano tutti appoggio. Ora il kernel cerca materiale sorgente all’interno delle finestre dei fori piani e verifica il contatto con l’insieme delle facce selezionate prima del fit. Il test rifiuta la lamina piena, accetta una lamina con foro allineato (due contorni, area 3,9999999952 mm²) e accetta il tappo completo sulla destinazione (area 4 mm²).

Un secondo test usa un’apertura circolare di raggio 0,00005 mm e un tappo dello stesso raggio spostato di mezzo raggio. Il tappo sostiene il centro ma lascia scoperta una falce. Il controllo delle frontiere proiettate trova la falce anche con sorgente sotto il piano, ruotata e traslata. Prima la deviazione dichiarata era 1,46 × 10⁻¹⁰ mm. Il tappo concentrico continua a sostenere tutta la lamina. Una prova supplementare in Release ha incrociato tre pose, due sorgenti (lamina e solido) e tre scelte della faccia primaria. Tutte le 18 falci sono state rifiutate e tutti i 18 tappi completi accettati.

Un bordo quadratico può entrare in una fessura proiettata e uscirne dallo stesso lato. Entrambi gli estremi rimanevano a sinistra della fessura, larga 0,00001 mm. Il vecchio controllo accettava il corpo con deviazione dichiarata di 3,61 × 10⁻¹¹ mm. Il kernel suddivide la curva ai parametri d’intersezione con i due limiti della fessura e verifica il contatto nei tratti interni. La curva spostata di 0,00002 mm sul lato sostenuto passa ancora con area 0,16666666667 mm². Le regressioni verificano anche che i rifiuti lascino intatti la sorgente, la genealogia e il valore di deviazione del chiamante.

I test permanenti sono in `kernel/tests/test_surface_mapping.cpp`. La ricerca locale considera le aperture interne delle facce piane ed è limitata per numero di finestre, frontiere, intersezioni e punti provati. Restano aperte la copertura continua generale, in particolare per fori su superfici non piane, vuoti obliqui non separabili e trim spline con estremi interni, e i casi di residuo tra appoggi curvi descritti nella sezione seguente.

## K10.3 — Residuo fra appoggi curvi

Due facce B-spline quadratiche tangenti, con un bordo topologico condiviso, rappresentano la parabola estrusa `z = x²/512`. Quattro nodi interni per direzione sulla seconda faccia localizzano un rilievo fra i campioni del fit. Il test deforma una lamina piana attraverso la giunzione. Nel punto interno scelto, il prodotto delle due funzioni di base vale esattamente 9/16. Si può quindi confrontare direttamente la quota teorica con il carrier del corpo restituito.

| Spostamento del polo | Residuo misurato prima, mm | Deviazione dichiarata prima, mm | Esito dopo la correzione |
| --- | ---: | ---: | --- |
| 0 | 1,11 × 10⁻¹⁰ | 3,26 × 10⁻¹⁰ | Accettato, limite continuo 3,88 × 10⁻¹⁰ mm |
| +10⁻⁸ mm | 5,626 × 10⁻⁹ | 3,26 × 10⁻¹⁰ | Accettato, limite continuo 6,9451 × 10⁻⁹ mm |
| −10⁻⁸ mm | 5,626 × 10⁻⁹ | 3,26 × 10⁻¹⁰ | Accettato, limite continuo 6,9456 × 10⁻⁹ mm |
| ±10⁻⁵ mm | 5,625 × 10⁻⁶ | 3,26 × 10⁻¹⁰ | Entrambi rifiutati con sorgente, deviazione e genealogia intatte |

Il kernel riconosce i grafici cartesiani dai coefficienti originali delle B-spline polinomiali. Sul rettangolo sorgente in UV ritaglia separatamente i domini degli appoggi e le celle di nodo. Per ogni cella limita il residuo sul carrier bicubico effettivamente costruito e confronta il massimo con il budget di fit preesistente (2 × 10⁻⁸ mm nella prova). Il limite vale dove esiste contatto. Il controllo della copertura dei trim resta distinto.

La sorgente a quota +0,02 o −0,02 mm verifica anche l’effetto della normale. Sul supporto liscio il massimo misurato in sette punti è 1,42 × 10⁻¹⁰ mm. Con uno spostamento del polo di ±10⁻¹¹ mm le quattro combinazioni di segno passano. Con ±10⁻⁸ mm la normale sul fianco del rilievo produce invece uno scostamento di circa 6,14 × 10⁻⁷ mm e l’operazione viene rifiutata.

Al nodo interno di una B-spline con normali diverse sui due lati, una faccia sorgente può proiettarsi interamente sul nodo. Il candidato di prova coincide con il lato usato dal kernel entro 3,14 × 10⁻¹⁶ mm. Una verifica che attribuisce anche il lato adiacente alla zona attiva lo rifiuta. Il controllo ora distingue le celle attraversate nel loro interno da quelle toccate soltanto sul bordo.

La verifica continua riguarda sorgenti piane, assi cartesiani, proiezioni separabili e grafici nativi non periodici di grado al più tre con coordinate proiettate riprodotte esattamente. Con carte generiche e appoggi B-spline dotati di nodi interni, il kernel può ancora misurare il residuo fra i nodi. Prova fino a 256 punti per faccia sorgente piana, dentro entrambi i contorni. Nella variante a nodi decimali i due rilievi piccoli sono accettati con deviazione misurata di circa 5,626 × 10⁻⁹ mm e quelli grandi sono rifiutati. Una prova con un rilievo fuori dal trim e una terza faccia che copre la zona ha mantenuto valido il wrap. La sezione seguente estende la prova alle mappe affini oblique sui grafici riconosciuti. Restano le proiezioni non lineari e la copertura continua dei contorni generici.

Verificati l’eseguibile Linux Release e le cinque suite di mapping (multifaccia, UV, diagonale, non piano e residuo globale). Il runner multifaccia con i moduli modificati supera anche AddressSanitizer, LeakSanitizer e UBSan.

## K10.3 — Proiezione obliqua fra appoggi curvi (21 novembre)

Una lamina ruotata di 0,4 rad e traslata di (0,05; −0,08) mm attraversa la giunzione fra due superfici B-spline quadratiche. Sulla seconda faccia la spaziatura disuguale dei nodi sposta il picco del rilievo a 17/73 dell’intervallo centrale in entrambe le direzioni. La prova confronta il punto fisico sul target con il punto corrispondente sul corpo restituito, ricavato invertendo rotazione e traslazione. Il budget di fit è 2 × 10⁻⁸ mm.

| Spostamento del polo, mm | Deviazione dichiarata prima, mm | Residuo misurato al picco prima, mm | Esito dopo la correzione |
| --- | ---: | ---: | --- |
| +1 × 10⁻⁸ | 4,809 × 10⁻⁹ | 6,099 × 10⁻⁹ | Accettato, limite continuo 1,883 × 10⁻⁸ mm |
| +2,8 × 10⁻⁸ | 1,346 × 10⁻⁸ | 1,707 × 10⁻⁸ | Accettato, limite continuo 1,926 × 10⁻⁸ mm |
| +3,3 × 10⁻⁸ | 1,586 × 10⁻⁸ | 2,012 × 10⁻⁸ | Rifiutato prima di pubblicare deviazione e genealogia |

Il kernel restringe il rettangolo sorgente alle due fasce UV di ogni cella di nodi a ogni suddivisione. Usa l’inversa quando gli intervalli determinano il segno del determinante. Se il rango è incerto, usa le fasce direttamente. Per rifiutare in base al residuo obliquo, verifica che un punto interno al materiale e alla cella attiva abbia un errore minimo già superiore al budget. La prova resta condizionata all’esistenza del contatto. Un rilievo fuori dalla lamina, ma dentro il suo rettangolo d’ingombro, lascia valido il wrap a entrambe le rotazioni di ±0,4 rad.

La prova su una faccia verticale include una mappa di rango uno e una quasi singolare. Con quota positiva, negativa e passante da zero, in 4.225 punti per configurazione il massimo residuo indipendente è circa 7,02 × 10⁻¹² mm. Su un carrier spostato volutamente di 10⁻⁵ mm, il primo appoggio poteva dare un esito inconcludente e interrompere la verifica del secondo. Con il budget di calcolo ancora disponibile, il kernel prosegue sulle celle successive e può rifiutare il risultato quando trova un punto fuori tolleranza. La certificazione dell’intero risultato richiede che ogni cella pertinente superi la verifica.

Il perimetro verificato comprende grafici cartesiani polinomiali nativi riconosciuti e sorgenti piane con proiezione affine. Restano aperti gli appoggi con proiezione non lineare, le carte di destinazione generiche e la copertura continua dei trim. Esaurito il budget numerico senza poter decidere, il controllo può restituire `Unsupported`.

Verificati l’applicazione Linux Release, le cinque suite del kernel relative al mapping e il runner multifaccia con il modulo del residuo compilato con AddressSanitizer e UBSan, incluso il controllo delle perdite. Aggiornata e verificata la mappa architetturale.

## K10.3 — Apertura racchiusa da più contorni esterni piani (21 novembre)

Quattro pezze rettangolari a un solo contorno lasciano fra loro una finestra
di 0,00008 × 0,00006 mm, decentrata rispetto alla lamina sorgente. Le loro
proiezioni complessive coprono sia X sia Y. Prima della correzione il wrap
accettava la lamina piena e pubblicava la genealogia. Dichiarava una deviazione
di 7,21 × 10⁻¹¹ mm a 0 rad e di 1,73 × 10⁻¹⁰ mm a 0,23 rad.

Con più appoggi piani i cui contorni esterni sono formati da rette, il kernel
applica la scansione delle frontiere anche al rettangolo proiettato della sorgente.
Le finestre dei fori delle singole facce mantengono la priorità. Gli incroci
delle frontiere e le ordinate dei vertici forniscono punti da verificare. Solo
un punto interno al materiale sorgente passa alla stessa ricerca di contatto
usata dalla deformazione. Le rette native, rifilate e trasformate condividono
il riconoscimento già usato per le fessure lungo gli assi.

La regressione rifiuta la lamina piena in entrambe le orientazioni prima di
pubblicare un risultato. La variante ruotata inverte l'ordine delle pezze e
usa bordi rifilati, alcuni anche trasformati. La prova controlla che sorgente,
deviazione e genealogia restino intatte al rifiuto. Con una quinta pezza che
chiude la finestra, o con un foro coincidente nella sorgente, il wrap riesce e
conserva area e numero di contorni. [Regressione pubblica](../kernel/tests/test_surface_mapping.cpp).

Verificati la build Linux Release di ForgeCAD e i cinque CTest di mapping.
Il runner multifaccia con il test e i due moduli del wrap modificati compilati
con AddressSanitizer e UBSan ha superato anche LeakSanitizer. Su una lamina
piana di area 3,24 mm² e una griglia completa di 4, 25 o 100 pezze, le mediane
locali di cinque wrap sono passate rispettivamente da 1,21 a 1,31 ms, da 3,54
a 4,27 ms e da 11,78 a 12,20 ms. In tutti i casi il B-rep era valido e l'area
conservata. La griglia piena misura il costo del comando. Con più di 256 bordi
pertinenti in una finestra, la scansione dettagliata di quella finestra viene saltata.

La ricerca ha un budget condiviso di punti e operazioni. Esaurirlo lascia
attivi i controlli ordinari del wrap. Rimangono aperte la certificazione
continua dei trim generici e la proiezione non lineare su appoggi curvi.

## Correzioni e semplificazioni applicate

- **Undo delle tavole dopo lo spostamento del modello.** Dopo avere individuato il file rinominato, modificare un foglio e premere Annulla prima del salvataggio riapriva il percorso ormai inesistente. Il percorso entra ora con l'impronta nella copia validata prima dell'installazione del modello e della base della cronologia. Eliminata la seconda assegnazione del modello dopo il commit. Riprodotto via Qt con un modello reale in una directory temporanea. Verificati ripristino del titolo, mantenimento del nuovo percorso, stato modificato e fallimento atomico con un percorso assente. [Sorgente](../cad_drawing_window.cpp#L303), [regressione](../tests/test_linked_drawing.h#L57).
- **Solver schizzo.** Ogni solve ordinario ricostruiva un secondo insieme di blocchi geometrici, contatti, derivate e dipendenze al solo scopo di contarli. Il bersaglio di trascinamento ora è esplicito sul blocco. Il solver costruisce l'insieme una volta. Eliminato anche il duplicato locale della verifica dei numeri finiti. I vincoli su sole entità rigide conservano il trattamento precedente e i trascinamenti continuano a partecipare alla verifica. [Costruzione](../cad_constraints.cpp#L1714), [consumo](../cad_constraints.cpp#L2250).
- **Esito del runner kernel.** Prima un filtro assente stampava `0/0 test superati` con codice 0. Ora restituisce 1 e identifica il filtro. Il filtro sul rango incerto esegue un test con codice 0. Senza filtro la suite globale ne esegue 36. Controllare il numero effettivo resta importante per i rapporti storici. [Runner](../kernel/tests/fk_test_main.cpp#L46).
- **Mappa della review.** Il benchmark mesh era associato a un gruppo inesistente. Il file spariva dall'elenco e il dettaglio non poteva risolvere il nome del gruppo. Il benchmark è nel gruppo degli altri benchmark e il generatore verifica ogni associazione sia in generazione sia in `--check`. L'inventario esclude le copie di documentazione, comprese sei vecchie copie di CMake nella cartella delle prove. [Catalogo](../tools/generate_architecture.py#L414), [validazione](../tools/generate_architecture.py#L863).

## Quattro semplificazioni della roadmap verificate il 21 novembre

1. **Kernel, intervalli e polinomi di Bernstein.** L'analisi aveva rilevato 81 righe significative consecutive in comune fra due certificatori. Ora condividono le operazioni intervallari con arrotondamento verso l'esterno e le operazioni univariate sui coefficienti. Il certificatore bordo/superficie conserva il limite di grado 21, quello della superficie portante il limite 20. Restano locali la gestione del superamento del limite, i budget e l'annullamento. Verificati 61 casi bordo/superficie e 93 casi di residuo globale e normali. [Operazioni condivise](../kernel/fk_surface_bound_math.h)
2. **Conferma dei pannelli funzione.** I due percorsi usano lo stesso gestore per applicare i parametri, mostrare gli errori e ripristinare i controlli. L'attesa della chiusura e il blocco del viewport seguono il tipo di pannello. Un runner Qt offscreen ha esercitato 24 combinazioni di presentazione, visibilità durante il calcolo, posizione dei pulsanti e sequenza di conferma. Dopo un errore si può correggere e riprovare oppure annullare. Verificata anche l'emissione di una sola chiusura finale. [Gestore di Applica](../forgeCad2026_dialogs_common.cpp#L1165)
3. **Modifica dettaglio nelle tavole.** Il comando di creazione apre direttamente le opzioni interattive. Le proprietà di un dettaglio esistente mostrano la vista madre effettiva e aggiornano il contorno con la stessa funzione dell'editor. Eliminati il ramo di creazione irraggiungibile dopo la conferma e la ricerca di un identificativo libero durante la modifica. [Proprietà](../cad_drawing_editor.cpp#L2612), [aggiornamento del contorno](../cad_drawing_editor.cpp#L121)
4. **Undo multifoglio.** Modifica del foglio, operazioni sul contenitore e ripristino di una revisione passano alla cronologia i byte già validati. Verificati i byte dopo Undo/Redo, l'identità del foglio attivo e lo stato di salvataggio anche dopo un annullamento o un errore. Su 16 fogli con 30 annotazioni ciascuno (85.517 byte), tre serie alternate di 30 operazioni per versione hanno dato 60 codifiche prima e 30 dopo per serie. La mediana delle tre mediane per una modifica del cartiglio è scesa da 7,20 a 3,68 ms e per la rinomina di un foglio inattivo da 10,30 a 6,67 ms. [Cronologia](../cad_drawing_sheets_ui.cpp#L79), [ripristino](../cad_drawing_restore.cpp#L31)

La build Release Linux di `forgecad` e dei runner interessati è riuscita. Passate 10 suite CTest mirate su certificatori, mapping, sorgenti geometriche, pannelli, riuso dell'anteprima, dettagli, scale, multifoglio e ripristino. Le verifiche Qt hanno usato il backend offscreen. Rigenerata e controllata la mappa su 414 unità di compilazione, con 808 file e 11.954 relazioni. Controllati anche i chiamanti del nuovo nucleo numerico e dei gestori di conferma, contorno e cronologia.

## Pareti spline, cache di analisi, offset ed export concorrente (21 novembre)

- **Spessore e ripristino delle aperture.** Il kernel costruisce il supporto rigato cubico×lineare in un unico punto. Allinea nodi e molteplicità delle due guide e restituisce le guide raffinate ai chiamanti. Ogni percorso assembla ancora i propri bordi, le curve UV e la genealogia. Sul fondo curvo con tre asole e sulla lamina spline liscia il confronto prima/dopo ha dato B-rep binari identici e le stesse origini per tutte le facce, gli spigoli e i vertici. Verificati anche nodi esclusivi e molteplicità 1/2 fra le guide. [Supporto condiviso](../kernel/fk_ruled_wall.cpp), [aperture](../kernel/fk_open_offset.cpp), [Spessore](../kernel/fk_thicken.cpp)
- **Cache di analisi dell’architettura.** La vecchia cache creava un JSON per generazione e unità. Ogni modifica a un header invalidava tutte le unità. La prima ottimizzazione già documentata aveva dimezzato le letture a cache calda, da 826 a 413 per 413 unità. La nuova cache SQLite conserva una voce compressa per unità e verifica i file effettivamente letti da Clang. Con un inventario stabile il cambiamento di un header transitivo ha rianalizzato la sola unità dipendente su due. Provati anche header generati, aggiunte e rimozioni di include opzionali, precedenza fra directory, voci danneggiate, processi concorrenti e rimozione delle unità obsolete. La mappa della prova coincide byte per byte con quella generata senza cache. Il database ha un limite di 256 MiB più il journal temporaneo. La precedente cartella JSON resta sul disco dell’utente. [Generatore](../tools/generate_architecture.py), [regressione](../tests/test_architecture_cache.py), [perimetro e rigenerazione dopo un cambio di SDK](architecture/README.md)
- **Offset B-spline.** Rimossi due parametri privati inutilizzati su `offsetBSplineC2` e `offsetBSpline`. Il chiamante continua a decidere quando controllare la curvatura prima del fit. Verificati il fit C2 denso e la diagnosi su un loft con offset interno oltre il raggio, inclusa la riuscita a distanza inferiore. [Selezione del fit](../kernel/fk_offset.cpp#L1522)
- **Timestamp STEP/IGES.** Un riproduttore su Linux ha fermato il primo export dopo la lettura dell’ora locale e ha fatto convertire l’epoca Unix a un altro thread. Sul codice precedente compariva erroneamente il 1970 sia in STEP sia nei due campi IGES. Dopo la correzione ciascun export usa una struttura oraria privata. Superati lo stesso riproduttore e la regressione con esportazioni concorrenti e timestamp espliciti, anche con fuso locale UTC+05:30. Il ramo Windows usa `localtime_s` e resta da verificare su Windows. [Formattazione comune](../kernel/fk_exchange.cpp#L42), [test](../kernel/tests/test_exchange_robustness.cpp)

**Misura sul checkout Linux.** Per le 415 unità di compilazione la prima generazione ha richiesto 218,27 s. Con tutti i risultati disponibili ha richiesto 2,51 s. Dopo una modifica al nuovo header delle pareti la cache ha riusato 412 unità e sono state rilette da Clang soltanto `fk_open_offset.cpp`, `fk_ruled_wall.cpp` e `fk_thicken.cpp` (4,68 s totali). Modificare il test di scambio ha richiesto l'analisi di una sola unità. Il database con 415 voci occupa 20.574.208 byte, circa 19,6 MiB. La vecchia directory della build occupa circa 12 GiB e il nuovo generatore la ignora. Alla ricompilazione, cambiamenti nell’inventario dei percorsi possono ancora provocare un passaggio completo. Una build successiva già aggiornata, a inventario invariato, ha riusato tutte le 415 unità. Nel confronto sul progetto sia il JSON sia la pagina HTML prodotti dal percorso incrementale sono risultati identici byte per byte a una generazione senza cache. Verificata anche la ricostruzione esplicita dopo un cambio di SDK: se il nuovo header dipende dall’ora e impedisce di memorizzare la singola unità, viene ritirata la voce precedente.

**Verifica sul modello disponibile.** La versione presente di `Cesto.prt` (SHA-256 `d5e7a7fe839105a645dee701a19f139d87f3f0da8a613bfe7d25b68907fd948b`) ha tredici lavorazioni, compreso Raccordo 13. Rigenerato in memoria il suo Spessore salvato a 1 mm su Cucitura 11. Il risultato coincide byte per byte con il B-rep salvato: un solido a una shell, genere 3, 102 facce e zero rilievi al checker. Impostando 2 mm su questa revisione il kernel segnala il raggio limite di 2 mm sulla faccia 21. Lo stesso messaggio si riproduce collegando il runner ai due moduli delle pareti precedenti al refactoring. La prova storica a 2 mm sulla Cucitura originale identifica separatamente il documento e la sua impronta. [Versioni e prove su Cesto](review-cesto-spessore-2026-11-18.md)

La build Release Linux dell’applicazione è riuscita. Passate le sei suite CTest mirate, inclusi entrambi i casi Shower Head. Passati anche i due filtri specifici sul fit offset e i sei casi Spessore nel runner con OCCT. Le sei semplificazioni prioritarie di questa review sono completate. Rimangono aperti i filoni K10 descritti nel registro, fra cui i trim generici e la proiezione non lineare su superfici curve.

## K10.3, aperture di Spessore e conferma della Booleana (21 novembre)

**Foro curvo nel wrap multifaccia.** Un foro di raggio 0,00005 mm è delimitato da intagli semicircolari sui contorni esterni di due pezze piane. Un tappo spostato di 0,000025 mm lascia una falce scoperta. Prima il wrap pubblicava la lamina piena di area 4 mm², quattro spigoli con genealogia e una deviazione di 1,46 × 10⁻¹⁰ mm. Il kernel ora cerca punti interni al materiale anche in presenza di questi contorni curvi e verifica il contatto con gli appoggi effettivi. Rifiuta la falce scoperta prima di pubblicare il risultato. Con tappo completo o foro sorgente più ampio il wrap conserva area e topologia. La ricerca conserva i limiti di punti e operazioni e non dimostra la copertura di ogni possibile trim. [Test multifaccia](../kernel/tests/test_surface_mapping.cpp)

**Due appoggi curvi con proiezione non lineare.** Per due pezze B-spline native con trim rettangolare, interni disgiunti, X(U) monotona e Y=V, il residuo si calcola nei parametri degli appoggi. La sorgente rettangolare è piana e a quota zero. Prima della correzione il wrap accettava due rilievi con residui indipendenti di 2,077 × 10⁻⁸ e 2,130 × 10⁻⁸ mm contro un limite di 2 × 10⁻⁸ mm. Ora rifiuta entrambi prima di pubblicare il risultato, e accetta tre controlli entro tolleranza. Con il rilievo interamente nel foro della sorgente, il controllo ordinario resta applicabile e accetta la lamina. Senza contatto dimostrato o a budget esaurito il nuovo verificatore restituisce `Unsupported`. Resta aperta la copertura continua dei trim generici, delle proiezioni su carte diverse e delle sorgenti a quota variabile. [Verificatore](../kernel/fk_surface_mapping_bounds.cpp), [corpi e residui di prova](../kernel/tests/test_mapping_global_validation.cpp)

**Aperture del solido di Spessore.** Una curva UV cubica può uscire dal contorno della pelle fra i tre parametri già controllati. Sul fondo bicubico della prova la versione precedente arrivava al controllo finale del solido e falliva. Durante il ripristino del foro, il kernel confronta ora ciascuna curva UV con tutti i bordi del supporto rifilato prima di costruire la parete. Il caso che esce è segnalato in questa fase e l'asola curva che resta nel materiale passa. Sulla copia corrente di Cesto.prt lo Spessore salvato a 1 mm rigenera il B-rep di 102 facce identico al salvato, con una shell di genere 3 e zero rilievi al checker. L'intersezione spline generica è numerica; rimane aperta la prova continua della corrispondenza con la pelle a distanza. [Ripristino](../kernel/fk_open_offset.cpp), [regressione](../kernel/tests/test_open_offset.cpp)

**Booleana dalla voce Unione del menu.** Un contatore temporaneo collegato a `forgeBoolean` ha misurato due chiamate prima della correzione: una durante l'anteprima e una alla conferma. Ora il risultato pronto viene promosso e la conferma aggiunge zero chiamate. Se cambia la geometria dell'operando dopo l'anteprima, la conferma esegue una nuova costruzione. Annulla conserva i due operandi. La regressione aziona il menu reale e confronta il B-rep serializzato, l'identità degli operandi e le origini di facce, bordi e vertici con una creazione senza anteprima. Anche Parallelepipedo e Scala rispetto all'origine riusano il B-rep a parametri invariati. Modificando subito il parametro, il secondo calcolo produce il nuovo risultato. La conferma assegna la stessa identità persistente della costruzione diretta e registra nuovamente la genealogia con tale identità. [Conferma](../forgeCad2026_viewport_document.cpp), [test del comando](../tests/test_boolean_box.h)

**Estrusione dal menu.** Su un profilo con distanza esplicita l'anteprima pronta viene riusata alla conferma, anche nella prova con fusione manuale. Il contatore delle costruzioni passa da due a una. Geometria serializzata, ID e nomi di facce, spigoli e vertici coincidono con la creazione diretta. L’estrusione conserva la ricostruzione per i limiti geometrici, la ricerca automatica degli operandi e le anteprime ancora in calcolo. Le prove «fino al prossimo corpo» includono il caso senza limite, Undo/Redo e salvataggio.

Verificata la build Release Linux dell'app. Sono passate 17 suite CTest distinte fra kernel, anteprime, comandi di creazione, limiti di Estrusione e genealogia. I test dei comandi sono stati eseguiti con Qt offscreen. Il runner multifaccia con il controllo dei trim strumentato e i due casi non lineari con il nuovo verificatore strumentato passano anche con AddressSanitizer e UBSan. Verificata la mappa su 415 unità di compilazione (812 file, 11.991 relazioni), inclusi i chiamanti della promozione, del residuo non lineare e del controllo delle aperture.

## K10.3 — Estremi delle spline nei trim e proiezione non lineare sui due assi

- **Foro piano con bordo spline.** La prova usa una corda e un arco B-spline
  quartico, anche razionale, con ingombro dei poli di 0,0004 × 0,0002 mm.
  Il centro dell’ingombro è coperto dall’appoggio. Il foro è più in basso,
  sopra il materiale sorgente. Con la scansione precedente, gli estremi e i
  nodi del bordo fornivano la stessa ordinata e il wrap accettava la lamina.
  La scansione ora calcola gli estremi interni delle curve proiettate per
  ciascun tratto di Bézier. Per le razionali usa le radici di
  `A'_y W − A_y W'`, dove `A_y` è il numeratore della coordinata e `W` è il peso.
  Ogni punto candidato passa dalla verifica di contatto sulle facce effettive.
  Le due varianti con foro vengono rifiutate prima di pubblicare deviazione
  e genealogia. L’appoggio pieno conserva l’area di 4 mm².
  [Scansione](../kernel/fk_mapping_trim_witness.cpp), [regressione](../kernel/tests/test_surface_mapping.cpp)
- **Due appoggi curvi con X(U) e Y(V) non lineari.** La variante del
  riproduttore usa `X = U + U²/1024` e `Y = V + V²/1024`, una lamina di quota
  zero con rotazione di 0,4 rad e una traslazione di (0,05; −0,08) mm.
  Prima, il controllo richiedeva `Y = V`. La misura campionata che subentrava
  permetteva due falsi successi sul rilievo. Il controllo continuo ora compone
  entrambe le coordinate dai coefficienti B-spline nelle celle dei nodi.
  Usa gli intervalli delle derivate per verificare la monotonia e richiede
  interni proiettati disgiunti. Accetta il supporto liscio e gli spostamenti
  del polo di ±10⁻⁸ mm, con un limite dichiarato che copre i residui misurati.
  Rifiuta +2,5 × 10⁻⁸ e −4,4 × 10⁻⁸ mm prima di pubblicare deviazione o
  genealogia. Il budget del residuo resta 2 × 10⁻⁸ mm.
  [Verificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressione](../kernel/tests/test_mapping_global_validation.cpp)

La Release Linux dell’applicazione è stata ricompilata e le sei suite CTest
del mapping sono passate. Passano anche il runner multifaccia e i due test
non lineari con i moduli modificati strumentati con AddressSanitizer e UBSan,
incluso il controllo delle perdite.

Restano aperti i trim su appoggi non piani e la prova continua generale
della copertura. Il controllo continuo della proiezione non lineare si applica
a due appoggi quadratici nativi polinomiali, con trim rettangolari,
coordinate proiettate separabili crescenti e sorgente rettangolare piana a
quota zero. Restano da verificare più appoggi, carte accoppiate o razionali
e quota variabile.

## K10.3 — Fori su cilindro, tre appoggi curvi e quota affine

1. **Trim su appoggio curvo.** Un foro proiettato di 0,00008 × 0,00006 mm
   è rifilato sulla faccia di un cilindro di raggio 200 mm. La finestra del
   foro entra nella ricerca anche quando il carrier è curvo. Nella prova il
   target è una singola faccia forata oppure quella stessa faccia con un
   tappo cilindrico. Il foro aperto e la striscia lasciata dal tappo parziale
   causano un rifiuto prima che il chiamante riceva genealogia o deviazione.
   Il tappo completo dà una lamina valida con i vertici sul cilindro.
   [Controllo dei trim](../kernel/fk_mapping_trim_witness.cpp), [prova del comando](../kernel/tests/test_surface_mapping.cpp)
2. **Più di due appoggi curvi.** Il residuo si calcola sulle celle dei nodi
   di tutte le pezze pertinenti. Le impronte devono avere interni disgiunti.
   La regressione attraversa due cuciture tangenti e controlla un rilievo
   sulla terza pezza. Sono provate 20 combinazioni di due o tre appoggi,
   con una o due coordinate quadratiche e cinque ampiezze di rilievo.
   I tre controlli entro budget passano in ogni configurazione. I due
   rilievi superiori al budget causano un rifiuto con sorgente, appoggi,
   deviazione e genealogia intatti.
3. **Quota affine.** La lamina sorgente ha inclinazione di circa 10⁻⁴ rad.
   La quota può restare positiva o negativa oppure attraversare lo zero.
   Il controllo nelle celle proiettate compone la quota con le normali
   naturali orientate. Limita il residuo senza dividere per la quota e usa
   lo stesso controllo puntuale delle proiezioni affini prima di rifiutare.
   Le verifiche dirette della quota, dei punti deformati e del certificato
   riguardano due o tre appoggi. Il rilievo fuori budget sulla faccia finale
   viene rifiutato con la quota che attraversa lo zero.
   [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

Le regressioni falliscono anche collegando i runner ai due moduli
precedenti. Il vecchio controllo accettava il foro cilindrico scoperto e i
rilievi sopra budget su tre appoggi o a quota variabile. La Release Linux è
stata ricompilata e sono passate tutte le sei suite CTest di mapping.
Il runner multifaccia e i tre casi di proiezione non lineare passano con
i moduli modificati strumentati con AddressSanitizer e UBSan, incluso il
controllo delle perdite.

La ricerca dei fori resta limitata da un budget di lavoro e dalla
disponibilità della proiezione dei bordi. L’estensione ai fori interni curvi
convive con la scansione delle lacune fra contorni esterni piani. Restano
aperte la copertura continua dei trim generici e le lacune fra contorni
esterni curvi. Il certificato del residuo considera sorgenti piane
rettangolari, appoggi quadratici nativi con proiezione separabile monotona,
trim rettangolari e impronte proiettate a interni disgiunti. Quote di sorgenti
non piane, carte razionali o accoppiate e la selezione generale dei rami di
contatto restano da verificare.

## K10.3 — Lacune fra appoggi curvi e grafici accoppiati o razionali

- **Lacuna fra contorni esterni curvi.** Quattro pezze cilindriche con un
  solo contorno ciascuna lasciano una finestra proiettata decentrata. Un
  tappo parziale ne copre il centro, lasciando una striscia scoperta.
  La versione precedente accettava il wrap anche invertendo l’ordine
  delle pezze. La scansione ora usa le frontiere proiettate degli appoggi
  curvi entro i budget esistenti. Verifica un punto di materiale nella
  striscia e rifiuta prima di pubblicare il risultato. Il tappo completo
  dà una lamina valida. I punti di prova usano il contatto rifilato senza
  alterare lo stato della ricerca di contatto del fit. Un bordo condiviso
  da più facce selezionate non avvia da solo la scansione degli esterni.
  [Ricerca delle lacune](../kernel/fk_mapping_trim_witness.cpp), [prova cilindrica](../kernel/tests/test_surface_mapping.cpp)
- **Coordinate accoppiate.** Su tre appoggi quadratici la proiezione ha
  la forma `X = Aᵢ(U) + H(V)`, `Y = B(V)` con `H` e `B` comuni.
  La stessa ordinata implica lo stesso parametro trasversale. Tolto
  `H(V)`, gli intervalli di `Aᵢ` sono disgiunti al loro interno, anche
  quando gli ingombri cartesiani degli appoggi si sovrappongono.
  La regressione usa `X = U + U²/1024 + V/4` e `Y = V + V²/1024`.
  Passano il supporto liscio e i tre rilievi entro budget. Il caso
  negativo supera 2 × 10⁻⁸ mm di residuo sul carrier precedente e adesso
  viene rifiutato. Una suddivisione inconcludente impedisce la
  certificazione e lascia cercare un punto fuori budget nelle regioni
  successive entro il limite di lavoro. Appoggi con correzioni
  trasversali incompatibili restano fuori dal perimetro della prova.
- **Appoggi razionali con proiezione affine.** Il kernel riconosce la
  proiezione esatta sui coefficienti omogenei dei supporti Bézier nativi.
  Il controllo del residuo globale riusa il calcolo razionale del
  denominatore positivo. Il test usa pesi 1, 2 e 3 lungo U, con
  `X = U`, `Y = V` e quota geometrica razionale. Una bicubica
  entro budget viene certificata e una bicubica di prova con residuo
  fuori budget viene rifiutata. Il wrap effettivo passa per il rilievo
  piccolo e conserva la genealogia. Un cambiamento che rende la
  proiezione del supporto non affine restituisce `Unsupported`.
  [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

La scansione riconduce a un segmento le proiezioni B-spline rettilinee
allineate agli assi quando gli estremi racchiudono tutti i poli.
I regressori delle carte accoppiate e razionali falliscono con il
certificatore precedente. Passano nella Release Linux ricompilata,
insieme alle sei suite CTest del mapping. Passano anche il runner
multifaccia e i quattro test mirati con i moduli modificati strumentati
con AddressSanitizer e UBSan, incluso il controllo delle perdite.

Restano da verificare la copertura continua generale dei trim, i
grafici accoppiati con trasversali diversi o riferimento di proiezione
generico, i razionali con proiezione non affine o quota variabile e le
sorgenti non piane.

## K10.3 — Copertura continua ortogonale, trasversali distinti e proiezione razionale

1. **Copertura continua dei trim ortogonali.** Per facce piane parallele
   alla proiezione con coordinate cartesiane esatte, i vertici dei contorni
   dividono l’ordinata in fasce aperte. In ogni fascia, l’insieme degli
   intervalli di materiale è costante. Il kernel confronta l’unione
   degli intervalli sorgente con quella delle facce destinazione.
   L’inclusione comprende anche frontiere e vertici per chiusura.
   I controlli comprendono contorni concavi, fori allineati, una sorgente
   piena sopra un rientro e una fessura di un solo ULP. Quest’ultima è
   individuabile dal controllo esatto diretto pur senza un double interno.
   Nel wrap pubblico si rifiutano solo lacune con un margine in entrambi
   gli assi oltre la tolleranza di contatto. Una fessura entro tolleranza
   torna al percorso ordinario. Il rifiuto avviene prima del fit e
   conserva deviazione e genealogia. La diagnosi riporta le coordinate
   della lacuna. Senza questa prova le vecchie sonde rilevano comunque
   la fessura più larga, in un secondo momento. Il controllo dei contatti
   resta attivo anche se la copertura proiettata è provata.
   [Verifica continua](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
2. **Parametrizzazioni accoppiate con trasversali distinti.** Il kernel
   verifica le impronte a coppie. Se una coordinata proiettata ha interni
   disgiunti, le carte possono avere trasversali diversi. Se condividono
   la stessa coordinata monotona Y(V), la prova accetta anche carte
   ordinate confrontando i coefficienti delle frontiere X(V).
   La prova del wrap attraversa la giunzione tangente fra
   `X = U + V²/8` e `X = U − 3V²/16`, con trim UV su intervalli
   trasversali adiacenti. Verifica geometria, genealogia e certificato
   continuo della superficie prodotta.
3. **Proiezione razionale non affine e quota variabile.** Sui grafici
   Bézier nativi della prova i pesi 1, 2 e 3 lungo U producono un
   denominatore positivo W. Si dimostra la monotonia di X con i
   coefficienti della derivata omogenea e si compone la bicubica emessa
   nei parametri dell’appoggio. Il residuo usa un denominatore comune
   W⁶. Per la quota variabile le tangenti omogenee danno la normale
   orientata e il limite evita divisioni per la quota. Le prove
   verificano proiezioni affini e non affini, quota positiva, negativa e
   passante per zero. Un carrier errato viene rifiutato anche con la
   sorgente proiettata interamente sulla faccia razionale.
   [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

Le sei suite CTest del mapping e la build Release Linux sono passate.
Passano anche il runner multifaccia e i casi mirati con i moduli
modificati strumentati con AddressSanitizer e UBSan, incluso il controllo
delle perdite. I regressori sui trasversali distinti e sulle due
estensioni razionali falliscono con il certificatore precedente.

Il perimetro della copertura continua comprende contorni di segmenti
ortogonali semplici e fori disgiunti. Il wrap lo usa con direzioni
cartesiane, rotazione e traslazione piana nulle e corrispondenza esatta
fra rappresentazioni 3D, UV e vertici. I limiti sono 128 contorni e
512 spigoli. Restano aperti i trim curvi o obliqui, i casi numericamente
inconcludenti, le carte accoppiate senza separazione dimostrabile,
i razionali con pesi dipendenti da entrambe le coordinate e
proiezione non affine e le sorgenti non piane.

## K10.3 — Trim circolari e obliqui, pesi bidimensionali e sorgenti non piane

1. **Trim curvi e obliqui.** Per facce piane con proiezione cartesiana,
   il controllo continuo usa i semipiani dei poligoni strettamente
   convessi e l’inclusione di dischi. Controlla che i fori circolari
   siano contenuti e disgiunti e che ogni foro pertinente del target
   sia coperto da un foro della sorgente. Nelle prove passano i trim
   obliqui coincidenti, l’inclusione fra dischi e i fori coincidenti.
   Due pezze senza fori possono essere unite per la prova quando
   condividono un lato completo e formano un contorno convesso.
   Il caso multifaccia copre un disco che attraversa la diagonale
   fra due triangoli. Si rifiutano la parte di un triangolo oltre
   la diagonale dell’altro,
   una corona scoperta e il foro circolare sopra una sorgente piena.
   Il rifiuto richiede un punto provato con margine dalla frontiera
   della sorgente e degli appoggi. Le bande di tolleranza mantengono
   il percorso ordinario quando la prova resta inconcludente.
   [Copertura](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
2. **Pesi dipendenti da U e V.** Per appoggi Bézier con Y=V e X
   a estremi trasversali costanti, il kernel verifica nel continuo le
   derivate di X(U,V) a partire dal numeratore omogeneo. La prova
   confronta i coefficienti di entrambe le derivate con il quadrato
   del peso. Il denominatore è ora tensoriale quando varia lungo V,
   e il residuo della bicubica mantiene il denominatore comune W⁶.
   La griglia del test ha pesi non fattorizzabili nei due parametri.
   Passano il certificato e il wrap effettivo sul rilievo piccolo,
   sia a quota zero sia con sorgente inclinata. Una superficie
   candidata con residuo fuori budget viene rifiutata in entrambi
   i casi. La prova usa le impronte rettangolari e appoggi disgiunti
   al loro interno.
3. **Sorgente non piana.** Il certificato comprende anche una pezza
   Bézier polinomiale di grado al più due per asse e con X=U, Y=V
   esatti. La quota del punto sorgente viene composta dai poli reali
   con le normali degli appoggi a proiezione affine. La regressione
   deforma una sella di quota (U²−V²)/8192 attraverso la giunzione di
   due appoggi tangenti, con piano sorgente traslato e offset normale.
   Verifica B-rep, genealogia e residuo pubblicato e rifiuta una
   superficie candidata priva del rilievo sorgente. Una direzione di
   proiezione inclinata fuori da questo perimetro dà `Unsupported`.
   [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

I casi con diagonale obliqua, pesi bidimensionali e perdita del
rilievo della sorgente sono stati confrontati con il certificatore
precedente. Le nuove regressioni fallivano con quella versione.
Passano le sei suite CTest del mapping e la Release Linux ricompilata.
Passano anche il runner multifaccia e le nuove prove del residuo con
i moduli modificati strumentati con AddressSanitizer e UBSan, incluso
il controllo delle perdite.

In quel passaggio rimanevano da verificare i contorni spline generici,
le unioni proiettate, le carte razionali senza l’invariante Y=V e le
sorgenti non piane con proiezione non cartesiana, trim generici o
parametrizzazione razionale.

## K10.3 — Trim spline, unioni e proiezioni non cartesiane

1. L’inviluppo convesso dei poli dei trim sorgente B-spline piani
   racchiude il contorno nel continuo. La prova richiede curve native
   non periodiche e bloccate agli estremi, stessi nodi e pesi fra
   curva 3D e pcurve e coincidenza esatta con i vertici e il piano.
   Un polo esterno all’appoggio rende la prova inconcludente. Il
   wrap usa la copertura dimostrata per saltare i controlli ridondanti
   sulle lacune fra box e conserva i controlli di contatto sui trim.
   [Copertura](../kernel/fk_mapping_trim_witness.cpp), [wrap](../kernel/fk_surface_mapping.cpp)
2. Le unioni con un lato intero in comune si possono comporre su più
   appoggi. La verifica per fasce copre anche unioni non convesse di
   pezze poligonali con giunzioni parziali. Fra due ordinate di
   vertice, il confronto certificato dei lati alle due estremità
   vale per tutta la fascia. I test includono un disco su tre
   appoggi e un disco sopra una giunzione parziale fra un rettangolo
   e un triangolo, con una variante che esce dal bordo obliquo.
   [Regressioni](../kernel/tests/test_surface_mapping.cpp)
3. Per appoggi Bézier razionali con impronta rettangolare, il kernel
   verifica i segni delle derivate della proiezione nei coefficienti
   Bernstein. La dominanza diagonale garantisce un determinante
   positivo e un’unica inversa nell’impronta. Il residuo omogeneo
   resta tensoriale e comprende entrambe le coordinate accoppiate
   con pesi bidimensionali, a quota zero e con quota affine. La
   regressione certifica il wrap e rifiuta il carrier che perde la
   componente di rilievo oltre il budget.
4. Il certificato per sorgenti Bézier polinomiali non piane di grado
   fino a due per asse comprende una proiezione inclinata. L’altezza
   originale contribuisce sia alle UV sugli appoggi a proiezione
   affine sia alla quota normale. Bernstein racchiude il contributo
   dell’altezza quando il kernel ritaglia le celle di appoggio. Nel
   test la sella attraversa la giunzione di due appoggi tangenti e
   il residuo pubblicato è circa 1,57 × 10⁻⁸ mm sul budget di
   2 × 10⁻⁸ mm. Il certificatore rifiuta il carrier senza rilievo.
   [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

La Release Linux e le sei suite CTest del mapping passano. Passano
anche il runner multifaccia e le prove mirate del residuo con
AddressSanitizer e UndefinedBehaviorSanitizer sui moduli modificati.
Le controprove falliscono quando si disattiva la copertura per fasce
o si ripristinano i vincoli precedenti per le carte razionali e le
sorgenti inclinate. Mappa architetturale rigenerata e verificata,
con 819 file e 12.147 relazioni e senza diagnostica sulle unità di
compilazione.

Restano aperti gli appoggi con trim spline generici, le unioni con
contorni curvi o con ordine dei lati che cambia all’interno di una
fascia, le carte razionali fuori dalle impronte e dai limiti verificati
e le sorgenti non piane con trim generici, parametrizzazione razionale
o appoggi a proiezione non affine.

## K10.3 — Appoggi spline, giunzioni curve e carte non affini

- Sui trim spline piani esatti, la poligonale delle corde delimita una
  regione interna quando i poli di ogni fin restano dal lato esterno
  della corda. La deformazione del fin alla corda conserva il
  numero di avvolgimento per i punti interni. Due appoggi possono
  condividere un intero fin razionale con gli stessi coefficienti e
  verso opposto. I due contributi si cancellano e la prova usa il
  perimetro restante. Il regressore verifica anche il Wrap pubblico
  e una giunzione con poli differenti che lascia una fessura.
  [Copertura](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
- La prova del Jacobiano delle carte razionali usa il determinante
  e la normale in coordinate omogenee. Su carte con estremi
  trasversali non costanti richiede la parte simmetrica positiva
  della derivata, che assicura l’iniettività. Le impronte possono
  avere lati curvi. I loro inviluppi devono lasciare disgiunti gli
  interni delle pezze razionali. Il regressore varia entrambi gli
  assi, i pesi e due lati dell’impronta e confronta il residuo con
  una valutazione indipendente.
- Una sorgente Bézier polinomiale non piana può contribuire con la
  sua quota anche sugli appoggi a proiezione non affine. Il kernel
  compone la quota nei parametri dell’appoggio, con denominatore
  comune W⁶ nel caso razionale. La prova usa appoggi affini e
  razionali nella stessa selezione, misura il residuo e rifiuta
  la perdita del rilievo originale. Ai punti medi si affiancano
  punti interni alle celle iniziali per i casi in cui il centro
  cade sulla frontiera della sorgente.
  [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

La Release Linux e le sei suite CTest del mapping passano. La mappa
architetturale è aggiornata, con i collegamenti dei certificatori
verificati e zero unità con diagnostica.

Restano aperti i trim spline con fori o con corde senza un inviluppo
interno dimostrabile, le giunzioni curve parziali, le carte razionali
a più campate o con inviluppi sovrapposti e la combinazione di una
sorgente non piana proiettata in direzione inclinata con appoggi a
proiezione non affine.

## K10.3 — Fori spline e carte NURBS a più campate

- Per ogni foro spline piano con fin esatti il kernel usa un
  inviluppo esterno dei poli e, quando le corde lo consentono, un
  poligono interno. L’inviluppo certifica che il foro è estraneo
  al materiale sorgente, già compreso in un foro della sorgente
  oppure coperto da un secondo appoggio. Il poligono interno
  certifica eventuali punti scoperti con un margine dalla frontiera,
  anche quando il centro è coperto da un tappo parziale. Le prove
  usano quattro archi razionali e verificano il comando Wrap con
  sorgente perforata, foro scoperto e foro tappato.
  [Copertura](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
- La prova del Jacobiano e della normale lavora su ciascuna campata
  chiusa di una carta razionale a gradi fino a due e nodi bloccati
  agli estremi. Verifica la continuità alle giunzioni interne e la
  positività del Jacobiano su tutti i lati. Quando i pesi sono
  costanti lungo V e le ordinate dei poli corrispondono alla
  coordinata lineare esatta, il ritaglio Y=V evita di spendere il
  budget fuori dalla sorgente. Le regressioni esercitano appoggi
  con nodi interni in U e in U/V, verificano il B-rep, confrontano
  il residuo anche sui nodi e rifiutano il rilievo fuori tolleranza.
  [Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressione](../kernel/tests/test_mapping_global_validation.cpp)

Passano la build Release Linux e tutte le sei suite CTest del mapping.
L’avvio della Release dal launcher installato è verificato su Wayland
con EGL Mesa e preferenze temporanee. Il riavvio ha riallineato il
modulo e le librerie NVIDIA. ForgeCAD lascia la selezione del driver
OpenGL alla sessione grafica. Mappa rigenerata con 819 file e 12.147
relazioni. Verificati i collegamenti di avvio e del certificatore.

Restano aperti i fori con contorno privo di una regione interna
dimostrabile con le corde, le giunzioni curve parziali, le carte con
inviluppi sovrapposti e le sorgenti inclinate il cui rilievo influenza
le coordinate sulle carte a proiezione non affine.

## K10.3 — Giunzioni spline parziali e proiezione inclinata

- Per i fori spline piani senza un poligono inscritto, il kernel
  estrae le campate Bézier omogenee con de Boor polare in intervalli
  e le suddivide finché il loro inviluppo è separato dal quadrato
  di tolleranza del punto da classificare. Sostituire quei tratti
  con le corde conserva il numero di avvolgimento. Il test copre
  un foro polinomiale con nodi multipli e uno razionale con nodi
  semplici, con tappo completo o parziale. La prova è limitata a
  curve native non periodiche bloccate agli estremi, grado fino a
  otto e budget di suddivisione e classificazione.
- Una spline di una giunzione parziale può essere scomposta nelle
  campate Bézier con poli già presenti ai nodi saturi. Il kernel
  cancella la catena di campate con poli e pesi uguali percorse
  in versi opposti, anche se i due appoggi usano intervalli
  parametrici diversi. Verifica che il perimetro restante sia
  chiuso e ammetta un poligono inscritto strettamente convesso.
  La regressione prova entrambi gli ordini degli appoggi e una
  variante con un polo spostato. Un secondo caso ha riprodotto
  un rifiuto al contatto allineato a un nodo C0. Il classificatore
  verifica ora entrambe le derivate laterali a un nodo incontrato
  dal raggio e riprova quando il contatto non è trasversale,
  con le tolleranze già in uso. Passano entrambi i classificatori
  e il Wrap pubblico anche su questa disposizione.
  [Classificatore](../kernel/fk_classify.cpp), [Certificatore dei trim](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
- Il residuo certifica appoggi a grafico con contatti coincidenti
  in zone sovrapposte, per carte native identiche o diverse
  parametrizzazioni di uno stesso appoggio piano. Con trasversale
  affine esatto, l’ordine dei poli e pesi proporzionali lungo i
  bordi possono anche dimostrare che due impronte con ingombri
  sovrapposti hanno interni disgiunti. Il Wrap rifiuta le falde
  incompatibili nella sovrapposizione.
- Su appoggi a proiezione non affine, la quota di una sorgente
  Bézier inclinata può modificare anche una coordinata proiettata.
  Se il rilievo dipende soltanto dalla coordinata che resta
  invariata, il kernel inverte la mappa per sostituzione esatta.
  Per una carta razionale la correzione porta il peso delle
  coordinate a W² e il residuo bicubico a denominatore W¹², entro
  il budget algebrico esistente. Sono verificati un appoggio piano
  e un appoggio curvo di equazione Z=kXY con parametrizzazione
  razionale non affine. Il certificatore rifiuta il risultato che
  perde lo spostamento trasversale del rilievo.
  [Certificatore del residuo](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

Passano la Release Linux e nove suite CTest su mapping, proiezione,
bordi delle booleane e pcurve di cucitura. Le controprove isolate
falliscono disattivando la cancellazione delle giunzioni parziali o
il controllo della derivata sinistra al nodo C0. Mappa architetturale
rigenerata e verificata con 819 file, 12.147 relazioni e zero unità
con diagnostica.

Restano aperti i tagli arbitrari all’interno delle campate condivise,
i perimetri rientranti che impediscono la prova inscritta,
le sovrapposizioni di grafici per cui manca una prova di equivalenza
o di separazione e le sorgenti inclinate il cui rilievo coinvolge
entrambe le coordinate nell’inversione.

## K10.3 — Tagli interni, rientri e proiezione accoppiata

- Una giunzione può iniziare o finire dentro una campata Bézier.
  Il certificatore allinea gli intervalli dichiarati dalle fin e
  ricalcola la restrizione omogenea con operazioni esatte. Un
  estremo topologico, un polo o un peso diverso impedisce la
  cancellazione. Il test usa un arco razionale di grado due,
  prova entrambi gli ordini degli appoggi e verifica Wrap. Una
  variante ha estremi uguali ma lascia una fessura. La prova
  del foro esercita anche un arco razionale rifilato.
- Quando il perimetro residuo rientra, il kernel scompone il giro
  delle corde in triangoli orientati. Usa nella copertura per
  fasce soltanto i triangoli il cui interno è separato dagli
  inviluppi dei poli delle spline residue. Le prove comprendono
  un rientro reale, un arco che entra nella regione delle corde
  e la combinazione con un taglio interno al tratto condiviso.
  [Certificatore](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
- Per grafici sovrapposti con proiezione cartesiana affine,
  verifica l’identità delle quote omogenee sulle campate comuni,
  anche per gradi, nodi, pesi e ordine delle UV differenti. Per
  proiezioni non affini ricava una quota fisica quadratica
  candidata e controlla l’identità mediante sostituzione
  omogenea su ogni campata dei due appoggi. Una perturbazione
  di quota, anche inferiore al budget del fit, impedisce di
  certificare l’identità.
- Per una sorgente Bézier inclinata con rilievo quadratico nelle
  due coordinate, un passo di punto fisso conserva il contributo
  del rilievo in entrambe. Il limite della derivata direzionale
  dimostra la contrazione e racchiude l’errore dell’inversa.
  Il certificatore aggiunge al residuo gli errori della superficie
  emessa e dello spostamento normale calcolati su tale intervallo.
  Le regressioni comprendono bilineare e quadratica totale su
  appoggi con proiezione razionale non affine, su carrier piani
  e curvi. La prova rifiuta il risultato che perde i due
  spostamenti e lascia inconcludente la proiezione non contrattiva.
  [Residuo](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

L’applicazione Release per Linux è stata ricompilata. Passano le nove
suite CTest mirate. AddressSanitizer e UndefinedBehaviorSanitizer
passano sui due moduli modificati con la suite dei trim e le due
regressioni su sovrapposizione e sorgente inclinata.
Le controprove isolate riproducono il rifiuto disattivando
l’allineamento dei tagli interni o la triangolazione del rientro.
Rimuovendo la separazione dei poli si riproduce invece una falsa
copertura nell’area rientrante. La mappa architetturale è stata
rigenerata e verificata. Sono presenti le chiamate dal wrap alla
prova di copertura e al calcolo del residuo, oltre a quelle verso
restrizione, triangolazione, equivalenza e proiezione accoppiata.

Restano aperti i tagli la cui restrizione o identità non è
dimostrabile in aritmetica esatta, i rientri senza separazione
certificata degli inviluppi, le sovrapposizioni non affini senza
una quota fisica quadratica comune e l’inversione delle sorgenti
con rilievo di grado totale superiore o contrazione non provata.

## K10.3 — Nodi semplici, rientri e rilievi cubici

- **Tagli su nodi interni non saturi.** Il certificatore valuta il
  de Boor polare in coordinate omogenee sulla campata nativa. Estrae
  e rifila le Bézier con la stessa procedura e le confronta in verso
  opposto solo dopo aver verificato pcurve, estremi, poli e pesi. La
  prova attraversa un nodo semplice con un taglio interno alla campata.
  Passa in entrambi gli ordini degli appoggi, produce un Wrap valido e
  lascia scoperta una fessura fra due curve con gli stessi estremi.
- **Perimetri rientranti con poli fuori dal semipiano iniziale.** Il
  certificatore biseca con Bernstein il numeratore della distanza
  orientata dell’arco e la corda allo stesso parametro. Ogni tratto
  deve dimostrare che tutta la deformazione dall’arco alla corda resta
  fuori dall’interno del triangolo. La prova separa un arco cubico il
  cui inviluppo intero attraversa il semipiano e rifiuta una variante
  che entra davvero nel rientro. La prova lavora sulle Bézier già
  certificate, fino a nove poli e otto bisezioni per arco.
  [Certificatore](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
- **Sovrapposizioni con quota fisica cubica.** Le equazioni omogenee
  ricavano una quota candidata di grado totale tre e ne provano
  l’identità su tutte le campate attive. Due carte razionali native
  quadratiche con proiezioni e partizioni diverse descrivono
  `z = x*y²/512`. Lo spostamento di quota di `2^-40` lascia
  l’equivalenza inconcludente.
- **Sorgenti inclinate con rilievo cubico.** Il kernel riconosce il
  grado totale dai controlli Bézier con differenze finite e compone
  la quota come polinomio omogeneo. Se il primo tentativo non dimostra
  la contrazione, assorbe il piano tangente nella proiezione affine e
  nella quota normale. La seconda prova riguarda il rilievo residuo.
  I limiti sui rapporti dei coefficienti Bernstein mantengono la
  correlazione tra numeratore e peso, anche nel limite sul cono delle
  normali. La regressione ripida supera 1 nel limite direzionale iniziale.
  Dopo il cambio di base il limite scende sotto 1 e il Wrap soddisfa il
  budget. L’immagine che perde lo spostamento del rilievo viene rifiutata.
  [Residuo](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

La Release Linux è stata ricompilata e le nove suite CTest mirate
passano. I runner isolati riproducono i precedenti esiti inconcludenti
per il nodo semplice, il rientro e la quota cubica. Disattivando il
cambio di base si perde il certificato della sorgente ripida. Passano
AddressSanitizer e UndefinedBehaviorSanitizer sui moduli interessati.
La mappa architetturale è rigenerata e verificata. Le chiamate in
ingresso e in uscita sono risolte. La chiamata interna al predicato
generico dei triangoli è stata verificata nel sorgente.

Restano aperti i tagli la cui estrazione esatta richiede coefficienti
non rappresentabili, i rientri senza prova di separazione entro il
budget, le carte non affini senza quota fisica polinomiale di grado
totale al più tre e le sorgenti per cui neppure il rilievo residuo
ammette una contrazione dimostrabile. Per le sorgenti di grado
superiore il limite rimane anche il costo della composizione nel
budget polinomiale.

## K10.3 — Trim omogenei, winding residuo e quartici

- **Tagli con divisioni cartesiane non rappresentabili.** Il de Boor
  polare conserva numeratori e pesi esatti. Il certificatore confronta
  le coordinate omogenee per cancellare le giunzioni e usa intervalli
  verso l’esterno per gli inviluppi cartesiani. Nella prova, due poli
  interni hanno ascisse 1/48 e 23/48. La giunzione passa in entrambi
  gli ordini e il controllo respinge due archi quasi coincidenti con
  gli stessi estremi. È verificato anche il foro con trim rifilato.
- **Rientri con corde inconcludenti.** Dopo le prove per fasce e
  triangoli, il controllo Bernstein dimostra che il perimetro
  residuo evita l’intero rettangolo sorgente e ha avvolgimento
  positivo al centro. La prova copre una lamina che attraversa la
  giunzione curva e lascia inconcludente il caso con un rientro più
  profondo che espone un punto della stessa lamina.
  [Copertura](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
- **Sovrapposizioni non affini con quota quartica.** Il sistema di
  equazioni omogenee riusa gli stessi monomi e verifica la quota
  candidata su ogni campata. Due carte razionali quadratiche con
  parametrizzazioni diverse descrivono z = x²y²/512. Una
  perturbazione di 2⁻⁴⁰ sulla seconda campata invalida l’identità
  anche quando la prima campata coincide.
- **Sorgenti con rilievo quartico o proiezione non contrattiva.** Su
  appoggi complanari cartesiani con parametrizzazioni non affini,
  il residuo si confronta in UV sorgente. Il certificatore prova un
  segno stretto del Jacobiano, conserva la quota e lo spostamento
  sulle due coordinate e usa rettangoli interni agli appoggi per i
  testimoni di errore. Le regressioni coprono una Bézier quartica
  e una cubica che resta non contrattiva dopo il cambio di base.
  Le prove mantengono l’offset normale e respingono il rilievo perso.
  Con appoggi a quote diverse o con un Jacobiano che cambia segno il
  certificato diretto resta inconcludente.
  [Residuo](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

L’applicazione Release per Linux è stata ricompilata. Passano le nove
suite CTest mirate. Le controprove isolate riproducono il limite
precedente per il trim, il rientro e l’identità quartica. Disattivando
il residuo diretto si perdono i certificati sulle sorgenti quartica e
non contrattiva e il rifiuto dimostrabile del rilievo perso. Passano
AddressSanitizer e UndefinedBehaviorSanitizer sui moduli interessati.
La mappa architetturale è rigenerata e verificata in entrambi i versi.
La chiamata alla prova di avvolgimento nel predicato generico è
stata confermata nel sorgente.

Sono ancora aperti gli arrotondamenti nelle operazioni omogenee o agli
estremi topologici dei trim, i rientri senza separazione dall’intera
regione da certificare, le quote comuni oltre il quartico o non
polinomiali e la prova diretta delle sorgenti di ordine superiore o
non contrattive su appoggi curvi. Anche sugli appoggi complanari
servono regolarità della proiezione e un testimone di contatto.

## K10.3 — Omogenee intervallari, riquadri e appoggi curvi

- **Trim con arrotondamento nelle omogenee.** Il de Boor polare
  racchiude i coefficienti quando la restrizione richiede un
  arrotondamento. Per le celle sature il de Casteljau verifica
  l’estremo tenendo il denominatore comune fino alla conversione
  cartesiana. Per cancellare una giunzione con coefficienti
  intervallari occorrono gli stessi dati nativi e lo stesso
  intervallo in verso opposto. La prova esercita una divisione
  di restrizione per tre con estremo esatto, una fessura a
  estremi uguali e un vertice spostato entro la tolleranza del
  validatore B-rep.
- **Rientri che toccano il rettangolo ma evitano il materiale.**
  Suddivide il rettangolo sorgente fino a sei livelli. Ogni
  riquadro deve essere disgiunto dal disco o dal poligono
  convesso esatto, oppure avere avvolgimento residuo positivo
  e contorno separato. Il disco della regressione passa in
  entrambi gli ordini degli appoggi. Il rientro più profondo
  lascia un punto scoperto.
  [Copertura](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)
- **Quote fisiche razionali.** Il controllo delle sovrapposizioni
  può confrontare una quota data dal rapporto di un numeratore
  al più quadratico e un denominatore affine. Certifica il
  denominatore positivo e verifica l’identità omogenea in tutte
  le campate dei due appoggi. La regressione usa due proiezioni
  non affini e la quota z = (3/8388608)·y² / (1 + (y + 1,5)/3).
  Il residuo certificato sul carrier emesso è circa 1,85·10⁻⁸ mm
  su un budget di 2·10⁻⁸ mm. Una perturbazione di 2⁻⁴⁰ mm
  confinata a una campata rende la prova inconcludente.
- **Sorgenti di grado superiore o non contrattive su appoggi curvi.**
  Il residuo diretto si applica quando le carte condividono una
  quota fisica quadratica certificata. Compone la posizione e
  la normale del grafico nelle UV sorgente e mantiene il
  controllo del Jacobiano a segno stretto. Le prove su appoggi
  a sella comprendono un rilievo quartico e una sorgente cubica
  che resta non contrattiva dopo il cambio di base. Verificano
  anche l’offset normale, l’errore dell’immagine senza rilievo
  e il caso di proiezione con cambio di segno del Jacobiano.
  [Residuo](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

La Release Linux è stata ricompilata e passano le nove suite CTest
mirate. I runner isolati riproducono l’esito precedente sui trim
intervallari, sul disco presso il rientro e sulla quota razionale.
Disattivando il residuo diretto curvo si perdono i certificati e
il rifiuto dimostrabile delle immagini senza rilievo. Passano
AddressSanitizer e UndefinedBehaviorSanitizer sui moduli modificati.
La mappa architetturale è aggiornata e verificata. I predicati
generici della copertura per riquadri sono stati verificati nel sorgente.

Restano aperti gli estremi la cui coincidenza esatta è inconcludente,
le restrizioni intervallari con definizioni native diverse e i
rientri che esauriscono la prova per riquadri. Le sovrapposizioni
generiche oltre il quartico e le quote razionali fuori dal rapporto
verificato richiedono altre prove. Il residuo diretto su appoggi
curvi richiede ancora una quota quadratica comune e una proiezione
regolare della sorgente.

## Taglia superficie — Più strumenti e regioni per corpo

Il pannello **Con più superfici** accetta gli strumenti dall’elenco e
dal viewport. Il rifilo unidirezionale divide il sorgente con tutti
gli strumenti. In **Rifila tutti i corpi** ogni partecipante viene
diviso contro gli altri. Ogni corpo ha il proprio elenco di regioni
e le parti spuntate si possono cambiare anche nel viewport.

L’anteprima usa direttamente le regioni già divise. Sostituisce le
facce dei corpi consumati con il risultato in ambra e i contorni
selezionabili. Durante la modifica, anche a scelta vuota, tiene
nascosti il risultato salvato e le lavorazioni che ne dipendono.
Il formato documento 71 conserva i punti di scelta per ogni corpo.
I file precedenti conservano i modi di taglio a strumento singolo.
La prova del formato 70 conferma anche il riconoscimento della
stessa definizione da parte delle tavole collegate.

La prova geometrica con tre lamine ortogonali genera dodici regioni.
Conserva due quadranti del sorgente come due lamine di area totale
50 mm². Nel rifilo reciproco ottiene una shell aperta cucita di area
75 mm², con un quadrante da ciascun corpo. La prova del pannello
esercita picking, anteprima, conferma, riapertura senza cache e
Undo/Redo. In modifica verifica che la Scala successiva rimanga
nascosta anche con selezione vuota, e che annullando torni lo
stadio finale. La regressione è stata eseguita con gli oggetti
dell’applicazione Release Linux. Passano anche le due suite CTest
per dipendenze e cucitura delle lamine. La mappa architetturale
conferma le chiamate da pannello, picking e costruzione, quelle
verso divisione e cucitura e il recupero dell’impronta 70. [Pannello](../forgeCad2026_dialogs_trim.cpp), [regressione](../tests/test_viewport.cpp)

## K10.3 — Estremi a nodi semplici, rientri spline e quote fisiche

Per gli estremi di trim in una campata con nodo semplice, il
certificatore valuta de Boor in omogenee rinviando le divisioni.
I rapporti intermedi possono essere non rappresentabili quando le
operazioni controllate e l’uguaglianza cartesiana finale sono esatte.
La regressione usa i rapporti 1/3 e 2/3 su un trim quadratico.
Passa per entrambi gli ordini delle facce e con entrambi i lati già
rifilati. Una fessura con gli stessi estremi e un vertice spostato
entro la tolleranza B-rep restano inconcludenti.
[Prova degli estremi](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)

Per i rientri si possono scartare anche i riquadri separati dai poli
del trim spline sorgente. I semipiani candidati provengono
dall’inviluppo convesso. La separazione è controllata con intervalli
uscienti su tutti i poli, inclusi quelli in intervalli. La regressione
spline razionale attraversa la giunzione in entrambi gli ordini.
Un rientro più profondo lascia un punto del materiale scoperto.
[Copertura](../kernel/fk_mapping_trim_witness.cpp)

Per le carte con impronte sovrapposte, il rapporto di quota comune
ammette numeratore e denominatore di grado totale al più due.
La positività del denominatore quadratico si verifica con Bernstein
sul rettangolo proiettato dei poli di ciascuna carta. L’identità
omogenea va provata in tutte le campate. Una regressione con due
proiezioni non affini e termine incrociato XY certifica sul carrier
pubblicato circa 1,89·10⁻⁸ mm su un budget di 2·10⁻⁸ mm.
La prova diventa inconcludente alterando di 2⁻⁴⁰ mm l’ultima campata.

Il residuo diretto per sorgenti quartiche o prive di contrazione
dimostrata comprende appoggi che condividono una quota fisica
cubica o quartica. Compone anche le derivate della quota fisica
per la normale. Quando i prodotti supererebbero il limite del grado
Bernstein, usa una lunghezza positiva di riferimento e aggiunge al
limite del residuo l’errore di normalizzazione racchiuso con intervalli.
Le prove includono appoggi a parametro affine e razionale, rifiuto
del rilievo mancante, sorgente piegata e appoggi a quote disallineate.
[Certificatore](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

Passano le nove suite CTest mirate. AddressSanitizer e
UndefinedBehaviorSanitizer passano sui due moduli modificati e sui
runner delle prove mirate, collegati al resto della libreria Release.
I riproduttori isolati verificano anche l’esito inconcludente delle
versioni precedenti.

Restano aperti gli estremi la cui uguaglianza esatta non è dimostrabile,
le giunzioni intervallari con dati nativi diversi, i rientri fuori
dal budget, le quote polinomiali oltre il quartico o razionali di grado
superiore e il residuo diretto su appoggi senza un grafico comune
certificabile o con proiezione della sorgente irregolare.

## K10.3 — Pesi comuni, rientri concavi e giunzioni tangenti

- **Estremi dei trim razionali.** Quando i prodotti nativi XW e YW
  richiedono arrotondamento, il calcolo dell’estremo può eliminare
  un fattore comune positivo dai pesi. Usa il peso massimo locale
  e verifica esattamente ogni quoziente e prodotto prima di
  ripetere de Boor. L’estremo deve ancora coincidere esattamente
  con quello topologico. La regressione a nodo semplice verifica
  entrambi gli ordini, due fin già rifilate e il Wrap pubblico.
  Una fessura a estremi uguali, un vertice spostato entro la
  tolleranza B-rep e un peso spostato di un ULP restano inconcludenti.
- **Rientri delle sorgenti spline.** Dopo un esito inconcludente
  dell’inviluppo dei poli, il certificatore può escludere un
  riquadro il cui avvolgimento rispetto al contorno esterno
  sorgente è provato zero sull’intero riquadro. Il secondo
  tentativo usa il budget residuo e la stessa profondità massima.
  La lamina concava a L è coperta in entrambi gli ordini, anche
  dove il suo inviluppo attraversa il vuoto. Con un rientro più
  profondo un punto del materiale resta scoperto.
- **Quote comuni di quinto grado.** Due carte non affini con
  coordinate accoppiate condividono la quota
  z = (x − y²/8)²·y/2¹⁸. L’eliminazione tenta una sottrazione
  con fattore esatto per il sistema quintico, ripiegando sulle
  operazioni senza divisione in caso di arrotondamento. La
  sostituzione omogenea resta esatta in ogni campata. Il residuo
  del carrier pubblicato è entro 2·10⁻⁸ mm. Una perturbazione
  di 2⁻⁴⁰ mm sulla seconda campata lascia la prova inconcludente.
- **Sorgenti su appoggi con curvature diverse.** Il residuo
  diretto ammette una catena di appoggi quadratici con impronte
  rettangolari contigue, anche a parametro razionale. Verifica
  quota e derivate lungo ogni giunzione e copertura chiusa
  della proiezione sorgente. Racchiude il residuo valutando
  separatamente tutti i tasselli che il riquadro sorgente può
  toccare. Le regressioni usano una sorgente quartica e una
  cubica non contrattiva. Verificano entrambi gli ordini, il
  rilievo perso, una piega, un salto di quota e una fessura.

La Release Linux è stata ricompilata. Passano le nove suite
CTest del kernel. Passano anche ASan e UBSan sui due moduli
modificati e sui runner pertinenti, collegati al resto della
libreria Release. L’avvio dall’eseguibile usato dal launcher e
la mappa architetturale sono verificati. Le controprove isolate
riproducono l’esito inconcludente del percorso precedente.

Restano aperti i trim con estremi ancora inconcludenti, le
restrizioni con dati nativi diversi e i rientri fuori dal
budget. Restano anche le quote polinomiali oltre il quinto
grado o razionali oltre il rapporto quadratico, gli appoggi
senza giunzione tangente certificabile o con impronte più
generali e le sorgenti con proiezione ancora inconcludente.
Il residuo diretto con una quota comune resta al quartico.
[Trim](../kernel/fk_mapping_trim_witness.cpp), [residuo](../kernel/fk_surface_mapping_bounds.cpp), [prove di copertura](../kernel/tests/test_surface_mapping.cpp), [prove del residuo](../kernel/tests/test_mapping_global_validation.cpp)

## Taglia superficie — Isolamento e picking in Senza nome

Il taglio reciproco salvato nel documento usa una superficie di
rivoluzione e quattro corpi di ripetizione. Riaprendo il taglio si
ottengono cinque regioni sul sorgente e due per ciascuno dei quattro
strumenti. Il pannello permette di isolare un corpo alla volta e di
ripristinare la vista d’insieme. Il puntatore mostra il nome del corpo,
il numero della parte e lo stato di conservazione. Regioni disponibili
in grigio, mantenute in ambra, puntate in verde.

Il disegno e il picking applicano lo stesso filtro. Nella vista
d’insieme un corpo opaco in primo piano blocca il clic sulle regioni
sottostanti. La scena mostra le mesh già divise anche a scelta vuota e
nasconde i corpi originali coperti dalle regioni. In modifica nasconde
anche il risultato e gli stadi che ne dipendono. La scelta degli
strumenti dal viewport esclude il sorgente. Il punto salvato per un
rifilo a strumento singolo proviene dalla regione selezionata, così un
clic vicino alla giunzione conserva la regione evidenziata.
Lo zoom e la profondità della scena includono le regioni visibili.
Durante la scelta si caricano soltanto le mesh di contesto necessarie.

La Release Linux è compilata. La prova GUI attraversa tutti i
tredici frammenti della copia di Senza nome.prt e controlla che
annullando torni Taglio 7. Le regressioni con lamine verificano
anche i modi precedenti, l’occlusione, la conferma e Undo/Redo.
Una cattura OpenGL su Wayland verifica i colori di scelta e
puntamento e l’etichetta nel viewport. Passa la suite CTest delle
dipendenze. La copia e l’originale hanno la stessa impronta SHA-256.
[Pannello](../forgeCad2026_dialogs_trim.cpp), [picking](../forgeCad2026_viewport_selection.cpp), [disegno](../forgeCad2026_viewport_render.cpp)

## Senza nome — Tre facce sul bordo di un rifilo

La copia del file Desktop salvato alle 23:07 contiene le due
rivoluzioni e i corpi della ripetizione. La feature di taglio
non è ancora salvata. L’analisi usa la stessa divisione e
cucitura del comando su Rivoluzione 1 e sui quattro risultati
di Ripetizione 3.

- **Rivoluzione 1** ha cinque regioni. La Parte 5 misura circa
  4.348,06 mm². Le altre quattro misurano circa 100,17 mm².
- **Ciascuna ripetizione** ha due regioni. Un raggio orizzontale
  lanciato dall’asse di Rivoluzione 1 alla quota del campione
  conferma che il campione della Parte 1 sta all’interno e
  quello della Parte 2 all’esterno. La regione interna
  misura circa 111,54 mm².
- **Conferma con una spunta per corpo.** Parte 5 sulla rivoluzione,
  Parte 1 sulle quattro ripetizioni. Il normale percorso
  di rigenerazione produce un’unica lamina con cinque facce
  valide. I soli bordi liberi sono le due imboccature, a quota
  0 mm e 50 mm.
- **Controprova.** Aggiungendo la piccola Parte 1 della
  rivoluzione prima della Parte 5, nell’ordine del pannello,
  compare esattamente l’errore sull’edge 0 dello screenshot.
  La regione principale, il dischetto della rivoluzione e la
  regione della ripetizione arrivano sulla stessa curva
  d’intersezione. Una cucitura manifold richiede due fin in
  versi opposti. La versione del pannello usata in questa controprova
  preselezionava la Parte 1. Il clic su una seconda regione
  aggiungeva una spunta. Anche lasciare
  entrambe le parti di una ripetizione crea una terza fin.

La prova è stata eseguita sulla copia. L’impronta originale
è identica. [Rigenerazione](../cad_forge.cpp), [spunte del pannello](../forgeCad2026_dialogs_trim.cpp), [vincolo topologico](../kernel/fk_topology.cpp)

## Taglia superficie — Scelta esplicita delle regioni

Il rifilo multiplo apre i gruppi con caselle vuote. Durante
la selezione degli strumenti dal viewport, **Mostra suddivisione**
prende il posto di **Conferma** nel piè fisso, anche con il form
scorso in cima. Il comando esce dalla scelta dei corpi e mostra
le regioni. Anche Esc nel viewport completa la selezione degli
strumenti. I colori del viewport
e le spunte restano sincronizzati durante i clic.

I tagli singoli mostrano **Scegli una parte** finché l’utente non
indica una regione nell’elenco o nel viewport. La conferma
richiede le parti necessarie al modo scelto. Una nuova
suddivisione parte senza spunte quando cambiano gli operandi
o il modo reciproco del rifilo multiplo. Rientrando nella scelta
dei corpi e uscendo con gli stessi operandi si conservano i clic
espliciti. Alla riapertura si recuperano le regioni delle feature
già salvate.

La prova su una copia di Senza nome.prt mostra 13 regioni
vuote con **Mostra suddivisione**. Selezionando nel viewport
Parte 5 di Rivoluzione 1 e le quattro Parti 1 delle ripetizioni,
la conferma produce una sola lamina valida con cinque facce.
Passano anche i test mirati per rifilo singolo, ritorno con Esc,
ripristino delle scelte, salvataggio e Undo/Redo. Su Wayland il
pulsante è leggibile nel piè anche con l’illustrazione in vista
e la lista sotto il limite dello scroll. La Release è compilata,
l’avvio dal launcher e la mappa architetturale sono verificati.
[Pannello](../forgeCad2026_dialogs_trim.cpp), [selettore](../forgeCad2026_dialogs_common.cpp), [prove](../tests/test_viewport.cpp)

## Cuci superfici — Crash in apertura del pannello

Il core dump del 22 novembre alle 06:47:20 mostra un SIGSEGV in
`QLayout::removeWidget`, chiamato da `sewDialog` durante l’apertura.
Il selettore condiviso dei corpi, dopo la modifica per Taglia superficie,
creava un `QBoxLayout`. Nel pannello Cuci superfici il cast a
`QHBoxLayout` restituiva un puntatore nullo. Il programma si arrestava
prima di elaborare la geometria.

Il selettore crea di nuovo un `QHBoxLayout` per le righe orizzontali
e un `QVBoxLayout` per quelle verticali. Cuci superfici usa
l’interfaccia comune `QBoxLayout` per disporre il campo e i pulsanti
di selezione. La prova su una copia di Senza nome.prt ha riprodotto
il SIGSEGV con i moduli precedenti. La Release corretta passa
apertura, Dalla vista, selezione completa, deselezione e conferma
del pannello su Qt offscreen e Wayland. Passano anche le prove
mirate di Taglia superficie per picking, anteprima, salvataggio
e Undo/Redo. Verificato l’avvio del binario usato dal launcher.
[Pannello](../forgeCad2026_dialogs_surfaces.cpp), [selettore](../forgeCad2026_dialogs_common.cpp)

## Guscio — Apertura F8 di Solido cucito 14

La copia di Senza nome.prt salvata il 22 novembre alle 06:57
(SHA-256 `a1e0d5ba812c362e00e0695fdbc232401c2da9b90ff32cadc3d2a71ea1e60865`)
contiene il Solido cucito 14, a dieci facce. Con apertura F8 e spessore
interno di 1 mm il bordo E0 contro la faccia di rivoluzione F0 cade a
`v = -0,059981764`, oltre l’inizio del meridiano NURBS `[0, 2]`.
L’intersezione con il piano aveva un residuo di circa 10⁻¹⁴ mm rispetto
all’offset teorico. Il fit della parete, ristretto al vecchio dominio,
veniva estrapolato sul tratto mancante. Sul bordo emesso lo scarto
era circa 0,0000101 mm contro 0,000001 mm ammessi. A 0,5 mm la
verifica del bordo passava, ma l’inversione usata per il volume
rimaneva vincolata al dominio più corto.

L’offset con giunzioni estende il tratto terminale del meridiano
B-spline fino a contenere i poli UV del contorno rifilato. Il fit
certificato del meridiano a distanza lavora sul dominio esteso,
con i budget esistenti. Il test con un solido di rivoluzione creato
dal kernel riproduce il difetto anche senza il documento. Verifica
l’apertura, il materiale residuo, lo spessore su un punto della parete,
le origini della faccia interna e l’integrità della sorgente.
[Offset](../kernel/fk_offset.cpp), [regressione](../kernel/tests/test_offset_validation.cpp)

Il comando dalla pipeline applicativa genera un singolo solido
chiuso di 19 facce con zero rilievi a 0,5 e 1 mm. A 1 mm il
volume è circa 5.153,91 mm³. La lettura del B-rep conserva i
conteggi topologici e supera il checker. Il B-rep sorgente
rimane identico durante la prova. A 1,5 mm passa il controllo
del B-rep appena costruito. Release e avvio del binario
verificati, sei CTest mirati e 18 casi del filtro Shell superati.

## K10.3 — Fattore dei pesi, quote razionali cubiche e residuo quintico

- **Giunzioni di trim con pesi proporzionali.** La cancellazione delle
  giunzioni spline con coefficienti intervallari ammette un fattore
  comune positivo tra i pesi locali. Verifica i prodotti incrociati
  esattamente, oltre ai poli, alla finestra dei nodi e all’intervallo
  parametrico. Conserva la prova esatta degli estremi. Il caso
  negativo altera un solo peso lasciando coincidenti gli estremi
  ma apre un varco nell’ultima campata.
- **Quota razionale con numeratore cubico.** La prova di coincidenza
  delle sovrapposizioni omogeneizza numeratore e denominatore allo
  stesso grado, mantenendo la prova di positività del denominatore.
  Il Wrap pubblico passa per la quota proporzionale a
  x·y²/((x+4)·(1,5+0,5y)), con y normalizzata nel riproduttore.
  Il residuo certificato rispetta il budget di 2·10⁻⁸ mm e
  l’alterazione confinata alla seconda campata resta inconcludente.
- **Residuo diretto su appoggi quintici.** La composizione determina
  il grado fisico ammesso in base al grado nativo della sorgente.
  Una sorgente quartica inclinata passa sul carrier emesso dal Wrap
  quando i due appoggi condividono una quota quintica, entro il cap
  Bernstein invariato di 20 per asse. Un carrier traslato di 2⁻¹⁶ mm
  viene respinto con un testimone sul materiale.

La Release Linux è stata ricompilata e l’eseguibile del launcher
è stato avviato. Passano le nove suite del kernel e ASan/UBSan
sui due moduli modificati e sui runner pertinenti, collegati
al resto della libreria Release. La mappa architetturale è aggiornata
e verificata. Le prove isolate riproducono l’esito inconcludente
dei tre casi prima dell’estensione.

Restano aperte le giunzioni con nodi e poli non riconducibili agli
stessi dati locali, i rientri ancora fuori dal budget, le quote oltre
il quintico e i rapporti con numeratore oltre il cubico o denominatore
oltre il quadratico. Gli appoggi con quote diverse richiedono ancora
impronte rettangolari contigue e una giunzione tangente provata.
[Copertura trim](../kernel/fk_mapping_trim_witness.cpp), [residuo](../kernel/fk_surface_mapping_bounds.cpp), [regressioni](../kernel/tests/test_mapping_global_validation.cpp)

## Confronto con il registro precedente

| Filone | Esito del ricontrollo |
| --- | --- |
| K10.1, quadratura | I casi mirati e i due Shower Head passano. Rimangono rappresentabilità, rumore e condizioni difficili. Il difetto di orientamento con i cubi disgiunti è corretto e dispone di regressioni dedicate. |
| K10.2–3, carrier e famiglie | Confermati il caso positivo e il rifiuto sui target polinomiali coperti. Il controesempio a pesi variabili è corretto nel perimetro descritto sopra. I target periodici e a gradi misti sono trattati nel seguito dello stesso intervento. Restano da valutare per chiamante i target analitici e procedurali, le facce sorgenti non piane e i risultati diversi dalla bicubica nativa. |
| K10.4, ramo di Newton | I test mirati di inversione passano. Il controllo del residuo finale e la scelta del ramo restano due verifiche distinte. Il caso generale `nearPole` attende un esempio riproducibile. |
| K10.5, B-rep finale | La distanza rail/carrier delle aperture di Spessore è verificata. L'uscita dalla regione rifilata fra i tre campioni è stata riprodotta e ora viene rilevata cercando le intersezioni UV con i contorni. Rimane da certificare lungo l’intera curva la corrispondenza con la pelle a distanza e da estendere il controllo delle tangenze generiche. [Ripristino aperture](../kernel/fk_open_offset.cpp#L446). |
| K10.6, costo e rifiuti | Conservare i budget. Separare costo di costruzione del candidato, costo del certificato e rifiuto per limite di lavoro. |
| Anteprime e conferma | La creazione di Booleana, Parallelepipedo e Scala riusa il risultato pronto sul comando reale. Per Estrusione il riuso interessa i casi a distanza con operandi espliciti. Restano i limiti associativi e le fusioni automatiche, oltre ai percorsi specifici di Taglio, Estendi e Pattern. [Prova del comando](../tests/test_boolean_box.h). |
| Tema e schede | Due popup contestuali usano direttamente QMenu per le schede e per l'aggancio del pannello. Differenza accertata nei sorgenti, confronto visivo chiaro/scuro ancora aperto. UI11 riguardava altri menu. [Schede](../cad_document_tabs.cpp#L49), [aggancio](../forgeCad2026_dialogs_common.cpp#L424). |
| Orfane | Estesa la ricerca ai nomi con una o due occorrenze e senza chiamata risolta nel grafo. Su 27 candidati, 13 sono override/codice Qt, sei sono le query/cataloghi già conservati, sette hanno un chiamante visibile nel sorgente e uno è un wrapper interno senza usi nel repository. È `boundSweepSurfaceProjection`: la via attiva delle calotte passa da `SweepSurfaceQuery::projection`. Proposta la rimozione del solo wrapper dopo verifica del perimetro API. Il validatore e K2 rimangono attivi. [Wrapper](../kernel/fk_sweep_surface_validation.cpp#L1096), [chiamante effettivo](../kernel/fk_sweep_caps.cpp#L480), [esempio di override Qt](../forgeCad2026_dialogs_surface_tools.cpp#L26). |
| Memoria e GPU | La cache anteprime misura 64 MiB sui punti della mesh. B-rep e snapshot hanno un costo distinto. L'esportazione PDF multifoglio materializza più editor e scene contemporaneamente. Restano il profilo lungo su più documenti 3D, operazioni annullate e risorse GPU. [Cache](../forgeCad2026_viewport_preview.cpp#L30), [PDF](../cad_drawing_window.cpp#L400). |
| Già chiusi e verifiche esterne | K2, ellisse per estremi, vecchie 22 orfane e riparazioni dei menu conservano i rispettivi esiti. Per Supporto e Cesto vale la versione identificata nelle prove precedenti. La scansione locale non ricertifica modelli modificati, CI remota, macOS o driver GPU. [Inventario precedente](review-punti-aperti-2026-11-17.md). |

## Copertura e interpretazione delle prove

Inventario da Git, inclusi i sorgenti locali nuovi, esclusi prodotti di build, copie storiche, fixture binarie e risorse grafiche. La ricognizione sistematica ha interessato **804 file attivi**. 226.514 righe fisiche prima delle modifiche di questo intervento, 226.531 nella mappa finale. 207 file sono kernel, 277 applicazione e servizi, 306 test e benchmark, 14 strumenti e configurazione di build. La [tabella per file](review-generale-prove-2026-11-20/copertura.tsv) identifica gruppo, righe, unità AST Linux e file collegati da chiamate o riferimenti. Gli header sono visitati anche attraverso le unità che li includono. La lettura manuale approfondita ha privilegiato orientamento e massa, scambio, mapping, offset, solver, documenti e cronologia, picker e conferma di anteprima, temi, cache e verifica dei test. La lettura riga per riga dell'intero corpus resta distinta dalla scansione di tutti i file.

- Cppcheck 2.22.0, C++17, un processo, configurazione singola, livello `reduced`, classi warning/performance/portability, modelli di libreria Qt e POSIX. Visitati 776 file C++ e header, inclusi i test. Il primo passaggio completo emette 483 segnalazioni, di cui 48 classificate error dallo strumento. Due diagnostiche di macro OCCT richiedevano la definizione di `Handle(x)`; ripetuto il sottogruppo con la definizione. [Diagnostiche per sito](review-generale-prove-2026-11-20/segnalazioni.tsv). La convalida di ogni warning resta aperta. Esempi di triage verificato:
  - In Spessore il vettore delle storie riserva in anticipo lo spazio per **tutte** le pareti e tutti i bordi di chiusura. I puntatori rimangono stabili durante quelle aggiunte. In tassellazione `fin` è un riferimento a un elemento di `Body`; i nodi d'orecchio sono in `std::deque`. Queste segnalazioni non dimostrano una perdita o un puntatore pendente.
  - Il secondo controllo in `checkBody` segue una funzione che può aggiungere problemi al contenitore. Nel picking di schizzo la seconda ricerca può modificare `found` attraverso una lambda. Entrambe le condizioni sono raggiungibili. La condizione irraggiungibile verificata manualmente è nel pannello Dettaglio.
  - I confronti di `constData()` nelle cache di preview verificano l'identità dei dati condivisi Qt. La ricerca di orfane comprende sorgenti dei test e backend opzionali. Qt virtuale, slot, callback e codice dipendente dalla piattaforma richiedono ispezione oltre al grafo.
- Ricerca di cloni sulle implementazioni di produzione in C++/CUDA/Objective-C++, soglia di otto righe significative consecutive con token uguali. Trovate 61 coppie nella fotografia iniziale. La soglia non misura complessità ciclomatica e i blocchi possono sovrapporsi.
- Controllata la sintassi di 11 script Python, quattro shell, workflow YAML, QRC, XML MIME e plist macOS. Due unità CUDA e tre Objective-C++ sono incluse nell'inventario e nella ricognizione testuale. La configurazione AST/Linux e Cppcheck C++ non esercitano i driver o il rendering macOS.
- **Memoria delle tavole.** Un processo Qt offscreen ha aperto, rilocalizzato, modificato, annullato e chiuso 60 tavole nel ciclo di prova, usando una copia temporanea dello stesso modello. Dopo la chiusura e lo smaltimento degli eventi differiti, zero widget di primo livello ai sette punti di misura. RSS 52.576 KiB al ciclo 1, 52.640 al 10 e 52.652 al 60. È un controllo del flusso 2D corretto con un piccolo modello. Restano da misurare finestre 3D, driver grafici, cache e geometrie grandi su sessioni prolungate.
- **Disco.** Il rapporto conserva i sorgenti dei riproduttori e tabelle testuali. I sorgenti di prova sono separati dai target di produzione. [Istruzioni per riprodurre](review-generale-prove-2026-11-20/README.md).


## Verifiche eseguite sul checkout

- Build Release dell'applicazione Linux e dei runner kernel interessati completata. Nella suite globale, filtro assente 1 come codice di uscita, filtro presente 1/1 e suite senza filtro 36/36. Otto CTest su posa di shell, quadratura, inversione, certificati dei bordi, cucitura e mapping globale passano. I due Shower Head superano nuovamente i relativi casi nel runner open-offset.
- Nel runner kernel con OCCT già presente nel checkout passano i gruppi filtrati Step 22/22, Iges 9/9, Sew 12/12 e Mass 11/11. I riproduttori di questo rapporto esercitano condizioni che quei gruppi non intercettano.
- Mappa rigenerata e controllata con 804 file, 11.867 relazioni e 413 unità AST senza diagnostica d'errore. Verificati gruppi e catalogo, uguaglianza HTML/JSON e collegamenti effettivi fra solver e chiamanti, fra finestra tavola e cronologia, fra wrap e certificatore. La prova con gruppo sconosciuto viene rifiutata.
- Runner applicativo ricompilato. Sette CTest su tavola collegata, multifoglio, vincoli delle maniglie, solver partizionato, catena connessa, schizzo con punti fissi ed ellisse per estremi superati. Nei due eseguibili prima/dopo, le catene da 64, 256 e 512 segmenti danno le stesse coordinate, dimensioni del sistema e due iterazioni in ogni caso. È verificata la rimozione della seconda costruzione dei blocchi. Non è stata stimata una percentuale generale di accelerazione.
- **Modelli correnti dell'utente.** Su copie di Supporto.prt e Cesto.prt il runner appena compilato ha eliminato le cache di corpo e display in memoria e rigenerato la storia. Supporto ha 16 funzioni, nove schizzi, 13 B-rep rigenerati; Cesto ha 13 funzioni, sei schizzi, 12 B-rep rigenerati. Nessuna funzione ha restituito errori. Il confronto con l'apertura ordinaria dà lo stesso SHA-256 del B-rep serializzato a ciascuno dei 13 e 12 passi con corpo. SHA-256 degli originali ricontrollati a fine prova: `efd53520670acac760d7471cde3e45fbb005574c28a4c4ff87131106179ab406` (Supporto) e `d5e7a7fe839105a645dee701a19f139d87f3f0da8a613bfe7d25b68907fd948b` (Cesto). La prova riguarda le storie salvate in queste versioni. Le verifiche dimensionali delle tre asole e le prove sugli operandi isolati restano quelle dei rispettivi rapporti.

## K10.3 — Estremi arrotondati e separazione continua dai rientri

Su una campata Bézier razionale il de Boor in omogenee poteva
fermarsi quando un prodotto nativo XW o YW richiedeva
arrotondamento, anche con un estremo cartesiano esatto. Dopo i
controlli esistenti, sulle campate sature con parametro
normalizzato rappresentabile la prova usa espansioni binarie di
double e verifica l’uguaglianza esatta del prodotto con il peso
finale. I controlli sugli esponenti delimitano le operazioni per
evitare overflow e underflow. Il risultato deve ancora coincidere
con il vertice B-rep. Nella regressione cubica, l’estremo (9/16, 0)
è esatto. La verifica con frazioni conferma il prodotto intermedio
non rappresentabile. Passano entrambi gli ordini e il caso con
entrambi i trim già tagliati. Un vertice spostato di un ULP e una
fessura con estremi comuni restano fuori dalla copertura provata.
[Predicato](../kernel/fk_predicates.cpp), [estrazione](../kernel/fk_mapping_trim_witness.cpp)

Quando la prova per riquadri resta inconcludente vicino a un
rientro, un contorno residuo con avvolgimento positivo deve essere
separato da tutto il disco o poligono convesso sorgente. La
suddivisione Bernstein racchiude ogni tratto e prova la separazione
dalla regione chiusa. L’avvolgimento è allora costante sulla regione
sorgente. Il budget di lavoro disponibile è quello della prova
preesistente. Il disco di raggio 40705/2¹⁸ mm attraversa la
giunzione con un margine inferiore a 10⁻⁵ mm dal rientro. Le
frazioni esatte certificano la distanza quadratica positiva dalla
Bézier razionale e rilevano l’intersezione per il raggio
40708/2¹⁸ mm. I due casi positivi della review si fermano nel
kernel precedente e passano nei riproduttori con le correzioni.
[Copertura](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp)

La Release Linux è compilata e il binario del launcher si
avvia su Wayland. Passano nove CTest mirati. ASan, UBSan
e LeakSanitizer passano sui due moduli modificati e sui
riproduttori mirati collegati al resto della libreria Release.
Un confronto separato su 2.005 ingressi con frazioni esatte
conferma tutte le 504 risposte pubblicate dal predicato,
incluse 500 configurazioni simmetriche. I risultati rimanenti
sono inconcludenti. Il Wrap pubblico del disco a margine
ridotto produce una lamina valida in entrambi gli ordini
degli appoggi. La mappa architetturale verifica 819 file e
12.166 relazioni. La chiamata dalla lambda annidata al
certificatore di separazione è stata verificata anche nel
sorgente.

Restano aperte le celle non sature che richiedono espansioni,
i parametri privi di rappresentazione esatta in questo percorso,
le giunzioni con dati locali diversi e i rientri senza separazione
provata entro il budget, inclusi i contatti tangenti sul contorno.
