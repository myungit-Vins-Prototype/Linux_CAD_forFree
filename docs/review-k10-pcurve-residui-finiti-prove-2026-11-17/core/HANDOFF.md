# Freeze sorgente — metrica SP-curve non finita

Staging soltanto; nessuna scrittura nel repository, nessuna compilazione/esecuzione, nessuna copia di librerie o modifica a CAD. La memoria condivisa e l'integrazione rimangono al root.

## Causa e modifica

`pcurveDeviation` ritornava 0 per samples<=0 o se le distanze erano NaN: `std::max(worst, NaN)` mantiene worst. Il ciclo int includeva inoltre un incremento oltre INT_MAX quando samples==INT_MAX.

La metrica ora restituisce +Inf per conteggio non positivo, range non finito/non crescente o con lunghezza non rappresentabile, breakpoint non finiti/fuori range o copertura incompleta, parametro campionato/UV/punti/distanza non finiti. I breakpoint si verificano PRIMA di sort; i due estremi del range devono essere presenti nella loro unione. Il break all'ultimo campione evita l'incremento int non rappresentabile. Cancellation cooperativa a ingresso, scansione breakpoint, ogni campione e uscita.

`exactPCurve`, `fitHermitePCurve` e `fitLinearPCurve` accettano solo una misura finita, anche con tolleranza infinita; deviation viene scritto soltanto dopo l'accettazione, come prima. Nessun aumento di tolleranza/cap; formula e posizioni dei campioni, numero di sonde, soglia legacy 50x e famiglie geometriche restano invariati. Non si aggiunge un controllo specifico del dominio NURBS: una valutazione non finita è respinta dalla metrica comune.

## Evidenze e limiti

Root riferisce runtime prepatch: piano/linea con gap1, samples=-1/0 => metrica0; samples1/16 =>1. Cerchi nativi con centro DBL_MAX e raggio .5DBL_MAX danno valutazioni non finite, metrica0 ed exactPCurve non null. Caso deliberatamente fuori campo nominale, non un documento CAD pubblico. Root riferisce anche BSpline cubica poli0 e pesi21/29/37/45, dominio1..2/query0..1: W=24t-3 si annulla a.125; baseline fitPCurve ritorna non null/dev0. Query intenzionalmente fuori dominio.

Questi runtime sono del root e non sono stati rieseguiti da questo agente. Qui è stata eseguita soltanto lettura statica, verifica patch/hashes e confronto baseline con repository. Test baseline/candidato e build app sono PENDING a root/tester.

Il precedente rischio nearPole/manual unwrap resta distinto e NON riprodotto come successo pubblico scorretto: nessuna patch né nuovo bug attribuiti a quel sito. La metrica resta un massimo campionato; questo fix non la trasforma in un certificato continuo né certifica Newton/ramo/topologia globali.

## Consultazione architetturale

AGENTS.md, CLAUDE.md, docs/tolerance-policy.md letti; grafo consultato prima della modifica, descrizioni e relazioni call/include/reference in ENTRAMBI i versi (snapshot795nodi/11651archi), conservato in graph-evidence.json. Modifica limitata a fk_pcurve.cpp; nessun modulo o confine pubblico aggiunto.
