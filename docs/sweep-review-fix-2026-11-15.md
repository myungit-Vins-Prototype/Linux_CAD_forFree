# Correzioni sweep K1/K3 — 15 novembre 2026

**Implementato e verificato:** corretti K1 e K3 della [review](code-review-2026-11-15.md).
**Aperto:** K2 (autointersezioni globali); K4 resta riproducibile e non è stato modificato.
Due agenti **GPT-6.1 Sol**, con revisione incrociata e integrazione del coordinatore, hanno lavorato
sul frame e sulla cucitura; l'agente del frame ha poi analizzato K2. Nessun modello diverso usato dagli agenti.

## Modifiche

- `kernel/fk_sweep.cpp`: piano/retta riconosciuti dal supporto affine della rappresentazione
  (rette, coniche, poli NURBS, trim/trasformazioni), senza griglie uniformi di classificazione.
  Le curve senza certificato disponibile usano il trasporto Bishop adattivo già presente.
  Proiezione e normalizzazione con getti mantengono ortonormale il riferimento e le sue derivate.
- Le facce tensoriali ricevono SP-curve esplicite: longitudinali u=costante e trasversali con
  v fissato al bordo corretto. Il parametro u(t) è conservato anche quando non è affine,
  come per cerchi convertiti in NURBS. Due fin della stessa cucitura mantengono v.lo/v.hi distinti.
- `kernel/fk_mass.cpp`: quando la SP-curve è troppo approssimata per l'integrazione diretta,
  resta la stima iniziale dell'inversione, con srotolamento periodico. La successiva inversione
  mantiene accuratezza e soglie precedenti, senza perdere il ramo della cucitura.
- `kernel/tests/test_sweep.cpp`: eliche 15/16/17 giri, ultima con posa rigida; massa e area;
  spline spaziale chiusa con cerchio/quadrato, intera/divisa, continuità UV e due usi della seam;
  fallback massa forzato su una copia del body e controllo di una spline aperta.
- Descrizioni architetturali in `tools/generate_architecture.py` aggiornate.

Nessun aumento delle tolleranze, modifica delle enum persistite o suddivisione forzata del percorso.
Le modifiche locali precedenti a sweep/solver/cache/test sono preservate.

## Riproduttori sulla build finale

| Caso | Prima | Dopo | Riferimento |
| --- | --- | --- | --- |
| K1, elica R4/passo1/16giri, r0,2 | V=25,3261477033 | V=50,5723586571 | Atteso 50,5723587147 |
| K3, spline chiusa in un tratto | V=0; A=0 | V=0,822445219580; A=16,4489043843 | Atteso V=0,822445218851 |
| K3, stessa spline in due tratti | V=0,822445219580 | V=0,822445219580 | Stessa area del tratto intero |
| K2, spire sovrapposte | V=19,7417086215 > box12,6 | Identico: ancora aperto | Checker ancora senza errori |
| K4, cerchio equivalente con frame ruotato | Rifiuto nel caso NURBS/45° | Identico: ancora aperto | Tre altre rappresentazioni riescono |

Per K1/K3 checker senza errori; sul K3 tessellazione con zero facce fallite e SP-curve
stampate con loop UV chiusi. Le quantità vengono calcolate sul B-rep, non sulla mesh.
L'agente K1 ha anche misurato l'ortonormalità in 514 parametri: massimo errore
0,999205 prima, 3,33e-16 dopo (prova isolata, non certificato globale).

## Verifiche effettive

- Build completa `cmake --build forgecad-release --parallel 4`: PASS, app e runner aggiornati.
  Presenti avvisi di inizializzatori/deprecazioni in test non modificati; nessun errore.
- **42 test kernel distinti PASS**, 43 esecuzioni: Sweep18, Mass11, PCurve11,
  FaceAreas1, Quadrature1, Revolve1. `MassWithAndWithoutPCurves` compare in due filtri.
- **16/16 CTest PASS**, zero skip: storia, riferimenti sweep, ripetizioni, estrusione,
  rivoluzione, superfici, documenti/precisione, istanze/parametrizzazione e isolinee NURBS.
  Ambiente XDG isolato e Qt offscreen; elenco completo nelle prove.
- Cinque riproduttori della review ricompilati contro la libreria finale: alias,
  chiusura, stampa seam, sovrapposizione e rappresentazione del cerchio.
- Architettura: **724 file, 10.414 relazioni, 353 unità C++**, zero diagnostica;
  `--check` PASS, descrizioni e relazioni in entrata/uscita controllate, HTML e simboli verificati.
- `git diff --check` PASS. Hash confrontati con l'inizio: solo i cinque file versionati previsti
  sono cambiati durante questo intervento. Nessun CAD originale modificato, nessun processo utente chiuso.

[Log, sorgenti delle prove, hash e analisi degli agenti](sweep-review-prove-2026-11-15/README.md).
L'eseguibile su disco è aggiornato; riaprire l'app per usarlo. Nessuna nuova verifica del renderer
OpenGL, macOS/Windows o CUDA. Non eseguita l'intera suite generale in questo intervento.

## K2 e lavoro rimanente

Il controllo locale di curvatura non rileva sovrapposizioni fra sezioni lontane.
Il marching attuale supporta anche B-spline (il commento dell'header è obsoleto), ma può perdere
componenti piccole: un risultato vuoto non certifica disgiunzione. Collegarlo allo sweep senza
questo distinguo lascerebbe un criterio di accettazione insufficiente.

Serve un validatore geometrico dedicato con bound conservativi sulle pezze razionali,
gestione della diagonale/adiacenze e degli interni rifilati, e risultati distinti fra disgiunzione
certificata, intersezione provata e verifica non conclusa. L'assenza di intersezioni fra sole facce
non basta senza i prerequisiti di iniettività/molteplicità del volume.
[Analisi concreta, componenti riusabili e regressioni necessarie](sweep-review-prove-2026-11-15/K2-analisi.md).
Questa strategia è documentata, **non implementata**. Il caso K2 continua a essere accettato erroneamente.

Dopo M1/M2 e ora K1/K3, **15 dei 19 rilievi originali restano aperti**. K4/K5, solver,
pannelli, stili, anteprime e candidati orfani conservano lo stato della review precedente.
Il fit e la misura dello scarto delle SP-curve usano i controlli adattivi/campionati esistenti;
queste correzioni non certificano l'assenza globale di autointersezioni.
