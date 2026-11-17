Audit indipendente arcParameter / distanza fisica di extendSheet

CandidatoV2 d584e6cf0622bcb59f8a4f57b19230279ab7fd7b7509a48b39a2b3e28ff8df76: nessun blocker statico. Questo observer non compila né esegue C++; root possiede i runtime. Solo algebra Fraction sui coefficienti originali e letture/salvataggi testuali.

Contratto pubblico: kernel/fk_sheet.h32–42 dichiara distanza lungo la superficie, e per BSpline lungo l'iso trasversale al punto medio del bordo. L'interfaccia dice Distanza lungo la superficie. Nessun extendSheetMode in HEAD: bool linear=false continua il supporto/polinomio, true costruisce la striscia tangente dove prevista. La richiesta non è un incremento UV. Non è però esposta una tolleranza numerica finale dedicata alla distanza.

BUG pubblico riprodotto da ROOT con geometria nativa/coordinate ordinarie:
- Cubica x=(u−2)^3, originaria[-1,1] con poliX[-27,-9,-3,-1], estrusioneZheight1, bordo u1 selezionato per costruzione. Richiesta3: input/outputcheckBody0, API SUCCESS con uHi2 e emittedArc1.0000000000000013. Oracolo Fraction indipendente: source/reach[-1,3]p[-27,9,-3,1] sono identicamente x=(u−2)^3; speed(1)=3, initialu2, speed(2)=0, arc1..2=1 ESATTAMENTE, deficit2. La singolarità parametrica finale non veniva rifiutata dal B-rep pubblico.
- Variante REGOLARE y=2^-36*u: ROOT ottiene input/outputcheckBody0, SUCCESS uHi7.6203140591838299, emittedArc178.53408777564366 contro3, coordinate maxX177.534... . Conferma che il problema non riguarda soltanto un endpoint a derivata nulla. Questi valori sono runtime ROOT, non prove eseguite da questo observer.

Correzione verificata staticamente: gate finale su finite/direzione/lunghezza fisica usando tolerance1e-9*scale giàcalcolata sull'input e passata ai2call. È un controllo nuovo del successo, non un cap che il vecchio helper già verificava. La quadratura1e-13 è distinta e invariata. Formula Newton60 e stop UV restano; nessuna ricerca nuova o aumento budget. Dopo ogni arresto il parametro finale viene effettivamente misurato, oppure si solleva domain_error. Larghezza finita controllata prima di ogni quadratura; direzione/delta prima del finale.

Atomicità: topologia input const/copied by value, modifiche su body/model locali. History baseline giàazzerata ALL'INGRESSO: su failure va documentata vuota, NON sentinella precedente immutata. Nuova history pubblicata solo dopo i controlli finali, niente nuova pubblicazione parziale.

Limiti: arcLength è quadratura adattiva con errore stimato, non certificato intervallare continuo. Il gate misura reach helper, non prova separatamente ogni coefficiente del carrier ricostruito/unificato. La correzione è un rifiuto conservativo delle soluzioni non raggiunte; non è un nuovo solver né prova universale di convergenza. CheckBody/fin residual non certificano la distanza di costruzione. Oracolo dei coefficienti di reextension attesi non è un export indipendente dell'array finale effettivo. Cancellazione/nonfinite dentro quadratura restano invariate.

Graph799/11681, descrizioni e call/include/reference typed entrambi versi inclusi. Repo in sola lettura, no archive copy/UI/spawn/commit/push. Parent gestisce CLAUDE/documenti e prove seriali. Nessun sigillo precedente mutato.
