# Q razionale positiva: consegna staging

Solo due file di produzione candidati: kernel/fk_surface_curve_bounds.cpp/.h, API invariata. Nessuna scrittura del repository, commit/push, UI o build completa da questo agente. Root gestisce integrazione/catalogo/mappa/memoria; tests/audit i loro esiti separati. AGENTS/CLAUDE/politica e descrizioni+relazioni call/include/reference nei due versi consultate, evidenza allegata.

Sorgente5d85a734bf8ed6ee638843797cac8f2907c68427c1dafcd7e37268691fdd1ede; header647415920b7cd246ce307614aad2cb725292e3ad58f8407ef77b4a591dec7320. SOURCE-FREEZE.json228d2daf6dbf60ea6eee710e3e3f3f6d1dcb8192d22d5267ab1b794391df5d19 resta immutato. Baseline32b161/headerb6cc, unica libreria root/libforgekernel-baseline.a SHA8641c0a3c65dccd9906a935139669eb64c8e829fdb6bcf20aefcd7c97408c40e, non copiata.

## Costruzione e prova

Q è nativa B-spline grado1/3, pesi originali positivi finiti variabili; E resta nativa cubica positiva arbitraria; S resta2/3 per asse, positiva nativa/istanza. Nessun nuovo grado, wrapper, famiglia analitica o rilassamento del cap. La candidata UV ammette Q positiva variabile e conserva i pesi/nodi/grado; limita i poli Euclidei PRIMA dell'unica prova, mai dopo Failed.

Snapshot Q salva (U,V)=(P_u*w,P_v*w) in intervalli outward, D come Source<1> sugli stessi nodi e anchor originali, e conserva separatamente il convex hull ORIGINALE Euclideo. Source1 aggiuntiva costa3*nq+degree+3scalari:2nq per pesi intervallari,nq+degree+1 per nodi,2 per hull. Il conto è prima delle allocazioni sugli stessi2milioni scalari; work16384/depth24/celle256 e cancellazione invariati. La famiglia Q a pesi costanti non paga questa aggiunta.

Cox omogeneo calcola B_i,p(U/D)*D^p con fattori U-aD e dD-U e denominatori knot originali. Ogni asse ha il proprio grado pu/pv; il prodotto tensoriale ha denominatore comune D^(pu+pv). S POLINOMIALE usa esplicitamente Wsbar=D^(pu+pv), MAI1. S razionale usa Wsbar=sum(wS*Bu_h*Bv_h). HSbar ha gli stessi fattori e poli omogenei centrati originali. Il residuo è HSbar*WE-HE*WSbar, grado massimo18+3=21.

lowerD=max(minOriginalQWeight,hullD.lo)>0 è valido sull'intero nodo perché dentro il dominio Q. Immagine UV: hull(U/Vnumerator) diviso intervalloD[lowerD,hullD.hi] tramite4quotienti outward, poi intersezione con convex hull Euclideo originale: due enclosure indipendenti, non clamp al dominio S. Il provider prova inclusione prima di esaminare TUTTE le celle chiuse intersecate, comprese adiacenti ai nodi.

Sulla parte ATTIVA di ogni cella Wsbar>=effectiveMinSW*lowerD^(pu+pv). effectiveMinSW=1 sul carrier a pesi costanti normalizzati implicitamente dalla snapshot precedente; minOriginalWeightS solo sul ramo variabile. L'upper continuo è il minimo di due upper validi: numeratore/effectiveMinSW/lowerD ripetuto pu+pv/lowerE, e quando hullWsbar.lo>0 anche numeratore/hullWsbar.lo/lowerE. Tutte divisioni outward positive; nessun prodotto nominale di lower, nessun certificato se un intervallo rimane inconclusivo. Overflow/underflow e risorse possono dare Failed conservativo, terminale; non recupero campionato. Coefficienti/pesi non vengono normalizzati o ricostruiti double.

## Verifiche di questo agente

Compilazione overlay Release con Wall/Wextra/Wpedantic exit0, nessun output.7probe sintetici PASS:5E3/Q3 razionali matching su S polinomiale con peso comune4 e gaugeQ1,1e±20,1e±50, upper4.22..4.49e-12mm al cap1e-7;pairedQ1 reciproco+Ecorretta Certified~2e-14 e Ewrong=D³ Failed, che esercita il rischio D^p omesso.

Compatibilità: stesso driver pubblico mapping cylinder/NURBS, baseline8641 contro overlaycandidato,48fin con Q a pesi costanti: JSON IEEE e stdout byte-identici. Anche6probeErazionale/QLine matching/negativo/gaugeE/Sopposti fino1e±250 producono stdout byte-identico.3cmp exit0. Le funzioni basis/residual/snapshot/onRange del vecchio ramo sono byteidentiche (confronto statico allegato). Questi insiemi non sono prova universale di tutte le possibili input; non dichiarano carrierfit rispetto all'intento della mappa, topologia/trim/Newton o app aggiornata. Nessun'altra modifica software prevista senza un finding concreto.
