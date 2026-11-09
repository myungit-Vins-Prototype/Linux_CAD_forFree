# Poli NURBS delle superfici analitiche — 9 novembre 2026

Richiesta: usare Modifica poli NURBS anche sulle facce analitiche, convertendole automaticamente. Il filtro precedente richiedeva già un supporto BSplineSurface e rifiutava, per esempio, una semplice superficie piana.

## Comportamento implementato

Le facce analitiche selezionate vengono convertite in supporti NURBS esatti. Il piano parte da una griglia bicubica 4×4; anche le direzioni lineari di cilindri e coni vengono elevate esattamente per avere poli interni modificabili. Restano disponibili gli spinner e la ricostruzione approssimata con il suo limite vincolante, distinta dalla conversione iniziale esatta.

Il nuovo modulo kernel `fk_surface_poles_conversion` ricava una finestra finita dalle SP-curve dei bordi. Le forme razionali conservano la geometria; il parametro delle direzioni circolari cambia, quindi le SP-curve vengono ricostruite rispetto agli stessi bordi 3D e verificate nel budget geometrico. Fori, facce, bordi e vertici mantengono le identità; non si attribuiscono facce per vicinanza. Nessuna mesh viene usata nel calcolo.

La funzione agisce su una copia del supporto. Il corpo sorgente resta analitico e invariato. I riferimenti salvati indicano esplicitamente le facce analitiche originali; lo schema14 consente di ricreare la conversione prima di applicare gli spostamenti, senza dipendere dalla cache B-rep. Gli schemi precedenti restano leggibili; un record analitico su uno schema precedente viene rifiutato.

G0/G1 includono anche le facce analitiche adiacenti convertibili. Restano i limiti di compatibilità dei giunti del comando. Le cuciture implicite delle superfici chiuse e i lati degeneri di sfere e coni vengono collegati dai vincoli; con i vincoli spenti una modifica che li apre viene rifiutata. Le singolarità al bordo già presenti sono conservate; non vengono ammessi nuovi punti singolari nella griglia di controllo campionata.

## Verifiche

Build completa di `forgecad`, runner e suite kernel terminata. Le nove suite CTest pertinenti passano in 13,49 s: piano analitico, continuità, multifaccia, griglie grandi, strumenti superficie, pannelli, kernel superficie, compressione e dipendenze. Le dodici prove kernel sui poli passano, comprese le quattro nuove prove di conversione.

Verificati piani, cilindri, coni, sfere e tori: punti della NURBS sulla superficie analitica entro 1e-8 mm nei campioni, entità topologiche conservate, bordi 3D condivisi con la sorgente durante la conversione e sorgente byte-identica. Il controllo B-rep passa dopo la conversione e dopo le deformazioni. Su cilindro/cono/sfera/toro sono verificate apertura della cucitura rifiutata a vincoli spenti e propagazione al polo corrispondente con G0 acceso. Le superfici parziali di tutte le quattro famiglie curve mantengono anche un foro interno dopo conversione e modifica.

Giunto misto: una faccia analitica convertita e due facce NURBS conservano G0/G1 dopo lo spostamento, con tre compensativi e scarti sotto 1e-9. La prova verifica anche il caso precedente che lasciava la faccia analitica esclusa e protetta: la rigenerazione non la include implicitamente.

Workflow del piano passato anche nella sessione Wayland/OpenGL: griglia 4×4 visibile, modifica di un polo interno, spinner 6×8 e ritorno alla base, opzioni G0/G1, anteprima e conferma, Undo/Redo, schema14, rifiuto del record analitico su schema13, riapertura e rigenerazione senza cache. La nuova griglia deforma il piano e il corpo originale resta analitico.

Hook/Cucitura8, su copia congelata: incluse otto facce, fra cui una analitica convertita, con 2.288.285 righe virtuali. Spostamento e compensativo riusciti; sui dieci giunti interessati apertura massima campionata 4,50016e-8 mm. Conferma, B-rep, Undo/Redo e codec14 riusciti, sorgente e file originale invariati. Questa prova interessa uno spigolo e verifica G0; G1 è coperta dalle fixture miste.

Mappa architetturale: controllo finale in corso. Restano i limiti dei controlli campionati e della compatibilità delle griglie del comando: questa modifica non certifica l’assenza globale di auto-intersezioni né introduce G2. Una conversione o deformazione che non supera le verifiche viene rifiutata senza aumentare le tolleranze.
