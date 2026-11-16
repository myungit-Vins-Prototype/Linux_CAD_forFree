# K4 — controllo locale sulla geometria della sezione

Artefatti: `fix.patch`, `fk_sweep.cpp`, `original.cpp`, `original.sha256`,
`tests.snippet.cpp`, `tests.log`, `probe.log`, `positive.log`, `invalid.log`.
Nessuna modifica del repository; preservate K1/K3 dallo snapshot corrente.

## Implementato nello staging

Per un parametro del percorso, q(X)=dot(dX/dv,T) è affine nel punto X della
sezione. Su ciascuna Bezier razionale a pesi positivi il segno è quello del
polinomio Bernstein con coefficienti w_i*q(P_i). Un coefficiente negativo
non dimostra intersezione: si suddivide esattamente il polinomio per de
Casteljau finché i coefficienti provano positività oppure un estremo di
sottotratto fornisce un testimone negativo. Non si campiona una griglia per
accettare la sezione. Un margine di arrotondamento proporzionato alle
operazioni controlla i casi numericamente indecidibili; profondità48 o
contatto indistinguibile da zero provocano errore di verifica non conclusa.
I pesi sono riscalati con fattore positivo per evitare overflow inutile.

Il frame viene valutato una volta per parametro e riusato per le righe del
fit. Il ramo analitico circolare usa lo stesso controllo del funzionale
radiale sul profilo effettivo, sostituendo la preesistente griglia33punti.
La soglia radiale preesistente1e-7*scale è conservata. Nessuna tolleranza
geometrica aumentata o controllo fold disattivato.

## Verificato

- Compilazione isolata fk_sweep.cpp C++17/O2/-ffp-contract=off PASS.
- Riproduttore K4: quattro combinazioni path analitico/NURBS e frame0/45°
  accettate, checkBody0. Volumi analitici3158.27340834859 e
  NURBS3158.27340755945 contro atteso3158.27340834859.
- Probe r11/R10: quattro combinazioni rifiutate per vero fold locale.
- Probe diretto Bernstein: [1,-.1,1], [1,-.999,1], [.01,.01,.01] accettati;
  [1,-2,1] e [-1,2,1] rifiutati per testimone negativo;
  [1,-1,1] rifiutato per contatto non certificabile. Zero fallimenti.
- Runner isolato con object staged e test_sweep.cpp ricompilato:
  **19/19 Sweep PASS**. Include nuovi18validi (scale.1/1/10, posa rigida,
  frame0/45°, path analitico/NURBS, profilo cerchio/NURBS) con massa e checker,
  e12invalidi local-fold. K1, K3 e suite Sweep storica PASS.

## Limiti

Il controllo resta locale ai parametri richiesti dal fit lungo il percorso,
come prima: certifica la sezione continua a quei parametri, non tutto il
prodotto sezione×percorso. Non certifica l'assenza di sovrapposizione globale
K2. Il fit delle superfici resta adattivo con scarti misurati preesistenti.
Nessuna build completa/app/CTest/UI o benchmark globale. Parent deve
integrare patch e snippet, rigenerare mappa e aggiornare memoria condivisa.
