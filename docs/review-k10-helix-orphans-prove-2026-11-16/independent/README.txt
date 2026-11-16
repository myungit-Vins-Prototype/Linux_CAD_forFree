Audit indipendente Helix — 16 novembre 2026; consegna solo /tmp

ESITO: nessun difetto concreto dimostrato o blocco nuovo.
Orfane: consegna distinta /tmp/orphan-five-review/delivery, invariata.
Nessuna scrittura al repository, CLAUDE o archivi precedenti.

ESEGUITO DA QUESTO AGENTE
- constants.py: Fraction indipendente, bracket pi/2 Machin24/8 e1/32!<2^-100.
- build-edge.py: ricompila privatamente provider finale con C++17 -O1,
  -fno-fast-math -ffp-contract=off e collega kernel Release reale.
  edge-probe.cpp:37 PASS/0 FAIL; edge-results.log contiene comandi e risultati.
- depth-probe.cpp: figlio con stack256KiB e20000 wrapper, domain_error budget,
  exit0. Eseguito prima e dopo miglioramento iterativo. Nessun crash dimostrato.
- Hash esatti fonti lette/linkate: edge-source-hashes.json. Manifest testuale.

SNAPSHOT E ANALISI STATICA
Audit.txt e source-hashes.json sono l'audit matematico precedente sullo snapshot
16349e7a...; la prima consegna era solo statica. Il probe edge ricompila invece
provider finale ffa39b649457abb46ed5641444ecb21772e165a0b45e1f5e87e5a7d89b453e30.
Rilette le ultime differenze: prepare controlla domain solo Trimmed/Helix,
non ricorre nei Transformed; UI projectionBasis e' iterativo, cap256 e cancellabile,
chiamato dentro try prima di isImplicitSketchProjection. Miglioramento di
robustezza richiesto al core; nessun difetto di crash classificato o contato.
Matematica invariata rispetto ad audit.txt; formattazione precedente documentata
in core/formatting.json. Hash aggiornati registrati, nessuna pretesa di coprire
versioni successive.

Schema validator:3n+1 poli,degree3 nonrazionale,dominio identico,cardinalita' nodi,
estremi quadrupli e tripli interni,span finiti crescenti,originali poli finiti,
bound entro tolleranza. Costruttore BSpline rifiuta gia' nodi/poli nonfiniti:
non simulati con const_cast o UB. Callback possiede snapshot e non dipende da
source ancora vivo. Cancellazione nei loop,callback,fitter,validator. Propagazione
OperationCancelled distinta eseguita con token gia' cancellato e oracle posseduto;
non eseguito test temporizzato di cancellazione concorrente durante fit.
Atomicita' interna letta: deviation scritto soltanto dopo validate e ultimo
cancellationPoint,output geometry non pubblicata prima del return.

COPERTURA E LIMITI
37 assert:fit/bound valido;unsupported valido null+deviation invariata e require
throw;tol0/-1/Inf/NaN;null source;range invertito/nonfinito/fuori Helix eTrimmed;
256/257 wrapper;NaN matrice/phase;fase enorme irrisolta;preflight enorme atomico;
query degenerata/fuori dominio e poloNaN ->infinity;controlli finiti alterati;
actual rational/null/nodi semplici/dominio diverso;cancellation e recupero.
Unsupported valido restituisce null;invalidi riconosciuti lanciano,senza fallback
sampled. Preflight16384 e fase irrisolta possono rifiutare curve valide:policy
conservativa,non prova di impossibilita'. Scope UI statico:solo generic fit per
Helix wrapped;Helix nuda eTrimmed nuda mantengono grado5/C2,altri tipi fallback.
Non estendere il certificato al matching sampled o a tutta UI/proiezione.
Non eseguiti da questo agente:dense sweep/dyadic curve proof,UI,fullbuild/suite.
Test linkato a kernel Release preesistente,non equivale a fullbuild integrata.
I risultati di altri agenti non sono conteggiati. Nessun nuovo test permanente.

RIPRODUZIONE
python3 /tmp/helix-independent-audit/constants.py
python3 /tmp/helix-independent-audit/build-edge.py
Depth build:stessi include/provider.o/kernel archive, -std=c++17 -O1
-fno-fast-math -ffp-contract=off -pthread;fonte depth-probe.cpp.
