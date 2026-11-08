# Bottle — raccordo del contorno B25:E64, 7 novembre 2026

Richiesta: correggere il raccordo e raccordare tutto il contorno dell’incavo, con il raggio piu grande verificabile.

## Modello e riproduzione

Copia congelata di `/home/myungit/Documenti/Cad_Example/Bottle.prt`, 73.140.674 byte, 26 feature e 7 schizzi.
SHA-256: `8a12146fef0e5e774349c3bc5e2f07590454a4dcb79131976fc50dff9ee09e24`.
B25 indica l’indice zero-based della feature `Raccordo 26`, sul Corpo 2: 83 facce, 216 bordi, una shell solida chiusa, nessun errore di `checkBody({true,true})`.

Il contorno richiesto comprende E58, E64, E65, E66, E71, E72, E73, E79. Ordine dalla selezione E64:
64,79,73,72,71,58,66,65. I fianchi del taglio sono F37–F40; gli appoggi esterni sono F6–F9.

## Cause e correzioni

- Il loft contiene piccole creste reali: circa 0,00194 e 0,00321 rad tra le normali. La propagazione tangente si fermava correttamente, ma il raccordo non sapeva gestire i giunti vivi quando cambiava il supporto sul lato opposto.
- Aggiunte mitre sul secondo supporto, comprese le svolte concave esplicitamente selezionate. La superficie razionale viene orientata invertendo esattamente V per riusare lo stesso tracciamento e poi ricomponendo contatti, bordi e genealogia sui lati corretti.
- Intersezione dei contatti con Newton smorzato, inizializzazione dei punti tripli dai contatti rifiniti e tracciamento della chiusura lungo la coordinata trasversale monotona. Vicino al contatto comune viene valutato anche il ramo inizializzato da quel contatto.
- Prolungamento polinomiale B-spline della curva di riferimento delle sezioni sul dominio della ricerca: evita la cancellazione numerica nell’estrapolazione di microcampate terminali. La curva del B-rep originale resta immutata. Nessun aumento delle tolleranze o dei budget.
- Pulsante **Completa contorno dell’incavo** nella scheda Costante: selezione esplicita degli otto bordi tramite generatori analitici e adiacenze topologiche, compreso il fondo piano. Nessun collegamento per prossimita; contorni ambigui rifiutati.
- Verifica dell’orientamento dei contorni non periodici rifilati integrando le SP-curve esatte. Un raggio che consuma o inverte un appoggio viene rifiutato prima del documento. Il semplice controllo topologico/geometrico non rilevava questa inversione sui fianchi estrusi.

Moduli: `kernel/fk_blend_mitre.*`, `kernel/fk_blend_section_curve.*`, `kernel/fk_blend_contour.*`; coordinamento in `fk_blend_surface.cpp`, riferimenti in `cad_fillet_paths.*`, pannello in `forgeCad2026_dialogs_fillet.cpp`.

## Raggio e limiti

R0,5, R1, R1,1, R1,125, R1,6, R1,61 e R1,675 mm costruiti sul contorno completo. R1,6 e R1,675 superano anche la triangolazione di tutte le 93 facce; per R1,675: 240 bordi, nessun errore B-rep, 75.096 triangoli di display, tolleranza massima effettiva dei bordi circa 0,00854125 mm, entro il budget preesistente delle mitre approssimate.

**R1,675 mm e il massimo verificato con tutti i controlli nella ricerca eseguita**, non un massimo geometrico globale certificato. La convergenza delle intersezioni quasi tangenti non e monotona rispetto al raggio: alcuni valori inferiori o superiori falliscono ancora il tracciamento. Il fix gestisce il caso richiesto, senza garantire ogni raggio su qualsiasi contorno.

La ricerca preliminare basata soltanto su `checkBody` arrivava a R2,236888 mm (R2,236889 consumava un bordo vicino). Quel valore NON viene consegnato: tre facce non erano triangolabili. Anche R2,1 invertiva F38 e F40: aree parametriche orientate circa −0,489391 e −0,490147, rispetto a +17,9096 e +17,9152 iniziali. Il nuovo controllo geometrico lo rifiuta. Non usare quei risultati sperimentali come solidi validi.

## Verifiche

Fixture permanente `kernel/tests/data/blend_cavity_loft.body`: otto sole facce isolate dal modello, 4.500.678 byte. Test del contorno da ciascun bordo, raggio circolare entro 1e-7 mm sui campioni delle nuove pezze, sorgente immutata, genealogia dichiarata, serializzazione B-rep, triangolazione, inversione razionale V, prolungamento di riferimento e rifiuto degli appoggi consumati.

Preliminare: app compilata; 10 CTest e 62/62 test kernel Blend PASS. Prova Wayland R1,6: selettore contorno a otto bordi, conferma, 93 facce/0 errori, mesh completa, Undo/Redo, salvataggio e rigenerazione completa delle 27 feature PASS. Seconda rigenerazione identica interrotta nel runner QA per evitare lavoro e memoria ridondanti; nessun processo utente toccato.

Dopo il controllo degli appoggi: app completa compilata; **10/10 CTest e 62/62 kernel Blend PASS**. Il target `forgekernel_blend_cavity_tests` contiene cinque test, con raccordi R0,5/R1,1/R1,675 e propagazione tangente anche sul caso maggiore.

Prova finale **Wayland R1,675 PASS**: pannello nativo, pulsante contorno con otto riferimenti, 93 facce/0 errori, triangolazione completa, Undo/Redo, codec con/senza cache, ricostruzione della feature dai parametri decodificati, riapertura del file salvato, contesto GL valido. Screenshot del pannello e del modello in `/tmp/bottle-fillet/`; nessuna dipendenza OCCT aggiunta all’app.

Copia: `output/Bottle-raccordato.prt`, 77.010.175 byte, documento con identita autonoma, 27 feature. Deriva dalla copia congelata iniziale. Non comprende le tre lavorazioni aggiunte dall’utente mentre era in corso questa verifica. L’originale e stato solo letto, mai sovrascritto: nel frattempo l’utente lo ha portato a 29 feature, con nuovi raccordi R3,5/R0,4/R1,2.

Verifica aggiuntiva sulla versione aggiornata: B25 conserva 83 facce, E64 passa da (−13,4453;26,1732;160) a (−9,65419;38,5475;38,0019). La rappresentazione B-rep non e byte-identica alla cache congelata, quindi non si presume identita dalla sola etichetta. Il selettore riconosce gli otto bordi e R1,675 produce 93 facce/0 errori/0 fallimenti di triangolazione anche su questo B25. Ricostruiti inoltre i due raccordi preesistenti B24/R3 e B25/R5 dai parametri della versione aggiornata: 67 e 83 facce, nessun errore. Le lavorazioni aggiunte dall’utente restano nel suo file.

La nuova build richiede di riaprire ForgeCAD per essere caricata. Raggi ulteriori, altri contorni, massimo geometrico globale e massa non certificati. Mappa finale aggiornata: **645 file, 9.133 relazioni, zero diagnostiche AST**. `--check` PASS; panoramica e sette dettagli HTML aperti e verificati, con chiamate in entrata e uscita per contorno, mitre, curva di riferimento, coordinatore, riferimenti, pannello e regressione. Include e riferimenti distinti dalle chiamate; HTML/JSON restano ignorati da Git. `git diff --check` PASS.

SHA-256 della copia consegnata: `1ddb9d427a4f7d4b12cdb405c418fa60a5386e54d1ac0e061cdb497cc79ff700`.
