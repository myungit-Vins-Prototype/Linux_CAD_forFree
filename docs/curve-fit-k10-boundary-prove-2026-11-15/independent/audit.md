# K10 Boundary — revisione indipendente Sol

15 novembre 2026. Consultati AGENTS.md, CLAUDE.md e grafo delle responsabilità/chiamanti prima dell’audit. Nessuna modifica a produzione o build condivisa. Gli snapshot compilati, i log e il manifest finale sono conservati qui.

## Architettura e prova del bound

La nuova API `CurveFitSourceSpan` rende espliciti sorgente, sourceRange ordinato, fitRange ordinato e inversione. `curvePositionErrorBound` costruisce un oracle solo se TUTTI i pezzi del lato appartengono a Line/BSpline o wrapper Trimmed/Transformed supportati. Le coniche nel parametro angolare e Other restano unsupported; non si riparametrizzano con conversioni razionali. Un lato misto conserva il percorso campionato esplicito e non è dichiarato certificato.

Per C=H/W con W>0 e candidato cubico Q, J=H−QW dà il bound continuo ||J||/Wmin. Source IEEE poles/weights/knots/matrici sono trattati come input reali esatti; estrazione originale outward, elevazioni, prodotti e restrizioni Bernstein propagano intervalli. I pesi positivi sono normalizzati come nel provider offset preesistente. Non si campiona il residuo per certificarne l’assenza altrove.

Il mapper calcola con intervalli outward l’immagine della query, la estende a tutti gli sliver di arrotondamento (sourceQuery), visita ogni campata originale intersecata e riporta il candidato al parametro originale con composizione affine in forma polare. Questa composizione ammette endpoint invertiti o lievemente estrapolati: non assume arbitrariamente che una mappa arrotondata conservi il verso o resti in[0,1]. Il bound sul dominio allargato include il dominio effettivo richiesto. I nodi rimappati sono aiuti di raffinamento; la prova non dipende dalla loro posizione nominale in double. Le fitRange sono validate prima del sort, non nulle e contigue; source nulle/range non finiti sono errori. I budget sorgente/pezzi/net e il fallimento inconcludente sono conservativi.

## Coons: verifica dell’output reale

Con gli angoli scelti dai fit C0/C1, il bordo inferiore effettivo è C0(u)+(1−u)(D0(0)−C0(0))+u(D1(0)−C0(1)); il bordo superiore ha la correzione analoga. Quindi fit<=tolerance da solo non basta quando la chiusura tollerante ammette endpoint diversi. L’inserzione dei nodi e l’assemblaggio possono inoltre introdurre arrotondamento.

Il nuovo codice fitta i lati supportati con metà budget, estrae le quattro righe/colonne dai POLI FINALI della Coons dopo makeCompatible e richiama l’oracle su ogni campata cubica entro il budget originale. La molteplicità3 dei nodi interni è verificata; non viene assunto che la spline finale coincida esattamente con quella certificata prima dell’assemblaggio. Il limite1milione di poli è controllato prima di allocare la rete.

Le pcurve dei lati certificati sono BSpline2 di grado1 nel parametro canonico dell’edge, con estremi UV ricavati dalla stessa mappa esplicita. Side2/3 hanno già fitRange invertito dalla costruzione del lato: sideUV usa sigma diretto, evitando una seconda inversione. `data.maps` resta nell’ordine dei SubPiece, anche se il provider ordina una copia per fitRange; la provenienza della pcurve resta corretta. Fin.sense conserva il verso originale della Piece. La tolleranza della fin usa il budget del bordo certificato; non dipende dall’inversione generica con budget1e−7. BSpline2 e Trimmed2 sono già serializzabili.

## Difetti intercettati durante l’audit

1. Coons: `corners.cpp`/`corners-baseline.log` riproducono quattro rette con un endpoint separato da δ, accettato da join1e−6scale. Prima della correzione δ=2tol e5tol erano accettati, il bordo finale era spostato esattamente di δ e checkBody riportava0problemi. Il nuovo controllo finale deve rifiutarli; δ=.25tol deve rimanere accettato.
2. Oracle: prevalidazione fitRange NaN/nullsource spostata prima del sort dopo segnalazione indipendente.
3. Oracle chiuso alle giunzioni: con map0Line0→1 su[0,1], map1Line3→4 su[1,2], il candidatoQ3→4 sulla query[1,2] aveva bound9.4591e−14, ma il callback Boundary nel puntoσ=1 sceglie il vicino sinistro, con errore2. `junction-original.cpp`/`junction-before.log` conservano il caso. Non è stato osservato un falso successo del body: il fitter partiva dal vero nodevalue e la verifica di tutte le righe copriva il nodo anche dal vicino. Il contratto pubblico dell’oracle su query chiuse era però incompleto. Il fix aggiunge a entrambi gli endpoint il residuo puntuale verso entrambi i vicini, con valutazione outward della sorgente e candidatoP0/P3.

