# Verifica indipendente continua — bordi offset di estrusioni

## Esito

**40 / 40 casi PASS** sullo staging congelato: tutti i **32 bordi finali dopo assemblaggio** di otto offset pubblici, più otto fit diretti con bordo E e generatore G distinti. Sono coperti **943 poli effettivamente restituiti e 301 campate complete**.

Il massimo limite superiore continuo indipendente è

`4.364583310131473e-8 < 1e-7`.

Il valore è convertito in double con un passo `nextafter` verso +infinito. Ogni confronto con la tolleranza viene prima effettuato con razionali esatti; numeratore e denominatore del bound di ciascun caso sono conservati in `continuous-results.json`.

Le varianti includono curve polinomiali e razionali positive di grado 27, U crescente e decrescente, senso della faccia invertito, distanza negativa, wrapper Trimmed e Transformed, posa con rotazione/traslazione, SP-curve affini originali e dominio del bordo traslato a [-3, 5]. Sono comprese le due curve laterali con U costante di ogni risultato pubblico. Tutti i bordi finali vengono abbinati una volta sola e controllati con tutti i loro poli; nei casi reverse l'ordine finale è diverso (3, 2, 1, 0).

Il corpo sorgente e ogni risultato pubblico superano `checkBody` nell'esportatore. Questo controllo topologico è distinto dalla certificazione geometrica continua Python.

Un test negativo sposta di `1e-4` un solo polo del bordo finale del riproduttore. Il verificatore rifiuta il risultato con residuo non risolto di circa `1e-4` alla profondità terminale 18. Il test non modifica sorgenti, fixture positive o geometrie persistite.

## Staging identificato

Hash confermati prima delle prove:

- `fk_curve_fit_bounds.cpp`: `2e530b7bcf6e5095bc5328896e3e4e7f61af599c018b19cbab415c0f4513e493`.
- `fk_offset.cpp`: `a7b2bf30ba1e8e92bd565104075e43b5704f3928b3acb77cc9cab6a940e30e42`.
- `fk_curve_fit_bounds.h`: `ea301eda4078be8f7c8e7f17036c8e63f32e71c15cba2d758f968c3bb1c18dc5`.

I due oggetti privati aggiornati dello staging sono stati collegati all'esportatore insieme alla libreria kernel della macchina. Nessuna compilazione condivisa o modifica di produzione è stata necessaria per questo audit.

## Attribuzione alla Release finale

Dopo l'integrazione da parte del coordinatore, l'esportatore è stato ricompilato collegando **soltanto** la libreria Release corrente, senza i due oggetti dello staging. La nuova esecuzione ha prodotto tutti i 40 JSON **identici byte per byte** a quelli della prova privata: **zero differenze** fra coefficienti originali e poli finali.

I certificati Fraction delle 301 campate si applicano quindi anche agli esatti poli restituiti dalla Release finale. La libreria finale ha SHA256 `d9dba05b72a5d049ac40a5f650c023c652965986dd7147e109c2a53e7f38e0d3`. Comando effettivo:

```sh
c++ -std=c++17 -O2 -DHAVE_EDGE_PROVIDER \
  -I/home/myungit/Linux_CAD_forFree/kernel \
  /tmp/review-offset-proof/export_audit.cpp \
  /home/myungit/Linux_CAD_forFree/forgecad-release/kernel/libforgekernel.a \
  -o /tmp/review-offset-proof/export_audit_release
/tmp/review-offset-proof/export_audit_release
```

`release-comparison.json` conserva hash dell'esportatore e della libreria, più i due hash identici di ciascuna fixture. `release-comparison.log` riporta l'esito compatto.

## Indipendenza della prova

`export_audit.cpp.txt` contiene fixture ed esportatore completi. Esporta in hexfloat gli **input effettivi** E, G, SP-curve, pesi, nodi, intervalli e matrici originali dei wrapper, oltre alla direzione effettiva normalizzata dal costruttore di ExtrusionSurface. Esporta direttamente tutti i poli, nodi e pesi delle curve finali, senza usare `bezierSegments()` per fornire al verificatore coefficienti già estratti in double.

`certify_final.py.txt` non importa, chiama o legge il provider di produzione. Esegue autonomamente:

