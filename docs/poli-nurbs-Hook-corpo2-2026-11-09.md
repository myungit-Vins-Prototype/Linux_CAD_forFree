# Modifica poli NURBS — Hook, Corpo 2 finale

## Riproduzione

Caso dell’utente: `Hook.prt`, Corpo 2, risultato della funzione `Raccordo 15` (indice14),184facce.
L’apertura selezionava tutte le facce ma produceva0righe/0poli. Nessuna → F3 mostrava286219poli;
G0/G1 includevano di nuovo tutte le facce e fallivano. Le prove storiche sulla funzione intermedia
`Cucitura 8` (indice7,8facce) non coprivano questo risultato finale.

Copia congelata `/tmp/forgecad-poles-Hook-input.prt`, originale intatto;
SHA256 `13883a7873687759cc18c108ba3d0786117d4d07f848a58961284cf086c96f64`.

## Cause e correzioni

- F10 cilindrica: trim elicoidali attraverso u=0. La NURBS convertita era non periodica e il fit UV saltava
  fra0 e2π. Estensione periodica esplicita sulle NURBS ottenute da un intero periodo analitico;
  valutazione/jet/isoparametriche/breakpoints coerenti anche oltre il dominio fondamentale.
  Gli estremi esatti restano distinti, così il controllo delle cuciture continua a rilevare aperture.
- F166 sferica: i bordi stavano tutti alla stessa latitudine; il rumore numerico dava una finestra V quasi
  nulla. Inclusi i vertici isolati e usato il dominio analitico finito nei casi degeneri. Anche il bordo
  della calotta attraversa la cucitura. La conversione non modifica i bordi3D o le tolleranze.
- Giunti rifilati incompatibili: prima si fissavano tutti i poli del supporto, fino a superare il budget
  del risolutore. Ora si fissano soltanto le basi B-spline che intersecano box Bezier conservativi dei trim,
  con suddivisione adattiva e deduplicazione. Bordo e primo jet rimangono invariati; l’interno è libero.
- Anche senza spostamenti il risolutore tentava di azzerare le piccole connessioni tolleranti del corpo,
  entrando in conflitto con i poli protetti. In assenza di refit le equazioni agiscono sui delta, conservando
  lo stato neutro e le connessioni dichiarate; la verifica geometrica finale rimane vincolante.
- Il pannello indica “in attesa di modifiche” se manca soltanto un gesto; un errore riporta la causa accanto
  ad Anteprima, senza il solo messaggio generico “non disponibile”.

Persistenza kernel: v3 per body con NURBS periodiche; gli altri restano v2. Lettura v1/v2 conservata,
istanze trasformate e pool includono la periodicità. Nessun cambio dei valori persistiti di SurfaceType.

## Verifiche completate

Kernel36/36 PASS, incluse quattro regressioni aggiunte: trim elicoidali, calotta a latitudine costante,
protezione locale e connessioni tolleranti neutre. Prove di jet periodici/istanze/roundtrip/pool incluse.
Corpo2 completo: conversione184facce, B-rep0errori; F3 griglia193×1483, polo96/741 spostato0,1mm
con G0 e G1 separatamente, B-rep0errori, apertura6,33911e-8mm, angolo3,4967e-6rad.
App completa e runner Qt ricompilati (`forgecad-release/forgecad`, `forgecad_view_tests`).
CTest14/14 PASS: quattro suite poli, superfici, pattern, mapping, fill, precisione e cinque kernel
(supporti/36casi, istanze, derivate, isoparametriche e classificazione UV).

Prova nativa Wayland/OpenGL sul documento congelato, sorgente finale14:
- Apertura:184facce selezionate,2.323.734righe virtuali,3.644poli di display, griglia visibile.
- Nessuna → F3:286.219righe. G0 e G1 reincludono184facce, conservando griglie e stato neutro.
- Polo F3(96,741), +0,1mm: anteprima pronta; ulteriore trascinamento mediante gizmo,
  rilascio registrato, interazione terminata e anteprima aggiornata.
- Conferma, B-rep0errori, Undo/Redo, encode/decode del documento, impostazione G1 persistita
  e rigenerazione della funzione finale senza la sua cache riusciti. Le cache a monte rimangono;
  non è una rigenerazione integrale di tutta la storia di Hook.
- SHA256 dell’originale verificato uguale alla copia dopo la prova.

Sonde/log/screenshot locali in `/tmp/forgecad-poles-Hook-panel/`. Le sonde native sono isolate
nelle impostazioni e non modificano o chiudono l’app dell’utente. Build eseguita sul workspace condiviso;
le modifiche preesistenti di altre chat sono conservate ed escluse dal commit di questa correzione.

Mappa672file/9.702relazioni/329unità,0diagnostiche; `--check`, relazioni in entrambi i versi
ed esame delle sei viste HTML PASS. Chiamate, include e riferimenti restano distinti;
le chiamate inline di BodyIO vanno all’header BSplineSurface, senza attribuire una chiamata diretta
al costruttore attraverso `std::make_shared`. HTML/JSON generati ignorati da Git.

Riaprire ForgeCAD per usare la build aggiornata.

## Limiti

G1 conserva la tangenza esistente; non impone C1 parametrica o G2. I giunti senza una corrispondenza
esatta di righe e pesi restano protetti nella fascia del trim: quei poli non sono deformabili finché
il vincolo è attivo. Controlli geometrici campionati, nessuna certificazione globale di auto-intersezione.
Il refit comune su tutte le184facce può risultare incompatibile sui giunti protetti; il limite resta
vincolante e la griglia originale è utilizzabile senza approssimazione.
