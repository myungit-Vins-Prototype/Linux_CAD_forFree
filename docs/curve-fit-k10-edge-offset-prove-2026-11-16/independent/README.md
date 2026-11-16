# Offset 680 — prova indipendente e piano di verifica

Repository consultato in sola lettura. Tutti gli artefatti di questo controllo sono in `/tmp/review-offset-proof`.

## Esito verificato sulla baseline

Il difetto supera la tolleranza **anche come distanza geometrica**. Non dipende dal confronto fra punti con lo stesso parametro.

La fixture originale costruisce una curva Bézier polinomiale di grado 27, la estrude lungo `D = (0, 1, 0)` e richiede un offset di `0.001` con tolleranza `1e-7`. Il corpo sorgente e quello restituito superano `checkBody`.

Al parametro originale esatto

` t = 3960015152346877 / 4503599627370496 `

la quota dell'offset originale soddisfa

`z_originale >= 0.001000574247517282988158673583375163961292636195410068054`.

Tutti i 16 poli dei quattro bordi B-spline restituiti hanno invece

`z_polo <= 0.001000000000108420238065232155122430413030087947845458985`.

I pesi restituiti sono positivi e gli intervalli dei bordi sono contenuti nei domini naturali. Per la proprietà di inviluppo convesso delle B-spline razionali con pesi positivi, la stessa maggiorazione vale per **ogni punto di tutti i bordi restituiti**, con qualunque parametro. Ne segue

`distanza(punto originale, unione dei bordi restituiti) >= 5.742474088627500934414282527335482625482475646090699016e-7`.

Il limite inferiore è oltre 5.742 volte la tolleranza richiesta. Si tratta di un testimone sufficiente del fallimento; non certifica il massimo globale dell'errore.

## Indipendenza e aritmetica della prova

- `export_original.cpp` conserva la fixture originale e aggiunge l'esportazione esadecimale dei dati IEEE effettivi. Esporta tutti i poli sorgenti e tutti i poli, pesi, nodi e intervalli dei bordi finali.
- `prove_fraction.py` ricostruisce i dati binari con `Fraction.from_float`, controlla ogni polo sorgente contro i coefficienti e le operazioni IEEE originali della fixture, poi usa de Casteljau in aritmetica razionale esatta per posizione e derivata della sorgente.
- Per questa fixture, la componente Z della normale originale è `Gx' / sqrt(Gx'^2 + Gz'^2)`. La radice è racchiusa fra due razionali con denominatore `2^256`, calcolati con `isqrt`. Le disuguaglianze sono ricontrollate mediante quadrati razionali esatti.
- Il parametro è un singolo valore IEEE originale scelto dal riproduttore. La ricerca numerica serve soltanto a individuare il testimone: nessuna scansione, valutazione del kernel o callback del fit fonda la disuguaglianza finale.
- La prova globale sul risultato usa direttamente i poli finali e la positività dei pesi; non usa un residuo restituito dal provider di produzione.
- `proof-result.json` riporta decimali arrotondati verso l'esterno; `rational-certificate.json` contiene numeratori e denominatori per un controllo esatto; `manifest.json` identifica fixture, esportatore, eseguibile, libreria e sorgenti consultate.

Ripetizione locale:

```sh
c++ -std=c++17 -O2 -I/home/myungit/Linux_CAD_forFree/kernel \
  /tmp/review-offset-proof/export_original.cpp \
  /home/myungit/Linux_CAD_forFree/forgecad-release/kernel/libforgekernel.a \
  -o /tmp/review-offset-proof/export_original
/tmp/review-offset-proof/export_original > /tmp/review-offset-proof/fixture-original.txt
python3 /tmp/review-offset-proof/prove_fraction.py
```

La libreria indicata è quella della baseline registrata nel manifest. Una ricompilazione successiva può correggere il difetto e quindi rendere intenzionalmente falsa l'asserzione di fallimento di questo script.

## Contratto da verificare nel risultato corretto

Per ogni bordo finale e ogni parametro originale `t` nell'intervallo del bordo:

`F(t) = E(t) + d * N(t) / |N(t)|`, con `N(t) = G'(U(t)) cross D`.

`E` è il bordo 3D originale, `G` il generatore originale dell'estrusione, `U` la prima coordinata della SP-curve originale. `E` e `G` sono indipendenti. Il segno di `d` include l'orientamento della faccia. Il verso di `U` non cambia il segno della normale: **non** usare la derivata composta `G'(U) * U'` nella normale. Sono da includere le trasformazioni originali delle sorgenti e del supporto.

L'accettazione deve verificare un limite continuo riferito alla geometria originale e ai poli **realmente restituiti dopo l'assemblaggio**. Un bound sulla curva candidata prima della sostituzione dei vertici non basta.

## Piano di verifica del bound finale

