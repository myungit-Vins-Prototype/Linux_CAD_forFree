# Svuotamento a 1 mm con aperture F0 e F14 — 3 novembre 2026

Richiesta: svuotare il corpo del documento abituale aprendo F0 e F14.

Copia congelata da `/home/myungit/Documenti/Cad_Example/SurfaceModellingExample219.prt`
in `/tmp/forgecad-shell-f0-f14/SurfaceModellingExample219.prt` (148355442 byte),
SHA256 `b39104d2e9bcaa7ad197a01e052f6d8815cc131bfc7cff39bd5de831196c455f`.
Originale non modificato. Il corpo finale è Raccordo 25, indice 24, 33 facce e 74 bordi.
Entrambe le aperture sono planari. Il B-rep rigenerato passa checkBody anche con loopCrossings.

Riproduzione kernel: shellBody con aperture F0/F14, spessore 1 mm, fallisce con
«BSplineCurve: polo non finito». Il debugger sulla sola sonda temporanea identifica
splitOffsetFaces → sewSheets (685 parti) → assembleBody → translatedCurve<2>,
con traslazione UV `(0, -inf)`. La suddivisione corrente separa tutte le pezze di Bezier
quando trova una discontinuità della normale, comprese le campate lisce.

In prova una suddivisione esatta solo sulle linee di nodo con salto della normale;
nessuna modifica produttiva integrata o nuova app dichiarata corretta a questo traguardo.
Evidenze e sorgenti delle sonde in `/tmp/forgecad-shell-f0-f14`.

## Prosecuzione dopo l'interruzione

La prova interrotta non viene conteggiata come esito. Ripetute le regressioni produttive:
`forgekernel_tests Shell`, **18/18 PASS** (`shell-baseline-resumed.log`). Nessuna modifica
ai sorgenti produttivi è stata applicata a questo traguardo.

Sono state isolate ulteriori difficoltà, senza ottenere ancora un guscio valido:

- F19/F24 hanno campate U larghe circa 3,725e-9, con una corda di circa 4,524e-8 mm.
  La valutazione locale delle curvature dà valori molto elevati: circa 128/mm,
  circa 87/mm traslando i poli. L'ordine di grandezza conferma la fragilità della
  rappresentazione; il valore 0,008 mm non va trattato come un raggio nominale del raccordo.
- Una suddivisione mediante imprint evita il primo errore, ma non completa lo svuotamento.
  Un prototipo che stabilizza la scelta del solve per tutto il bordo supera le giunzioni
  quasi tangenti di F5/F7. Il fallback per singolo punto resta diagnostico, non una soluzione integrata.
- Un prototipo ricompone campate cubiche omogenee e verifica un limite uniforme di scostamento
  razionale mediante i poli delle differenze e minimi positivi dei pesi. Sul modello la prima
  variante riduce 342→324 campate in F19/F24 e 396→380 in F21/F22, con limite massimo
  misurato dal certificato di circa 5,63e-10 mm; checkBody con loopCrossings resta senza issue.
  Questo da solo non risolve la curvatura locale né il guscio.
- L'estensione UV fissa dello 0,5% porta l'offset di F6 (Loft 13) in un dominio difficile
  da approssimare. Il prototipo limitato al dominio effettivamente raggiunto dai bordi supera
  quel fit, ma si ferma poi sulla curvatura di F19. La rimozione del margine è solo sperimentale:
  non è una garanzia che i 16 campioni delimitino tutti gli estremi UV dei contorni.
- La rigenerazione completa con valutazione B-spline traslata in coordinate locali è stata
  completata dopo la ripresa: tutti i corpi vengono costruiti, ma lo svuotamento fallisce ancora
  sul fit di una curva. Non basta quindi cambiare la valutazione delle derivate.

Il file originale viene salvato dall'utente durante il lavoro. Congelata anche la versione
`SurfaceModellingExample219-latest.prt`, SHA256
`3b51f24b9c34df7a8343e4616aab1d9366ac9bc337d0f1304685469c226e5fe5`.
Il confronto delle definizioni individua una modifica di continuità guida in Loft 13 (G1→G2);
segmenti, punti di controllo e nodi degli schizzi confrontati risultano uguali. Le prove geometriche
sopra si riferiscono alla prima copia b39104d, non certificano automaticamente la variante G2.
Log e sorgenti delle prove sono in `/tmp/forgecad-shell-f0-f14`.

## Prototipo riuscito: correzione a monte del fit del raccordo

