Audit indipendente K10 globale — finding P2 e verifica statica del candidato

Finding: fk_mapping_fit.cpp baseline f9187aa9..., sonde214–226.
NaN come secondo argomento di std::max(accumulatore, distanza) viene ignorato: acc<NaN è false. Una callback planare tranne f(.125,.125)=NaN può quindi lasciare jet/poli finiti e il ramo done accettabile nonostante una sonda effettivamente valutata non finita. La sonda esatta .125 è binaria; tolleranza1e-6 e griglia{0,1} evitano che il normale arrotondamento planare confonda il witness. Il riproduttore usa la vera API interna detail::fitMappingSurface; non è attribuito a un nuovo modello CAD pubblico.

La costruzione è valida anche con analyticJet vuota: gli stencil iniziali alle quattro estremità usano nodi vicino0/1 (passo.001), quindi non toccano(.125,.125). Con jet analitico planare i poli sono esplicitamente finiti. La successiva BSplineSurface controlla i poli ma non può rilevare il NaN già scartato dal massimo. Baseline-static-audit.json conserva dati e delimitazioni. Nessuna compilazione/esecuzione C++ eseguita da questo audit; il comportamento runtime va attribuito alle prove separate di root/core.

Candidato03d21cea...: nessun blocker statico trovato.
La callback è controllata in tutti i punti d'uso; i jet analitici nei quattro campi; le derivate numeriche e i poli prima delle sonde; ogni distanza è controllata prima del massimo, compreso overflow aritmetico. Validazione preventiva di griglie/tolleranza; formule/sonde/refinement e budget invariati per gli input finiti ordinari. Il costruttore della superficie precede la pubblicazione di deviation, migliorando l'atomicità del fallimento. Non è introdotto un nuovo contratto sull'accumulatore deviation iniziale: i chiamanti produzione esaminati lo inizializzano a0.

Limiti rimasti aperti:
- La callback finita può avere picchi non visti fra sonde: non è una garanzia continua del carrier rispetto alla mappa.
- Il provider continuo E/S/Q certifica la relazione fra bordo e carrier effettivi nella propria famiglia; non certifica l'intera intenzione della mappa, topologia o convergenza Newton.
- L'ispezione Newton non ha provato un ulteriore loop illimitato o successo non finito; non è una prova generale di correttezza.

Mappa consultata prima dell'analisi:791file/11622archi; descrizioni e call/include/reference in entrambi i versi salvati in graph-consult.json. Repository in sola lettura, nessuna UI/build/copia di librerie/commit/push. Fonti sorgenti conservate soltanto come testo inerte; root gestisce registri/documenti condivisi. Nessuna prova dei precedenti pacchetti sigillati è modificata.
