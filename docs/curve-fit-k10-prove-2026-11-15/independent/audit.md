# K10 — revisione indipendente Sol, 15 novembre 2026

## Esito e confine

Audit matematico e sorgente del certificatore su `fk_curve_fit_bounds.cpp` SHA eaaadbb944037473c08a0217b285f6a62e531c8058cccfbc95bc61258c97e284 e core/wrappers offset SHA 7c8a22480b028b1c460e41ef5e9c1c6e49bad1bdba99d6417b3668c3d6560185: nessun nuovo false accept individuato. È stata trovata e riprodotta una regressione del wrapper periodico; la versione finale restringe la correzione alle estrusioni NURBS, senza cambiare i percorsi coniche legacy. Replay finale completato:13/13 geometrie accettate con errore denso <=1e−7. I log pre-seam/broad-wrapper sono intermedi, separati dai log finali.

Il nuovo contratto continuo vale per la curva a distanza sorgente NURBS supportata nelle estrusioni e nei meridiani delle rivoluzioni; non per tutti i chiamanti del vecchio fitter, né per l'intero algoritmo generale di offset superfici. Un callback arbitrario interrogato finitemente non può avere un certificato universale: aggiungere un polinomio che si annulla nei punti interrogati produce la stessa osservazione e un errore arbitrario altrove. `fitCurve` rimane esplicitamente campionato; `fitCurveCertified` richiede un oracle geometrico su ogni campata candidata.

## Verifica della dimostrazione

Sia C=H/W con W>0 e Q il candidato cubico nello stesso parametro. J=H−QW, B=J/W. Il numeratore della normale A è (H'W−HW')×D per estrusione; per rivoluzione è [axis×(H−origin W)]×(H'W−HW'). Il fattore positivo della derivata rispetto al parametro locale della campata si cancella nella normale unitaria.

Verificati il segno e il limite usato: richiedere d(J·A)<0 evita la radice con il ramo opposto. Posti p≥||B|²−d²| e s≥|B×A|/|A|, la componente perpendicolare è ≤s e la componente parallela si razionalizza con denominatore almeno |d|. Quindi |C+dN−Q|≤s+(p+s²)/|d|. Non occorre un limite inferiore di |B|. A inferiore positivo e W inferiore positivo sono obbligatori. Il cattivo condizionamento per d piccolo può rifiutare, mai autorizzare un fit incerto.

L'estrazione polar de Boor usa nodi, poli omogenei e matrici originali; evita la riparametrizzazione delle coniche e la conversione cache in double non outward. Ogni operazione elementare aritmetica allarga con nextafter; prodotti Bernstein, elevazione e de Casteljau propagano gli intervalli. Il fattore binomiale massimo necessario con grado128 rimane entro double (grado prodotto massimo514). Normalizzazione dei pesi comuni, ordine delle trasformazioni annidate, molteplicità dei nodi e restrizione frazionaria delle campate risultano coerenti. Overflow, pesi incerti, normali non separabili da zero, segno incerto o limiti di risorse portano a errore/rifiuto.

Il core controlla finitezza di tolleranza/intervallo/break prima del sort e output callback/cubiche; non pubblica deviation su errore/cancellazione e non accetta più il residuo fino a100tol al limite terminale. Cancellazione prima/dopo callback e oracle, in estrazione/prodotti/raffinamento. Sourcebudget2M e calcolo pesi una volta eliminano il precedente rischio O(n²) della scansione poli per campata.

## Prove reali private

Oggetti compilati soltanto sotto questa directory usando compile_commands del progetto e archivio baseline congelato. Nessuna build condivisa o produzione modificata.

`countercases.cpp`, `fixtures.h`, `generate_cases.py`, `run_cases.py` riproducono 13 geometrie reali. `countercases-baseline.log` usa archivio prima di K10; `countercases-final.log` usa gli oggetti privati certificati finali; `source.sha256` e `final-results.json` ne archiviano gli hash. I massimi sono osservazioni dense (10001 parametri), non la prova del limite continuo. Il certificato è nell'oracle e nel suo argomento matematico.