La causa delle micro-campate è stata isolata nel fit di EdgeBlend: considera i nodi del bordo
selezionato, ma non i valori del parametro nei quali i **contatti** attraversano i nodi delle
superfici di appoggio. Nel caso reale il contatto attraversa il nodo U≈0,5 del loft a
T≈2,00392090592; il fit continuava a dimezzare una cella che comprendeva due espressioni
polinomiali diverse, creando la rappresentazione fragile del raccordo.

Il prototipo individua questi attraversamenti mediante le coordinate dei contatti e le loro
derivate, divide lì il dominio del fit e usa il limite delle derivate da ciascun lato, mantenendo
un punto comune. La tolleranza del fit del raccordo resta **1e-9 mm** e non è usata la ricomposizione
approssimata delle superfici sperimentata prima. La curvatura massima rilevata sui raccordi
F19/F24 torna a circa **0,500000000045/mm**, coerente con il raggio nominale di 2 mm.
Le campate vicine all'evento diventano circa 0,00392 e 0,01388, anziché 3,725e-9.

Combinato con la stabilizzazione delle giunzioni quasi tangenti e un dominio UV limitato ai
bordi effettivi, il prototipo completa lo svuotamento di **Raccordo 25**, 1 mm, F0/F14:
**64 facce, un guscio, zero issue checkBody con loopCrossings, volume circa 33894,7 mm³,
zero facce fallite nella tassellazione**. Log `knot-aware-shell-tight.log`.

Questo primo successo usa ancora codice sperimentale: il dominio UV senza margine ottenuto
con soli campioni va sostituito con un limite dei contorni completo; vanno isolate le modifiche
necessarie, aggiunte regressioni significative e completate build/app/QA e migrazione delle cache.
I sorgenti produttivi sono ancora invariati a questo traguardo. La variante G2 più recente riproduce
anch'essa il polo non finito nel kernel attuale (`latest-reproduce.log`); il prototipo riuscito
sopra è stato verificato sulla copia originaria G1 b39104d.

## Integrazione e regressioni (in corso)

Integrati nei sorgenti i seguenti interventi, con build completa ancora in corso:

- `fk_blend_contact_breaks`: eventi dei contatti su nodi degli appoggi, ricerca Newton
  con bracket e verifica delle normali dei due pezzi Bezier. Il fit mantiene il punto
  comune e stima le derivate da ciascun lato. Resta il rifiuto delle creste geometriche reali.
- `fk_blend_surface`: cache di continuazione ripristinate tra tentativi; se il nuovo
  percorso fallisce anche nel rifilo a valle, ritenta l'intera operazione precedente.
  Genealogia pubblicata solo con il risultato riuscito. Nessun aumento di tolleranza.
- `fk_offset`: scelta del solutore stabilizzato per tutto il bordo vicino alla tangenza;
  prima SP-curve sull'offset esatto, poi supporto sul box conservativo dei loro poli.
  Si elimina il margine UV arbitrario dello 0,5%, mantenendo controlli e budget 3D.
  Il rifilo cucito evita nodi artificiali agli estremi naturali nell'estensione del
  polinomio terminale. I fit pubblici conservano le sezioni terminali per Estendi superficie.
- Cache semantica 9: copie di lavoro con raccordi attivi rigenerano integralmente
  cache precedenti, derivati inclusi. Check-in immutabili e formato documento 61 conservati.
  Aggiunte prove di migrazione/preservazione; aggiunti test degli eventi G1 nei due
  versi, assi e appoggi, coincidenze con i probe e rifiuto G0.

Prima dell'integrazione, il candidato raccordi passa **57/57 Blend** (compresi i quattro
casi inizialmente regressi). Il candidato offset passa **18/18 Shell**, ma una variante
che rimuoveva gli estremi naturali in ogni fit falliva 2/25 Offset, entrambe Estendi.
La modifica ora è limitata al rifilo cucito; i nuovi esiti saranno registrati dopo la build.
Sul corpo congelato G1, il candidato senza cambiamenti globali alla valutazione B-spline
ottiene **64 facce/1 guscio/checkBody con loopCrossings/mesh PASS**, volume circa
33894,8 mm³ (`offset-true-breaks-user.log`). Nessuna ricomposizione o healing integrati.

## Verifiche consolidate prima delle ultime interruzioni

- Build Release completa dell'app e di tutti i runner: PASS. L'app è
  `forgecad-release/forgecad`; un'app già aperta deve essere riavviata.
- Kernel produttivo: **Blend 60/60**, **Shell 18/18**, **Offset 25/25**, **Draft 6/6**.
  CTest mirati a documento, cache genealogica, revisioni, storia raccordi/completa,
  superfici e riuso mesh: **7/7 PASS**.