1. Conversione esatta dei valori IEEE in `Fraction`.
2. Inserimento razionale esatto dei nodi e restrizione de Casteljau per E, G, SP-curve e risultato finale. I wrapper trasformati vengono applicati ai coefficienti omogenei con aritmetica esatta, senza trasformare prima i poli in double.
3. Partizione con nodi finali, nodi sorgenti e preimmagini razionali esatte dei nodi del generatore attraverso U originale. Il verso decrescente di U modifica la composizione e **non** il segno della normale. Le derivate del generatore restano rispetto a U originale.
4. Controllo dei pesi positivi e del numeratore normale non nullo sull'intero intervallo. Verifica della copertura esatta di tutti gli intervalli finali.
5. Limite superiore su ogni intera campata mediante coefficienti Bernstein esatti e radici superiori certificate da `isqrt` con denominatore `2^128`.

Le campate risultanti delle fixture erano già abbastanza piccole: tutte le 301 celle hanno concluso senza ulteriori bisezioni del verificatore indipendente. La possibilità di suddividere resta nel codice; un intervallo terminale inconclusivo viene rifiutato.

## Identità usata dal verificatore, diversa dal provider C++

Il bersaglio originale è

`F = E + d * n / |n|`, dove `n = G'(U) cross D`.

Su ogni cella E ha coordinate omogenee `H_E / W_E`; il generatore ha `H_G / W_G`, ed è derivato rispetto al suo parametro originale. Si costruisce esattamente

`A = (H_G' * W_G - H_G * W_G') cross D`, così che `n = A / W_G^2`.

Per Q, cubica polinomiale **finale**, si pone

`J = H_E - Q * W_E`,

`K = J * W_G^2 + d * W_E * A`.

Il primo contributo all'errore è il bound Bernstein di

`|E - Q + d*n| = |K| / (W_E * W_G^2)`.

Il contributo di normalizzazione usa l'identità e la disuguaglianza

`|n/|n| - n| = |1 - |n|| <= abs(|n|^2 - 1)`.

Quindi un limite superiore sufficiente è

`upper(|K|)/(min(W_E)*min(W_G)^2) + |d|*upper(abs(A dot A - W_G^4))/min(W_G)^4`.

Ogni operazione polinomiale e ogni hull è esatta; soltanto la radice del limite della norma viene racchiusa outward. L'identità non usa il segno del prodotto parallelo o la razionalizzazione adottata dal provider C++. Per queste fixture il generatore ha velocità vicina a uno, perciò la stima indipendente conclude con margine. La formula resta valida fuori da tale condizione, ma può risultare troppo larga e allora non conclude.

## Prova rigorosa del difetto precedente

`prove_fraction.py.txt`, `proof-result.json` e `rational-certificate.json` conservano la prova baseline separata. Un punto dell'offset originale ha quota Z maggiore del massimo globale dei poli di tutti i bordi restituiti dalla baseline di almeno

`5.742474088627500934414282527335482625482475646090699016e-7`.

Pesi positivi e inviluppo convesso forniscono una separazione geometrica dall'unione completa dei bordi della baseline, indipendente dal parametro. Il difetto precedente è quindi certificato, mentre lo staging rispetta il budget con un bound continuo indipendente sui poli finali.

## Artefatti e ripetizione

- `cases/*.json`: 40 input completi originali/finali in esadecimale.
- `continuous-results.json`, `final-summary.json`, `continuous.log`: certificati e riepilogo positivo.
- `negative-one-final-pole.json`, `.log`: controllo negativo.
- `export_audit.cpp.txt`, `certify_final.py.txt`, `run_continuous.py.txt`: sorgenti completi.
- `fixture-original.txt`, `export_original.cpp.txt`, `prove_fraction.py.txt`: prova baseline.
- `manifest.json`, `final-stage-sha256.json`, `SHA256SUMS`: identità e integrità.

Gli script sono archiviati con suffisso `.txt`. Per ripeterli, copiarli con i nomi originali sotto `/tmp/review-offset-proof`; `run_continuous.py` controlla tutti i JSON in `cases/`, usando processi Python con contesto `fork`. L'avvio predefinito `forkserver` è stato impedito dal sandbox per l'apertura del socket locale; il contesto `fork` ha concluso regolarmente senza escalation.

## Limiti

La prova certifica le fixture esportate, comprese famiglie e wrapper indicati, rispetto ai loro coefficienti originali IEEE. Non dimostra l'assenza universale di difetti del provider e non certifica famiglie non esportate, giunzioni di SP-curve non affini, curve con pesi non positivi, normali singolari, cuciture fra superfici generiche o il fit completo della superficie offset. Non certifica la genealogia topologica: l'esportatore abbina gli output per estremi e la prova dimostra poi l'intero bound geometrico contro gli originali. La mappa genealogica di produzione resta oggetto delle verifiche C++ separate.

Scritture nel repository limitate alla cartella `independent/` autorizzata; nessuna modifica a produzione, catalogo o memoria.