| Geometria | Errore baseline | Errore certificato finale |
|---|---:|---:|
| BSpline polynomial grado27 | 5.74248e−5 | 3.12614e−8 |
| NURBS grado27 pesi variabili | 5.41622e−5 | 3.84931e−8 |
| stessi pesi ×1e100 | 5.41622e−5 | 3.84931e−8 |
| stessi pesi ×1e−100 | 5.41622e−5 | 3.84931e−8 |
| trasformazione rigida | 5.74248e−5 | 3.12615e−8 |
| scala1000 e distanza1 | 9.59334e−8 | 4.46197e−8 |
| trasformazione affine | 2.87124e−5 | 4.54833e−8 |
| trim annidato parziale | 5.74349e−5 | 3.25826e−8 |
| grado15, campata stretta [.1,.15] fra altre2 | 4.45907e−5 | 4.18575e−8 |
| grado27, dominio[2,2.00001] | 5.74248e−5 | 3.12612e−8 |
| parabola nonclamped con nodo triplo e trim [.03,.7] | 3.41404e−9 | 3.41404e−9 |
| NURBS nonclamped pesi variabili, trim[3.13,4.87] | 9.99899e−8 | 4.88555e−8 |
| NURBS chiusa, finestra seam[2π−.2,2π+.2] | 4.72645e−8 | 4.04144e−8 (regressione corretta) |

Tolleranza1e−7 in tutti i casi. I nove casi difettosi baseline non dipendono da callback iniettati: offsetSurface chiama l'estrusione reale e la sua normale. La scala1000 è un controllo positivo già corretto sulla baseline. Le prove di regolarità rigorose della sorgente polynomial27 sono in `/tmp/forgecad-followup-fix-20261115/independent/source_bounds.py` e `source-bounds.json`: raggio minimo continuo≥14.5706778, dκ≤6.86310e−5, controllo dei veri poli double contro forma fattorizzata indipendente.

`oracle_queries.cpp`/`oracle_queries.log`: 24 asserzioni indipendenti su quattro sottodomini e distanze±.001. Il candidato esatto ha bound≤8.35e−15; il candidato con normale invertita restituisce inf; la traslazione tangenziale1e−4 ha bound≥1.2e−4. Nessuna accettazione tramite perdita del segno dopo quadratura.

## Regressione periodica individuata e correzione

`ExtrusionSurface` considera periodica una NURBS chiusa nonperiodica e ne riporta i parametri nel dominio naturale. Il vecchio wrapper sceglieva la finestra breve seam che supera il dominio raw; il factory certificato correttamente la rifiuta, ma questo introduce il throw su geometria prima funzionante. Il caso usa una vera BSpline razionale chiusa ottenuta da quattro pezzi del cerchio, D=Z, d=.001, tol1e−7.

Correzione concordata e integrata: per la sola curva di estrusione effettivamente NURBS (anche sotto wrapper), se la superficie è periodica e la finestra esce dal dominio naturale, certificare l'intero dominio naturale e conservare il wrapping del risultato chiuso. Nessun fallback campionato. Il fullperiod può rifiutare cuspidi/singolarità fuori trim: limite conservativo esplicito; un adapter che certifichi solo porzioni periodiche richiederebbe altro lavoro.

## Altri chiamanti: matrice di nonregressione

Inventario attuale:24 siti di produzione `fitCurve`, esclusa definizione, in `generic-callers.json`. Due siti offset extrusion/revolution scelgono ora l'oracle per NURBS e conservano fallback legacy per tipi non supportati; restano22 siti generici. Non li dichiariamo certificati.

