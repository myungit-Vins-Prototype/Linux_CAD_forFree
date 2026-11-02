# Curva proiettata da due schizzi

Funzione sviluppata il 2 novembre 2026 su richiesta dell'utente, con un agente
dedicato e verifica GUI indipendente. Riproduce il principio di **Projected Curve,
Sketch on sketch**: due schizzi su piani differenti definiscono una curva spaziale.
Nell'immagine fornita gli input sono soltanto le curve indicate dalle frecce
rossa/arancione e verde; il risultato è la curva gialla indicata dalla freccia scura.
Le altre curve non devono entrare nel calcolo.

## Uso

Aprire **Curva proiettata**, scegliere **Schizzo su schizzo (curva 3D)** e i due
schizzi. In ciascun elenco spuntare soltanto la curva desiderata; **Scegli curva
nella vista** permette di selezionarla direttamente. Sono disponibili anche
**Tutte** e **Nessuna**. Controllare l'anteprima in ambra e confermare.
Non serve un corpo solido. La precedente modalità **Schizzo su corpo** resta disponibile.

Il risultato è una feature di curva 3D nella storyboard, con dipendenza da entrambi
gli schizzi. Modificare uno dei due input rigenera il risultato. Se un riferimento
scompare il comando segnala errore, senza riagganciarlo a una curva vicina.
Undo/Redo e salvataggio/riapertura conservano gli input selezionati.

## Geometria

La curva è l'intersezione delle superfici generate proiettando ciascuna curva
lungo la normale al proprio piano. Non si sommano punti che hanno arbitrariamente
lo stesso parametro. Il kernel separa le sorgenti in rami monotoni rispetto
all'asse comune ai due piani, calcola gli intervalli condivisi e conserva le
componenti distinte. Sono ammessi anche piani inclinati e traslati non paralleli.

Quando la relazione parametrica lo permette, il risultato è una NURBS esatta.
Il caso generale non è necessariamente rappresentabile con una NURBS finita:
la feature conserva allora una curva implicita esatta, con inversione scalare
monotona ed equazioni/derivate analitiche in double. I campioni servono soltanto
alla visualizzazione; non definiscono la topologia o la geometria.

Le sorgenti e i riferimenti sono salvati nel formato nativo 60 già esistente;
la curva viene ricostruita in apertura. Non sono cambiate enum persistite o
versione del documento. Moduli dedicati: `cad_projected_curve`,
`forgeCad2026_dialogs_projection` e `kernel/fk_sketch_projection`.

## Limiti espliciti

- Piani paralleli, regioni coincidenti non univoche e tangenze comuni non regolari
  producono un errore esplicito. Non vengono scelti rami arbitrari.
- I rami impliciti sono visualizzabili e salvabili nel documento nativo;
  **sweep, conversione in schizzo ed esportazione STEP/IGES non sono disponibili**
  per questi rami. App e writer kernel rifiutano il fit nascosto, anche sotto
  wrapper Trimmed/Transformed. Le NURBS esatte conservano gli usi compatibili.
- Le curve razionali devono avere pesi positivi risolvibili in double. Restano
  attivi i limiti dimensionali del modello; massimo 1024 pezzi di risultato e
  cancellazione nelle operazioni lunghe. Casi troppo vicini a una degenerazione
  numerica vengono rifiutati senza allargare le tolleranze.

## Verifiche

- Build Release completa di applicazione e runner: PASS, Linux GCC 16.2.1 / Qt 6.12.
- Kernel dedicato: **21 gruppi PASS**, inclusi residui sulle due proiezioni,
  derivate, piani inclinati/traslati, rami multipli, curve razionali, domini
  ritagliati, pesi estremi, cancellazione e rifiuto dei casi degeneri.
- Export grezzo STEP/IGES: rifiuto delle implicite dirette, ritagliate e
  trasformate; NURBS esatte ed eliche legacy ancora esportabili.
