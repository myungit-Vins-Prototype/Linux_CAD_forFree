# Replay indipendente Plane — 16 novembre 2026

Queste prove sono state ricostruite dopo la perdita del vecchio staging temporaneo. Non sono i 67 controlli storici e non ne ricostruiscono artificialmente il totale. Risultati macchina: [results.json](results.json); hash sorgenti/archivio collegato: [source-hashes.json](source-hashes.json).

## Risultati realmente ottenuti

- **16 geometrie**: polinomiale/razionale × sopra/sotto il piano × piano identità/ruotato e traslato × direzione normale/obliqua. Grado 27, dominio [0,1], scala 1; fixture `hidden` e coefficienti `z27` dal test permanente originale.
- **16 verifiche esatte delle proprietà delle sorgenti**: W positivo, n·d diverso da zero, derivata della coordinata lungo X strettamente positiva per sorgente e proiezione, parametro della semiretta strettamente positivo.
- **16 certificati dei pezzi**, 688 campate: 44 per ognuna delle 8 curve polinomiali, 42 per ognuna delle 8 razionali.
- **16 certificati delle catene finali**, sulle medesime 688 campate, dopo `joinProjectedPieces`. Questo secondo passaggio non viene contato come ulteriori 688 geometrie distinte.
- **Due scenari di giunta fra tre sorgenti**, con gap di 5e-9: concatenazione normale (132 campate) e fusione primo/ultimo (catene da 88 e 44 campate). Sono 264 campate aggiuntive. Il massimo di questi scenari è 9.763302786519937e-8 mm.
- Massimo superiore di tutti i certificati correnti: **9.908761801215598e-8 mm**, riportabile conservativamente come **9.908761802e-8 mm**. Ogni campata rispetta sia la `deviation` effettivamente pubblicata sia la tolleranza IEEE `1e-7`. Non è stata necessaria alcuna suddivisione aggiuntiva del residuo: una foglia Bernstein per campata.
- **16 testimoni geometrici della baseline del modulo Git**: distanza inferiore dalla proiezione esatta all'involucro globale dei poli dell'output almeno **5.415640758484547e-5 mm**. È un nuovo valore ricostruito; non riutilizza la misura storica 5.41618e-5.

## Metodo e indipendenza

`dump.cpp.txt` chiama soltanto la pipeline pubblica per generare gli output e serializza in esadecimale tutti i dati `double`: poli/pesi/nodi sorgenti, origine e assi X/Y **della Plane effettiva**, direzione originale, poli/nodi finali e scarti dichiarati. Il main del test permanente viene rinominato: vengono riutilizzate le definizioni della fixture, ma le sue verifiche campionate e `exactProjection` non vengono eseguite dall'audit. Il kernel fornisce il risultato da verificare, mai l'oracolo matematico.

`certify.py.txt` usa soltanto la libreria standard Python e `Fraction.from_float(float.fromhex(...))`. Ogni dato IEEE è trattato come razionale esatto. Con `n=X×Y`, `C=H/W` e `nd=n·d`, costruisce esattamente

```
A = H*nd - d*(n·H-(n·O)*W)
R = A - Q*W*nd
P-Q = R/(W*nd)
```

Per ciascuna campata restringe H/W originali mediante de Casteljau esatto; i prodotti Bernstein usano i rapporti binomiali esatti. Il limite al quadrato è la somma dei quadrati dei massimi assoluti dei coefficienti per coordinata, divisa per `(min(W)*abs(nd))²`. Il confronto con `deviation²` e `tolerance²` avviene interamente fra frazioni. I limiti decimali JSON sono arrotondati verso l'alto e riconfrontati col quadrato razionale, conservato come numeratore/denominatore per ogni campata in [certificates.json](certificates.json).

Per le catene finali si verifica la molteplicità dei nodi e la forma Bernstein della cubica realmente pubblicata. Le mappe affine normalizzate portano ai domini sorgenti salvati **prima della giunta**; non si assume esatta una traslazione floating point dei nodi. Copertura completa [0,1] e assenza di buchi sono controllate esattamente. I controlli finali, compresi gli estremi mediati, sono direttamente il candidato Q.

`baseline_witness.py.txt` costruisce un punto esatto a parametro razionale `8793/10000` o `1207/10000`, e lo separa dall'intero hull dei poli degli output lungo l'asse Y originale. La separazione proiettata, divisa per la norma dell'asse, fornisce un limite geometrico inferiore indipendente dalla corrispondenza dei parametri o da un solver di punto più vicino. Il confronto `distanza² > tolerance²` è esatto; il numero stampato è arrotondato verso il basso.

Il provider `planeProjectionErrorBound` e le altre funzioni di bounds di produzione non vengono chiamati né importati dagli script di certificazione. La mappa architetturale è stata consultata prima della ricostruzione: quattro descrizioni e 85 archi incidenti sono conservati in [architecture-consult.json](architecture-consult.json).

## Baseline e limiti

La baseline è il solo `fk_project.cpp/h` da Git HEAD, preparato in `root-replay/baseline`, compilato separatamente e collegato prima dell'archivio kernel Release corrente. Il diff corrente dei due file è stato riletto: modifiche limitate al blocco K10 Plane/provenienza/giunte/contratti. Il test usa il trait `HasDeviation`, quindi la vecchia API resta compilabile; nel dump la metrica assente delle vecchie catene è rappresentata da -1 e non viene certificata. La baseline così ottenuta confronta il modulo storico sul resto kernel corrente: **non ricostruisce il vecchio archivio completo perduto**. Gli hash distinguono queste componenti.

Questa prova non certifica la selezione globale del primo impatto, cambi faccia, classificazione dei bordi, supporti diversi da Plane o altre famiglie sorgenti. Non ripete scale, trim/istanze o sanitizers del runner permanente e non verifica il renderer. Non cambia tolleranze né file di produzione, CMake, grafico o `CLAUDE.md`. L'inventario dei dieci siti completamente aperti è in [inventory.md](inventory.md).

## Ripetizione

Dalla radice `/home/myungit/Linux_CAD_forFree`, usare `bash docs/curve-fit-k10-projection-prove-2026-11-16/independent-replay/replay.sh.txt`. Lo script copia le sorgenti con suffisso `.txt` in `/tmp/projection-replay-audit-rerun`, compila i dump privati, confronta byte per byte gli output e ripete i certificati correnti e i testimoni baseline. I dati archiviati possono essere certificati anche senza ricompilare: copiare `certify.py.txt` come `certify.py` e `baseline_witness.py.txt` come `baseline_witness.py`, quindi fornire i corrispondenti JSONL e il percorso del risultato. Nessuna registrazione CMake o modifica del catalogo architetturale.
