# Riduzione della duplicazione geometrica negli specchi — 3 novembre 2026

Stato: implementato, verificato e pubblicato su origin/main in `3c644e2`. App e runner compilati.
Le misure seguenti precedono la successiva [riduzione delle griglie loft e dello STEP](riduzione-griglie-loft-step-2026-11-04.md).
Richiesta: continuare la riduzione della complessita', evitando di ricopiare la definizione delle superfici specchiate.

## Misura iniziale

Il corpo Raccordo25 del modello SurfaceModellingExample219 contiene 6.454.452 poli NURBS.
F6/F9 hanno grado33x5 e 2311x706 poli ciascuna; F7/F10 grado34x5 e 2177x726.
Queste quattro facce rappresentano il99,53% dei poli. Le due meta' derivano da una simmetria,
ma il trasformatore materializzava tutti i poli in nuove griglie. I dati numerici dei poli/pesi
occupano206.542.464byte; B-rep207.952.422byte. Definizioni complete e verifiche precedenti:
[svuotamento](svuotamento-f0-f14-2026-11-03.md).

La copia congelata prima dello svuotamento e'148.355.436byte; STEP745.117.543byte.
L'originale corrente contiene anche Svuota1 (26 feature) ed e'190.498.981byte.
Originale e copia congelata corrente hanno lo stesso SHA2568aaae87956e4c19818a6a0447646b615c196721bcca441e82444e6880e53498b: nessuna scrittura sull'originale.

## Implementazione

- `BSplineSurface` puo' essere un'istanza immutabile con base condivisa e similitudine.
  Conserva tipo, gradi, nodi e UV. I poli sono restituiti trasformati; i jet vengono valutati
  nella base e trasformati come punto/vettori. Nessun fit o riduzione delle tolleranze.
- Le trasformazioni successive si compongono e si appiattiscono su una sola base.
  Sono condivisi anche i vettori dei nodi e i poli delle pezze Bezier tramite alias con proprieta' condivisa.
  I box restano nelle coordinate dell'istanza. Topologia, orientamento e genealogia restano distinti.
- B-rep binario v2: definizione della base una volta, poi riferimento gia' definito e matrice/traslazione.
  Lettura legacyv1 conservata; indici errati, basi non NURBS e trasformazioni non finite/non uniformi rifiutati.
  La cache semantica10 rigenera i documenti lavorativi con Pattern/Transform precedenti;
  i check-in storici conservano i propri snapshot.
