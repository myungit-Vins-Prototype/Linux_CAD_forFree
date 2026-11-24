# K10 — quota affine al bordo zero e proiezione del toro fusiforme

Il wrap della lamina inclinata con quota nulla su un bordo dichiarava una deviazione di 9,92e-11 mm e perdeva un rilievo interno di 0,0001 mm. Il provider usava il percorso campionato quando l'intervallo della quota includeva zero. Il caso è documentato nel [rapporto precedente](review-k10-carrier-affine-2026-11-18.md).

La via continua copre ora anche quote affini che toccano o attraversano zero. Con D=F−T, C=Tu×Tv, ρ>0 pari a un limite inferiore di ‖C‖, A≥‖ρD−hC‖, H≥|h| e K≥|C·C−ρ²|, il residuo è al più (A+HK/(2ρ))/ρ. La prova usa coefficienti Bernstein con intervalli outward e la normale naturale. Mantiene il cutoff di regolarità, la copertura del rettangolo e i budget esistenti. Il confronto diretto a quota identicamente zero e il percorso delle quote strettamente di un segno restano disponibili.

La lamina con rilievo perso è rifiutata prima di pubblicare storia o deviazione anche con history assente. La stessa lamina sul target piatto riesce. Su un target a normale inclinata la prova accetta uno scarto noto entro il budget all'attraversamento dello zero, anche con UV scambiati, e rifiuta il verso opposto.

Nel filone delle inversioni, `projectPoint` saltava il meridiano opposto del toro fusiforme di raggi 1 e 2. Il punto (1,0,0) appartiene a (π,π) e la proiezione riportava distanza 2 sul dominio intero e 0,1414744 su una finestra che conteneva (π,π). Anche `invertPoint` dal seme (0,0) falliva. La ricerca ora aggiunge il candidato sul meridiano a raggio firmato negativo e applica gli stessi limiti di finestra e periodicità. Passano dominio intero, finestre traslate di un periodo, finestra che esclude il ramo e inversione dopo l'arresto di Newton.

Build completa Linux, 10 suite kernel mirate e 25 gruppi del runner OCCT con filtro `Project` superati. Il runner delle mappature globali passa 23 gruppi. Restano UV obliqui o di rango uno, target a pesi variabili o istanziati nel controllo globale e i casi generali Newton e B-rep finale.


Il caso UV di rango uno è stato isolato in esecuzione. Una lamina verticale X=0,25 con Y in [0,15; 0,65] e Z in [0,001; 0,0011] lascia U costante e V variabile lungo un asse sorgente. Il target bicubico ha quattro poli in U, nove in V con i nodi del rilievo precedente e tutti i poli Z a zero tranne la riga V=4, posta a 1,5e-4 mm. L'API accetta il risultato con deviazione dichiarata 9,41596e-11 mm e checker senza problemi, ma perde un rilievo di 0,0001 mm al centro della campata. Il controllo piatto conserva residuo circa 8,0e-15 mm. Il caso rimane aperto. Per U costante e V affine in un solo asse è sufficiente portare esplicitamente l'asse scelto nella tensorizzazione. Le mappe oblique condividono le variabili e richiedono composizione polinomiale distinta.


### Estensione al caso di rango uno separabile

Il certificatore conserva esplicitamente l'associazione degli assi quando una coordinata UV è costante e l'altra dipende da un solo asse della faccia sorgente. Posizioni e derivate naturali della superficie target seguono quell'associazione nel tensore Bernstein. Le celle chiuse che condividono il nodo della coordinata costante vengono ancora verificate tutte.

La lamina verticale del riproduttore passa sul target piatto. Sul target con rilievo, l'API rifiuta la superficie per residuo globale prima di pubblicare deviazione o genealogia. Il test del provider copre le quattro combinazioni di asse sorgente e coordinata costante su un nodo doppio, il verso inverso e la quota affine che attraversa zero. I casi con entrambe le UV attive sullo stesso asse rimangono fuori dal certificato, così come le mappe oblique, le istanze e i pesi variabili. Il runner globale passa 25 gruppi.

