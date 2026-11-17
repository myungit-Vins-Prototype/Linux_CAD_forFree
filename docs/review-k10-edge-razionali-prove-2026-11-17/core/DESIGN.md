# E cubica nativa razionale, Q polinomiale: candidata congelata

API invariata. Solo fk_surface_curve_bounds.{cpp,h}; il resto del repo non è modificato dall'agente. AGENTS/CLAUDE/policy e descrizioni+archi call/include/reference nei due versi consultati; snapshot grafo e hash fonti allegati. Root gestisce integrazione, catalogo/mappa e memoria.

E a pesi finiti strettamente positivi variabili diventa famiglia riconosciuta. Solo in questo ramo i coefficienti di Source<3> diventano H_E=(P-anchor)*w tramite intervalli outward; Source<1> usa gli stessi nodi originali, pesi originali I(w), e onRange già esistente. La partizione E/Q resta invariata, nessun nodo vicino viene fuso. Q rimane Line2/B-spline1o3 a pesi costanti positivi; wrapper E/Q restano Unsupported.

Per ogni nodo, lower_E=max(minOriginalWeightE,hull(W_E).lo)>0: tutto il nodo è provato nel dominio originale E, perciò la base positiva e la partizione dell'unità garantiscono il primo limite indipendente. Il secondo proviene dal polinomio intervallare locale. Per S continua a valere il precedente lower sulle sole porzioni attive di TUTTE le celle intersecate.

Il residuo è H_S*W_E-H_E*W_S, norma del numeratore divisa con due operazioni outward positive successive per lower_S e lower_E. Non si calcola un prodotto double nominale dei lower. Un overflow intermedio/underflow irrisolto può rifiutare conservativamente, mai autorizzare una falsa Certified. Grado massimo resta18+3=21. Nessuna normalizzazione dei pesi, conversione approssimata, aritmetica in float o certificato dai campioni.

La famiglia E a pesi esattamente costanti segue le stesse espressioni snapshot/residual/sum/hull/divisioni e lo stesso preflight numerico della baseline; nessun costo Source1 viene addebitato. Per E variabile il setup addebita altri3*ne+6scalari prima delle allocazioni (2ne per Iweights, ne+4 nodi cubic, 2 originalHull), sugli stessi maxScalars=2000000. maxVisits16384/maxDepth24/maxCells256 invariati; scansioni e nuovi loop cancellabili. Candidate Q ora consente E variabile se tutti i pesi sono validi, ma non rappresenta accettazione: la prova unica deve seguire prima di assegnare.

Prove di questo agente: compilazione overlay Release con Wall/Wextra/Wpedantic exit0;5positivi E/S razionali coincidenti Certified con cap1e-7 (gauge1,1e±100,1e±250 opposti), bounds fra7.38e-14 e8.00e-14mm;1negativo poloE z=.001 Failed. Probe diagnostico sintetico, non API pubblica. Tester/auditor curano regressioni e fin effettive; qui nessun claim app aggiornata o correttezza globale della mappa/Newton/trim/topologia. Nessun commit/push.

Baseline libreria unica in root/libforgekernel-baseline.a SHA57c11033779f33893b07d166c682a0c5e8e3d24c14c98abfee43c7253ca62fa7; non copiata. Candidate source32b161a97903c5ca746a87b3bde59fb3c79566316d0f274a189127cc396f62e6; headerb6ccad5f246b08c9e69523e485f978600b51ae46104d64d12b18c53dbd487d00.

Verifica aggiuntiva corrente: stesso driver mapping pubblico cylinder/NURBS linkato con baseline57c110 e overlay32b161.48fin effettive; JSON con coefficienti IEEE, metadata, status e upperBound IDENTICI byte-per-byte, come stdout (cmp exit0 entrambi). E resta polinomiale in questi48casi; confronto di compatibilità su questo insieme, non prova universale. Negativo narrow rifiutato in entrambi. Fonti e output allegati; nessun software modificato dopo freeze.
