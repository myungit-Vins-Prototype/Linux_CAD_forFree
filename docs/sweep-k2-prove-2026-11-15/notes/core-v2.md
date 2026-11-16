# Core K2 map-only — v2 separata congelata

v2 sostituisce il core sperimentale `../final/` esclusivamente per correggere
un falso rango da underflow. Non è codice di produzione e non modifica il
repository. Sorgenti/object e SHA256SUMS in questa directory.

## Correzione verificata

Il predicato orient2d del kernel esclude overflow/underflow dal proprio
contratto. Su triangolo {(0,0),(1e-200,0),(0,1e-200)} restituisce0; il core
precedente lo riduceva a segmento, certificando erroneamente la mappa
F(x,y,t)=(x,0,y+t). Riproduzione: `../probe_extreme.cpp`.

La v2 elimina il predicato per la riduzione del dominio: uno/due punti
DISTINTI per uguaglianza delle coordinate autorizzano dimensione0/1;
tre o più punti distinti vengono sempre trattati in dimensione2, anche se
collineari. È una scelta sufficiente conservativa, dichiarata nell'header:
un hull minimale di un segmento deve avere due estremi. Le derivate della
direzione del segmento continuano a usare differenze/divisioni intervallari.

`test_dimension.cpp` / `test_dimension.log`: otto casi PASS, exit0.
- Triangoli della mappa degenere a scala1,1e-200,1e-300: Unresolved.
- Triangolo con coordinate1e200/1e-200: Unresolved.
- Segmenti lungoX a scala1,1e-200,1e-300: CertifiedInjective.
- Profilo punto: CertifiedInjective.
`dimension_regression.inc` è lo snippet per la suite di esperimento.

## Contratto e prove precedenti

Rimane il certificato map-only descritto in `../final/REPORT.md`: mappa
F=C+Ux+Vy razionale a pesi comuni positivi, continuità/seam identiche,
separazione conservativa Bernstein e monotonia forte su chart con slab
intermedi tramite SPD intervallare del Jacobiano precondizionato. Hull
convesso dell'intera sezione; nessun testimone di intersezione emesso dal
solo hull. Duration validata positiva ma riparametrizzazione scelta dal core
tramite una costante positiva per pezza. Nessun campionamento come criterio
di assenza, nessuna adiacenza/diagonale ignorata.

La v2 non cambia il percorso aritmetico dei casi dimensionalmente2 (tre o
più punti non collineari) né dei segmenti/punti: i risultati precedenti sono
prove storiche su quella versione, non sostituiscono i test v2 del parent.
Parent riesegue solo le regressioni pertinenti sull'object congelato v2.

## Limiti residui: K2 resta aperto

- Non trasferisce la prova al BRep finale: residuoC0 del fit non basta,
  serve un ponteC1 o costruzione geometrica coerente.
- Il singolo hull convesso può rifiutare geometrie valide: sheet curva con
  hull2D ma superficie iniettiva, oppure più sezioni disconnesse i cui tubi
  non si toccano ma il riempimento artificiale del hull si sovrappone.
- maxVisits riguarda le visite della validazione, non costo/memoria della
  preparazione. maxVisits0 ritorna prima di costruire il Validator.
  Checkpoint cancellazione a ingresso/per-slab/build/visita, non per ogni
  singola operazione del prodotto Bernstein o del hull. Non è un hard
  resource limit complessivo; una API pubblica richiede limiti totali di
  coefficienti/hull e preparazione.
- Unresolved distingue il fallimento della prova da una intersezione
  dimostrata. Non si autorizza accettazione da un certificato non concluso,
  ma rendere obbligatorio questo criterio limitato romperebbe validi noti:
  pertanto l'esperimento deve restare separato dalla produzione.
