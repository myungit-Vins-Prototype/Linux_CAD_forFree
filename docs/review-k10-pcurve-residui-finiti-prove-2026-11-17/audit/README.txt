Audit indipendente — pcurveDeviation fail-closed

Candidato7d7574589225ba9e35899aab76285d824ee7f0bbbe3be48139d212775e1df85b: nessun blocker statico trovato. Solo letture repo e testi/tmp; nessuna compilazione/C++runtime/copia libreria/UI/commit/push da questo observer.

Finding concretamente riprodotti da ROOT, non eseguiti qui:
1. PlaneXY, C(t)=(t,0,1), Q(t)=(t,0), range[0,1]: samples−1/0 restituivano0, mentre il gap geometrico è1. Conteggio negativo salta il loop; zero genera0/0 nel parametro e il NaN viene ignorato dal massimo.
2. Circle3/Circle2 native con centroDBL_MAX e raggio.5DBL_MAX finiti: valutazione punti overflow, ma deviation0 ed exactPCurve non-null. È un caso di dati costruttori finiti con valutazione non definita, non un corpo CAD di scala valida.
3. NURBS nativa all-zero poles/positive weights21,29,37,45 e natural[1,2], query deliberatamente[0,1]: ROOT ha ottenuto sourcePointFinite0, returned1, deviation0, witnessFinite0. Algebra originale indipendente Fraction: W=24t−3 e H=0; a t1/8 il denominatore è esattamente0 e produce0/0. La query fuori dominio è esplicita: non viene presentata come fallimento CAD valido.

Verifica statica della patch: +Inf su samplecount/range/breakpoints/parametro/UV/punti/gap invalidi; NaN esclusi prima sort e prima max. Copertura esatta estremi della partizione. Finite measured richiesto da tutti e tre i controlli finali(exact/Hermite/linear), impedendo Inf<=Inf. Le due vie fit assegnano deviation soltanto dopo accettazione; l'oracolo NURBS non può pubblicarla attraverso il fallback lineare. Stessa formula dei campioni ordinari; uscita prima incrementoINT_MAX, cancellazione distribuita. Nessun cap aumentato, nessun recupero campionato di valore non definito.

Nessuna nuova conclusione sul solo nearPole/manualshift: il nodo privato manca di controllo locale dopo shift ma il fit finale verifica i nodi dopo baseShift. Il primo pacchetto di delimitazione è immutato: final-proof/MANIFEST SHA cde57e8ae9ee16cb6ab7c08f5abf303bbe6fd8f9e706cb7fb29316051c302c29. Questo supplemento aggiorna soltanto l'attribuzione del riproduttore NURBS, all'epoca proposto e ora eseguito da root. Nessun pubblico largephase/nearPole bug è rivendicato.

Limiti invariati: pcurveDeviation resta campionata; picchi finiti fra sonde non sono esclusi. Fallback lineare legacy fino50tol e relativo finTolerance misurato restano invariati, distinti dal cap prefit delle fin certificate. Nessun certificato globale della geometria o dell'intenzione. Graph795/11651, descrizioni+call/include/reference entrambi versi, conservato. Parent gestisce CLAUDE/documentazione e runtime/build seriali.