- STEP: `SURFACE_REPLICA` e `CARTESIAN_TRANSFORMATION_OPERATOR_3D`; una sola griglia per base.
  In scrittura le repliche compatte sono limitate ai moti rigidi diretti. Specchi e scale
  esportano i poli esatti in coordinate mondo per compatibilita' dei lettori.
  Il nostro lettore conserva anche determinante negativo e scala delle repliche standard.
  Riferimento primario: [ISO10303-42, superficie replica e trasformazioni](https://steptools.com/stds/smrl/data/resource_docs/geometric_and_topological_representation/sys/4_schema.htm).
  La lettura di repliche e' per ora limitata alle basi NURBS; le altre basi producono una diagnostica esplicita.

## Verifiche consolidate

Runner autonomo `forgekernel_surface_instances_tests`:8/8 PASS: jet fino all'ordine4,
isoparametriche, normali riflesse, doppio specchio, immutabilita', condivisione delle pezze,
box/proiezione, roundtrip binario/legacy e record malformati, STEP AP203/AP214/AP242 riletto dal kernel.
Trasformazioni10/10 e BodyIO2/2 PASS. STEP con moto diretto letto da OCCT, specchio materializzato
letto da OCCT in AP203/AP214/AP242 PASS; integrazione dei volumi alla precisione1e-9.

Prima prova nativa Wayland sul file reale rigenerato:
- PRT con cache148.355.436 →74.700.795byte (-49,65%).
- B24 Raccordo25 B-rep207.952.422 →104.833.703byte (-49,59%).
- Poli logici6.454.452; poli effettivamente memorizzati3.233.661, tre istanze.
- Cache cumulativa932.059.157byte; lettura accettata con il budget standard1GiB.
- Confronto delle superfici NURBS con il B-rep baseline: punto e seno angolare0 nei campioni
  interni7x7 per faccia. Topologia invariata. Nessuna modifica del CAD originale.
- Rigenerazione30,5s; anteprima svuotamento pronta270,1s totali, conferma270,6s, Undo/Redo271,1s.
- Roundtrip con cache e senza cache PASS; il secondo rigenera anche lo svuotamento (503,5s totali).
  Build e regressioni erano concorrenti: questi tempi non sono un benchmark comparativo.

Ultima versione corrente con Svuota1:
- PRT190.498.981 →116.868.166byte (-38,65%), conservando tutte le26 feature, nomi e featureId.
- Risultato64facce,31pareti interne,662campioni: errore massimo di spessore5,8343983143896764e-8mm.
  F0 e F14 aperte, genealogia e mesh verificate.
- Tutti i poli, pesi, gradi e nodi NURBS del risultato restano identici dopo la riapertura.
  Un confronto binario assoluto iniziale era troppo restrittivo per il contratto BodyIO: isolato un solo
  bit nell'asseY di un cerchio, rinormalizzato dal costruttore come documentato. La topologia e tutti
  gli altri record erano identici; confronto geometrico finale dei bordi: scarto0 nei campioni.
- La cache cumulativa con lo svuotamento e'1.082.774.364byte: supera ancora di9.032.540byte il limite
  standard1GiB, quindi la lettura standard conserva le definizioni e rigenera il modello.
  La verifica dello snapshot completo usa4GiB soltanto nella sonda QA; nessun limite produttivo aumentato.
- Copia consegnabile: `forgecad-release/complexity-reduction-qa/SurfaceModellingExample219-ridotto.prt`.
  La sonda finale rilegge lo snapshot in9,2s, con budget QA4GiB; non e' il tempo di apertura standard.

App completa e runner ricompilati; verifiche: istanze8/8, B-spline7/7, trasformazioni10/10,
BodyIO2/2, STEP22/22, Loft42/42, Blend60/60, Shell18/18 PASS. I nuovi controlli delle guide
hanno anche10/10CTest e QA XCB/Wayland PASS: [rapporto](loft-controlli-guide-2026-11-04.md).
Log, sorgenti delle sonde e immagini sono nella cartella QA ignorata da Git.
La mappa architetturale e' stata aggiornata e renderizzata; conteggi e controllo finale nel relativo log QA.

L'encoding dell'operatore STEP e' stato verificato anche contro il lettore esterno:
nei protocolli AP occorrono8 attributi ereditati (i6 geometrici piu' name/description).
Il lettore accetta entrambe le varianti. I sorgenti OCCT7.x usano un sistema destrorso e
non applicano la scala nella conversione della replica: per questo non abilitiamo tali
repliche nell'esportazione degli specchi. [Lettore pubblico OCCT](https://github.com/Open-Cascade-SAS/OCCT/blob/V7_8_0/src/StepToGeom/StepToGeom.cxx).
Lo STEP del modello specchiato conserva quindi per ora le griglie esplicite; questa fase
riduce soprattutto RAM e cache PRT, senza dichiarare dimezzato lo STEP.

## Limite distinto

La condivisione elimina i dati duplicati, ma non riduce il grado o il numero di poli delle loft originali13/15.
La causa a monte e' il prodotto dei denominatori razionali dopo la normalizzazione dei parametri,
combinato con la griglia comune molto fitta del fit delle guide. Richiede un intervento geometrico
separato e verificato su sezioni, guide e continuita'; non vengono sostituite superfici con mesh o fit grossolani.

Un passo successivo possibile e' condividere anche gli offset di appoggi specchiati e le geometrie
immutabili tra stati della storia. Richiede chiavi esatte per distanza, verso, dominio e tolleranza,
con nuove verifiche geometriche; non e' stato introdotto implicitamente in questo intervento.