Il successivo riproduttore ruota la lamina verticale: il suo asse orizzontale è (0,6; 0,8; 0), quindi entrambe le UV seguono il medesimo parametro sorgente. Il target piatto conserva il residuo locale di circa 2,87e-14 mm. Il target col rilievo è ancora accettato con deviazione dichiarata 6,08e-11 mm e perde 1e-4 mm nel punto di controllo. Questo caso resta aperto nel filone delle mappe oblique. Richiede composizione Bernstein delle due basi sullo stesso asse prima del confronto con il carrier emesso.


### Rango uno con entrambe le UV sullo stesso asse

Il certificatore ora compone le basi di U e V come polinomi Bernstein dello stesso parametro sorgente. Determina le derivate naturali del target prima di comporle. Il massimo grado per asse sale a 20 per la norma quadratica della normale. Resta la copertura conservativa delle coppie di celle chiuse e si applicano gli stessi limiti di lavoro.

Il riproduttore API della lamina ruotata accetta il target piatto e rifiuta quello con il rilievo di 1e-4 mm prima di pubblicare storia o deviazione. I test diretti coprono entrambi gli assi sorgente, pendenze di segno opposto, quota affine che attraversa zero e il prodotto UV quadratico sui target di grado 2 e 3. La variante con il termine quadratico sostituito dalla corda viene rifiutata. Sei CTest di mapping e offset superati, 28/28 gruppi nella suite globale e 25/25 gruppi Project.

Il perimetro residuo include mappe UV non separabili con entrambe le coordinate sorgenti attive, target a pesi variabili e istanze. Un test mantiene esplicitamente il caso di rotazione nel piano UV fuori dal certificato attuale.

Nel riproduttore diretto, il controllo piatto ha residuo di 2,87e-14 mm e limite globale dichiarato di 5,74e-9 mm. Il carrier con rilievo viene rifiutato dal controllo globale. Il runner con provider e test strumentati supera 18/18 gruppi ASan/UBSan con leak detection attiva.

Build Linux aggiornata. Mappa rigenerata su 810 file e 11.862 relazioni, 413 unità analizzate senza diagnostica. Verificati l'HTML e i collegamenti del mapping, della stima per la cucitura, del calcolo di massa e del certificato E/S/Q.


### Mappa UV obliqua di rango due

Una seconda lamina ruota l'asse quasi verticale di circa 1e-7 radianti. Le due colonne UV sono approssimativamente (−0,6; −0,8) e (8e-8; −6e-8), con determinante 1e-7. Prima della correzione il wrap accetta il carrier con rilievo, dichiara una deviazione di 1,17e-10 mm e perde 1e-4 mm al picco. Il checker del risultato rimane vuoto.

Il certificatore accetta ora mappe affini oblique con determinante di segno provato. Costruisce le basi Cox bivariate dai quattro vertici Bernstein, compone la posizione e le derivate naturali e verifica tutte le coppie di celle chiuse candidate. Il costo per cella è pesato rispetto al percorso già supportato. I limiti di grado, la cancellazione e il rifiuto conservativo rimangono applicati. La variante piatta ha residuo al punto di controllo di 7,28e-12 mm e limite globale dichiarato di 9,89e-9 mm entro il budget di 2e-8 mm. Il caso con rilievo è rifiutato prima che storia o deviazione siano pubblicate.

Verificati il verso naturale sotto inversione di un asse parametrico, un oracolo affine con scarto di 2e-8 mm, il termine misto UV su target di grado 2/3 e il rifiuto dopo la sua rimozione. Il rango obliquo incerto rimane fuori dal certificato. Sei suite CTest mirate, 32/32 gruppi nella suite globale e 25/25 gruppi Project. Build Linux completa.

Il controllo aggiuntivo attraversa nodi ripetuti in U e V con otto angoli, due versi del parametro e quote di segno opposto. Accetta 32 carrier analitici e rifiuta le 32 varianti spostate di 2e-4 mm. Il massimo limite dichiarato sui casi accettati è 1,05e-11 mm. Il provider e le regressioni mirate passano 22/22 con ASan/UBSan e leak detection attiva.

