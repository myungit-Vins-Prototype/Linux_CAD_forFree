# Handoff statico — quadrantBreakpoints

Staging esclusivo /tmp/k10-quadrant-review/core. Nessuna modifica repository/header, compilazione, esecuzione dei loop patologici, copia di librerie/ELF, commit o push. Memoria CLAUDE e integrazione delegate al root per il vincolo staging-only.

## Causa e patch minima

Il loop originale incrementava un indice double con k+=1 senza verificarne il progresso e senza cancellation. A fasi abbastanza grandi la spaziatura double supera1: l'indice resta identico mentre la condizione può restare vera. Con estremo -Inf, k resta -Inf. I casi sono raggiungibili tramite Circle/Ellipse2/3::breakpoints; lo stesso helper serve i breakpoint parametrici di superfici periodiche.

Candidata: cancellationPoint all'ingresso e in ogni iterazione; invalid_argument se un estremo è NaN/Inf o lo>hi; dopo ogni iterazione, next=k+1 deve essere finito e strettamente maggiore di k, altrimenti domain_error. Il risultato non viene pubblicato al fallimento. Nessun cast integrale dell'indice, cap arbitrario, modifica della granularità o normalizzazione di fase.

Sono identici l'indice iniziale ceil(lo/kHalfPi), il prodotto k*kHalfPi, la condizione superiore, la tolleranza1e-12*max(1,abs(hi)), il filtro inferiore e l'incremento double+1. Gli intervalli degeneri finiti continuano a restituire due estremi uguali {t,t}. Il controllo progresso avviene soltanto per iterazioni effettivamente richieste dalla condizione esistente; non si aggiungono rifiuti a intervalli già vuoti secondo quella condizione.

## Contratto e limiti

invalid_argument distingue intervalli non validi; domain_error distingue la non rappresentabilità dell'avanzamento numerico. OperationCancelled resta distinto e non viene catturato. Nessun header o firma pubblica cambiato. Un range enorme ma ancora progressivo può richiedere molto lavoro/memoria: resta cancellabile, senza introdurre un nuovo cap non autorizzato. La tolleranza preesistente può eliminare quadranti vicini agli estremi a grandi fasi; NON si promette una partizione esatta/completa globale.

## Verifiche effettuate qui

Letti AGENTS, CLAUDE, tolerance-policy e graph.json prima del codice; descrizioni curve e relazioni call/include/reference in entrambi i versi salvate in graph-evidence.json (797nodi/11666archi). Letti helper, deleganti Circle/Ellipse, contratto Curve::breakpoints, chiamanti Surface/mapping e CancellationScope. Revisione statica del diff, hash e confronto baseline byte-identico al repository. NESSUNA compilazione o prova runtime effettuata da core. Le prove watchdog baseline, i test candidati e la build sono responsabilità del root/tester.

## Riproduttori da verificare separatamente

Circle<2>::breakpoints({1e20,1e20+1e9}) e {-Inf,0}: non eseguire baseline senza watchdog di processo. Candidata attesa domain_error e invalid_argument rispettivamente. Casi ordinari positivi/negativi multi-quadrante e punti esatti ai bordi devono conservare vettori; {t,t} deve conservare duplicati; preset cancellation deve sollevare OperationCancelled anche su range piccolo. Questi sono esiti attesi, non runtime eseguiti da questo agente.
