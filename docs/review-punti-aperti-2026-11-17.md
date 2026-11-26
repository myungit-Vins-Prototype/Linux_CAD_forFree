# Punti ancora aperti della review — 17 novembre 2026

## Stato al 26 novembre 2026

**K10.6.** Il Wrap riusa la certificazione delle curve UV dopo aver
verificato che l'assemblaggio conservi le stesse fin e gli stessi
operandi. [Misure e perimetro](#k106--riuso-della-prova-sulle-fin). Il
certificatore multifaccia salta le campate lontane quando Y = V e
usa un'origine locale. [Budget e misure](#k106--campate-attive-e-budget-del-wrap-multifaccia). Per i
pannelli complanari, la prova continua lavora sulle campate
dell'immagine. [Prova del piano comune](#k106--piano-comune-con-campate-attive).
La stessa via certifica appoggi che condividono una quota quadratica e
le immagini dense dei pannelli complanari. [Prove](#k106--griglia-densa-e-quota-quadratica-comune).
La prova dai poli copre ora anche le [carte affini](#k106--griglia-densa-con-carte-affini).
Nelle immagini con dominio più ampio, conta i soli
[poli attivi nella finestra UV](#k106--poli-attivi-nella-finestra-uv).
La [scansione lineare dei poli](#k106--scansione-lineare-delle-griglie-attive)
estende la prova alle finestre più fitte del budget adattivo.

**Raccordo e storyboard.** Il nuovo comando apre sul risultato attivo.
Il riordino ricalcola le dipendenze cambiate fino alla maniglia.
[Causa e prove](#raccordo-e-riordino-della-storyboard).

**K10.5.** Spessore verifica il residuo della pelle anche lungo
rette UV oblique e spline UV polinomiali o razionali fino al terzo
grado sui supporti compatibili.
[Prove e limiti](#spessore-lungo-tracce-uv-oblique-e-curve).

**K10.5.** Spessore verifica lungo i bordi isoparametrici supportati
la corrispondenza con la superficie originale spostata lungo la
normale. [Prove e limiti](#corrispondenza-dei-fori-con-la-pelle-a-distanza).

**K10.5.** Il controllo degli incroci usa l'inviluppo delle SP-curve
per escludere coppie. Rileva tangenze e attraversamenti fra i campioni
UV su spline quadratiche polinomiali e razionali.
[Prove e limiti](#tangenze-e-incroci-fra-campioni-uv).

**K10.4 e K10.5.** Il riporto vicino ai poli verifica lo scarto sul
ramo periodico restituito. Il controllo del B-rep rileva gli incroci
fra contorni in rami periodici distanti.
[Prove e limiti](#fasi-periodiche-estreme-e-incroci-dei-contorni).

**K10.4.** Il fit delle SP-curve distingue i punti regolari vicini a un
polo e recupera l'azimut limite dagli UV del ramo adiacente.
[Prove e limiti](#k104--fit-delle-sp-curve-vicino-ai-poli).

**Tavola 2D.** La freccia di aggiornamento accetta il modello cambiato con
schizzi associativi anche quando il confronto con un'impronta formato 53
non è applicabile. [Causa e verifica](#aggiornamento-tavola-2d-con-schizzi-associativi).

**K10.4.** L'inversione recupera un ramo sul lato della stima anche
quando Newton converge sul ramo opposto della B-spline piegata.
[Prove e limiti](#k104--ramo-vicino-dopo-newton).

**K10.4.** L'inversione conserva gli UV già esatti sui punti singolari.
[Prove e perimetro](#k104--continuità-degli-uv-nei-punti-singolari).

**Ripresa della review.** K10.3 resta a copertura parziale. Il riepilogo
individuava già le famiglie problematiche del Wrap multifaccia. I contatti
a `21/65` e `1/√3` sono riproduttori emersi durante le prove della famiglia
delle tangenze. La review passa a K10.4 e mantiene i casi residui di Wrap
nel registro. [K10.4 sui piccoli paralleli](#k104--minimo-sui-piccoli-paralleli).

**Ultimo checkpoint K10.3.** Il Wrap multifaccia riconosce la tangenza
a `21/65` sulla cubica e a `1/√3` sulla quartica riparametrizzata.
[Prove e limiti](#tangenze-razionali-e-irrazionali-nei-rientri)

**Avanzamento K10.3.** Verificati un contatto tangente con distanza
sestica, le giunzioni con restrizioni intervallari su domini affini,
le quote comuni sestiche e razionali di ordine superiore e le sorgenti
inclinate su tasselli curvi anche con giunzione a T.
[Prove e limiti](#copertura-k103-su-trim-e-appoggi-di-grado-superiore)


**Avanzamento K10.3 sul bordo curvo.** La prova della distanza quartica
riconosce il contatto al parametro `1/3` e rifiuta il caso che mantiene
lo stesso contatto ma scopre materiale attorno al punto.
[Prove e perimetro](#tangenza-curva-con-distanza-quartica)


**Avanzamento K10.3.** Verificati il contatto del disco al parametro
`1/3`, le giunzioni con un fattore omogeneo positivo variabile e
l'allineamento dei nodi tra domini parametrici affini.
[Prove e perimetro](#tangenza-a-13-fattori-omogenei-e-domini-affini)

**Avanzamento K10.3.** Verificati il disco tangente a un rientro
obliquo, i contatti spline al nodo e all'interno della campata e due
cambi di parametro non affini. [Prove e perimetro](#contatti-curvi-e-giunzioni-riparametrizzate)


**Aggiornamento K10.3.** Passano i nuovi riproduttori su contatto tangente
con sorgente poligonale convessa, estremi spline al nodo semplice e a
parametro normalizzato non binario, giunzioni equivalenti con un nodo
inserito o con elevazione di grado. [Criteri e limiti](#correzione-del-contatto-tangente-degli-estremi-e-delle-giunzioni)

1. **K10.3, Wrap multifaccia (copertura parziale).** Restano i contatti in cui il segno della
   distanza o la separazione dall'inviluppo spline sono inconcludenti entro
   il budget. Per estremi e giunzioni mancano le equivalenze senza prova
   esatta tra basi differenti. Sugli appoggi curvi rimangono le quote comuni
   oltre il sestico, i rapporti con numeratore oltre il quartico o denominatore
   oltre il cubico e le proiezioni generiche per cui la copertura o il residuo
   sono inconcludenti. [Copertura e casi residui](#copertura-k103-su-trim-e-appoggi-di-grado-superiore).
2. **Resto di K10.** K10.1 riguarda rappresentabilità e quadrature difficili.
   K10.2 riguarda il residuo all'interno dei carrier ancora controllati per
   campioni. K10.4 ha corretto la ricerca del minimo sui piccoli
   paralleli e la perdita degli UV già esatti nella singolarità.
   Recupera anche il ramo vicino dopo la proiezione globale o Newton
   nei casi verificati. Il fit vicino ai poli è corretto sui casi
   riprodotti e il riporto periodico controlla gli UV effettivi.
   Rimangono le ambiguità entro metà dominio.
   K10.5 rileva gli incroci fra contorni separati da più periodi e
   usa inviluppi UV conservativi per i tipi supportati. Rimangono le
   intersezioni fra curve generiche, le fasi dei contorni con più
   avvolgimenti. La corrispondenza dei bordi dei fori di Spessore
   ha una prova continua per rette e spline UV polinomiali sui
   carrier supportati. Restano i tracciati razionali e i supporti
   non coperti dal certificato.
   K10.6 elimina la doppia verifica delle fin NURBS, salta le campate V
   disgiunte quando Y = V e riusa una quota affine o quadratica comune.
   Il limite dai poli copre le immagini dense su appoggi complanari
   con carte affini o non lineari, anche oltre il budget adattivo.
   La prova è estesa ad appoggi con quote polinomiali distinte
   fino al quinto grado, impronte rettangolari che coprono la
   sorgente e raccordi con quota e gradiente continui. Restano le giunzioni e le griglie
   per cui le stime del residuo sono inconcludenti.
   [Griglia densa](#k106--scansione-lineare-delle-griglie-attive),
   [giunzioni quadratiche](#k106--quote-quadratiche-distinte-su-appoggi-adiacenti),
   [giunzioni cubiche](#k106--quote-cubiche-su-carte-razionali-adiacenti),
   [giunzioni quartiche](#k106--quote-quartiche-su-carte-razionali-adiacenti),
   [giunzioni quintiche](#k106--quote-quintiche-e-grado-effettivo).
3. **Pannelli, anteprime e temi.** Ricontrollare Taglia, Estendi, Pattern e le
   estrusioni associative per gli stati del picker, la forma mostrata e il
   riuso alla conferma. Il popup contestuale delle schede e quello di aggancio
   dei pannelli usano tuttora QMenu direttamente. Il confronto visivo
   chiaro/scuro è aperto. [Schede](../cad_document_tabs.cpp#L50), [aggancio](../forgeCad2026_dialogs_common.cpp#L434).
4. **Memoria e GPU.** Misurare apertura e chiusura ripetuta di più documenti,
   annullamento di preview, cache di B-rep e risorse GPU. Distinguere memoria
   trattenuta e perdite effettive. M1 e M2 hanno già correzioni mirate.
5. **Orfane e analisi statica.** Il wrapper `boundSweepSurfaceProjection`
   ricompare solo nella dichiarazione e nella definizione attuali. Le
   calotte chiamano `SweepSurfaceQuery::projection`. Prima della rimozione
   resta da verificare il contratto API. Continua la validazione manuale
   delle segnalazioni Cppcheck. [Triage](review-generale-2026-11-20.md#copertura-e-interpretazione-delle-prove).
6. **Modellazione libera e configurazioni.** Restano i limiti nelle
   famiglie generali di raccordo, sformo, guscio e contorno apparente. La
   verifica visiva nativa macOS e i carichi prolungati su backend GPU sono
   campagne distinte. I riscontri su Cesto e Supporto si riferiscono agli
   esemplari identificati nei rapporti. Windows è lavoro di porting.

### Aggiornamento tavola 2D con schizzi associativi

La freccia **Aggiorna dal .prt salvato** rilegge il modello e verifica
l'impronta della definizione. Se il modello è cambiato, il confronto
prova anche le impronte storiche. L'ultimo tentativo con il formato 53
propagava al comando il rifiuto dell'encoder per i riferimenti
associativi degli schizzi, disponibili dal formato 57.

L'impronta corrente rimane obbligatoria. Un tentativo storico
inapplicabile equivale a un'impronta diversa. Per la copia di lavoro,
l'aggiornamento può quindi ricalcolare la geometria della tavola e
salvare la nuova impronta. La verifica dell'UUID e dell'impronta di
uno snapshot archiviato continuano a essere applicate.

Il test apre una tavola con una quota da 10 mm, salva sul modello
temporaneo una dimensione di 14 mm e aggiunge un riferimento
associativo da uno schizzo all'altro. La stessa azione della freccia
riproduce il messaggio del formato 53 con il codice precedente.
Con la correzione misura 14 mm, permette di salvare e riaprire la
tavola e verifica che il file sorgente conservi i suoi byte.

[Confronto delle impronte](../cad_drawing_file.cpp),
[regressione della freccia](../tests/test_linked_drawing.h).

### Raccordo e riordino della storyboard

Dopo la modifica di un raccordo intermedio, la riga resta selezionata.
Il nuovo Raccordo o Smusso risolve quella riga al risultato attivo del
corpo alla maniglia. La modifica di una lavorazione esistente riapre
la sua base storica. La regressione verifica tre raccordi, la scena
all'apertura, il predecessore dell'anteprima e il riuso del B-rep alla conferma.

Il riordino ricollega le basi implicite, invalida gli stadi spostati
e propaga le variazioni degli operandi. In una prova con un B-rep
ancora marcato come riutilizzabile, il riordino precedente conservava
la geometria che includeva già il raccordo passato a valle. La prova
misura il volume dello stadio spostato, controlla il ricalcolo del
terzo raccordo, la conservazione di un corpo indipendente e l'Undo.
Con maniglia anticipata, i raccordi coinvolti dopo di essa si calcolano
quando l'utente la avanza. Release Linux ricompilata e 11 regressioni
mirate superate, oltre alla prova nel viewport Wayland. La prova
separata di conferma mentre l'anteprima è
ancora in corso riproduce un fallimento già presente nel codice precedente.

Il file del Desktop «Senza nome.prt» riproduce un secondo difetto con
raccordi adiacenti. Raccordo 3 da 0,5 mm era già collegato a Raccordo 4
da 0,3 mm. La propagazione tentava di raccordare anche la continuazione
tra i fianchi cilindrici più piccoli. Al fallimento la costruzione
ripartiva da Raccordo 2 e invertiva le due operazioni. Il kernel ora
arresta la propagazione automatica al collasso dell'offset cilindrico
e ritaglia sul corpo corrente il raccordo dello spigolo scelto.
Il volume passa da 43,783233 mm³ nell'ordine 2 → 3 → 4 a 43,792565 mm³
nell'ordine 2 → 4 → 3. Riaprendo il file precedente, la cache geometrica 20
permette di rigenerare solo Raccordo 3. Dopo un nuovo salvataggio e la
riapertura, portare solo Raccordo 4 a 0,25 mm cambia il volume finale in
43,852723 mm³. Il kernel verifica un unico solido valido in entrambi i casi.
Passano le nove regressioni mirate su raccordi, smussi, storia e cache.

La successiva verifica locale sullo stesso documento ha individuato una
faccia piana triangolare (F20) alla stazione Y = −5,501678 mm. Il controllo
di chiusura e il volume sopra riportati ammettevano quella linguetta. Il
bordo da 0,3 mm arriva a Y = −5,801678 mm. Per uno spigolo rettilineo
convesso tra piani il kernel ora individua il piano tangente comune ai
fianchi cilindrici terminali e rifila il nuovo raccordo contro di essi.
Controlla l'ingombro del solo tratto aggiunto rispetto alle altre facce.

Nell'ordine 2 → 4 → 3 rimangono 21 facce, con due intersezioni tra i
cilindri da 0,5 e 0,3 mm. Il volume è 43,787244 mm³. A Y = −5,52 mm
il punto (X = −1,04, Z = 0,12) passa dal materiale al vuoto e il punto
adiacente (X = −0,98, Z = 0,13) resta nel materiale. Riaprendo il file
del Desktop o la sua copia cache 20 si rigenera soltanto Raccordo 3.
La copia cache 21 riusa il risultato. Con Raccordo 4 a 0,25 mm il volume
diventa 43,847912 mm³. Entrambi hanno una sola shell, controlli geometrici
e incroci dei loop superati e zero facce non triangolate.
Il caso di riferimento 1476/B175 ha anche rivelato che il blocco della
propagazione a raggio uguale troncava un contorno di 46 spigoli. Se
l'arco tra cilindro e piano normale ha lo stesso raggio del raccordo,
il centro è fisso e la superficie generata è sferica. La catena ora
prosegue lungo questo arco. Il controllo del volume asportato con
raggi di 4 e 7 mm passa sul corpo di riferimento. La suite filtrata
`Blend` è stata rieseguita sulla build finale, con 63 test su 63 superati.

[Raccordo e Smusso](../forgeCad2026_viewport_selection.cpp),
[riordino](../forgeCad2026_viewport_document.cpp),
[regressione](../tests/test_fillet_selection_review.h).

### K10.6 — Riuso della prova sulle fin

In `mapBody`, ogni fin NURBS certificata veniva verificata una seconda
volta dopo l'assemblaggio, con lo stesso bordo 3D, la stessa superficie,
la stessa SP-curve, lo stesso intervallo e lo stesso budget. La
certificazione iniziale contribuisce già allo scarto massimo dichiarato.
Ora la pubblicazione verifica l'identità degli oggetti, le tolleranze,
l'intervallo, il verso, la genealogia e la cardinalità delle fin. Per le
fin planari rimane la verifica alla tolleranza effettiva del B-rep.

Misure Linux Release, mediane di cinque esecuzioni alternate per caso.
Le chiamate di verifica esterne al comando nei test sono incluse nei
tempi e nei conteggi totali. Il conteggio della colonna centrale indica
solo le chiamate eseguite dal Wrap.

| Appoggio | Certificazioni nel Wrap | Tempo nelle certificazioni del test | Tempo totale del test |
| --- | ---: | ---: | ---: |
| Cilindrico | 96 → 48 | 135,75 → 80,78 ms | 151,04 → 96,01 ms |
| Quadratico | 96 → 48 | 4,15 → 2,53 ms | 348,65 → 335,89 ms |
| Razionale | 16 → 8 | 2,25 → 1,37 ms | 9,67 → 8,68 ms |

Si sono confrontati il B-rep serializzato, tutte le origini della
genealogia e lo scarto in formato esadecimale. I sei file risultano
identici prima e dopo. Le verifiche indipendenti sui B-rep emessi
coprono 24, 24 e 4 fin. Il controllo che rifiuta la fin affine oltre
budget conserva l'esito. Release Linux ricompilata e 8 suite CTest
mirate superate, incluse le prove multifaccia.

[Codice del Wrap](../kernel/fk_surface_mapping.cpp),
[verifica delle fin emesse](../kernel/tests/test_mapping_nonplane_validation.cpp).

### K10.6 — Campate attive e budget del Wrap multifaccia

Il ramo non lineare addebitava tutte le campate V prima di filtrare
quelle lontane dalla proiezione. Per un appoggio con Y = V verificato,
una ricerca binaria limita ora l'iterazione alla proiezione della
sorgente. Le campate ai due lati di un nodo coincidente con il limite
rimangono incluse. Il residuo usa un polo della campata corrente come
origine comune. Nel caso di prova, il primo polo della superficie era
a circa 9 m dal contatto e allargava gli intervalli anche dopo avere
escluso le campate estranee.

Su due pannelli piani da 9.002 campate V ciascuno, il limite di 16.384
unità veniva esaurito anche se l'immagine bicubica rispettava il
budget geometrico di 0,00000002 mm. La sola selezione delle campate
attive esauriva ancora il lavoro. Con l'origine locale, l'immagine
piana viene certificata con 136 unità in entrambi gli ordini dei
pannelli, anche quando la proiezione si chiude sui nodi. L'immagine
traslata di due volte il budget viene rifiutata con un testimone
all'interno della campata. Il budget resta 16.384. La Release Linux
è stata ricompilata e passano 6 suite CTest di Wrap, Offset e Spessore.

Nelle due suite strumentate prima della correzione sono state eseguite
330 prove continue multifaccia, di cui 137 dal comando pubblico e le
altre direttamente dai test. Nessuna esauriva il budget. Fra le 137
chiamate del comando, 86 risultavano certificate, 36 arrivavano al
controllo campionato e 15 restituivano `Failed`. Di questi 15 esiti,
13 avevano un testimone oltre tolleranza e tutti attivavano un nuovo fit.
Questi conteggi riguardano le due suite, non la frequenza nei documenti
degli utenti.

Un caso pubblico aggiuntivo usa due pannelli razionali piani con tutti
gli intervalli V attivi. Con 60 campate per pannello sono state
addebitate 15.480 unità e l'immagine è certificata. Con 64 campate per
pannello si esauriscono tutte le 16.384 unità e il Wrap passa al controllo
campionato, che ammette il modello planare della prova. La nuova
verifica del piano comune risolve questo caso.

[Certificatore](../kernel/fk_surface_mapping_bounds.cpp),
[regressione con campate lontane](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Piano comune con campate attive

Quando gli appoggi hanno proiezione regolare e tutti i poli sono sullo
stesso piano di quota, il punto richiesto per una sorgente piana è
affine in coordinate fisiche. Il calcolo riusa il verificatore diretto
sulla griglia dell'immagine. La prova verifica almeno un contatto
interno certificato. Per rifiutare un residuo serve un testimone dentro
l'appoggio. La verifica separata della copertura continua a gestire i
vuoti e i rifili. Se il limite sull'immagine è inconcludente, il kernel
prosegue con il controllo delle campate degli appoggi e lo stesso
budget rimanente.

La regressione usa due pannelli razionali con 64 campate V attive
ciascuno e quota comune di 0,375 mm. Con offset 0, +0,25 e −0,25 mm,
prima la prova raggiungeva il limite di 16.384 unità e il comando
passava ai campioni. Ora il comando restituisce la certificazione
continua con 5 unità per caso. Le unità misurano il lavoro del
certificatore, oltre al quale rimane la validazione degli appoggi.
Su queste tre esecuzioni strumentate il tempo della prova continua
è passato da 36–53 ms a circa 2,2 ms. Un'immagine traslata di due volte
il budget viene rifiutata con un testimone, mentre lo spostamento
che porta tutta la sorgente fuori dagli appoggi resta senza certificato.

ForgeCAD Linux ricompilato. Passano la nuova regressione e 5 suite
CTest. Resta lo studio dei casi in cui appoggi curvi o una griglia
d'immagine molto fitta esauriscono il limite. Il chiamante distingue
ancora soltanto gli esiti di certificato, fallimento e non supportato.

[Verifica diretta](../kernel/fk_surface_mapping_bounds.cpp),
[prova delle 64 campate](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Griglia densa e quota quadratica comune

Sui pannelli complanari riconosciuti nel ramo non lineare, il punto
fisico richiesto dipende in modo affine dalle coordinate UV della
sorgente. Per un'immagine bicubica densa, i poli della mappa affine
nella stessa base sono le immagini delle ascisse di Greville. La
convessità della norma e la partizione dell'unità della base
permettono di limitare lo scarto con la massima differenza fra poli.
Le differenze sono calcolate con intervalli orientati verso
l'esterno. Un polo con scarto grande interrompe la prova dai poli e
lascia al percorso per campate la ricerca di un punto reale fuori
tolleranza.

Su appoggi curvi con sorgente piana, il passaggio diretto riusa
l'identità omogenea esatta di quota quadratica in coordinate fisiche.
La sostituzione viene verificata su tutte le campate di tutti gli
appoggi prima di controllare il risultato. La verifica richiede
ancora un contatto interno e la ricerca del residuo lavora sulla
superficie emessa. Le verifiche della copertura restano separate.

| Caso | Unità della prova prima | Unità della prova dopo |
| --- | ---: | ---: |
| Immagine ruotata a 128 × 64 campate, appoggi complanari | 16.378 | 8.781 |
| Due appoggi razionali con quota Y²/512, 64 campate per pannello | 16.384 | 5 |

I conteggi riguardano il residuo e vanno letti separatamente dalla
verifica delle carte e dell'identità di quota. In entrambi i casi è
stata verificata anche un'immagine con scarto doppio della tolleranza.
La prova curva verifica il comando Wrap su una lamina, il B-rep emesso
e lo scarto dichiarato. Le due regressioni falliscono sulla baseline.
ForgeCAD per Linux è stato ricompilato e 5 suite CTest sono passate.

Per le sole carte affini si applica anche la prova descritta
nel passo del 26 novembre. Restano le carte senza quota quadratica
comune e i casi senza prova dalla rete di controllo entro il budget.

[Certificatore](../kernel/fk_surface_mapping_bounds.cpp),
[regressioni](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Griglia densa con carte affini

Il percorso delle carte affini riusa la prova dai poli per una sorgente
piana rettangolare e appoggi complanari regolari su tutto il dominio
rifilato. Le coordinate fisiche X e Y vengono dalle carte già provate.
La prova richiede almeno un punto interno all'appoggio e un'immagine
che contenga l'intervallo da verificare. Se un coefficiente supera il
budget, il kernel prova il residuo sulla superficie effettiva in
quel punto. Il limite superiore su un coefficiente serve solo per la
certificazione e non per dichiarare uno scarto geometrico.

La regressione costruisce un'immagine bicubica con 128 × 64 campate.
Con sole carte affini, la baseline esauriva il budget per l'immagine
esatta, sia con offset normale di −0,25 mm sia a quota zero con
rotazione di 0,125 rad. Ciascuna delle due prove viene ora certificata
con 8.781 unità. Lo scarto doppio della tolleranza viene dimostrato
sulla superficie emessa con 6 unità. Le prove con una mappa non lineare verificano
gli stessi risultati nello stesso esercizio.

ForgeCAD Linux ricompilato. Passano la regressione e 5 suite CTest.
Il controllo rapido richiede appoggi planari con una quota comune.
Il passo successivo restringe il confronto ai poli attivi.
Restano gli appoggi curvi privi di una legge quadratica comune e le
finestre in cui i soli poli attivi esauriscono il lavoro.

[Certificatore](../kernel/fk_surface_mapping_bounds.cpp),
[regressione](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Poli attivi nella finestra UV

Il limite dai coefficienti del residuo ora considera solo le funzioni
base che sono attive nell'intervallo in U e in V. Una ricerca binaria
trova la prima campata a destra dell'estremo inferiore e l'ultima a
sinistra di quello superiore. La continuità posizionale della bicubica
validata consente di includere anche gli estremi della finestra.
Il costo viene stimato sul prodotto dei soli indici attivi.

Il riproduttore contiene 51.541 poli e 9.801 influenzano la finestra.
I poli esterni cambiano quota di 0,001 mm senza influenzare il tratto
verificato. La baseline esaurisce 16.384 unità. Il certificatore ora
completa la prova con 9.805 unità, comprese le verifiche del contatto.
La prova include nodi tripli ai due margini in V e controlla la quota
anche sugli estremi. Un rilievo sulla prima colonna di poli attivi
produce un residuo fuori tolleranza e viene rifiutato. Un confronto
aggiuntivo su 5.000 finestre a nodi semplici e ripetuti ha verificato
gli indici contro il criterio di intersezione dei supporti B-spline.

ForgeCAD Linux ricompilato. Passano la regressione e 5 suite CTest.
Il passo successivo gestisce anche la finestra attiva più fitta del budget adattivo.

[Limite dai poli](../kernel/fk_surface_mapping_bounds.cpp),
[regressione](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Scansione lineare delle griglie attive

Per appoggi piani a quota comune, i contributi in U e V vengono
precalcolati una volta per indice. Si scorre la rete di controllo
attiva accumulando il massimo scarto quadratico con intervalli
arrotondati verso l'esterno. Il limite finale usa una sola radice.
La scansione lineare usa il limite già previsto di 100.000 poli
dell'immagine. Il budget adattivo di 16.384 unità conta le ricerche
di contatto, le prove puntuali e le operazioni per campata.

La regressione ruota una sorgente piana e applica un offset di
−0,25 mm su due appoggi complanari. I 25.545 poli sono tutti attivi.
Prima il percorso per campate richiedeva più del budget e si fermava
dopo 16.375 unità. Ora la prova continua scorre
25.545 poli e usa 4 unità adattive per trovare
un contatto interno. Il controllo con uno scarto doppio della
tolleranza viene rifiutato sulla superficie. Un rilievo sull'ultimo
polo viene rifiutato dopo 7 unità adattive,
valutando lo scarto in un punto interno al suo supporto.

ForgeCAD Linux ricompilato. Passano la regressione e 5 suite CTest.
Restano fuori dal percorso diretto le griglie senza una prova dai
coefficienti e gli appoggi curvi privi di una quota quadratica comune.

[Scansione dei poli](../kernel/fk_surface_mapping_bounds.cpp),
[regressione](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Quote quadratiche distinte su appoggi adiacenti

Con sorgente piana, il percorso diretto riusa la prova dei tasselli
polinomiali anche quando una sola legge quadratica non descrive
tutti gli appoggi. Verifica le impronte rettangolari, la copertura,
la quota e il gradiente alle giunzioni. La proiezione affine delimita
la parte di ogni campata dell'immagine che può ricadere nella
rispettiva impronta. I box inconcludenti vengono suddivisi in locale.
La continuità verificata alla giunzione estende il limite ai bordi
comuni. Un tassello che tocca una campata soltanto sul bordo non
richiede suddivisioni. Il tentativo diretto ha un proprio budget. Se è inconcludente, il controllo per campate dei
carrier riparte con il budget ordinario.

La regressione usa due parabole con coefficienti diversi, tangenti
sul bordo comune, e carte razionali con 256 campate in V sui
pannelli interi. Verifica la giunzione semplice, quella a T e
l'inversione dell'ordine degli appoggi. Nelle quattro combinazioni
il percorso precedente esauriva il budget dopo 16.366–16.376
unità. La prova diretta ora certifica l'immagine in 18–28
unità e rifiuta la curvatura alterata tramite un punto interno in
16–25 unità. I conteggi si riferiscono al residuo e
alle verifiche delle impronte e delle giunzioni nel percorso diretto.

Su una variante con una sola campata per appoggio, il comando Wrap
produce una lamina con una faccia e B-rep valido. Il carrier generato
è certificato con limite superiore 1,36e−08 mm su un budget
di 2e−08 mm in 460 unità. Il risultato
riporta uno scarto complessivo di 4,95e−08 mm.

ForgeCAD Linux ricompilato. Passano la regressione e 5 suite CTest.
Il passo seguente estende la famiglia a [leggi di quota cubiche](#k106--quote-cubiche-su-carte-razionali-adiacenti).
Restano le giunzioni per cui il ritaglio rettangolare conservativo
è troppo largo.

[Certificatore](../kernel/fk_surface_mapping_bounds.cpp),
[regressione](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Quote cubiche su carte razionali adiacenti

Per sorgenti piane, la prova diretta ammette una legge cubica
comune oppure leggi cubiche distinte su appoggi adiacenti. La
ricostruzione della legge fisica ammette carte native cubiche
entro il limite esistente dei prodotti omogenei. La legge deve
superare la sostituzione esatta in ogni campata. Per le leggi
distinte si applica anche il controllo di copertura e di
continuità della quota e del gradiente alle giunzioni.

La regressione usa due appoggi con Z proporzionale a X³, con
coefficienti diversi e 256 campate in V per appoggio. La carta
razionale riparametrizza X. Provati entrambi gli ordini di
selezione. La versione precedente esaurisce il budget della
prova continua dopo 16.375–16.377 unità
nei due casi validi. Il percorso diretto certifica entrambi in
16 unità e dimostra lo scarto
della curvatura alterata in 16–17
unità. Questi conteggi si riferiscono al residuo e alla prova delle
impronte e delle giunzioni.

Con una campata per appoggio, il comando Wrap genera una lamina
con una faccia e B-rep valido. Il limite certificato sulla superficie
generata è di 1,2e−09 mm, entro il budget di
2e−08 mm. ForgeCAD Release Linux ricompilato
e 5 suite CTest del kernel superate.

Il passo seguente estende la prova alle [leggi quartiche](#k106--quote-quartiche-su-carte-razionali-adiacenti).
Restano le giunzioni per cui impronta, copertura o residuo sono inconcludenti.

[Certificatore](../kernel/fk_surface_mapping_bounds.cpp),
[regressione](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Quote quartiche su carte razionali adiacenti

Il certificatore per sorgenti piane ora prova anche una legge
fisica del quarto grado, comune o distinta fra gli appoggi. La
sostituzione esatta su ogni campata e il controllo di copertura e
continuità riusano le operazioni del caso cubico. Il limite dei
prodotti omogenei e il budget di suddivisione restano quelli
del kernel.

La regressione usa una quota proporzionale a X²Y², con coefficienti
diversi tra i due appoggi. Le carte razionali hanno grado 2 sui
due assi e 256 campate in V per appoggio. Il grado totale quattro
deriva dal prodotto delle due coordinate quadratiche. L'immagine
bicubica può quindi rappresentare la legge ovunque. Nei due ordini
di selezione, il percorso precedente esauriva il budget dopo
16.376–16.380 unità. Il percorso diretto
certifica entrambi in 22 unità e
rifiuta la variante fuori tolleranza in
20–21 unità.

Con una campata per appoggio, il comando Wrap genera una lamina
con una faccia e B-rep valido. Il certificatore verifica la superficie
generata con un limite superiore di 8,14e−09 mm, entro il
budget di 2e−08 mm, in 26 unità.
ForgeCAD Release Linux ricompilato. Passano 5 suite CTest del kernel.

Il passo seguente copre le [leggi quintiche](#k106--quote-quintiche-e-grado-effettivo).
Restano le giunzioni per cui il residuo o la copertura non sono dimostrabili.

[Certificatore](../kernel/fk_surface_mapping_bounds.cpp),
[regressione](../kernel/tests/test_mapping_global_validation.cpp).

### K10.6 — Quote quintiche e grado effettivo

Per una sorgente piana il percorso diretto riconosce anche
leggi di quota del quinto grado. Le coordinate omogenee
riducono il grado soltanto dove i coefficienti superiori sono
esattamente nulli. Il sistema include tutte le potenze presenti
sui due lati dell'identità. Il limite di grado 12 per asse nei
prodotti omogenei resta invariato.

La verifica di ogni campata costruisce i termini effettivamente
presenti nella legge ricavata. Prima venivano sviluppati anche
termini con coefficiente nullo. Su una griglia fitta, alcuni di
questi prodotti non erano rappresentabili esattamente in double
e bloccavano la prova prima che venissero moltiplicati per zero.

La regressione usa due appoggi adiacenti con quote proporzionali
a X³Y² e coefficienti diversi. Le carte hanno grado nativo 3 × 2,
256 campate in U e una riparametrizzazione razionale in V. Il
controllo esatto si ferma al budget con il codice precedente nei
due ordini, dopo 16.359–16.380 unità adattive.
Ora usa 24 unità nei casi validi e
trova lo scarto effettivo della variante errata in
22–23 unità adattive. Questi conteggi
riguardano il residuo e la giunzione dopo il riconoscimento delle
carte, a cui si aggiunge la verifica algebrica delle campate.

Con una campata per appoggio il comando Wrap genera una lamina
valida a faccia unica. Il limite di scarto certificato sulla
superficie emessa è 8,8e−10 mm, entro il budget di
2e−08 mm, con 20 unità adattive.
ForgeCAD Release Linux ricompilato. Passano 5 suite CTest del kernel.

Restano le quote di sesto grado su sorgente piana, le carte che
superano il limite dei prodotti effettivi e le giunzioni la cui
copertura o il cui scarto rimangono inconcludenti.

[Certificatore](../kernel/fk_surface_mapping_bounds.cpp),
[regressione](../kernel/tests/test_mapping_global_validation.cpp).

### Spessore lungo tracce UV oblique e curve

La prova precedente a cinque campioni accettava sia la retta
obliqua sia la spline cubica UV con uno scarto di `2e-6 mm`
fra i campioni, a fronte di un budget di `1e-6 mm`. La regressione
ripete il picco anche nei due segni dell'offset e conserva il
controllo dei bordi isoparametrici.

La pelle e il supporto originale sono composti con la stessa
traccia UV. Le derivate naturali del supporto forniscono la
normale. La stima proiettata conserva la correlazione fra le
coordinate e limita il residuo con prodotti Bernstein univariati
fino al grado 48. Il limite di grado dei prodotti del certificato
di Wrap resta a 20. La regressione accetta una retta rifilata e
una spline rifilata corrette sul bordo pur con uno scarto di
oltre `0,09 mm` fuori traccia.

Quando un intervallo incontra troppe campate, il controllo
suddivide il parametro del bordo e confronta soltanto le coppie
di celle i cui intervalli nativi si sovrappongono. Un offset
esatto su 300 campate, rifiutato dalla prima versione, passa
entro il budget di lavoro.

Per le SP-curve razionali con pesi positivi la verifica conserva il
denominatore durante la composizione e usa un limite inferiore per
riportare lo scarto nelle coordinate fisiche. Riprodotto lo stesso
picco nascosto con una SP-curva quadratica a pesi disuguali. Verificata
anche una riparametrizzazione razionale esatta con supporto biquadratico
e pelle bicubica. Restano i supporti a pesi variabili, le SP-curve oltre
il grado tre e i casi in cui la prova esaurisce il budget. Per le
famiglie non supportate rimane operativo il controllo campionato.

ForgeCAD Release Linux ricompilato. Per le tracce polinomiali
superate cinque suite CTest, inclusi Shower Head, il fondo curvo
con tre asole e l'asola con bordo cubico. Per l'estensione razionale
superate quattro suite e cinque casi mirati della suite Offset.
ASan, UBSan e rilevamento perdite superati sulle prove polinomiali
con i moduli offset e mapping strumentati. Mappa e collegamenti verificati.

[Certificato della traccia](../kernel/fk_surface_mapping_bounds.cpp),
[controllo dei fori](../kernel/fk_open_offset.cpp),
[regressioni](../kernel/tests/test_open_offset.cpp).

### Corrispondenza dei fori con la pelle a distanza

Al ripristino dei fori, Spessore confrontava il carrier rifilato
con la superficie originale spostata lungo la normale ai due
estremi e al punto medio di ogni bordo. Una deformazione locale
del carrier può risultare invisibile a quei parametri. La regressione
bicubica concentra uno scarto di circa `2e-6 mm` nel tratto
`[1,08; 1,20]`, con un budget di `1e-6 mm`. Anche i due campioni
ai quarti sono fuori dal supporto locale della deformazione. La
prova controlla entrambe le direzioni UV, i due segni dell'offset
e un caso entro il budget.

Per le rette UV isoparametriche, il kernel riusa il certificatore
a intervalli del mapping con mappa identità e quota pari allo
spessore orientato. Il rettangolo contiene l'intero bordo e
include una fascia di un ULP per lato lungo la coordinata fissa,
limitata ai domini dei supporti. Il budget geometrico è quello
già in uso. Una prova fallita su carrier supportato interrompe
la costruzione prima del fit del bordo. Le verifiche sui trim e
sul residuo bordo/carrier restano attive. Il
[checkpoint successivo](#spessore-lungo-tracce-uv-oblique-e-curve)
estende la prova alle rette oblique e alle spline polinomiali.

Nella prova sul fondo curvo con tre asole, tutti i dodici bordi
isoparametrici sono verificati e le tre aperture sono preservate.
La Cucitura 11 disponibile in `Cesto.prt` ha 44 facce e include
Raccordo 13. A 2 mm il controllo di curvatura della faccia 21
interrompe il calcolo prima del ripristino dei fori anche con il
kernel precedente alla modifica. È un caso distinto dalla
Cucitura 11 storica a 36 facce.

ForgeCAD Release Linux ricompilato e cinque suite CTest superate.
ASan, UBSan e rilevamento perdite sulle tre prove mirate con il
modulo offset strumentato. Mappa e collegamenti verificati.

[Controllo dei fori](../kernel/fk_open_offset.cpp),
[regressione](../kernel/tests/test_open_offset.cpp).

### Tangenze e incroci fra campioni UV

Nel controllo opzionale di `checkBody`, i box ricavati da 17
campioni più il margine potevano scartare due contorni prima
dell'intersezione. Il riproduttore usa un foro con un picco
quadratico il cui supporto parametrico è compreso fra
`0,5034` e `0,5040`. Le quote del picco sono 9, 10 e 11 mm
rispetto a un contorno esterno a 10 mm. L'intersezione diretta
distingue foro interno, tangenza e attraversamento in due punti.
Il vecchio `checkBody` accettava tutti e sei i casi nelle due
varianti, polinomiale e razionale.

Il filtro riusa l'inviluppo UV di `faceBox` per rette, coniche e
spline nel loro dominio, comprese le curve rifilate. Se una coppia
è priva di inviluppo conservativo, passa al solutore disponibile.
La tolleranza e la stima dei rami periodici restano quelle del
controllo precedente. Dopo la modifica, i due fori interni
passano e gli altri quattro casi ricevono `LoopsCross`. Per la
tangenza il solutore restituisce tre candidati numerici e il
controllo emette una sola segnalazione. Rimangono
da coprire i tipi senza solutore adatto e la ricerca esaustiva
delle fasi per contorni con più avvolgimenti.

Per Spessore, il certificato del rail controlla che questo
segua il carrier rifilato. La corrispondenza con la superficie
originale spostata lungo la normale è trattata nel
[checkpoint del 25 novembre](#corrispondenza-dei-fori-con-la-pelle-a-distanza).

ForgeCAD Release Linux ricompilato, otto suite CTest superate.
ASan, UBSan e controllo perdite passati sui due moduli modificati
e sulla regressione mirata. Mappa e collegamenti verificati.

[Controllo B-rep](../kernel/fk_body_check.cpp),
[inviluppo UV](../kernel/fk_classify.cpp),
[regressione](../kernel/tests/test_trimmed_face_bounds.cpp),
[bordi dei fori](../kernel/fk_open_offset.cpp).

### Fasi periodiche estreme e incroci dei contorni

**K10.4.** Su un parallelo regolare a circa `0,0005` mm dal polo di
una sfera unitaria, la proiezione iniziale ha scarto zero. Nel
riproduttore del nodo Hermite, il riporto a fasi di modulo
`1e12`, `1e16` e `1e20` radianti poteva accumulare uno scarto fino a
`4,58e-4` mm con tolleranza `1e-10` mm. Il nodo verifica ora
finitezza e scarto sul ramo effettivo. Le sei fasi fuori tolleranza
sono respinte e le otto fasi entro tolleranza sono accettate. La
prova delle fasi estreme inietta una stima nel nodo interno. La
regressione dell'API pubblica controlla invece che il fit conservi
256 giri del parallelo, in un intervallo positivo e in uno negativo,
con scarto entro `1e-10` mm su ogni giro verificato.

**K10.5.** Il controllo opzionale degli incroci fra contorni della
stessa faccia confrontava la SP-curva originale e le copie a un
periodo di distanza. Un foro che attraversava il contorno esterno
di un cilindro sfuggiva al controllo a cinque periodi. Il controllo
stima lo spostamento relativo dai box dei tratti UV, prova i rami
vicini a quello spostamento e conserva la ricerca preesistente.
La regressione usa cilindro e toro, con fori interni validi e fori
che attraversano il bordo, da zero a 64 periodi in entrambi i
versi. Sul toro varia contemporaneamente la fase di U e di V.
Questo checkpoint usava box campionati. Il
[checkpoint successivo](#tangenze-e-incroci-fra-campioni-uv)
sostituisce il filtro e mantiene da verificare i contorni con più
avvolgimenti.

ForgeCAD Release Linux ricompilato, sette suite CTest superate.
ASan, UBSan e controllo perdite superati sui moduli modificati e
sulle prove mirate. Mappa architetturale e collegamenti verificati
nelle quattro unità interessate.

[Fit Hermite](../kernel/fk_pcurve.cpp),
[verifica B-rep](../kernel/fk_body_check.cpp),
[regressione SP-curve](../kernel/tests/test_pcurve_deviation_validation.cpp),
[regressione contorni](../kernel/tests/test_trimmed_face_bounds.cpp).

### K10.4 — Fit delle SP-curve vicino ai poli

Una curva esatta della sfera raggiunge il polo con velocità nulla e
attraversa la giunzione U. Il fit precedente poteva rifiutarla. Nel fit
Hermite il controllo del polo usava l'intera tolleranza geometrica e
sostituiva V con la quota del polo anche in punti regolari. L'azimut
all'estremo, estrapolato da campioni più lontani, era inoltre spostato
di `0,125` radianti nel riproduttore con azimut variabile.

Il riconoscimento del polo nel fit Hermite ora si limita al margine di
arrotondamento, al massimo pari alla tolleranza richiesta. Agli estremi
singolari usa il valore e la derivata U del campione regolare adiacente
per stimare il limite. I campioni vicini ma regolari conservano i loro
UV. La regressione comprende distacco lineare o quadratico, percorrenza
in entrambi i sensi, estremi sul polo o leggermente discosti e rifiuto
di una curva fuori tolleranza.

La batteria sulle curve esatte passa da 85/120 a 120/120 successi.
Sulle 85 curve riuscite in entrambe le versioni i poli complessivi
scendono da 3.274 a 1.135. Sulle curve entro tolleranza ma leggermente
scostate dalla superficie il fit passa da 104/144 a 144/144. La misura
separata sui 276 riporti periodici registra uno scarto entro tolleranza
in tutti i campioni, con incremento massimo inferiore a `2e-19` mm.
Le fasi estreme sono trattate nel
[checkpoint successivo](#fasi-periodiche-estreme-e-incroci-dei-contorni).

ForgeCAD Release Linux ricompilato e sette suite del kernel superate.
ASan, UBSan e controllo perdite superati con il modulo SP-curve
strumentato. Mappa e chiamate delle due unità C++ verificate.

[Fit Hermite](../kernel/fk_pcurve.cpp),
[regressione](../kernel/tests/test_pcurve_deviation_validation.cpp).

### K10.4 — Ramo vicino dopo Newton

Sulla stessa B-spline piegata, a `V=0,4` l'arrotondamento lascia una
derivata U piccola ma non nulla. Partendo da `U=0,5`, per un punto a
`X=-0,249` Newton converge direttamente a `U≈-0,9996` con uno scarto
di circa `5,6e-17` mm. La correzione restituisce `U≈0,4740`, con uno
scarto di circa `7,9e-17` mm. Anche il caso `X=-0,24` recupera il ramo
nella metà positiva.

Il controllo esistente della metà che contiene la stima ora si applica
anche dopo la convergenza di Newton. Interviene per salti oltre metà
dominio su una coordinata non periodica con una stima nel dominio.
Adotta il ramo locale soltanto entro la stessa tolleranza fisica.
Su una B-spline piana con un'unica soluzione a `U=0,875` e stima a
`U=0,125`, conserva la soluzione lontana.

I due controlli aggiunti falliscono con il codice precedente. Dopo la
correzione passano i 12 test di inversione, inclusi quelli con Jacobiano
singolare già presenti. ForgeCAD Release Linux ricompilato, sei suite
pertinenti superate. ASan, UBSan e controllo perdite passano nella
suite di inversione con il proiettore strumentato. Mappa aggiornata
e chiamate delle due unità C++ verificate. La ricerca non certifica
l'inversione globale e resta limitata ai salti maggiori di metà dominio.
Il [checkpoint sulle SP-curve](#k104--fit-delle-sp-curve-vicino-ai-poli)
documenta i riporti periodici esercitati.

[Inversione](../kernel/fk_surface_algo.cpp),
[regressioni](../kernel/tests/test_invert_point_validation.cpp).

### K10.4 — Ramo vicino dopo la proiezione globale

Sulla B-spline cubica con `X=u³−3u/4` il Jacobiano è singolare
a `u=±0,5`. Per punti con più soluzioni, il proiettore globale
restituiva `u=±0,875` sul lato opposto alla stima anche se esisteva
un ramo valido sul lato iniziale.

Quando l'inversione usa la proiezione globale e il salto supera metà
del dominio non periodico, ripete il controllo nella metà che contiene
la stima. Adotta il risultato locale entro la tolleranza richiesta e
ricontrolla il residuo degli UV finali. I due versi della piega passano
con un budget di `1e-9` mm, mentre un punto senza soluzione resta
rifiutato. Il caso quasi singolare con convergenza diretta sul ramo
lontano è trattato nel [checkpoint successivo](#k104--ramo-vicino-dopo-newton).
Il riporto `nearPole` nelle SP-curve resta da verificare.

ForgeCAD Release Linux ricompilato e otto CTest pertinenti superati,
compresi tavole collegate, revisioni, fogli multipli e inversione UV.
Mappa e collegamenti verificati sulle quattro unità coinvolte.

[Inversione](../kernel/fk_surface_algo.cpp),
[regressione della piega](../kernel/tests/test_invert_point_validation.cpp).

### K10.4 — Continuità degli UV nei punti singolari

Il ripiego alla proiezione globale poteva cambiare le coordinate di una
stima con residuo già nullo. Al vertice di un cono la stima
`(1,234; 0)` diventava `(0; 0)`, con lo stesso punto restituito. La
regressione riproduce 13 salti su cono, toro a corno, toro fusiforme,
superficie di rivoluzione e lato collassato di una B-spline. Le superfici
periodiche sono provate anche con stime traslate di più giri.

Se il Jacobiano non consente il passo di Newton, l'inversione conserva gli
UV finiti quando le coordinate non periodiche sono nel dominio e lo
scarto è zero su tutte le componenti. Seguono il riporto periodico e il
controllo finale del residuo. Il controllo con un punto a distanza di
`1e-9` mm dall'apice verifica che l'inversione trovi ancora l'azimut
determinato, anche quando la stima iniziale è già entro tolleranza.
Una stima fuori dal dominio di una B-spline viene riportata sul dominio.

ForgeCAD Release Linux ricompilato, sei suite CTest del kernel superate.
La suite d'inversione passa 10 test, inclusi ASan, UBSan e controllo
perdite sul proiettore strumentato. Le due unità C++ modificate sono
state rianalizzate e i collegamenti della mappa verificati. La selezione
fra intersezioni distinte e il riporto `nearPole` nel fit delle
SP-curve restano da verificare.

[Inversione](../kernel/fk_surface_algo.cpp),
[regressioni](../kernel/tests/test_invert_point_validation.cpp).

### K10.4 — Minimo sui piccoli paralleli

Il proiettore con dominio UV limitato poteva perdere il minimo su un
parallelo troppo piccolo per essere rappresentato come `Circle`. Il
ripiego campionava il bordo in tre punti e restituiva il primo se le
distanze fra i punti erano sotto la risoluzione. Nel riproduttore sul cono
lo scarto saliva da `2e-8` a `2,77e-8` mm e l'angolo passava da `0,8` a
`0,2` radianti. Il caso riguarda il contratto del minimo di `projectPoint`.

Su cono, sfera, toro e superficie di rivoluzione, la prima e la seconda
derivata angolare ricostruiscono le direzioni radiali del parallelo. Il
proiettore valuta il minimo analitico nell'intervallo U e confronta
entrambi gli estremi, mantenendo V sul bordo. Il ramo a raggio firmato
negativo e l'apice di raggio zero sono coperti dalle prove. Verificate
finestre a quota fissa e con intervallo V non nullo, anche con U traslato
di un giro. Il controllo include la scelta dell'estremo quando l'angolo
del minimo non rientra nel ritaglio.

La regressione pubblica fallisce con la baseline e passa sul modulo
corretto. ForgeCAD Release Linux ricompilato e sette suite CTest del
kernel superate. ASan, UBSan e controllo perdite passano per gli otto
test di inversione con il proiettore strumentato. La mappa è stata
aggiornata analizzando le due unità modificate e riproducendo prima i
loro collegamenti precedenti. Restano da isolare le inversioni su più
rami e il riporto manuale `nearPole` nel fit delle SP-curve.

[Proiettore](../kernel/fk_surface_algo.cpp),
[regressione](../kernel/tests/test_invert_point_validation.cpp).

### Ripresa di K10.3 sul contatto tangente

Nel kernel Release corrente una lamina quadrata attraversa la giunzione
fra due appoggi piani con contorni razionali. Il fondo del rientro tocca
il lato destro della lamina in `(-1/8, 3/4)` mm. Il controllo continuo dà
`Inconclusive` con entrambi gli ordini degli appoggi, mentre il wrap
pubblico produce una faccia con zero rilievi al checker. Arretrando la
lamina di `2^-10` mm il controllo continuo dà `Covered`. Avanzandola, i
controlli di ripiego rifiutano tutte le distanze provate da `2^-10` fino
a `2^-24` mm. A `2^-25` mm il wrap riesce. In quest'ultimo caso il
semispessore della zona scoperta è inferiore alla precisione locale
di `2e-8` mm. Il risultato lascia aperta la prova del contatto tangente.

Per riprodurre, usare la costruzione di `checkContinuousReentrantSplineSeam`
con il polo della giunzione a X = `-5/8`. Sull'appoggio sinistro porre
il polo del rientro a `(-3/8, 3/4)` con peso `1/2`. Il quadrato è centrato
in `(-7/16, 9/16)` e ha semilato `5/16`. Ponendo `s=t(1-t)`, la X del
rientro è `(-3/8)s/(1-s)` e assume il minimo a `s=1/4`. Verificata con
frazioni esatte la posizione del testimone di scopertura a metà dello
spostamento sul lato destro. Passano i tre CTest Release
`forgekernel_surface_mapping_tests`, `forgekernel_mapping_global_validation_tests`
e `forgekernel_quadrature_validation_tests`.
[Casistica di partenza](../kernel/tests/test_surface_mapping.cpp#L996), [chiamante del controllo](../kernel/fk_surface_mapping.cpp#L775).

### Correzione del contatto tangente, degli estremi e delle giunzioni

- **Contatto tangente.** Per una sorgente poligonale convessa il certificato
  prova che il contorno residuo non entra nell’interno. I coefficienti
  Bernstein del numeratore del semipiano possono essere esattamente nulli.
  L’avvolgimento positivo verificato su un punto interno vale su tutto
  l’interno e la chiusura delle facce assicura la copertura del contorno.
  La lamina tangente è certificata in entrambi gli ordini. La lamina spostata
  di `2^-24` mm rimane fuori dalla copertura provata.
- **Estremi spline.** Le espansioni calcolano un prodotto comune dei
  denominatori a ogni livello de Boor. Si prova l’uguaglianza esatta con
  le coordinate cartesiane dell’estremo. Le regressioni verificano la
  cubica razionale al nodo semplice e il parametro normalizzato `1/3`.
  Il confronto separato a frazioni esatte su 1.440 ingressi conferma tutte
  le 430 coppie cartesiane pubblicate dal predicato. In 559 ingressi il
  risultato esatto era rappresentabile in binary64.
- **Giunzioni.** L’estrazione allinea le campate in un dominio parametrico
  comune. Il confronto omogeneo ammette elevazione di grado e pesi
  proporzionali quando i coefficienti coincidono esattamente. Passano la
  coppia con un nodo semplice inserito e la coppia quadratica/cubica, in
  entrambi gli ordini. Spostando un polo si conserva la fessura.
  Rimangono inconcludenti le parametrizzazioni generiche senza corrispondenza
  dimostrata e le estrazioni i cui coefficienti restano solo intervallari,
  salvo il caso già provato a dati nativi identici.

Con i due moduli precedenti collegati al resto della stessa libreria
Release, falliscono i quattro gruppi nuovi e passa il controllo sulla
campata satura già supportata. Con le modifiche passano tutti e cinque
i gruppi e gli otto CTest mirati. La Release Linux di ForgeCAD è
ricompilata. ASan, UBSan e controllo delle perdite passano sui due
moduli modificati e sul runner dei cinque riproduttori, collegati
al resto della libreria Release. Una seconda verifica con frazioni
conferma la coincidenza omogenea di entrambe le giunzioni e il punto
scoperto nei due controlli negativi. La mappa aggiornata contiene
837 file e 12.389 relazioni, con 424 unità analizzate. Verificate
le chiamate del wrap e del certificatore nelle posizioni effettive.

[Predicato](../kernel/fk_predicates.cpp), [certificatore](../kernel/fk_mapping_trim_witness.cpp),
[regressioni](../kernel/tests/test_surface_mapping.cpp).

### Contatti curvi e giunzioni riparametrizzate

- **Disco.** Il certificatore limita il segno della distanza quadratica
  del contorno residuo dal centro del disco in coordinate omogenee.
  Moltiplica i Bernstein senza dividere per coefficienti binomiali,
  preservando gli zeri esatti. Il rientro tangente obliquo è coperto
  in entrambi gli ordini. L'aumento del raggio lascia un punto
  scoperto e la copertura rimane inconcludente.
- **Sorgente spline.** I semipiani certificati sui poli ammettono il
  contatto sul bordo dell'inviluppo. Se l'inviluppo originale supera la
  tangente, il kernel prova quello delle due metà Bézier dopo una
  suddivisione, con divisioni esatte quando rappresentabili. Passano il
  contatto al nodo e quello all'interno della campata. Lo spostamento
  della sorgente di `2^-12` mm lascia almeno un punto scoperto in ciascun
  controllo negativo. Il wrap pubblico restituisce una lamina valida
  in tutti e tre i casi positivi di contatto curvo.
- **Giunzioni.** Per i cambi proiettivi, i poli corrispondono e i
  rapporti fra pesi seguono una progressione geometrica positiva.
  Per un bordo con una coordinata affine, il kernel ricava il parametro
  dalla stessa coordinata monotona dell'altro bordo e prova l'identità
  della seconda coordinata dopo la composizione. I casi verificati
  usano `u = 2t/(1+t)` e `u = (t+t²)/2`, entrambi con fessure
  dimostrate nei controlli negativi. Il limite del confronto composto è
  il grado 20. Rimangono aperte le parametrizzazioni che non ammettono
  queste prove e i contatti oltre il budget o con segno inconcludente.

I cinque riproduttori passano con il certificatore aggiornato. Con quello
precedente collegato alla stessa libreria, tutti e cinque falliscono al
controllo di copertura. Il riscontro indipendente a frazioni esatte conferma
le coordinate dei contatti, i segni sul disco dopo la suddivisione,
l'inviluppo spline più stretto e le identità delle due riparametrizzazioni
nei rispettivi coefficienti.
La Release Linux di ForgeCAD è ricompilata. Passano otto CTest mirati
e ASan, UBSan e il controllo delle perdite sui cinque riproduttori
collegati al certificatore strumentato e al resto della libreria Release.
Mappa aggiornata e verificata su 837 file, 12.389 relazioni e 424 unità,
con riscontro delle chiamate dal wrap e delle quattro nuove prove.

[Certificatore](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp).

### Tangenza a 1/3, fattori omogenei e domini affini

- **Disco contro parete rettilinea.** Quando il polinomio della
  distanza quadratica omogenea ha grado due, il kernel limita il
  discriminante usando i limiti inferiori dei tre coefficienti.
  La prova ammette uno zero interno senza cercarlo con bisezioni.
  Il disco tangente alla parete obliqua al parametro `1/3` è
  coperto in entrambi gli ordini. Allargandolo si trova un punto
  scoperto e la copertura rimane inconcludente. Il wrap del caso
  positivo restituisce una lamina valida.
- **Rapporti omogenei.** Dopo avere orientato due campate Bézier
  sul parametro unitario, il certificatore confronta nel continuo
  i prodotti incrociati `X₁W₂ = X₂W₁` e `Y₁W₂ = Y₂W₁`.
  Passa la giunzione fra la quadratica razionale e la cubica
  ottenuta moltiplicando i coefficienti omogenei per `1+2t`
  a meno di una scala costante. La fessura con un polo spostato
  non viene certificata.
- **Domini affini.** Quando gli estremi nativi proiettati
  coincidono, i nodi candidati sono trasferiti esattamente fra
  gli intervalli. Le singole campate si eliminano dal perimetro
  solo dopo la prova di coincidenza. È verificato il nodo
  inserito in `4` su `[2,6]` in corrispondenza di `1/2` su
  `[0,1]`, con controllo negativo a polo perturbato.

I tre riproduttori passano nel kernel aggiornato e falliscono con
il certificatore precedente collegato alla stessa libreria. Il riscontro
a frazioni verifica il discriminante nullo, i coefficienti del fattore
comune e la corrispondenza dei nodi. Restano aperte le tangenze con
polinomi di grado superiore il cui segno è inconcludente, le estrazioni
con soli intervalli quando i dati nativi differiscono e i domini
senza una corrispondenza esatta dei nodi.

Release Linux ricompilata, otto CTest del kernel superati. I tre
riproduttori passano con ASan, UBSan e controllo perdite sul
certificatore strumentato. Mappa aggiornata e verificata con 837 file,
12.389 relazioni e riscontro delle chiamate nei sorgenti.

[Certificatore](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp).

### Tangenza curva con distanza quartica

Il disco tocca la parete parabolica al parametro locale `1/3`.
La curva attraversa il semipiano tangente al disco pur restando
sul suo esterno. Per i coefficienti Bernstein scalati della
distanza, la sostituzione `z=t/(1-t)` porta a un polinomio quartico
su `z≥0`. Il kernel parte dai limiti inferiori dei coefficienti,
riduce un fattore comune con la derivata tramite pseudodivisione
intera e certifica la divisione esatta per il quadrato del fattore.
La prova pubblica la copertura solo se il quoziente è non negativo
nel continuo. Le operazioni intere sono limitate in ampiezza e
la prova viene tentata una volta per campata prima della suddivisione.

Nella regressione la distanza contiene il fattore `(2z−1)²`
e ha quoziente positivo. Il wrap certifica il disco in entrambi gli
ordini e restituisce una lamina valida. Aumentando il raggio si
lascia materiale scoperto. Aumentando invece la curvatura della
parete si conserva lo stesso zero doppio, con quoziente negativo
e un punto scoperto verificato all'interno del disco. Anche questo
caso rimane fuori dalla copertura certificata.

Il riproduttore del contatto positivo fallisce col certificatore
precedente e passa con quello aggiornato. Il controllo indipendente
su 4.096 quartiche con fattori costruiti esattamente certifica
1.943 delle 2.167 non negative. Le altre 224 rimangono inconcludenti.
Tutti i 1.929 quozienti negativi e tutte le 4.096 perturbazioni
negative nel punto di contatto rimangono esclusi. Restano aperte
le quartiche che superano i limiti interi e le distanze di grado
superiore senza prova di segno mediante suddivisione.

Release Linux ricompilata, otto CTest del kernel superati. Il
riproduttore geometrico e la batteria sui coefficienti passano con
ASan, UBSan e controllo perdite sul certificatore strumentato.
Mappa e chiamate verificate su 837 file e 12.389 relazioni.

[Certificatore](../kernel/fk_mapping_trim_witness.cpp), [regressioni](../kernel/tests/test_surface_mapping.cpp).

### Tangenze razionali e irrazionali nei rientri

Una lamina circolare attraversa la giunzione fra gli appoggi e tocca
un bordo del rientro in due riproduttori distinti. Con il certificatore
precedente restano inconcludenti una distanza al quadrato di grado sei
con contatto a `21/65` e una di grado otto con contatto a `1/√3`, ottenuta
con il cambio di parametro `s=t²` di una parete quadratica.

Il polinomio limite inferiore con coefficienti diadici viene ridotto
prima di applicare il limite intero. Per le sestiche i convergenti dei
punti stazionari propongono fattori razionali. Per il grado otto si
possono ricostruire fattori lineari e quadratici dal massimo comune
divisore della distanza e della sua derivata modulo un primo. Il massimo
comune divisore in interi può verificare anche un fattore cubico. Ogni
percorso richiede la divisione esatta per il quadrato del fattore e la
prova del segno del quoziente.

I due contatti passano in entrambi gli ordini degli appoggi e il comando
Wrap genera lamine con topologia valida. Due varianti conservano lo stesso
punto di contatto e lasciano una zona del disco scoperta. Entrambe
rimangono escluse dalla prova di copertura. Restano inconcludenti i casi
che eccedono il budget o per cui divisione o segno del quoziente restano
senza prova. Il percorso tratta le distanze al quadrato fino al grado otto.

ForgeCAD Release Linux ricompilato e otto suite CTest mirate superate.
ASan, UBSan e controllo perdite passano sul certificatore strumentato
con le regressioni geometriche. La batteria algebrica certifica 512/512
sestiche e 512/512 polinomi di grado otto non negativi. Rimangono esclusi
tutti i 2.048 casi negativi, con un quoziente negativo o un coefficiente
ridotto di un ULP sul contatto. Mappa e chiamate verificate su 837 file
e 12.389 relazioni.

Con il solo massimo comune divisore intero passavano 10 dei 512 casi
di grado otto. Il confronto con interi senza limite ha identificato
500 resti intermedi oltre il limite e due fattori cubici. La
ricostruzione modulare recupera i primi 500 casi e la divisione per
il quadrato di un fattore cubico recupera gli altri due. La regressione
irrazionale fallisce anche con la variante priva di ricostruzione modulare.

[Certificatore](../kernel/fk_mapping_trim_witness.cpp),
[regressioni](../kernel/tests/test_surface_mapping.cpp).

### Copertura K10.3 su trim e appoggi di grado superiore

| Filone | Caso verificato | Limite residuo |
| --- | --- | --- |
| Contatto tangente e rientri | Un disco attraversa due appoggi con un rientro obliquo. Il tratto cubico è tangente all'interno della campata e la distanza quadratica dal centro ha grado sei. Il fattore doppio e il segno del quoziente sono verificati con aritmetica intera. Entrambi gli ordini degli appoggi passano. Un disco più grande che scopre materiale resta escluso dalla prova di copertura. Il wrap del disco tangente produce una topologia valida. | La ricerca limitata dei fattori razionali e il segno del quoziente possono restare inconcludenti. Rimangono i rientri fuori dal budget di separazione. |
| Estremi e giunzioni spline | Le restrizioni omogenee intervallari coincidono quando poli, pesi proporzionali, nodi e intervalli locali corrispondono esattamente anche in domini affini diversi. Passano il bordo intero e quello parziale. Un'estremità modificata di un ULP e gli intervalli con un vuoto non sono certificati. | Restano i casi con basi o estremi diversi per cui le verifiche esatte già disponibili non dimostrano la coincidenza. |
| Appoggi curvi sovrapposti | Quota polinomiale comune sestica con carte diverse. Verificati anche un rapporto con numeratore quartico e denominatore quadratico, e un rapporto con denominatore cubico su una carta razionale cubica. Per i sistemi più grandi una stima può proporre i coefficienti. L'accettazione richiede la sostituzione esatta su ogni campata delle due carte e un denominatore strettamente positivo. Le prove perturbano un appoggio per verificare il rifiuto. | Restano le quote oltre il sestico, i rapporti di grado superiore o senza una prova del denominatore e le proiezioni che eccedono il limite algebrico. |
| Sorgenti inclinate su appoggi curvi | Verificate una sorgente quartica e una cubica senza contrazione dimostrata su grafici cubici con curvature distinte, anche in una giunzione a T. Le giunzioni richiedono uguaglianza esatta di quota e gradiente e si prova la copertura dell'impronta rettangolare della sorgente. Verificata anche una sorgente di quinto grado su due appoggi quartici con proiezione accoppiata. Le prove coprono il rilievo perso, fessure, pieghe e appoggi perturbati. | Per quote distinte rimangono le impronte non rettangolari, le proiezioni non riconducibili ai grafici verificati e le composizioni oltre il limite Bernstein esistente. |

Gli otto nuovi casi riproducono il limite del certificatore con i due
moduli precedenti e passano con quelli aggiornati. ForgeCAD Release
Linux ricompilato e otto suite CTest mirate superate. La suite dei trim
e i sei nuovi casi del residuo passano con ASan, UBSan e controllo delle
perdite sui due moduli strumentati. Mappa e chiamate dal comando Wrap
verificate su 837 file e 12.389 relazioni.

[Certificatore dei trim](../kernel/fk_mapping_trim_witness.cpp),
[certificatore del residuo](../kernel/fk_surface_mapping_bounds.cpp),
[prove sui trim](../kernel/tests/test_surface_mapping.cpp),
[prove sugli appoggi](../kernel/tests/test_mapping_global_validation.cpp).

**21 novembre.** Completate le sei priorità di semplificazione, comprese le pareti spline e la cache di analisi. Chiusi anche i rilievi sui parametri privati dell’offset e sul timestamp concorrente STEP/IGES. La copertura geometrica K10 rimane quella descritta nei filoni sotto. [Ultimo intervento e verifiche](review-generale-2026-11-20.md#pareti-spline-cache-di-analisi-offset-ed-export-concorrente-21-novembre)

**Aggiornamento del 20 novembre.** La [review trasversale](review-generale-2026-11-20.md) documenta la correzione dello scambio STEP/IGES sul cubo traslato e delle cavità STEP. Verificato anche l'orientamento delle shell disgiunte e annidate in Cucitura. La review documenta i casi del wrap a pesi variabili, a gradi misti, periodico e su estrusione a profilo aperto o chiuso. Riporta anche le correzioni a Undo delle tavole e runner, la semplificazione del solver e le nuove priorità per ridurre duplicazioni e costi. Ricontrollate anche le versioni correnti di Supporto e Cesto, con 13 e 12 B-rep rigenerati identici all'apertura con cache. Il conteggio storico dei 35 rilievi sotto si riferisce al precedente checkpoint.

Inventario consolidato su richiesta dell’utente, dopo le correzioni numeriche K10 e la segnalazione del sottomenu della cache. I rapporti precedenti conservano fotografie storiche: un punto descritto come aperto il 16 novembre può essere già risolto nel seguito. Questo elenco distingue lavoro tecnico residuo, lacune di verifica e miglioramenti proposti; non assegna nuovi ID di bug senza riproduzione.

Il conteggio passa a **35 rilievi funzionali corretti**. Ai 34 del 17 novembre si aggiunge PRE9, lo stato della scelta nella vista che restava attivo nel pannello Proiezione dopo un cambio di modalità. K10 è un punto ampio ancora parziale: i 24 siti iniziali hanno ricevuto interventi almeno parziali, ma questo non significa 24 percorsi integralmente certificati. I successivi casi K10 non sono conteggiati come altrettanti nuovi rilievi indipendenti.

## K10 — sei filoni ancora aperti

| Ordine di lavoro proposto | Punto | Stato effettivo e criterio per chiuderlo |
| --- | --- | --- |
| 1 | Quadratura: criteri residui e rappresentabilità | Overflow e prodotti pesati subnormali/azzerati recuperati e verificati su scalare, vettore, B-spline nativa e cache; le scale miste ancora non risanabili sono rifiutate. Anche la profondità massima ora segnala il fallimento se i criteri non sono soddisfatti: corretto il falso risultato su un segmento razionale di1mm. Restano arrotondamenti della normalizzazione, underflow finale non rappresentabile, criterio legacy del rumore e supporto delle parametrizzazioni difficili. I due Shower Head ora passano. La stima per orientare la cucitura usa trim verificati lungo tutto il bordo entro il suo budget. [Prove](review-cesto-spessore-2026-11-18.md#correzione-shower-head-in-produzione). La quadratura resta una stima numerica. [Nuove prove](review-k10-underflow-2026-11-17.md). |
| 2 | Superficie costruita rispetto all’intera mappa richiesta | Il controllo continuo dei bordi non dimostra che l’interno della superficie rispetti la trasformazione voluta. Il fit controlla ancora sonde finite. Riprodotto pubblicamente un rilievo NURBS C2 interno perso nel wrap di un solido: residuo1e-4mm con tolleranza1e-6. 18/11: limite continuo per Plane a quota affine, anche se tocca o attraversa zero, UV separabili e target polinomiali nativi di grado 2/3. Box inclinato e lamina con rilievo perso rifiutati, controlli piatti riusciti. Il rango uno con una UV costante e una affine lungo un solo asse è verificato. La lamina verticale piatta passa e il wrap che perde il rilievo è rifiutato. Il caso diagonale con entrambe le UV attive sullo stesso asse ora rifiuta il wrap che perde il rilievo di 1e-4 mm e accetta il target piatto. Il controllo copre ora UV affini oblique con determinante di segno incerto o zero. Per il target polinomiale istanziato legge la base nativa e applica il collocamento in intervalli. Il controllo omogeneo per pesi variabili positivi rifiuta il rilievo perso di 1e-4 mm e accetta il piatto anche su mappe oblique. Verificati anche i gradi misti 1×3, 4×3 e 5×3. Per i target B-spline il limite dei polinomi composti può ancora causare rifiuti conservativi. [Prove](review-k10-quota-zero-toro-2026-11-18.md). |
| 3 | Famiglie geometriche ancora senza controllo continuo | Nel wrap di facce piane il controllo comprende anche i target B-spline periodici e di grado misto, entro i limiti di calcolo. Restano da esaminare i target analitici e procedurali, le sorgenti non piane e le immagini costruite diverse dalla bicubica polinomiale. Corretto il rilievo perso di 0,0001 mm su estrusione aperta con generatrice B-spline, anche ritagliata. Corretto anche il caso a profilo chiuso che attraversa la giunzione, compresi i ritagli con un periodo proprio. Coperte anche le generatrici B-spline trasformate, con ritagli e collocamenti annidati. Verificata la deviazione di cilindri ed estrusioni di cerchi ed ellissi, inclusi gli archi e le facce generate da Estrusione. Verificate anche riflessioni, scale non uniformi e trasformazioni annidate delle generatrici circolari ed ellittiche. Coperte le rivoluzioni con meridiano rettilineo, conico o B-spline e le forme canoniche. Nel multifaccia sono corretti il riferimento UV obliquo e il passaggio di un bordo sopra una fessura proiettata. Verificati fori interni piani, coperture parziali e bordi rientranti in fessure separabili. Per più grafici B-spline cartesiani è verificato il residuo continuo dove esiste il contatto, anche con proiezione affine obliqua o di rango incerto. [Prova obliqua con nodi disuguali](review-generale-2026-11-20.md#k103--proiezione-obliqua-fra-appoggi-curvi-21-novembre). Restano le carte di proiezione generiche e i limiti della copertura dei trim. Il seguito dettaglia i test sulle aperture degli appoggi curvi. [Residuo fra due appoggi curvi](review-generale-2026-11-20.md#k103--residuo-fra-appoggi-curvi). [Prove su fori e bordi rientranti](review-generale-2026-11-20.md#k103--fori-interni-e-bordi-rientranti-nel-wrap-multifaccia). [Prove e perimetro](review-generale-2026-11-20.md#k103--rivoluzioni-e-wrap-su-più-facce). [Misure sulle coniche trasformate](review-generale-2026-11-20.md#k10--riflessione-e-scala-non-uniforme-delle-coniche-estruse). [Prove sulle coniche](review-generale-2026-11-20.md#k10--estrusioni-di-cerchi-ed-ellissi). [Prove sulle trasformazioni](review-generale-2026-11-20.md#k10--estrusione-con-generatrice-trasformata). [Prove sulla giunzione](review-generale-2026-11-20.md#k10--giunzione-dei-profili-estrusi-chiusi). [Prova](review-generale-prove-2026-11-20/estrusione-wrap.cpp.txt). Identificare per ogni chiamante il comportamento `Unsupported`, aggiungere provider mirati o un rifiuto esplicito quando necessario. |
| 4 | Newton, inversioni e scelta del ramo | Controllare il residuo finale non dimostra convergenza globale o scelta del ramo desiderato. Il ramo interno del toro fusiforme è ora incluso nella proiezione e nel recupero dopo Newton. Corretti anche il minimo sui paralleli sotto risoluzione e la perdita degli UV esatti nei punti singolari. [Prove sui paralleli](#k104--minimo-sui-piccoli-paralleli), [prove sugli UV](#k104--continuità-degli-uv-nei-punti-singolari). Verificato il riporto nei casi di [fit polare](#k104--fit-delle-sp-curve-vicino-ai-poli). Restano le fasi estreme. Le guardie aggiunte a invertPoint e alla lunghezza delle estensioni restano valide e non vengono riaperte come difetti. |
| 5 | Geometria finale dopo costruzione, ritaglio e unificazione | L’uguaglianza bordo/superficie nel parametro correlato non certifica appartenenza alla regione ritagliata, topologia, iniettività o assenza di autointersezioni. Restano da verificare i casi fuori dai registri di genealogia già supportati, incluse ricostruzioni o riparametrizzazioni successive. |
| 6 | Rifiuti conservativi e costo dei casi complessi | Limiti di lavoro, catene di wrapper lunghe e riparametrizzazioni non supportate possono rifiutare geometrie valide. Sono limiti dichiarati, non automaticamente errori. Migliorare supporto, diagnosi e costo su casi misurati, mantenendo i budget geometrici originari. |

Fonti aggiornate: [quadratura](review-k10-quadratura-2026-11-17.md), [fit finito](review-k10-mapping-fit-finiti-2026-11-17.md), [wrapper e perimetro attuale](review-k10-wrapper-curve-2026-11-17.md), [Mapping nonPlane](review-k10-mapping-nonplane-2026-11-17.md), [inversione periodica](review-k10-inversione-periodica-2026-11-17.md), [lunghezza delle estensioni](review-k10-estensione-lunghezza-2026-11-17.md).

Già incluse nel perimetro riconosciuto: curve E cubiche e Q lineari/cubiche a pesi positivi variabili; carrier B-spline a pesi positivi, periodici o aperti, nativi o istanziati, entro i limiti di grado e di suddivisione; Trim E/Q e trasformazioni affini E fino al limite documentato 32. Non vanno rimessi nell’elenco generico «razionali, istanze e wrapper da implementare».

## Altri punti della review

| Punto | Cosa rimane | Classificazione |
| --- | --- | --- |
| Pannelli e anteprime | Completare il confronto fra funzioni per patch/corpo completo, avvio manuale/automatico e riuso alla conferma; rendere esplicite le differenze giustificate dal costo. PRE1–9 e i runner già migrati coprono i rispettivi perimetri. PRE9 sincronizza pulsanti, picker e messaggi al cambio modalità in Proiezione. | Uniformazione residua; non una nuova lista di bug dimostrati. |
| Stili e menu | Corretto UI11 in questo intervento: sottomenu cache e overflow ribbon usano CadMenu. Resta il confronto complessivo dei pannelli e degli altri stili, senza presumere altre regressioni. | Segnalazione concreta dell’utente; altre incoerenze richiedono riscontro prima di essere dichiarate bug. |
| Memoria e GPU | Profilazione prolungata con apertura/chiusura di documenti, annullamenti e carichi GPU; verifica delle perdite dell’intera app. M1/M2 sono corretti e i sanitizer mirati non sostituiscono questa campagna. | Lacuna di verifica: nessun leak permanente dell’app dimostrato. |
| Modellazione libera | Restano limitati ai casi verificati raccordi liberi, sformo generale, collasso dei raccordi nello svuotamento e alcune geometrie di contorno apparente. Le reti U/V sono Coons G0, non Gordon globale G1/G2. | Limiti funzionali generali, distinti dai riproduttori già corretti. |
| Configurazioni e rilascio | Verifica visiva nativa macOS e stili desktop non provati; GPU/hardware e Windows fuori dal perimetro certificato. Checkpoint18d0adc: build Linux/macOS PASS, test FAIL su profilo/pose. Riparazioni pubblicate in78d7268, CI44142011489 Linux121/121 e macOS124/124 PASS; il recupero underflow/arresto resta locale. | [Stato CI e checkpoint](review-checkpoint-push-2026-11-17.md); verifica visiva nativa separata. |
| Documenti aggiornati | Supporto/K11 è verificato sulla versione congelata e sulla copia consegnata; l’originale successivo con SHA 70e4ec5c… è diverso e non eredita quei PASS. Ricontrollarlo se è la versione ancora interessata dal problema. | Ricontrollo condizionato alla versione, non K11 automaticamente riaperto. |

Fonti: [pannelli e anteprime](review-preview-unification-2026-11-16.md), [UI9/UI10](review-ui-colori-api-2026-11-16.md), [memoria e copertura](code-review-2026-11-15.md#funzioni-orfane-e-memoria), [Supporto](supporto-booleana-2026-11-16.md), [booleane e tema](review-boolean-order-tabs-2026-11-17.md), [registro](../CLAUDE.md).

La creazione di Booleana dal menu reale è verificata: una costruzione in anteprima, riuso alla conferma e ricalcolo dopo la modifica di un operando. Verificati anche Parallelepipedo e Scala; per Estrusione la promozione riguarda i casi a distanza con operandi espliciti. [Prove e genealogia](review-generale-2026-11-20.md#k103-aperture-di-spessore-e-conferma-della-booleana-21-novembre)

## Punti da non considerare ancora da correggere

- **Orfane iniziali:** 22 candidate tutte classificate; 16 rimosse, 6 query/cataloghi conservati intenzionalmente. Non rimangono 17 API legacy da decidere. Una nuova ricerca più estesa è attività di review, non prova di altre orfane. [Esito finale](review-legacy-api-2026-11-16.md).
- **Tema schede:** UI8–10 corretti, schede e tema Wayland ricontrollati con PASS il 17 novembre. Resta la copertura visiva di altri sistemi; i timeout storici non sono una regressione corrente dimostrata.
- **K2:** in produzione e verificato, non lasciato sperimentale. **Ellisse per estremi:** implementata con quattro estremi quotabili/vincolabili.
- **Superfici scala/posa:** riproduttore corretto nei casi documentati. **Flacone3 inverso:** fallimento storico non riprodotto sugli stessi record; resta esplicita la connessione tollerante, non una nuova patch da fare senza un caso negativo.
- **K11 Supporto:** correzione e copia unita verificate sulla versione delle prove; distinguere il file successivo indicato sopra.

## Ordine di prosecuzione

Il controllo globale K10 è esteso alla quota affine e alle mappe UV separabili, diagonali e oblique anche a rango incerto. Sono compresi i target B-spline a pesi positivi nativi o istanziati e le estrusioni a profilo aperto o chiuso con generatrice B-spline anche ritagliata o trasformata. Sono compresi anche i cilindri e le estrusioni di coniche anche ritagliate, riflesse o scalate in modo non uniforme. Nel multifaccia restano la copertura continua del materiale e il residuo fra facce curve con proiezione diversa dai grafici cartesiani verificati. La prosecuzione riguarda le famiglie escluse, i rami di Newton e la verifica del B-rep finale. Il lavoro numerico rimane sui casi residui del filone 1. Test pertinenti alle modifiche, compilazioni e analisi seriali.

Per K10.3 il checkpoint corrente copre i trim intervallari in domini
affini, il contatto di distanza sestica, le quote comuni sestiche e
razionali di grado superiore e le sorgenti inclinate su appoggi curvi
con giunzioni a T. [Prove e limiti](#copertura-k103-su-trim-e-appoggi-di-grado-superiore).

La [roadmap funzionale generale](roadmap.md) contiene anche nuovi strumenti e miglioramenti (saldature strutturate, pacchetto autonomo di rilascio, ulteriori casi HLR, prestazioni e budget GPU). Sono sviluppi separati, non bug pendenti di questa review.

Checkpoint18d0adc e riparazioni78d7268 pubblicati su richiesta dell’utente; CI44142011489 verde Linux/macOS. Il successivo lavoro numerico e il controllo globale del carrier a quota affine di segno stretto restano locali, con prove mirate e app completa aggiornate. Nessun CAD originale ricontrollato in questo passo e nessuna modifica alle prove precedentemente sigillate.

Il perimetro già supportato delle fin E/S/Q non equivale a quello dell’intero carrier: il provider globale copre target polinomiali e razionali a pesi positivi anche periodici e a gradi misti entro il budget algebrico, nativi o istanziati, quota affine anche con cambio di segno e UV affini anche oblique a rango incerto. Il controllo comprende anche estrusioni a profilo aperto o chiuso con generatrice B-spline anche ritagliata o trasformata. Sono compresi anche i cilindri e le estrusioni di coniche anche ritagliate, riflesse o scalate in modo non uniforme. Le altre famiglie globali restano nel filone2/3.