Mappa finale verificata su 810 file e 11.862 relazioni. Analizzate 413 unità senza diagnostica e verificati i collegamenti del certificatore obliquo e dei messaggi di analisi delle micro-geometrie.


### Mappa UV obliqua con rango incerto

L'API riproduce il difetto con una lamina verticale riparametrizzata a 45°. Entrambe le colonne UV hanno due coefficienti attivi e coincidono. Il determinante è esattamente zero. Prima della correzione il caso con rilievo viene accettato con deviazione dichiarata di 5,64e-10 mm e residuo al picco di 1e-4 mm. Il checker non segnala problemi.

La composizione affine in avanti usa le stesse basi Cox bivariate anche se il determinante si annulla o il suo intervallo contiene zero. La prova della normale naturale del target continua a richiedere derivate regolari. Il controllo piatto riesce con limite dichiarato 8,62e-9 mm entro 2e-8 mm. Il rilievo perso fa scattare il rifiuto del residuo globale prima che storia o deviazione siano pubblicate. I test diretti coprono inversione di un parametro, quota di segno costante o variabile, grado 2/3 e scarti analitici entro e oltre il budget. Nei coefficienti IEEE del test diretto il determinante esatto è circa 3,33e-18 a scala 2 per asse. Il suo intervallo include zero.


### Target B-spline polinomiale istanziato

Sul riproduttore a lamina diagonale, il target traslato di (0,125; −0,25; 0,5) mm lasciava passare un residuo di 1e-4 mm con deviazione dichiarata di 6,08e-11 mm. La prova ora legge i coefficienti dalla base nativa e applica il collocamento in intervalli alle posizioni centrate e alle derivate. Usa la normale naturale calcolata con le derivate collocate, anche in caso di riflessione. La candidata continua a essere una bicubica nativa polinomiale.

Per il target traslato e piatto il limite dichiarato è 5,74e-9 mm e il checker è vuoto. La variante con rilievo perso viene rifiutata, con deviazione e genealogia conservate. L'oracolo con riflessione controlla entrambi i versi UV e quote positive e negative. Una prova con traslazione di 2^18 mm e contributo nativo di 2^-36 mm distingue i poli nativi dal loro arrotondamento in coordinate globali. Il confronto con la copia appiattita passa a 1e-8 mm e fallisce al budget inferiore allo scarto esatto del polo d'angolo. La suite globale supera 36 gruppi.

Il caso combinato con determinante UV esattamente nullo e target traslato accetta il controllo piatto con limite 8,63e-9 mm e rifiuta il rilievo. Otto CTest mirati, 25/25 gruppi Project e 26/26 test con ASan/UBSan e leak detection. Build Linux completa. Un controllo aggiuntivo su nodi ripetuti e collocamenti composti combina scale 0,5 e 2, rotazioni, riflessioni, traslazioni, quote di entrambi i segni e UV separabili o oblique. Accetta 32 immagini analitiche e rifiuta le 32 con verso della quota errato. Il limite dichiarato massimo sui casi riusciti è 4,45e-12 mm.

### Pesi variabili, riproduttore ancora aperto

La stessa lamina diagonale mappata su un target con denominatore W(U)=1+0,5U perde il rilievo di 1e-4 mm. Il numeratore della prima coordinata è U+0,5U², quindi il controllo piatto conserva la stessa geometria. La variante con rilievo e pesi variabili viene ancora accettata con deviazione dichiarata di 6,71e-11 mm, mentre il target polinomiale con identico profilo viene rifiutato. Il checker è vuoto sulla variante accettata. Il riproduttore consente di verificare il futuro certificato omogeneo di posizione e normale con denominatore positivo.

### Verifica architetturale

Mappa finale verificata su 810 file, 11.866 relazioni e 413 unità senza diagnostica. Controllati la corrispondenza fra HTML e JSON, gli ingressi del certificatore da wrap e dai test e le chiamate in uscita verso la base e il collocamento B-spline.
