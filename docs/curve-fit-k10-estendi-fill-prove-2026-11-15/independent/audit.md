# K10 successivo — audit indipendente Sol

15 novembre 2026. Sola lettura di produzione e build condivisa; tutte le prove sotto questa directory. Consultati AGENTS.md, CLAUDE.md e descrizioni/collegamenti del grafo per sheet, fill, boundary e oracle.

## Esito delle migrazioni

Nessun rilievo bloccante sul sorgente finale. Manifest completo in `manifest.json`; SHA principali:

| Sorgente | SHA256 |
|---|---|
| fk_sheet.cpp | fdf2810ae1777d376fd80719e9fa8210cdfda810e450c706f6e05963129f098c |
| fk_fill.cpp | 5fdf7cefbdd80f0d7baf2f1b5bf53bca295c55eee145ae09cea376fcbc371a3d |
| fk_curve_projection.cpp | 3ac3894a7994099e54df297417dc7cf7d7c77e253f6dbee3f18817051a886dda |
| fk_curve_projection.h | d7dcf237bae4926b4478d10c9ed0f89cfaa17f6f57d78def863b98f1515aad9c |

### Sheet: orientamento, parametro e degenerazione

Nei due siti fk_sheet.cpp:215 e596 il callback esistente è esattamente C+signedDistance*N con N=normalize(C'×planeNormal) e signedDistance=length*traversal*orientation. Il segno della fin sceglie il lato, mentre il parametro della curva resta quello canonico dell’edge. Non c’è un doppio ribaltamento asse/distanza. PlaneNormal è unitario dal frame; il factory usa lo stesso numeratore della normale. Una normale degenere/incerta sulle NURBS non diventa un successo: l’oracle resta inconcludente e il core rifiuta al limite di raffinamento. Tipi non supportati continuano al fallback sampled esplicito. Il terzo fit della cucitura tra supporti resta generico, non è dichiarato certificato.

Prova indipendente `sheet_hole.cpp`/`sheet_hole.log`:8/8 combinazioni single/multiface × verso canonico inverso × face.sense. Il bordo scelto è su un foro interno, con curva NURBS razionale cubica su piano ruotato e traslato. Il bordo esteso va verso l’interno del foro (riduce il foro), come richiesto dall’esterno della faccia. Le estremità coincidono con l’offset atteso; massimo errore osservato8.32476e−8. Entrambi i rami sono percorsi. Questa prova è aggiuntiva ai12 casi permanenti dell’implementer (8 orientamenti/pose+4hidden27/scala), il cui esito è riferito nel suo report.

### Proiezione: parametro, famiglie e persistenza

fk_curve_projection.cpp:15 proietta i poli B-spline, conserva grado/nodi/pesi, e costruisce una curva2 già serializzabile. Il prodotto transform*moved.transform():42 compone correttamente gli involucri dall’esterno verso l’interno, incluse shear e matrici non similarity. La proiezione rank2 o completamente collassata è ammessa: non viene rinormalizzata una direzione 2D e non è introdotta una premessa di regolarità non necessaria. Le rette diventano BSpline1 su intervallo finito con gli stessi parametri degli endpoint.

L’audit ha individuato e fatto correggere il caso Trimmed(Line,[0,10]) richiesto su [2,4]: costruire la base lineare soltanto sul range richiesto e ricostruire il trim[0,10] avrebbe lanciato estremi fuori dominio. Il ramoTrimmed:32–38 ora proietta la base sull’intero dominio del trim, necessario anche per trim annidati.

Verifica indipendente sulla versione finale: `projection.cpp`/`projection-final.log`,4/4 casi TrimmedLine, nestedTrimmedLine, affine shear e successiva rotazione/trim; range richiesto[2.2,3.8], domini naturali degli involucri conservati. Errori posizione<=4.441e−15; prima derivata<=2.289e−16. `projection_build.py` compila una copia locale congelata del helper finale.

Null/invalid: source nullo, range vuoto/invertito/nonfinito lancia all’ingresso; BSpline valida poli/pesi/nodi. Circle/Ellipse/Other, inclusi sotto wrapper, restituiscono null e fk_fill.cpp:1116 mantiene il fit legacy esplicito. Non si convertono coniche angolari in NURBS cambiando parametro. I risultati sono BSpline2/Trimmed2, già supportati da BodyIO (writer:342/352, reader:454/463); non si introduce CurveType::Other. Il test permanente fill end-to-end razionale e roundtrip BodyBinary, più i controlli di pesi/nodi/jet/C0, sono stati letti;5/5 normali e sanitizer mirati sono riportati dall’implementer, non duplicati qui. La proiezione non certifica che la sorgente giaccia sul piano né che l’intero riempimento rispetti un limite continuo.

## Residuo boundary: manifestazione K10 da affrontare nel prossimo blocco

Non è un nuovo numero di difetto. Nessuna modifica a boundary in questa tornata. Sorgente fk_boundary.cpp SHA67d30f932214b9b16ca378f5c1ece74d8f48e7392bbec9e995637a708f32dec9.

Causa leggibile: sideFunction:184–211 costruisce i breaks solo ai confini dei SubPiece; non riporta i nodi interni di ogni curva nel sigma del lato. fit:212–217 passa poi il callback generico campionato. Inoltre un’unica campata di alto grado può nascondere una deviazione anche se tutti i nodi sono riportati: trasferire i nodi è necessario ma non risolve da solo K10.

`boundary.cpp` costruisce tre contorni chiusi reali: un lato NURBS/B-spline e tre rette, tolleranza1e−7. Non è un callback artificiale. La feature dei primi due è limitata alla campata[.1,.101], con raccordo geometrico C2 ai tratti rettilinei. Degree6,19poli; il secondo caso usa pesi positivi per W=1+.5u. Il terzo è la polynomial27 già studiata, ora con fixture copiata localmente in `fixtures.h`.

| Sorgente | Risultato | Divario osservato | Limite inferiore geometrico provato al picco | checkBody |
|---|---|---:|---:|---:|
| degree6 polynomial, nodi interni | accetta | .001 | .001 |0issues|
| degree6 razionale, nodi interni | accetta | .000952154248988336 | .000952154248988336 |0issues|
| degree27, singola campata | accetta |5.74247506018e−5 |>=4.4061668e−5 |0issues|

Il divario non è solo una differenza di parametro. `boundary.log` registra min/maxZ di TUTTI i poli della superficie prodotta: esattamente0 nei due casi degree6, e compresi in±4.665e−12 per degree27. Le superfici sono polynomial tensor-product: il convex hull Bernstein contiene tutta la superficie nella stessa fasciaZ. La coordinataZ della sorgente al parametro.1005 è dunque un limite inferiore della distanza geometrica dalla superficie, indipendente dalla scelta di (u,v). Nei due casi con nodi mancanti il bordo è a10000×/9521× la tolleranza e il controllo di validità non lo segnala.

Regolarità continua dei veri dati IEEE: `*.csv` esporta poli e pesi dal kernel; `regularity.py` usa Fraction esatti su tali double e il numeratore razionale Hx'W−HxW'. Per ognuna delle tre campate dimostra x'>0 tramite coefficienti Bernstein; i minimi globali conservativi sono .9999999999999722 polynomial e .4904667777777777 rational. Quindi la sorgente è regolare, non un caso singolare né una velocità stimata solo su campioni. Dettagli in `regularity.json`.

Proposta per il prossimo blocco: riportare nel sigma tutti i nodi interni (anche reverse e SubPiece parziali) e aggiungere un oracle di posizione della curva originale, senza normale: J=H−QW, errore<=normUpper(J)/Wmin. Comporre l’oracle con i tratti affini t(sigma), invertendo i poli del candidato quando il parametro è percorso al contrario. Restare inconcludente per i tipi non supportati; niente conversione coniche che cambi parametro. Un semplice aumento di campioni o il solo trasferimento dei nodi non risolve il caso degree27.

## Riproduzione e limiti

`build_boundary.py` compila solo il probe, collega l’archivio baseline congelato e i due oggetti core/oracle K10 congelati (hash in boundary-source.sha256); boundary è identico al sorgente finale. `python3 regularity.py` produce i bounds esatti dai CSV. `build_sheet_hole.py` compila il probe indipendente e collega l’oggetto sheet finale privato dell’implementer. `projection_build.py` compila il probe e il helper da snapshot locale. Non vengono invocati CMake/build condivise né usati CAD utente.

I massimi densi delle prove positive sono controlli numerici, non nuovi certificati matematici globali. La build app, i test consumer completi e l’aggiornamento CLAUDE/grafo appartengono al coordinatore. Nessuna pretesa di certificare coniche legacy, altri callback fitCurve o l’intero fill/boundary.