| Gruppo | Siti generici | Verifica pertinente da eseguire sul runner finale |
|---|---:|---|
| sketch offset e riferimenti |3| test sketch offset/riferimenti con spline, trim e Undo |
| mapping superfici/quilt e poli |5| SurfaceMapping, Quilt, SurfacePoles/Conversion con UV curvi |
| proiezione |1| Project, curva su cilindro chiusa e bordi curvi |
| exchange |1| Exchange/STEP/IGES roundtrip curve fitted |
| riempimento e boundary |2| Fill, Boundary, curve chiuse e sghembe |
| blend superficie/mitre |2| Blend, cavity/mitre e continuazioni |
| sheet |3| Sheet estensione, sezione e parallele |
| thicken |2| Thicken BSpline e bordi tra supporti |
| offset curva/pcurve |3| Offset, Shell e facce con UV fitted |

Il coordinatore possiede l’esecuzione finale dei gruppi; questo report non attesta l'esecuzione di ogni riga della matrice. Il cambiamento comune da sorvegliare è il rifiuto terminale rigoroso (errore>tol) e degli input non finiti; gli output validi già convergenti mantengono il contratto sampled.

## Assunzioni e limiti della certificazione

Gli input double finiti (poli/pesi/nodi/matrici) sono trattati come numeri reali esatti; i limiti riguardano la geometria definita da tali input. L’enclosure outward presuppone aritmetica IEEE754 double ordinaria, operazioni e sqrt correttamente arrotondate, nessun fast-math né flush-to-zero. Il comando reale di compilazione usa -ffp-contract=off, senza fast-math. Le misure dense includono invece la valutazione numerica reale del kernel e non sostituiscono la dimostrazione. Il controllo non prova assenza di auto-intersezioni globali, non certifica la pcurve generica o gli offset BSplineSurface, e non promette convergenza per input degeneri o molto mal condizionati.

Le coniche analitiche non vengono convertite al parametro NURBS per certificarle: i parametri non coincidono. Le costruzioni esatte già autorizzate geometricamente rimangono; gli altri tipi non supportati mantengono il contratto campionato esplicito. I pesi devono essere positivi; se i limiti outward non separano denominatore/normale da zero il fit fallisce in modo conservativo.

## Deviation e rivoluzione affine

`deviation.cpp`/`deviation-final.log` controllano esplicitamente il confronto bound pubblicato >= errore denso (10001 parametri), oltre a bound<=tol. Quattro fit passano: polynomial27, rational27, campata stretta15, meridiano razionale trasformato affine con asse obliquo e origine non nulla. I bound rispettivi sono7.68327e−8,8.97247e−8,7.05041e−8,9.50584e−8; gli errori osservati3.12614e−8,3.84931e−8,3.96140e−8,4.19309e−8. Anche il vero offsetSurface della rivoluzione affine passa una griglia101×101UV con errore3.56344e−8. I numeri del replay finale vengono conservati nel log.

## Riferimenti al sorgente finale e ripetizione

- fk_curve_fit_bounds.cpp:28 aritmetica outward;66 prodotto Bernstein;194 estrazione blossom;234 factory e limiti;302 oracle;337 segno;344 limite finale.
- fk_offset.cpp:1179 core comune;1258 API certificata;1338 wrapper seam ristretto NURBS;1354/1377 selezione oracle nei due percorsi.
- fk_offset.h:50 documenta il confine del fit campionato. API certificata in fk_curve_fit_bounds.h.

Per ripetere i13 casi nel progetto corrente: eseguire `python3 /tmp/forgecad-k10-20261115/independent/generate_cases.py`, poi `python3 /tmp/forgecad-k10-20261115/independent/run_cases.py`. Il secondo compila due oggetti privati dai sorgenti attuali e usa per la baseline l’archivio congelato in questa directory; non avvia la build condivisa. Le versioni authoritative di questa consegna restano i log finali e gli hash archiviati. `REVIEW.md`, `deviation.log`, i log pre-seam e broad-wrapper descrivono passaggi intermedi.
