# Audit indipendente K10 — composizione affine diagonale su estrusione

Esito finale: nessun blocco rilevato nello scope congelato. Replay su oggetti privati ricompilati dai sorgenti produzione: **107 controlli precedenti + 76 nuovi PASS**. Il riproduttore pubblico diagonale aggiuntivo conserva ora il carrier originale e rispetta il budget. Produzione, build condivisa e CAD utente non sono stati modificati da questo audit.

## Versione e ripetibilità

`manifest.json` associa percorsi relativi al repository ai SHA256 dei sette file core effettivamente compilati/esaminati e del test permanente letto. `final-sources/` conserva gli stessi byte. Il controllo di uguaglianza con il repository è in `finalize.py`. Le fonti sono V3: bounds e parser invariati V2, helper con preferenza isoparametrica debitata e allocazione del budget in tre fasi. Il file surface_mapping è invariato dal blocco precedente.

Esecuzione: `python3 build_final.py`, poi `python3 finalize.py`, dalla directory delle prove. Il primo script usa i comandi reali in `forgecad-release/compile_commands.json`, oggetti privati, `-fno-fast-math -ffp-contract=off`, e l'archivio kernel esistente dopo gli oggetti sostitutivi. Non invoca CMake né ricompila la build condivisa. Le intestazioni quote sono copiate accanto alle fixture. I log definitivi terminano in `-final.log`; `*-first.log` e `first-sources/` sono intermedi. `baseline-before.log` è il replay V6 precedente, preservato con `before/` e `before-manifest.json`, non un risultato V3.

## Caso reale e regolarità

Fixture `baseline.cpp`, etichetta `native_rotation_shift`: estrusione del profilo B-spline originale grado 6, campata stretta [.1,.101], gobba Z=.001, lamina piana; angle=.29, shift=(.03,.02), anchor=(.05,.1), tolleranza pubblica 1e-6 e budget interno 2e-8. Prima: successo, checkBody=0, deviazione dichiarata 4.2489214391262393e-11, errore bordo .001, carrier rifittato planare. Dopo: successo, checkBody=0, deviazione 1.6641748144111367e-8, errore indipendente 1.0235892836792716e-8, carrier originale Extrusion.

`regularity.py/json` conserva la prova sui coefficienti IEEE originali: su tutte le campate il minimo Bernstein esatto di x' è positivo, limite globale outward 0.9999999999999721. Per S(u,v)=(x(u),v,z(u)), ||Su×Sv||>=x'>0. Il profilo è un grafico monotono regolare e le derivate ai limiti della gobba sono compatibili; la rotazione UV affine a quota materiale zero conserva la regolarità. Il vecchio carrier con tutti i poli Z=0 fornisce anche il lower bound geometrico .001, indipendente dalla parametrizzazione. Questa prova riguarda il riproduttore quota zero, non un offset normale materiale.

## Formula e fonti originali

Per ogni pezzo UV affine originale, T(t)=C(u(t))+v(t)D. La candidata cubica Q(t) viene sostituita dalla cubica R(t)=Q(t)-v(t)D, con sottrazione outward nei quattro coefficienti Bernstein sul **parametro originale della query**. Quindi Q-T=R-C(u(t)). Non viene formato il rapporto dv/du: un du diverso da zero, anche 1e-16, non viene identificato con zero. Per du diverso da zero la query u esterna e il pullback inverso outward di R riducono il problema al provider di posizione sulla curva originale; per du esattamente zero si confronta la cubica con un enclosure del punto originale C(u). Il verso negativo è trattato dall'inversa affine, senza alterare gli originali.

La preparazione legge poli, pesi e nodi originali del generatore Line/NURBS e i wrapper Trimmed/Transformed. Non usa uIso/vIso arrotondate come certificato. BSP1 UV conserva poli e ancore originali, compresi sottointervalli e trim annidati; Line2 conserva origine/direzione. I break preimmagine sono suggerimenti nominali per il fitter, non la prova di copertura. Le disuguaglianze continue dipendono da pesi positivi, coefficienti finiti, aritmetica IEEE con arrotondamento outward e compilazione senza fast-math/contrazione FMA. Overflow, denominatori o enclosure non risolti non producono un bound finito.

Il cambiamento condiviso già precedente affineBezier=a+(b-a)t è algebricamente equivalente per tutti i t reali; qui il replay 30 factory +24 query protegge anche le composizioni precedenti. Nessun aumento di una griglia fissa sostituisce l'oracolo.

## Query chiuse, contratti e risorse

La primitiva typed copia gli span ed esige partizione ordinata contigua dell'intervallo richiesto, ancore finite originali contenenti la restrizione e copertura completa. I confronti chiusi includono entrambe le mappe adiacenti anche se la loro intersezione con la query è un punto. Il test con salto UV typed di 10 produce bound >=10 sulle due query terminanti/iniziante al nodo e sulla query ±1 ULP. Un tentativo ulteriore di costruire la stessa discontinuità come BSP1 originale è rifiutato dal costruttore («molteplicità interna > grado»); non è un bug riproducibile con curva ammessa.

