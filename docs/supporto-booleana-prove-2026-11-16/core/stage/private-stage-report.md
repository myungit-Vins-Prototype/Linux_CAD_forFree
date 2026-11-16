# Prototipo privato: contatti con il bordo ellittico

Le sorgenti candidate sono congelate in final-freeze.json. La produzione e il documento originale sono invariati. Nessun formato, enum, API pubblica, soglia globale o coefficiente del CAD è stato modificato.

## Contratto

Il nuovo percorso è abilitato soltanto da booleanOperation. Altri entrypoint (sezione, imprint, splitSheet, clip e unione di operandi dichiarati disgiunti) conservano il percorso precedente. La sostituzione è provata soltanto per intersezioni Line con bordo Ellipse su faccia Plane e carrier opposto Plane/Cylinder.

pairArcs conserva gli eventi originali p.s / p.t e gli ID body / face / fin / edge. Una multimap indicizzata dal parametro originale conserva l'identità esatta degli eventi; non viene cercato un bordo vicino. La catena segue esclusivamente loop/fin/vertici originali. Entrambe le direzioni sono considerate, incluso un ritorno su una singola fin ellittica chiusa. Gli anchor sono limitati a 4096; il lavoro di ricerca è limitato e annullabile. Ambiguità o dati non supportati disabilitano soltanto la sostituzione.

Il certificatore usa operazioni intervallari outward sui coefficienti IEEE originali. Valuta sin/cos tramite Taylor con resto esplicito, non con un campionamento libm. Il limite di Peano confronta la curva con la corda finita e comprende lo scarto degli estremi. La monotonia è certificata mediante derivata al punto centrale più limite Lipschitz. Il controllo del carrier cilindrico include le imperfezioni del frame originale e usa il polinomio quadratico della distanza radiale. Il budget è min(tolleranza booleana, linearResolution), senza moltiplicatori nuovi.

La provenienza estera è separata dai source/sourceFin/sourcePCurve locali. Le origini degli edge vengono raccolte alla fin realmente montata e associate al BuildEdge dopo pairRadially e splitVertices. Per gli edge sostituiti vengono unite le origini locali ed estere note; gli altri edge conservano il precedente comportamento della storia.

Le curve e gli intervalli realmente montati sono ricertificati contro l'intersezione originale. Questo è necessario perché l'assembler può scegliere un intervallo leggermente diverso da quello proposto. La ricertificazione copre esplicitamente lo scarto degli estremi e conserva il medesimo budget.

Per ogni nuovo uso su un carrier Plane/Cylinder viene costruita una pcurve affine nel parametro originale dell'ellisse. Il limite continuo comprende i due errori di Peano (ellisse e composizione della superficie) e lo scarto degli estremi. Il bound positivo viene dichiarato in fin.pcurveTolerance; l'esito campionato di exactPCurve/fitPCurve non autorizza questo nuovo uso. La verifica viene ripetuta dopo unify attraverso i discendenti dichiarati dalla storia dell'assemblaggio. Un discendente mancante è rifiutato.

Se un percorso che ha effettivamente utilizzato la sostituzione fallisce con domain_error, booleanOperation riprova transazionalmente il percorso precedente su nuove copie degli stessi input e con le stesse opzioni. La storia viene azzerata prima del tentativo. OperationCancelled e logic_error non sono intercettati come errori geometrici recuperabili.

## Verifica al congelamento

Entrambi gli ordini raw del caso reale passano: Corpo 2 / Corpo 3 e Corpo 3 / Corpo 2 producono 6 facce, 10 edge, 8 vertici, una shell solida e zero problemi da checkBody. I log sono raw-2-5.log e raw-5-2.log.

L'agente indipendente di test ha già confermato 51 asserzioni sul certificatore puro, incluse pose/scale, pullback del parametro originale, monotonia, reverse, budget, input invalidi e overflow. Sta eseguendo il replay del prototipo congelato sulle 176 prove kernel precedenti e sui percorsi applicativi reali. Questi esiti ulteriori non sono anticipati qui.

L'audit matematico indipendente ha verificato i bound di corda, carrier, frame e pcurve e il mapping di provenienza. I coefficienti originali del caso reale sono inoltre verificati da un certificatore Python Fraction indipendente: l'intera catena di bordo trattenuta dista dal cilindro meno di 2e-11 mm.

## Compilazione e artefatti

build_stage.py compila soltanto due unità private (fk_boolean.cpp e fk_boolean_boundary.cpp) e riusa il restante kernel/app Release. Il nuovo certificatore è compilato esplicitamente con -fno-fast-math e -ffp-contract=off. Le due compilazioni finali e il link terminano senza diagnostiche. commands.json conserva gli argomenti esatti; final-freeze.json contiene hash di sorgenti, oggetti, comando e binario.

La baseline di fk_boolean.cpp è fk_boolean.before.cpp.txt. Il delta leggibile è delta.diff.txt. Il confronto SHA256 conferma che la produzione è ancora identica alla baseline c325db71e6b55547253b9e27ebc365185aeccdcb71f543840fd8463b9ac934b9.

Il CAD originale mantiene SHA256 79887e91d75feb748af19fcdb14cd469cc40e74abab03e3cbeaa12e2d3271f7f. La sua rigenerazione completa ha prodotto B-rep byte-identici allo snapshot salvato, con zero errori delle feature. Le cause di cache/storia sono quindi escluse per la riproduzione corrente.