Un salto originale B-spline di molteplicità interna p+1 non è un input supportato: `validateKnotVector` lo rifiuta. Il probe conserva anche questa controverifica, senza inventare un’ulteriore famiglia valida.

## Prove indipendenti

`oracle.cpp`:27 controlli, forward/reverse e query parziali; candidato esatto/traslato/ordine sbagliato; feature grado27; NURBS nonclamped, pesi scalati1e−100/1/1e100; compound con pezzo invertito; copertura con gap rifiutata; input NaN/null/infinito/vuoto; conica unsupported. Nei confronti numerici il bound deve dominare l’errore denso. Il risultato della linea esatta è circa1e−13; l’errore tangenziale.001 non viene nascosto; il polinomio27 nascosto non può essere accettato alla vecchia tolleranza.

`junction.cpp`:8 query sui due lati di una giunzione discontinua di mappe, con Line originali e BSpline1, mappe forward/reverse, più1 verifica del rifiuto di knot discontinuo originale. Il bound deve coprire gap2 o4 anche quando il candidato coincide con il solo ramo interno della query.

`boundary.cpp` con `fixtures.h`: tre contorni reali prima difettosi (grado6 polynomial, grado6 razionale, grado27) rieseguiti sul sorgente finale. Si misura anche la distanza edge→surface attraverso le pcurve realmente pubblicate, su10001 parametri per ogni edge, e il confronto con fin.pcurveTolerance. Le due prove di regolarità continua sui veri poli/pesi IEEE, precedentemente ottenute con Fraction/Bernstein, sono copiate e ripetibili in `regularity.py`/`regularity.json`: x'>=.9999999999999722 e x'>=.4904667777777777, quindi nessuna degenerazione nascosta. Le fixture e CSV sono qui, non solo riferimenti a directory precedenti.

`corners.cpp`:δ0/.25tol positivi;2/5tol devono essere rifiutati dalla certificazione del bordo finale;20tol resta rifiutato già come contorno aperto. I test permanenti dell’altro agente coprono inoltre tutti e quattro i lati, pose/scala, trim annidati, lati composti, inversione canonica e SubPiece parziali: quei risultati appartengono al suo report.

## Limiti

Il certificato riguarda i bordi dei lati interamente supportati. Non prova regolarità o assenza di autointersezioni nell’interno della Coons, non certifica le coniche angolari/mixed legacy e non migra gli altri callback generici fitCurve. Le griglie nei probe sono controverifiche numeriche: la garanzia continua proviene dagli intervalli Bernstein e dal controllo delle righe finali. Assunte operazioni IEEE754 ordinarie, no fast-math/flush-to-zero; il comando reale usa -ffp-contract=off. Degenerazioni, cattivo condizionamento, limiti di precisione o budget possono produrre un rifiuto conservativo.

## Risultati del replay finale

- Oracle27/27 e junction9/9PASS: `oracle-final.log`, `junction-final.log`.
- Tre body reali: max gap edge→surface via pcurve1.16502195333e−8,3.29388208562e−8,3.12393161356e−8; tutti <= fin.pcurveTolerance1e−7 e checkBody0issues. Il probe percorre10001 punti per edge, sufficienti anche a risolvere la campata stretta, oltre alla certificazione continua del codice.
- Corner: δ0 e.25tol accettati;2tol e5tol rifiutati dalla nuova certificazione finale;20tol rifiutato come contorno aperto.
- Replay ricompilato sul freeze finale effettivo: boundsfeed286d, boundary12e1ca37, headerbounds67b41e0c, headerboundaryd1304645. Tutti e quattro gli hash coincidono con il repository. Manifest repo-relative in `manifest.json`; risultati strutturati in `results.json`; log precedenti distinguibili dai `*-final.log`. Nessun ulteriore rilievo bloccante.

## Ripetizione

I quattro eseguibili `oracle-current`, `junction-current`, `boundary-current`, `corners-current` sono congelati sul manifest finale. I sorgenti dei probe e le copie `fk_*.snapshot.cpp`/header permettono di ricostruirli; `build_final.py` usa questi snapshot, compile_commands del progetto e l’archivio kernel locale, senza invocare build condivise. I log `*-final.log` sono la consegna autorevole; gli altri log documentano passaggi intermedi. La build app, i test consumer completi, sanitizer e aggiornamento CLAUDE/grafo restano del coordinatore e degli agenti assegnatari, non sono dichiarati eseguiti da questo audit.