Budget controllati: <=100000 span/poli UV, <=200000 eventi/intersezioni aggregati di preparazione/query; il conteggio query include anche i rami puntuali. Restano i budget dei coefficienti originali (2M) e prodotti polinomiali (1M). Famiglia conosciuta ma costo/numero/finite/domain inconcludente: eccezione o bound infinito, non fallback campionato dichiarato certificato. Famiglie fuori scope: oracolo vuoto. Cancellation point nelle preparazioni e nei cicli query; provata cancellazione già richiesta e sopravvivenza della closure dopo distruzione delle variabili source/surface locali. Non è una prova esaustiva di tutte le interleaving di cancellazione.

## Helper e assemblaggio

Preferenza iso V3: solo quando l'intero scarto continuo dei poli UV originali verso la proposta iso, moltiplicato per i bound metrici globali, più la quota scartata, sta nel medesimo budget. Non è una soglia angolare. Altrimenti Extrusion conserva entrambe le coordinate originali. Il provider non fa snap di du!=0. Il caso con nodi del generatore riscalati 1e-16 mantiene geometria invariata e verifica questa distinzione.

Il corridoio rettangolare completo fra UV originali e UV costruite deve stare nel dominio naturale. Prima si preparano UV e bridge dei bordi, poi si aggiungono i bridge di **tutti i fin originali**, infine si alloca al fit il residuo del budget massimo. Le cubiche finali effettive, inclusi i poli impliciti ai join, vengono ricertificate. Ciò risolve il precedente fallimento numerico a scala1000 senza allargare la tolleranza.

Restano verificati quota dell'intera faccia, trim originali anche tolleranti, vertici originali con gap accumulato nella deviazione, sense/pose e B-rep finale. CheckBody e ultimo cancellation point precedono la pubblicazione di history/deviation. I casi tolleranti falliti e la cancellazione conservano deviation=123/historyFaces=2; il vertice distante 1.9e-8 ammesso viene dichiarato con deviazione >=gap. Il budget 100001 poli rifiuta con output intatti, senza ricadere nel legacy. La guardia Jacobiana esistente a 27 punti nel chiamante è invariata.

## Prove definitive

| Gruppo | Numero | Evidenza |
|---|---:|---|
| Metrica originale | 16 | tensor non-clamped, estrusione razionale/trasformata, pesi estremi failclosed |
| Fit iso precedente | 24 | polynomial/rational e wrapper |
| Wrap pubblico già supportato | 8 | identità, razionale, metrica, D obliqua, pose, frame opposto, quota compensata, tensor iso |
| Tolleranze/atomicità | 4 | vertice e fin scostati, cancel, piccolo gap dichiarato |
| Factory composizione precedente | 30 | supporto/partial/periodico/contratti |
| Query precedenti | 24 | bound >=errore denso indipendente |
| Risorse precedente | 1 | 100001 poli rifiutati senza pubblicazione |
| Nuovi fit diagonali | 48 | rational/polynomial × tre wrapper × due scale nodi (1 e1e-16) × quattro percorsi UV |
| Nuovo contratto typed | 28 | gap/overlap/ordine/NaN/overflow/budget/null/unsupported/cancel/lifetime/join chiusi/constantU |

I quattro percorsi nuovi comprendono diretto, reverse con parametri [-2,3] e trim parziale annidato, Line2 diagonale e BSP1 composta diagonale+constantU+reverse. Tutti i48 fit rispettano errore denso<=deviazione<=1e-7. I campioni indipendenti sono evidenza di non regressione, **non** il certificato continuo. La fixture pubblica reale è ulteriore rispetto ai conteggi107+76. Il test permanente `test_mapping_diagonal_validation.cpp` è stato letto e archiviato; la sua esecuzione/sanitizer e le verifiche app/CTest/build/grafo appartengono agli altri agenti/root, non sono rivendicati come esecuzioni di questo audit.

## Limiti del blocco

Supportati qui diagonali affini su Extrusion originale Line/NURBS, UV Line2/BSP1 con pesi uguali sul pezzo attivo e Trimmed. Tensor B-spline diagonale, UV razionale non affine, coniche riparametrizzate e normali offset materiali non sono certificati da questa estensione. Il controllo legacy `native_normaloffset` conserva uno scarto osservato .011718... ma la regolarità/iniettività di quella mappa offset non è dimostrata: non lo presentiamo come nuovo caso regolare confermato. I corridoi fuori dominio o non risolti, inclusi seam periodici non rappresentabili nel dominio naturale corrente, restano failclosed nello scope conosciuto.

Conservare il carrier originale evita il fit dell'interno della superficie per questa famiglia; il certificato è di posizione, non di iniettività né di Jacobiano globale. K10 resta aperto sugli altri rami campionati. Conteggio invariato: **8/24 siti parzialmente coperti, 16 rimanenti**; questa estensione chiude un'altra famiglia dello stesso sito mapBody-edge e non aggiunge un sito all'inventario.
