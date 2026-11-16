# K10 — Composizione superficie e curve parametriche, 15 novembre 2026

## Stato e perimetro

Prosecuzione con tre agenti GPT-6.1 Sol dopo [BoundarySheet](curve-fit-k10-boundary-2026-11-15.md). Implementata la correzione di Modifica poli per bordi isoparametrici NURBS con parametro UV affine, inclusa la verifica dei supporti adiacenti. Mapping generale conserva un difetto riprodotto e richiede una correzione distinta. Verifiche integrate completate.

## Difetti riprodotti

Modifica poli: una superficie NURBS di grado 6×1, regolare e inizialmente valida, contiene una variazione fra i nodi U=0,1 e 0,101. Il bordo originale coincide con il supporto; la SP-curve è una retta senza nodi interni. Modificando realmente un polo, il comando accetta un nuovo bordo distante 0,001 mm dal supporto con tolleranza 1e-7 mm: **10000 volte il budget**, e `checkBody` non segnala errori. Il corpo originale rimane immutato. Una variante di grado 1×1 con nodi più ravvicinati conferma lo stesso meccanismo.

Il fit usa i nodi della sola SP-curve, omettendo quelli attraversati sulla superficie. Il successivo confronto campionato può perdere la stessa variazione. Aggiungere soltanto campioni o nodi nominali non certifica il residuo continuo.

Mapping: riprodotto `wrapBodyOnSurface` su rettangolo piano e supporto di estrusione NURBS regolare con rilievo 0,001 mm. L'operazione, con tolleranza 1e-6 mm, restituisce una superficie i cui poli hanno tutti Z=0 e dichiara scarto circa 2,82e-11 mm; al picco manca il rilievo di 0,001 mm (**1000 volte** la tolleranza). `checkBody` non segnala problemi. La regolarità è verificata anche tramite coefficienti Bernstein e frazioni esatte dei dati double originali: la derivata X resta maggiore di 0,99999999999997 in tutte le campate. La perdita del rilievo non dipende quindi da cuspidi o pieghe. La variante di grado 27 viene invece rifiutata: non tutti i casi attraversano i controlli nello stesso modo. Questo residuo resta aperto in questo blocco, senza conteggiarlo come nuovo rilievo distinto da K10.

## Criteri della correzione

Il provider deve lavorare sui poli e pesi originali del supporto, con aritmetica verso l'esterno. Non basta estrarre un'isoparametrica arrotondata e trattarla come sorgente esatta. Anche la mappa affine UV deve conservare origine/direzione o poli/nodi originali, compresi i ritagli e i versi inversi.

La ricostruzione del bordo e la verifica di ogni faccia adiacente sono due controlli separati. Un controllo supportato ma non conclusivo deve fallire, senza tornare ai campioni. Le composizioni non riconosciute mantengono esplicitamente il percorso precedente: non vengono dichiarate certificate. I controlli campionati di regolarità dell'interno della superficie restano distinti dal certificato dei bordi.

## Implementazione e limiti

`kernel/fk_curve_fit_bounds.cpp/.h` espone il provider `surfaceCompositionErrorBound`: riconosce rette UV assiali, campate B-spline UV di grado uno a pesi uguali e wrapper Trimmed. Estrae coefficienti omogenei isoparametrici dai poli/pesi tensoriali originali mediante de Boor e conversione Bernstein con intervalli verso l'esterno. Le catene di istanze e trasformazioni sono incluse. Le coordinate UV conservano origine/direzione oppure poli/nodi originali, anche per intervalli parziali, verso inverso e parametri non unitari; la query comprende gli scarti di arrotondamento delle mappe e ambedue i valori ai raccordi fra campate. Le preimmagini dei nodi del supporto guidano la suddivisione, senza sostituire il certificato.

`kernel/fk_surface_poles.cpp` usa `fitCurveCertified` al budget precedente di un quarto della tolleranza. Un secondo controllo confronta i coefficienti cubici effettivamente prodotti con ciascun supporto adiacente riconosciuto, anche quando il supporto scelto per il fit ha seguito il percorso campionato. La soglia preesistente non aumenta. La sorgente resta immutata anche al fallimento.

