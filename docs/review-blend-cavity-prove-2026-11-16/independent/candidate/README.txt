Audit candidato cavita' — 16 novembre 2026
Sola lettura delle fonti core; file nuovi esclusivamente in questa /tmp.
Candidato frozen:dfad2aa7ca0996210a2882a349d518e9746989ea19b132ed8a459dfe53909ca8.
Esito auditstatico finale:nessun blocco nuovo individuato dopo le guardie.

CONTRATTO MATEMATICO CONTROLLATO STATICAMENTE
Trace risolve S(u,v)-T(a,b)=0 (3componenti) e un quarto vincolo:
- coord S[fixedAxis]=lo+s*dv oppure
- dot(S-from,D)=s*|D|².
Il controllo finale deve ricontrollare entrambi,i4residui,non soltanto S-T.
Il residuo asse normalizzato per|dv| e'invariante a cambio unita'parametrica;
constraintScale=|D|/|dv| lo converte in mm per la funzione merito.
Il residuo corda e'in mm²;constraintScale=1/|D| lo riporta inmm.
Threshold finali del4°vincolo riprendono le soglie done esistenti:
1e-13*|dv| oppure1e-13*scale*|D|. Non tolleranza geometrica aumentata.
Nel modoasse,n.y[fixedAxis] e'ripristinato esattamente al target a ogni passo.
Jacobian quarta riga seleziona correttamente u ov,dy/ds ha RHSdv.
NodeBetween asseU preferisce interpolazione dei parametri,poi continuazione;
non cambia gli oggetti S/T,le loro parametrizzazioni o il parametrofinale[0,1].

BUDGET E ATOMICITA'
Il controllo di fit resta campionato (3punti oppure5nel fallback fisico),non un
nuovo certificato continuo. Ogni span fuori budget deve suddividersi o fallire:
non puo'essere accettato per h<=1e-9. Il budget fit resta max(kFitTolerance,
surfaceError). Validazione fisica richiede gapfinito su entrambi supporti e
sezione richiesta. NaN non deve scomparire per l'ordine di std::max.
Seed stabili mantengono i punti originali:il nuovo prototipo seedsolve che
li poteva spostare e'stato contestato e rimosso dal core. Condizione endpoint
originaria entrofit su S/T;gapfinale include gli scarti degli endpoint.
I tentativi v/corda/stabile/u usano gli stessi carrier/incidenza/modelpoints.
Cut viene assegnato solo al completamento;model.addEdge viene dopo.
BodyHistory dichiarata e'pubblicata solo dopo checkBody al return finale.
API pubblica azzera history OUTPUT all'ingresso (fk_blend.cpp773 ewrapper
blendSurfaceChains4056):su fallimento resta vuota,non si promette preservazione
di una sentinella precedente. Atomicita' e'assenza di risultato parziale/inputmutato.
Catch solamente domain_error:OperationCancelled attraversa i fallback.
ComputePCurves/checkBody/tessellation finali non vengono rimossi o indeboliti.

RISORSE E NONFINITI
Argomenti finiti,range finito nondegenere,axis0/1;risultati solve/y/dy/p/dp
finiti. Tutti gli intermedi confrontati con budget devono essere controllati
prima dei max. I loop solve sono limitati50iterazioni,step damping25circa,
pending20000,continuationWork cumulativo200000;midpoint deve stare tra sa/sb.
Cap di lavoro puo'rifiutare casi validi:non dichiarare impossibilita'geometrica.
Cancellazione controllata nei loop solve,continuation,pending. Non promessa di
interruzione istantanea dentro ogni chiamata projectPoint o evaluate.

ESEGUITO DA QUESTO AGENTE
Prove precedenti in directorypadre:48punti noti,tracedel puntofallente e ricerca
piu'profonda confermano scarto2.019e-5,non falso negativo projectPoint.
check-fraction-input.py qui verifica input/schema/deBoor della prova Fraction
root. fraction-review.txt revisiona matematicamente la separazione globale.
Il traversal Fraction completo e le prove blend/cavita'/UI/fullbuild vengono
eseguiti da root/core/tests,non attribuiti a questo audit statico.
Non nuovi test di implementazione duplicati o patch production da questo agente.

LIMITI
La scelta della coordinata e'locale:se ancheU e'degenere il fallback puo'fallire.
Regolarita',branchtracking e fit continuo globale non certificati da questi
campioni. Il range sV tiene i nodi,non prova inviluppo fra tuttiipunti.
Le iterazioni numeriche trace possono valutare prolungamenti polinomiali;
le validazioni finali sul carrier originale restano necessarie e intatte.
Non genealogia da prossimita',nessun aumento tolerance,nessun cambiofixture.

DOMINIO V DELLA NUOVA DELTA U — RISOLTO STATICAMENTE NEL FREEZE
Nel vecchio tentativo delta lungoV il parametro era fissato nell'intervallo
[w0/w1,1];la nuova scelta asseU lasciaV libero. Misure del CORE nel suo
sv-diagnostic.err:due deltaU r0.5,Vmin.99786900100069675 e.99769448601398103,
Vmax1;vecchiLo.99976826888026438 e.99974867714111126. La curva esce dunque
DAL VECCHIO INTERVALLO DI CONTINUAZIONE,ma non dal dominio intrinseco del
carrierV[0,1]. Richiesta audit del root recepita nel freeze:prima model.addEdge
nuovodeltaU controlla sV in[-1e-6,1+1e-6],identico guardUV esistente per kappa.
Nessuna estensione del supporto o cambio unita'. sV e'nodale:non certificato
continuo fra i nodi e non prova assenza di rami alternativi. CheckBody con
loopCrossings,computePCurves e geometria/tessellazione finali restano necessari.

NOTA DI VERIFICA
I37controlliHelix della fase precedente NON sono prove di questo candidato.
Qui non compilato/ne'eseguito il candidato finale;il codice e'riletto e hashato.
Runtime candidati,regressioni6FK_TEST,cancelinjection,sanitizer efullbuild sono
responsabilita'core/tests/root e non vengono conteggiati come prove mie.
Non emersi altri blocchi statici:limiti espliciti sopra restano validi.