- Nuova fixture sintetica di 3957 byte, `blend_contact_knot.body`: prisma con
  profilo cubico G1 a velocità parametrica diversa nei due tratti e tagli inclinati
  (`z'=z+0,1x`). Il raccordo R2 attraversa il nodo dell'appoggio in un parametro
  diverso da quello del bordo. Il test controlla la curvatura e l'offset interno 1 mm.
  Controllo negativo collegando il vecchio fit: **FAIL atteso**, curvatura 11,1362/mm
  contro 0,5/mm e offset rifiutato. Con il nuovo fit: **3/3 test contatti PASS**,
  incluso raggio/offset, senza indebolire le asserzioni.
- Sul corpo G1 corretto, svuotamento completo con 64 facce, 1 shell, nessun bordo
  laminare, checkBody con loopCrossings senza issue e mesh senza facce fallite.
  Misura su **662 punti interni** delle facce offset: errore massimo rispetto a
  `S - N * 1 mm` **5,8344e-8 mm**. Aperture F0/F14 classificate libere; un punto
  a 0,5 mm nella parete è materiale e uno a 1,5 mm è cavità. Ingresso immutato.
  Volume misurato **33894,7870112 mm³**. Queste misure campionate non certificano
  globalmente ogni possibile auto-intersezione.
- QA XCB sul documento G1: migrazione cache, anteprima reale, OK con riuso dello
  stesso B-rep, Undo e Redo PASS; circa 35 s di rigenerazione iniziale e 150 s
  ulteriori di anteprima nella sessione con altre verifiche contemporanee.
- Il primo round trip cache aveva un'aspettativa QA troppo forte: la cache grande
  supera il budget cumulativo di lettura degli stadi, anche se il blocco compresso
  è solo circa 182 MiB. Il lettore standard ignora atomicamente gli snapshot e
  mantiene la definizione per rigenerarla. Con budget esplicito QA 4 GiB la cache 9
  ripristina tutti i B-rep, incluso il guscio. **Il limite produttivo 1 GiB non è
  stato aumentato**. La migrazione/check-in è verificata anche dalle fixture piccole.
- Architettura: **569 file, 7860 relazioni, 270 unità AST, zero diagnostiche**;
  controllo aggiornamento e 4 confini di chiamata PASS, entrata/uscita distinte
  per call/include/reference. Resa HTML dei contatti, offset e codec verificata.

Una nuova sessione ha rimosso `/tmp`, quindi i log/immagini intermedi e le copie
congelate di quella cartella non sono più disponibili. Gli esiti già osservati sopra
restano distinti dalle verifiche interrotte. Il round trip senza cache e la variante G2
non erano ancora conclusi. Ripresa su copia G2 con hash `3b51f24…`, evidenze d'ora in poi
in `forgecad-release/shell-f0-f14-qa` (ignorata da Git). Sorgenti e binari sono conservati.

## Esito finale dello svuotamento

Verifica completata il 3 novembre sulla copia **G2** più recente, con lo stesso hash
`3b51f24b9c34df7a8343e4616aab1d9366ac9bc337d0f1304685469c226e5fe5` dell'originale
ricontrollato dopo le prove. Il file originale non è stato modificato.

QA **Wayland** con pannello reale: apertura/cache8 migrata, rigenerazione, scelta F0/F14,
anteprima, conferma con riuso del B-rep, Undo/Redo, salvataggio e riapertura con cache
(nella sonda con budget esplicito) e senza cache **PASS**. I B-rep rigenerati sono validi,
chiusi, 64 facce e una shell. Log persistente `forgecad-release/shell-f0-f14-qa/native.log`.
Tempi osservati: 27,9 s caricamento/rigenerazione, anteprima pronta a 156,5 s dall'avvio,
conferma a 157,2 s, round trip cache a 166,2 s, round trip senza cache a 324,4 s.
Sono tempi locali, non una garanzia per altre macchine.

Controllo indipendente sul G2 salvato, mediante la genealogia dichiarata dal codec:
**31 facce interne, 662 punti, errore massimo di spessore 5,8343983e-8 mm**;
aperture F0/F14 classificate libere, checkBody con loopCrossings senza issue e mesh
senza facce fallite. Log `geometry-G2.log`. Immagini `panel-wayland.png`,
`result-wayland.png`, `opening-F0.png`, `opening-F14.png` nella stessa cartella.

App completa ricompilata e verificata aggiornata dopo il riavvio. CTest 7/7 e nuovi
contatti 3/3 ripetuti con log persistenti; gruppo Offset ora **26/26**, perché include
anche la nuova prova del raccordo con offset 1 mm. Restano consolidati Blend60/60,
Shell18/18, Draft6/6. Verifica architetturale aggiornata PASS. macOS e CI non rieseguiti
in questa verifica locale.

