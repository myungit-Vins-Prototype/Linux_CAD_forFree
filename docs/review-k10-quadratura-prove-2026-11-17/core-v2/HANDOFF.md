# Quadratura V2 — componenti che guidano il raffinamento esplicite

V1 NON è consegnabile da sola: il root ha individuato in fk_mass.cpp520 l'uso intenzionale di HUGE_VAL come magnitude per le componenti11..13 di avvolgimento, documentate come non rilevanti al raffinamento. Il nuovo guard V1 rifiuterebbe massa legacy. Si mantiene intatto il sigillo V1 per provenienza, NON come esito finale del lotto.

V2 vive soltanto in /tmp/k10-quadrature-review/core/v2; nessuna scrittura repository/compilazione/esecuzione C++ o Git da core. Letti sorgente mass, descrizione e archi call/include/reference entrambi versi prima della patch; il grafo completo è del root e qui è conservato solo riepilogo compatto.

Delta incrementalV1: integrateVector e adaptivePanel ricevono controlled=N opzionale. Controllano controlled<=N PRIMA di ogni callback/pannello nuovo. Soltanto le prime controlled componenti partecipano ai confronti tolleranza/roundoff e noise; controlled si propaga ai due rami ricorsivi. Tutte N restano campionate, integrate e sottoposte ai guard di finitezza per value/magnitude/error e somme. controlled0 è lecito: primo pannello finito accettato senza controllo dell'errore. Il defaultN conserva i chiamanti. L'overload vettoriale adaptiveIntegral non cambia firma: rimane defaultN e non riceve un controlled arbitrario.

Mass: le ultime tre magnitude rimangono a zero dal default di Evaluation; niente Inf. La sola chiamata integrateVector del loop passa kFaceValues=11 su kLoopValues=14. Le prime11grandezze fisiche continuano a guidare il raffinamento; le tre componenti di conteggio vengono accumulate sugli stessi pannelli, senza promettere una nuova accuratezza autonoma. Altri consumatori e altri callmass restano invariati.

Questa rappresenta esplicitamente il significato della vecchia sentinella, senza accettare Inf generici né aggirare i guard per componenti passive. Resta la medesima quadratura stimata, non una prova continua/globale; restano i limiti V1 su weighted-underflow finito e normalizzazione di scale miste. Tolleranze/cap/profondità30/campioni15 no-replay invariati.

Parent gestisce integrazione, compiler flags dei consumatori e build/test/SAN/mappa/memoria. Qui solo review statica e verifica hashes: runtimecandidato PENDING root/tester. Diff v1-to-v2.patch è incrementale rispetto header048c8007 e mass sorgente corrente congelata. Pacchetto V1 e sorgenteV1 sono stati verificati immutati.