| Gruppo | Varianti necessarie | Evidenza richiesta |
| --- | --- | --- |
| Polinomiali | Fixture grado 27, semplici cubiche, più campate, nodi ripetuti e derivata one-sided | Ogni campata finale ha bound finito entro la tolleranza; witness originale corretto; nessuna accettazione tramite campionamento |
| Razionali | Pesi positivi disuguali, pesi uniformi non unitari, scale comuni dei pesi, campate multiple | Ritaglio omogeneo dagli originali; limite inferiore positivo per il denominatore e per la normale |
| Bordo e generatore indipendenti | `E != G`, bordo con componente lungo D, curve 3D su quote V diverse | Il bound usa E originale per la posizione e G originale per la normale; nessuna sostituzione E=G implicita |
| SP-curve | Line originale, BSpline2 grado uno con pesi uguali, Trimmed, più intervalli affini, U costante | Mappe ricostruite dagli anchor originali; copertura completa e nessun buco ai nodi o fra tratti |
| Reverse | U decrescente, Trimmed invertito, bordo 3D con parametro inverso, `face.sense` opposto e `d` negativo | U' non ribalta la normale; sense e distanza la ribaltano una volta sola; parametro finale e identità del bordo conservati |
| Pose | Traslazioni, rotazioni in assi diversi, riflessioni se supportate, fattori di scala compatibili col contratto | Valori IEEE delle trasformazioni trattati come originali esatti; normale da cross nel riferimento corretto, senza presumere ortogonalità matematica di una matrice arrotondata |
| Assemblaggio | Spostamento dei vertici, riordino dei bordi, bordi topologicamente equivalenti, campate unite | Mappa esplicita raw -> EdgeId; tutti i bordi migrati ricertificati sui quattro poli finali per campata e intervallo originale |
| Casi non regolari | Tangente nulla, N nullo, pesi non positivi, NaN/Inf, dominio fuori supporto | Rifiuto esplicito o percorso dichiaratamente non certificato; nessun fallback numerico viene etichettato come bound finito |
| Budget e cancellazione | Molti nodi, pesi estremi, grado alto, profondità terminale, cancellazione | Nessuna campata terminale accettata senza bound; risultato atomico e nessun residuo pubblicato dopo fallimento |
| Vita degli oggetti | Modifica/distruzione di E, G, pcurve e trasformazioni dopo costruzione provider | Snapshot dei coefficienti e delle mappe indipendente dai chiamanti |

### Verificatore indipendente del limite continuo

Per le famiglie polinomiali e razionali positive si può implementare una seconda verifica in Python `Fraction`, senza riciclare i coefficienti estratti dal provider C++:

1. Esportare gli originali IEEE di E, G, SP-curve, trasformazioni e del risultato finale. Estrarre autonomamente le campate Bézier mediante inserimento esatto dei nodi e de Casteljau razionale.
2. Partizionare con tutte le preimmagini dei nodi di E, G e SP-curve. Calcolare le preimmagini affini come razionali esatti; per query che toccano un nodo, controllare entrambi i limiti one-sided. U decrescente non modifica l'orientamento del numeratore normale.
3. Su ogni sottointervallo, costruire in Bernstein il residuo omogeneo `J = H_E - Q * W_E` e il numeratore normale originale composto `A = (H_G' * W_G - H_G * W_G') cross D`, includendo trasformazioni e mappa U originali. Non derivare la composizione rispetto a t.
4. Certificare `W_E > 0`, `W_G > 0` e `|A| > 0` nell'intero intervallo. Le maggiorazioni vengono dai coefficienti Bernstein esatti e le radici dai certificati interi outward. Se non si conclude, suddividere oppure dichiarare inconclusivo.
5. Con `R = J / W_E`, porre `r_perp = |R cross A| / |A|`. Verificare prima il segno `d * (R dot A) < 0`. Allora un limite sufficiente per il residuo è

   `|F - Q| <= upper(r_perp) + (upper(abs(|R|^2 - d^2)) + upper(r_perp)^2) / abs(d)`.

   Il passaggio segue dalla razionalizzazione della componente parallela: il denominatore `|R_parallel| + |d|` è almeno `|d|`. Il controllo del segno deve precedere il quadrato, per non confondere offset di lati opposti.
6. Suddividere in razionali finché il bound indipendente conclude entro la tolleranza oppure il budget è esaurito. Non accettare quando il verificatore è inconclusivo. Confrontare il massimo dei bound indipendenti con il massimo riportato dal codice, distinguendo arrotondamenti e budget.
7. Mutare in memoria un polo finale di oltre la tolleranza e verificare che l'ultimo controllo rifiuti il risultato. Ripetere su un bordo riordinato per dimostrare che la mappa finale controlla il bordo giusto.

Il caso `d = 0` richiede il normale bound di posizione, senza la divisione per `abs(d)`. Le famiglie non supportate dal nuovo provider, le cuciture fra superfici generiche e il fit della superficie offset restano fuori da questa certificazione: il test del bordo non certifica l'intera faccia.

## Stato attuale

Verificato: difetto della baseline e prova geometrica rigorosa, dati originali e risultati finali identificati, piano di verifica.

Successivamente verificato: 40 / 40 casi, 301 campate continue e 943 poli finali, incluse varianti razionali, reverse, sense, pose e trim. Test negativo su un polo finale rifiutato. Esportazione dalla Release finale collegata senza oggetti stage identica byte per byte in tutti i 40 casi. Dettagli in `final-audit.md`. Scritture nel repository limitate alla cartella `independent/` autorizzata; produzione e CLAUDE.md invariati da questo audit.