Copia consegnabile: `forgecad-release/shell-f0-f14-qa/SurfaceModellingExample219-svuotato-1mm.prt`.
La cache grande può essere ignorata dal lettore standard per il budget cumulativo:
la definizione rimane completa e il percorso di rigenerazione è stato verificato.

## Dimensioni e complessità — controllo aggiunto dall'utente

Misurati i file esistenti, senza riscriverli:

| File | Byte | MB decimali |
| --- | ---: | ---: |
| Originale `SurfaceModellingExample219.prt` | 148355436 | 148,36 |
| STEP originale | 745117543 | 745,12 |
| B-rep del corpo rigenerato prima dello svuotamento | 207952422 | 207,95 |
| B-rep svuotato | 253833926 | 253,83 |

La definizione completa del G2 svuotato, salvata **senza cache**, occupa circa **17 KB**.
Questo distingue il peso degli snapshot dal contenuto parametrico, ma non riduce la
complessità geometrica né il tempo necessario a rigenerarla. La cache della storia
contiene complessivamente **1701486372 byte** di B-rep espansi, superando il budget
cumulativo produttivo di 1 GiB. La condivisione dei chunk e Zstandard riducono il peso
su disco, non il numero di poli delle superfici.

Il corpo originale rigenerato ha **6454452 poli NURBS**. Quattro facce ne contengono
**6424136, il 99,53%**:

| Facce del corpo finale | Gradi U,V | Rete dei poli per faccia | Poli per faccia |
| --- | --- | --- | ---: |
| F6, F9 | 33,5 | 2311 × 706 | 1631566 |
| F7, F10 | 34,5 | 2177 × 726 | 1580502 |

Gli altri fianchi principali F5/F8 hanno grado12,5 e 13×671 poli. Il guscio arriva a
**8284360 poli** contando anche le superfici interne. La serializzazione dei soli
numeri di controllo del corpo originale richiede circa **206,54 MB**: quasi tutto il
B-rep binario da 207,95 MB, anche prima di considerare la storia del documento.

L'audit degli stadi localizza l'aumento:

| Stadio | Poli NURBS | B-rep binario |
| --- | ---: | ---: |
| Loft11 | 8723 | 336824 byte |
| Loft13 | 1631566 | 52308964 byte |
| Loft15 | 1580502 | 50675652 byte |
| Cucitura16 | 3220791 | 103284466 byte |
| Cucitura18, dopo lo specchio | 6441582 | 206470792 byte |
| Raccordo25 | 6454452 | 207952422 byte |

Lo STEP contiene un solo solido, 33 facce e 12 superfici B-spline. I **6455223
CARTESIAN_POINT** occupano **579624135 byte**; le entità delle superfici B-spline
occupano altri **165242422 byte**. Insieme sono il **99,97%** del file. Le quattro
superfici maggiori dello STEP hanno gli stessi gradi e numeri di poli rilevati nel
corpo attuale: il peso è già presente nei loft, prima dello svuotamento. Non si tratta
di una mesh esportata.

I passaggi di codice individuati per la successiva riduzione sono:

- `fk_loft_profiles.cpp::normalizeLoftParameterJoins`: composizione cubica per
  correggere la velocità ai giunti, con aumento globale del grado del gruppo.
- `commonLoftDenominator`: prodotti dei denominatori razionali e ulteriore elevazione
  del grado per i campi delle guide. Conserva esattamente la geometria delle sezioni,
  ma può produrre rappresentazioni molto più grandi.
- `fk_loft.cpp`, percorso endpointControlled: l'unione dei nodi delle guide e
  l'inserimento su ogni riga produce la rete tensoriale completa; molte righe U
  moltiplicate per centinaia di poli V spiegano le reti da oltre 1,5 milioni di poli.
- `fk_step.cpp::writeSurface`: scrive l'intera rete NURBS, con 17 cifre per numero;
  deduplica punti identici, ma conserva la complessità del supporto della faccia.

**Non è stata ancora implementata una riduzione dei loft.** La correzione dello
svuotamento conserva le superfici sorgenti. Il prossimo intervento geometrico deve
limitare la crescita di gradi/nodi a monte o ridurre la rappresentazione con limiti
verificati dello scarto e dei jet, mantenendo sezioni, guide e giunti reali. Il solo
salvataggio senza cache o una diversa compressione non risolve il costo del kernel.
L'eventuale rifilo esatto dei supporti STEP va valutato separatamente: sui quattro
fianchi maggiori i trim coprono già quasi tutto il dominio, quindi non promette da
solo una riduzione sostanziale.

Evidenze persistenti: `source-size-audit.log`, `result-size-audit.log`, `stages-size.log`,
`step-size-audit.json`, con sorgenti delle sonde nella cartella QA della build.
