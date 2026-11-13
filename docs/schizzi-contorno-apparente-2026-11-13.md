# Contorno apparente associativo nello schizzo — 13 novembre 2026

## Richiesta e causa

Le due immagini mostrano un profilo estruso con estremità raggiate. Il tratto
verticale all'estremo della curva è un contorno apparente, privo di uno spigolo
B-rep proprio. In assenza di un bordo selezionabile, il picker proponeva la
sezione della faccia con il piano. La richiesta è invece la proiezione ortogonale
del bordo sul piano dello schizzo, lungo la normale del piano. Il piano può
essere completamente esterno al corpo: non deve intersecare la faccia.

Consultati prima delle modifiche descrizioni e chiamanti nella mappa:
`cad_sketch_refs`, `cad_snap`, viewport schizzo/riferimenti e contorni delle tavole.

## Implementato

- `kernel/fk_apparent_contour.*`: curve analitiche con normale della superficie
  ortogonale alla direzione di proiezione, ritagliate dai loop B-rep esatti.
  Generatrici cilindriche/coniche, cerchi massimi sferici e meridiani toroidali
  laterali; nessuna geometria ingegneristica derivata dalla mesh.
- `cad_sketch_apparent.h`: sorgente di tipo faccia con ruolo e ramo espliciti.
  Identità della faccia attraverso genealogia e nome persistente; nessun
  riaggancio automatico per vicinanza. Invertire la normale conserva i rami.
- `cad_sketch_refs.cpp`: cache automatica per snap e inferenze; proiezione
  senza contatto col piano, refresh al cambiamento della faccia o dello schizzo.
  Gli spigoli reali mantengono precedenza in caso di sovrapposizione.
- Viewport riferimenti: Quota sceglie il contorno proiettato prima del fallback
  alla sezione della faccia. Un solo collegamento per faccia/ramo; selezione
  locale conservata. Badge con «Contorno apparente della faccia F… di …».
  Le proposte restano limitate ai comandi attivi; Selezione senza hover associativo.
- Codec PRT69: stessi campi persistenti, nuova semantica protetta dalla versione;
  lettura legacy conservata, codifica legacy rifiuta il nuovo riferimento.
  Sorgenti eliminate identificano faccia/funzione e vincoli interessati.

## Verifiche finali

Kernel completo compilato. Probe dedicato: geometria esatta, trim delle mezze
facce della capsula, coni/cilindri obliqui, sfera/toro e pezzo trasformato PASS.
Mouse Polilinea/Quota, aggancio unico, Undo/Redo, codec/riapertura, raggio 4→6,
piano esterno al corpo e sorgente eliminata PASS. Variante Wayland/OpenGL PASS:
badge realmente presente nel framebuffer, cattura ispezionata.

La fixture importata del test include un STEP effettivo: il solo B-rep in cache
senza definizione d'importazione non sopravvive correttamente alla riapertura.
Build app e runner complete; **18/18 CTest PASS**, inclusi contorno apparente,
cache dei corpi, workflow e visibilità dei riferimenti, strumenti schizzo,
intersezioni, proiezioni, lettura/compressione, revisioni native, contorni delle
tavole e riuso dell'anteprima in modifica. Mappa definitiva: **695 file / 10025 relazioni / 339 TU**, zero diagnostica;
check dei sorgenti/HTML e QA browser dei blocchi con relazioni in entrata e
uscita PASS. Include e chiamate C++ restano distinti.

Il test GL prepara il display, completa l'esposizione iniziale e controlla i
pixel del testo verde del badge, anziché qualsiasi differenza tra due frame.
Runner definitivo: **due avvii Wayland/OpenGL consecutivi PASS**. Una cattura
appariva incompleta nello strumento d'ispezione, ma il confronto dei PNG
conferma fotogrammi completi byte-identici, anche prima del rafforzamento:
SHA256 `15084d5ae6997a125ee30d2dcc5b1d95fc41e1642e8ba28ea43a304bfa66add3`,
RGBA interamente opaco. Non è stato rilevato un difetto di disegno dell'app.

Prova aggiuntiva su copia congelata di `KnifeHandle.prt`: creazione schizzo
3924 ms, cache449 ms, 14089 segmenti e12022 curve; mouse, Undo/Redo, codec e
conservazione dei B-rep originali PASS. È una misura sul file corrente, non
un confronto con revisioni storiche del modello. Originale SHA256 invariato:
`997af90788b7b5f1159462fcb6fd543d7ae96b893d652cec08606c2946720c8a`.

Eseguibile `forgecad-release/forgecad` aggiornato con build completa. Riaprire
l'applicazione per usare la nuova versione; nessun processo utente terminato.

## Limiti

Nessuna approssimazione per facce libere/NURBS o contorni toroidali obliqui.
Il contorno viene offerto soltanto per geometrie/direzioni supportate esattamente;
la sezione della faccia resta un riferimento distinto. Cambiamenti di topologia
che impediscono di conservare il ramo producono un errore esplicito.
Tolleranze e solver non rilassati; originali CAD e processi utente non modificati.