- ASan/UBSan mirati: **21 gruppi PASS**, strumentati solo il nuovo modulo e
  il suo test; writer e resto della libreria non strumentati. LeakSanitizer
  disabilitato per il limite ambientale ptrace. Nessuna diagnostica sanitizer.
- Test applicativo permanente `forgecad_sketch_projection_tests`: PASS;
  selezione entità 0 con entità 1 esclusa in entrambi gli schizzi, anteprima,
  creazione, Undo/Redo e identità, rigenerazione dopo la modifica di ciascun
  input, codec con/senza cache e riapertura, errori e guardie senza fit.

QA indipendente sulla build comune: **2/2 sessioni native PASS**, Wayland/Intel
P630 Mesa e XCB/Quadro RTX 3000 NVIDIA. Finestra e viewport realmente visibili;
veri clic su curva 0 in entrambi gli schizzi, curva 1 esclusa, spunte off/on,
anteprima e conferma, Undo/Redo, salvataggio e riapertura. Su 21 punti della
curva risultante, residuo massimo rispetto alle due proiezioni delle quadratiche
**8,88e-16**. Nessun errore GL rilevato. Catture ispezionate; nessuna prova macOS.

La sonda iniziale aveva mantenuto autoFit mentre impostava manualmente la camera
e tentava alcuni clic dietro il pannello incorporato. Corretta soltanto la sonda:
camera stabile come dopo uno zoom utente e clic nell’area libera della vista.
Non è stato attribuito a tali errori della sonda un difetto del prodotto.

Copia dimostrativa: `/tmp/forgecad-superfici-fix/Curva-proiettata-due-schizzi.prt`.
Riaprire l’app per usare il nuovo eseguibile.

Suite comune completa in una sola esecuzione: **154 PASS, 1 SKIP, 1 FAIL su 156**,
421,76 s. Unico fallimento già presente prima dell’intervento:
`DraftCurvedAndConcaveSolids`, `kernel/tests/test_draft.cpp:134`, volume
253,5270716475676 contro 253,5270734420086, differenza 1,794441e-6 > 1e-6,
identica al checkpoint precedente. Kernel generale **457/458**. Nessuna tolleranza
modificata. Skip `forgecad_overlay_uniform_tests` per il contesto offscreen;
le prove GL reali sono quelle separate sopra. Nessun nuovo fallimento.

Tre manifest CTest copiati conservano comandi, timeout e proprietà; cambia solo
`FORGECAD_EXAMPLE_DIR`, rivolto alle copie congelate. Hash delle cinque copie e
di quattro originali confermati invariati dopo le prove. Il file originale
`Superfici.prt` presenta invece un salvataggio successivo allo snapshot QA:
la distinzione e il controllo della nuova versione sono nel rapporto estrusione.
Le sonde non scrivono alcun originale; autore del salvataggio non attribuito.
I 29 sorgenti verificati sono registrati nel manifest della build finale.

Mappa rigenerata: **541 file, 7437 relazioni, 255 unità AST, zero errori**.
`--check` PASS; sei confini di chiamata verificati, incluse API nuove e guardie
STEP/IGES. Controllati entrambi i versi dei collegamenti, distinguendo chiamate,
include e riferimenti a funzione. HTML reso in Firefox e ispezionato sul modulo
`cad_projected_curve.cpp`, con entrate e uscite leggibili. HTML/JSON generati
restano ignorati da Git.

[Archivio delle evidenze](history/superfici-curva-proiettata-2026-11-02-verifica.zip):
log/JUnit della singola suite completa, hash build e fonti, verifiche fixture,
sonde e rapporti, sanitizer mirati, collegamenti production, catture native e
controlli della mappa. Le sonde intermedie sono distinte dalla build comune finale.


Semantica confrontata con la [documentazione ufficiale SolidWorks, Projected Curve](https://help.solidworks.com/2024/english/SolidWorks/sldworks/HIDD_DVE_PROJ_SKETCH.htm). La nuova modalità usa entrambe le direzioni normali ai piani. Non aggiunge in questa versione il filtro per semirette positive/invertite.
