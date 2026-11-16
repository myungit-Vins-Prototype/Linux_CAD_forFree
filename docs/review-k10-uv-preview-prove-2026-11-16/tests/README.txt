Archivio curato K10 UV — prove tests del 16 novembre 2026.

fontefinale.txt è il candidato main SHA270088... usato dal runner V2
normale e dal sanitizer V2. fonte-prima-newline.cpp.txt, SHA5ceb..., è
il main del replay baseline e del primo sanitizer V1: tre newline sono
l'unica variazione. Dati e asserzioni sono invariati.

Baseline: 9 gruppi, 6 fallimenti geometrici attesi, 3 PASS, exit1. Fixture
Box1 con 12 edge e 6 Plane; un edge contiene una campata cubica stretta
[0.1003,0.1013], con pcurve originali coerenti. Tutte le distanze joined
sono0. Checker input/output PASS, input binario invariato. A t0.1008 il
residuo è7.5000052e-6, contro un budget1e-6. Varianti: razionale, trim con
range[-3,5], edge invertito e ordine specs inverso, posa, scala1000.

V2: le stesse6 regressioni risolte,2 positivi offsetFaces(trim),1 gruppo
atomicità invalid/cancel e1 gruppo provider:10 PASS. Il gruppo provider
verifica projection/residual distinti, componente normale5e-7 conservata
nel residuo3D, proprietà dei coefficienti dopo distruzione di E e Plane,
polo candidato corrotto, rifiuto di pesi0/NaN e polo infinito, Other non
supportato, Plane singolare, Trimmed(Transformed), cancellazione query.

Sanitizer V2: ASan/UBSan/LSan su2 TU modificati più main. Leak check
attivo, nessuna diagnostica o soppressione,10 PASS fuori sandbox. Il
build V2 riusa soltanto il provider strumentato V1, perché provider e
header conservano gli stessi SHA3faf.../d170... Il build-provider V1
storico conserva i comandi di tale compilazione. Il primo log V1 con
arresto ptrace è distinto: LeakSanitizer non funziona sotto ptrace.
Non si attribuisce un PASS V1 al sorgente offset V2.

Esportazioni:8 JSON baseline e8 JSON V2; ogni versione contiene192 fin
finali e192 fin sorgenti. Double come stringhe hex, curve/pcurve ricorsive,
frame Plane originale, range, budget, sense e provenienza esplicita delle
facce. Nessun binario, CAD o oggetto in archivio. Root verifica le campate
con Fraction indipendente dai provider. Le misure dense del main sono
regressioni, non certificati continui. I16 JSON sono identici ai rispettivi
dump privati originali; gli8 dump V2 coincidono anche con i dump V1.
Root eseguirà il replay Release dopo la build completa.

Comandi .sh.txt e percorsi /tmp sono testimonianze degli esatti run
storici; non risolvono automaticamente il nuovo target permanente.
metadata.txt conserva hash, scope, risultati e limiti. SHA256SUMS.txt
sigilla tutti i file curati dopo il completamento di questo archivio.
