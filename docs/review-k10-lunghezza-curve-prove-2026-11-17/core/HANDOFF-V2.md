# K10 arcLength — V2 norma stabile locale nei due speed callback

## Finding aggiuntivo confermato dal root

Dopo V1 il root ha RIPRODOTTO su native BSpline grado1 poli0..1: h=2^600 genera derivata finita2^-600, sqrt(dot) underflow0 e arcLength ritorna0 invece della vera lunghezza1. Il duale h=2^-600 ha derivata finita2^600 e normInf; baseline con watchdog0.3s esce124. Line3 con token già attivo ritornava1. Questi runtime sono del root: core non ha compilato o eseguito C++.

V1 aveva documentato l'underflow come fuori scope; il root ha esplicitamente autorizzato includerlo nella V2 della STESSA correzione numerica. Il pacchetto final-proof V1 e il suo sigillo restano IMMUTATI, non usati per attribuire risultati V2.

## Delta V2 rispetto a V1

SOLTANTO nei due callback speed di fk_curve_algo.cpp e fk_curve_length_cache.h: calcola squared=squaredNorm(d[1]); se squared è normale usa std::sqrt(squared), IDENTICA alla vecchia norm=sqrt(dot) per quel ramo. Negli altri casi, vettore esattamente nullo controllato componente per componente dà0; vettore non nullo usa std::hypot2/3. Il guard finito della derivata precede il calcolo e il guard finale della norma rimane: una derivata o una norma realmente non rappresentabile viene rifiutata. Non si usa norm==0 come prova di vettore nullo.

Questo consente i casi parametricamente scalati h=2^±600 senza scartare una lunghezza rappresentabile. Sui quadrati normali conserva formule e ordine aritmetico originali; le differenze sono limitate ai quadrati subnormali/zero/Inf. Nessun cambio alla norm globale o a fk_quadrature.h/publicheader.

## V1 conservata

Cancellazione a ingresso di entrambi gli overload arcLength, nel callback speed, sulle scansioni e ai ritorni; parametro t finito PRIMA di evaluate; range width finita; breakpoint finiti/ordinati/nell'intervallo/estremi coperti, inclusi puntuali con un breakpoint o duplicati; integrali e totali finiti. La cache valida value PRIMA del putlocale e total PRIMA del putwhole; prefissi validi possono restare al failure, nessun nuovo rollbackglobale. Chiavi/hit/contatori/eviction/partizioni/somme immutati sui casi validi.

NESSUN nuovo contratto di tolleranza: 0/negativi/NaN/Inf restano demandati alla semantica quadratura storica, senza promettere significati certificati. Nessun cap, epsilon, budget o profondità cambiati. d[0] resta escluso: l'integrando dipende dalla derivata, non dalla posizione.

## Limiti e verifica

Resta integrazione numerica stimata con GK7-15, roundofffloor e profondità30; non un bound continuo globale contro picchi fra sonde. La V2 stabilizza la norma dei vettori derivati finiti ma non rende finite derivate già non rappresentabili, callback o parametri non finiti, né quadcenter non rappresentabili. Quei casi sono rifiutati dai guard V1.

AGENTS/CLAUDE/politica, descrizioni/relazioni call/include/reference entrambi versi consultati prima del software. Perimetro2file approvato root; headercache dipende soltanto loft+2test. Nessuna scrittura repository, compilazione, esecuzione C++, copia di archivi/ELF, UI, commit/push da core. Runtime/test/SAN/build/mappa/memoria restano al root/tester. Freeze/source review e verifica integrità del pacchetto V1 effettuate. Eventuali baselineprobe riportati sono del root, non esiti candidati. Nuovi risultati V2 al momento del freeze PENDING.
