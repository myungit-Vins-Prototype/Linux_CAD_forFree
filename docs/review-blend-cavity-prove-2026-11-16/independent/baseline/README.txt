Audit indipendente cavita' — 16 novembre 2026
Scope: sola lettura repo; prove diagnostiche nuove esclusivamente in questo /tmp.
Nessuna modifica produzione, CLAUDE o archivi. Memoria e mappa lette.

ESITO PROVVISORIO SUL PUNTO REALE
La distanza riprodotta su failure.body/F0 e'2.0192233220685673e-5.
Ricerca B-spline con foglie approfondite restituisce esattamente la stessa
proiezione: minimo lower bound numerico delle6foglie terminali2.0189576344280409e-5,
contro budget3.811894136e-7 riferito dal core. Nessun miglioramento sostanziale
perso negli iterati. Questo punto NON dimostra un falso rifiuto del proiettore:
e' forte evidenza numerica di scarto reale dal supporto. Origine dell'edge e3
in indagine dal core, non attribuita a questo audit. Nessuna patch proposta.
I lowerbound usano aritmetica double esistente: non dichiarati certificati
outward/Fraction. Non provano l'intera curva o l'intero raccordo.

ESEGUITO
1) known-point-probe.cpp linkato al kernel baseline congelato:48punti UV noti
   sulle4facce finite originali, massimo residuo1.9131948672901813e-14.
2) trace-probe.cpp+instrumented-surface-algo.cpp: fonte corrente riprodotta
   con sola osservazione initial/minimum/final dentro refineProjection.
   Punto fornito dal core=(-36.852303240670373,9.4929399335520088,48.003251586376884).
   UV=(1.9962021126753973,.73015230958617428), gap2.0192233220685673e-5;
   una leaf depth25,box diag8.7231664217617296e-4,lower0;
   una refine,nessuna perdita best->final.
3) deeper-surface-algo.cpp: SOLO diagnostico, leafDiagonal coeff1e-7 anziche'
   1e-4, linearResolution invariata. Sei leaf depth45,diag8.5187e-7;
   gap/UV identici,max perdita best->final1.1994494752889245e-15.
   Non proposta di cambiare soglia produttiva o tolleranze.
4) Consultate descrizioni/chiamanti/collegamenti mappa:graph-context.json.
   Hash delle fonti lette,della fixture e del kernel registrati in source-hashes.

SOLO STATICO — INVARIANTI NECESSARI PER CORREZIONE GENERALE
- projectPoint(S,p,U,V) deve produrre UV finiti nel dominio richiesto e point
  coerente con S(UV);distance deve essere il residuo fisico reale sullo stesso
  carrier. Niente extrapolazione,riassociazione di faccia o metrica UV in mm.
- Il risultato deve preservare il miglior candidato ammissibile gia' valutato.
  Un passo accettabile entro noise non giustifica perdere un residuo minore.
  fk_surface_algo.cpp190-208 puo' accettare rialzi entro noise e rimpiazzare
  l'iterato;projectGeneric244-245 e leaf466-467 conservano soltanto il finale.
  Rischio statico generale,NON causa dimostrata sul punto reale:vedi misure.
- Residuo di un candidato e' UPPER BOUND della distanza minima. Residuo<=budget
  prova esistenza;residuo>budget da solo NON prova uscita dal dominio. Servirebbe
  lower bound globale>budget oppure diagnosi distinta di ricerca non risolta.
  fk_surface_algo.cpp465-468 termina foglia con un Newton dopo soglia geometrica
  o depth60:non equivale a certificato di ottimo globale per ogni geometria.
- B&B:bound conservativo deve riferirsi a tutto il dominio residuo;unico criterio
  di scarto valido e'lowerbound oltre miglior upperbound (con errore aritmetico
  incluso). Le finestre si intersecano con dominio delle pezze455-457. Pezze
  parziali:bound della pezza intera e' conservativo ma piu' debole,non bug.
- Lowerbounds double,diagonal leaf e Newton sono criteri numerici:non aumentare
  tolleranze per compensarli,non dichiarare certificazione non implementata.
- sampleParameters fk_pcurve.cpp116-117 rifiuta gia' la proiezione globale
  del punto medio se >tol;invertPoint759-763 ha stessa dipendenza come fallback.
  Una migliore proiezione puo' risolvere falsi negativi soltanto se esiste
  davvero un candidato entro lo STESSO budget sullo STESSO carrier.
- computePCurves484+ opera sulle fin mancanti mantenendo fin/edge/face;
  exactPCurve o fitPCurve devono mantenere parametro originale dell'edge e
  S(pcurve(t))≈edge(t),copertura dell'intero range e continuita' del ramo UV.
  Non basta proiettare pochi punti ne' spostare il bordo per far riuscire il fit.
- Il fallback lineare gia' ammette fino50*tolerance e registra pcurveTolerance:
  e' comportamento esistente,non va usato come nuovo espediente per cavita'.
  Il budget della diagnosi finale e' max(pCurveTolerance,edge.tolerance).
-3822 usa il globale come diagnosi 'esce dal dominio';se ottimo non risolto
  quel messaggio e' piu' forte dell'evidenza. Qui ricerca piu' profonda conferma
  invece numericamente lo scarto:nessuna ragione per aggirare il rifiuto.
- Compatibilita':candidati nuovi non devono degradare quelli esistenti;domini
  ristretti,bordi,C0 e superfici singolari restano coperti;no genealogia da
  vicinanza. Cancellazione/esito fallito non devono pubblicare mezzi risultati.
  computePCurves calcola job e rilancia eccezioni prima di assegnare;se restituisce
  missing>0 puo' aver popolato le altre fin:contratto da mantenere al chiamante.

NON ESEGUITO
Fullbuild,UI,CAD modificati,nuove regression permanenti,campagna completa raggi,
fit/conversione dell'edge e3 o prova globale outward della superficie.
I dati delle altre indagini sono esplicitamente riferiti,non test eseguiti qui.
