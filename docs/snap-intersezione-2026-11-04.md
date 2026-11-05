# Snap di intersezione e riferimenti del risolutore — 4 novembre 2026

## Richiesta e comportamento

Priorita indicata dall'utente: aggiungere l'intersezione agli snap e ai riferimenti associativi degli schizzi. L'indagine sulla suddivisione di facce con tratti coincidenti resta rinviata; nessuna modifica a quel percorso.

Il cursore cerca l'intersezione delle curve esatte dello schizzo e della cache dei riferimenti visibili: segmenti, archi, cerchi, ellissi, spline/NURBS e curve convertite supportate dal kernel. Le sorgenti possono appartenere allo schizzo attivo, a un altro schizzo, a bordi o a riferimenti di piani/assi. Appare l'etichetta **Intersezione**. Lo snap geometrico disattivato disabilita anche questo aggancio.

La selezione usa i domini finiti delle curve, non i prolungamenti o le polilinee di visualizzazione. Le coppie sovrapposte non individuano un'intersezione isolata; i vertici comuni mantengono lo snap Coincidente. L'origine mantiene la propria priorita. La tolleranza del kernel resta 1e-9 mm, come nell'editing esatto degli schizzi; la distanza in pixel seleziona il candidato ma non modifica la precisione geometrica.

Alla creazione si conservano i riferimenti alle due entita e due vincoli PointOnCurve. Non viene salvato un punto fisso ottenuto dal solo incrocio visuale: il risolutore puo seguire il movimento delle sorgenti. Non servono nuovi tipi di vincolo persistiti o un nuovo formato. Il passaggio del mouse non crea link persistenti, Undo o modifiche del documento. La conferma e atomica e riusa il normale Undo; la pulizia gia esistente rimuove le cache quando viene cancellato l'ultimo utilizzo.

Il trascinamento esclude le entita mosse dall'insieme dei candidati. Al rilascio, anche piu rami incidenti si vincolano alle due sorgenti ferme, evitando riferimenti reciproci accidentali fra i rami trascinati.

## Moduli

- `cad_snap.*`: ricerca esatta, coppia locale/esterna e conferma atomica dei link/vincoli.
- `cad_types.h`: nuovo SnapKind non persistito, aggiunto in fondo.
- `forgeCad2026_viewport_selection.cpp`, `forgeCad2026_viewport_sketch.cpp`, `forgeCad2026_viewport_interaction.cpp`: indicatore, inferenze, conferma e trascinamento.
- `tests/test_sketch_intersections.h`, `tests/test_viewport.cpp`, `CMakeLists.txt`: regressioni e CTest dedicato.
- `tools/generate_architecture.py`: descrizioni della mappa.

## Verifiche in questa sessione

Build completa Release dell'app Linux riuscita, inclusa la conservazione dello snap origine e il caso di trascinamento multiplo. Probe collegato agli oggetti correnti dell'app: PASS per intersezioni retta/retta, retta/cerchio su entrambi i rami, cerchio/cerchio, arco limitato e spline; esclusione dei prolungamenti, sovrapposizioni e geometria trascinata.

PASS: due sorgenti associative esterne, modifica di entrambe e nuova soluzione del punto; salvataggio/riapertura; cancellazione dell'entita, pulizia dei due link e Undo/Redo; creazione dal viewport con snap disattivabile; trascinamento di due segmenti incidenti con quattro vincoli alle due sorgenti esterne.

Prova finale Wayland/OpenGL PASS: eventi mouse, etichetta visibile, hover senza persistenza, creazione del punto con due link e due vincoli, Undo/Redo. Framebuffer ispezionato. Runner ufficiale compilato; prima suite 10 PASS/1 FAIL nella fixture storica di visibilita. La fixture fabbricava un payload 57–59 togliendo il solo byte di visibilita dal formato corrente 63: restavano gli ID delle quote aggiunti dal lavoro precedente. Corretto il test usando il codec storico 60 esistente prima della rimozione del byte. Nessuna modifica al lettore di produzione; riesecuzione finale **11/11 CTest PASS**. Mappa **586 file / 8.138 relazioni / 281 TU**, zero diagnostiche; check PASS, 8 confini e collegamenti in entrata/uscita di 6 moduli verificati; overview e 5 dettagli HTML renderizzati e ispezionati.

## Limiti

Il risolutore rappresenta ancora i poligoni regolari come una famiglia circolare e non identifica i singoli lati: tali entita non vengono offerte come nuove intersezioni associative. Le coppie di curve non gestite da `intersectCurves` non producono uno snap inventato; le immagini implicite delle curve proiettate mantengono i limiti del kernel (per esempio, le intersezioni con rette sono supportate). Nessun fit sostitutivo, nessuna modifica agli originali CAD. Verifiche Linux; macOS e CI GitHub non eseguiti in questa sessione.
