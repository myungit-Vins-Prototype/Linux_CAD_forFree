# K10 thicken: pareti su carrier Plane

Core privato congelato in final/. Il parent gestisce integrazione, CMake, grafo e documentazione condivisa. Nessuna build condivisa o modifica produzione eseguita dall'implementatore.

## Causa e prova

thickenAlongNormal fittava entrambe le curve della parete in cubiche polinomiali. La ruled surface hardcode grado3 e senza pesi era coerente solo con tale approssimazione, ma il fit poteva ignorare completamente un rilievo originale fra i parametri campionati. CheckBody e cucitura non rilevavano il difetto.

Probe pubblico su Plane, bordo originale NURBS grado27, offset .1/tol1e-6:
- Polinomiale: successo/solido/checkBody0 ma errore geometrico parete 5.74247506e-5.
- Razionale: successo/solido/checkBody0 ma errore 5.41621908e-5.
- Controcontrollo lungo direzione: errore 2.361894e-16 e 1.481673e-17 sugli stessi input.

## Patch minima

Dopo lo shortcut esistente Line, solo per carrier strutturale Plane e curve gestite da transformCurve, si costruiscono due traduzioni della curva ORIGINALE e una ExtrusionSurface dalla curva inferiore. La normale è zDir originale firmata con face.sense, senza ulteriore normalizzazione dello spostamento. Nessun nuovo provider, nessuna ricostruzione tensoriale o inserzione di nodi. Grado, nodi, pesi e parametro della generatrice restano originali, compresi Trimmed/Transformed.

OffsetFaces/caps, gestione pieghe, ordine upper/lower, genealogia e sewing restano invariati. Nessun aumento di tolleranza. Le nuove pareti non aggiornano wallAccuracy perché non effettuano fit; eventuali contributi degli altri rami restano invariati. Il parametro longitudinale della parete è ora quello fisico dell'ExtrusionSurface (il costruttore normalizza la direzione del carrier); non si deve assumere il vecchio Vnormalizzato0..1. Le SPcurve finali sono ricostruite dalla pipeline esistente.

Other anche annidato in Trimmed/Transformed conserva esplicitamente il percorso legacy. Il ramo nonPlane resta invariato. Le coniche note sono gestite dalla trasformazione già esistente. La forma è esatta per costruzione nella rappresentazione geometrica; rimane normale roundoff delle trasformazioni e del carrier esistente. Non è un nuovo certificatore intervallare di tutte le fasi offset/sew.

Il contratto history legacy viene mantenuto: thickenSheet la azzera all'ingresso; non promette preservazione del valore precedente in caso di errore/cancellazione. La nuova selezione dei wrapper ha checkpoint cooperativi e non recupera OperationCancelled.

## Prove private

build.py riusa compile_commands.json. Fonte produttiva nuova compilata senza warning. Le TU legacy test_thicken/test_history sono ricompilate privatamente sui header correnti (gli oggetti generici preesistenti avevano ABI datata di BSplineSurface); main/resto archivio corrente riutilizzati.

- legacy-thicken.log: 6/6 PASS.
- legacy-history.log: 38/38 PASS.
- probe-fixed.log: due casi corretti normal (poly/rational) e due controcontrolli direction, tutti PASS; errore stesso parametro0, distanza geometrica 2.361894e-16/1.481673e-17, solid1/checkBody0/source immutabile. I due fit erano siti della stessa causa, non due findings indipendenti.
- Agente test: 6/6 gruppi PASS (31 varianti), inclusi 6repro3side +12pose/scale/sense +4trim/wrapper/fori/codec +direzione/invalid/cancel/historyreset, closedNURBS circle e Circle angolare/splitwall. ActualPCurve dense<=1e-9. Test freeze f8864efc84c721d03f89cd5b58de146b1bddb971655ebff487aecfca5aa5ceb1. Sanitizer a cura proprietario tests.
- Reviewer indipendente: baseline24varianti e replay definitivo archiviati in ../independent.

Fonte, hash before/final e patch: final/manifest.json e final/changes.patch. Snapshot e grafo consultato archiviati prima della modifica.