Le composizioni UV diagonali/non lineari, le coniche, i tratti UV razionali a pesi diversi e gli altri tipi di superficie conservano il percorso campionato. Mappe riconosciute fuori dal dominio naturale, comprese traslazioni periodiche non trattate, falliscono esplicitamente. Budget finiti e limiti numerici non conclusivi possono rifiutare geometrie valide. I controlli campionati di regolarità dell'interno e l'assenza globale di auto-intersezioni non sono certificati da questo intervento.

## Verifiche finali

- Cinque gruppi nuovi PASS nella build privata: diciotto varianti per scala, pesi, posa, intervallo interno e verso inverso; bordi condivisi incompatibili e modifiche coerenti; nodi attraversati; UV Trimmed annidate; istanze; input non validi e cancellazione.
- Lo stesso test con il solo modulo Modifica poli precedente fallisce i due gruppi operativi (22 controlli), mentre i tre test diretti del provider nuovo passano. La baseline dimostra il difetto nell'operazione reale, non una differenza artificiale fra API.
- Venti test SurfacePoles preesistenti PASS sui moduli finali nella build privata.
- **Build completa app e runner PASS**: `forgecad-release/forgecad` aggiornato. Warning in codice preesistente (`fk_offset.cpp` e test Qt preesistenti); nessun errore di compilazione né warning nei file modificati in questo blocco.
- **129 casi kernel distinti / 145 esecuzioni PASS**, su Offset, Shell, Sheet, BoundarySheet, Fill, Projected, Sew, Thicken e sette gruppi Blend.
- **36/36 suite CTest PASS** sui binari finali, includendo le cinque nuove regressioni e i test Qt offscreen di modifica poli, pannelli, mapping, storia e anteprime. Una prima corsa avviata prima del termine del runner Qt è conservata come prova preliminare; il conteggio finale usa la riesecuzione dopo la build.
- **5/5 regressioni ASan/UBSan/LSan PASS**: strumentati il test, `fk_surface_poles.cpp` e `fk_curve_fit_bounds.cpp`; main e resto dell'archivio Release non strumentati. Nessuna certificazione globale dell'app o dei leak al di fuori di questa copertura.
- **Audit indipendente PASS**: 30 casi su tensore non clamped, pesi estremi, U/V, ritagli, direzioni e istanze; 24 query aggiuntive su nodi e parametri adiacenti rappresentabili. Un ulteriore corpo indipendente di grado 6×1 viene modificato con successo: scarto massimo misurato 9,65e-8 mm a tolleranza 1e-6 mm, checker senza problemi e sorgente immutata. I confronti densi sono controlli del provider, non la fonte del certificato continuo.
- **Mappa 746 file / 10743 relazioni / 369 unità PASS**, diagnostica vuota, controllo aggiornamento, HTML/JSON identici e collegamenti in ingresso/uscita verificati distinguendo chiamate, include e riferimenti.
- Hash delle fonti finali, conservazione del lavoro pregresso, flag floating point stretti e assenza di Qt/OCCT nel nuovo target verificati. [Prove ripetibili e manifest](curve-fit-k10-composition-prove-2026-11-15/README.md).

## Stato complessivo

Sette dei 24 siti originari hanno ora un percorso esatto/certificato per famiglie supportate; **17 restano interamente da migrare**, oltre ai fallback dei sette trattati. **K10 resta aperto**. Il mapping descritto sopra è un residuo dello stesso rilievo, senza incremento artificiale del conteggio: 24 rilievi precedenti corretti e 22 orfane candidate. Non sono stati modificati CAD originali o processi dell'app; le prove Qt offscreen non certificano il renderer OpenGL.


## Prosecuzione

Il blocco successivo [Mapping con conservazione del supporto](curve-fit-k10-mapping-2026-11-15.md) corregge il rilievo perso per le lamine piane e le mappe riconosciute. Conteggio aggiornato: 8/24 siti parzialmente trattati, 16 interamente da migrare, oltre ai fallback. Il mapping generale e gli offset normali restano aperti; numeri e prove sopra descrivono la consegna Modifica poli.
