# Lettura cache: confronto Linux del 25 ottobre 2026

Sul documento recente `2550_ASSIEME-ST5674-COMPLETO.prt` (1204 feature, 1200 B-rep, nessuno schizzo), il lettore a blocchi riduce la mediana di apertura da **7,213 a 2,908 s** (−59,7%). La sola lettura passa da 7,091 a 2,796 s; il picco RSS, inclusa la verifica successiva, da circa 2961 a 2590 MiB.

## Implementazione

`DataStream` legge le liste di QVector3D e int in blocchi di 64 KiB, rispettando byte order e precisione di QDataStream Qt 6.0. Per altre versioni resta il lettore scalare. Conteggi e disponibilità dei byte vengono verificati prima delle allocazioni; un blocco troncato produce errore. Le parti compresse del documento sono viste locali anziché copie. Formato, compressione e budget di sicurezza non cambiano.

La profilazione opzionale `FORGECAD_PROFILE_READ=1` separa decompressione, record/mesh, B-rep e storia. Il primo profilo individuava circa 4,794 s nei record/mesh; dopo la modifica 0,694 s. Questi profili diagnostici sono separati dai campioni prestazionali.

## Protocollo e verifica

Xeon W-10885M, 32 GiB, build Release Linux. Tre processi nuovi per binario, sei esecuzioni sequenziali alternate, stessa pipeline CPU e qualità 1, profilazione disabilitata. Riferimento conservato prima delle modifiche, non ricostruito dai sorgenti nuovi. Apertura riferimento: 6,931/7,213/7,261 s; nuova: 3,065/2,908/2,849 s. Nessuna build o analisi dell'agente concorrente durante le misure; frequenze, temperatura e carico esterno non controllati.

Firme B-rep, mesh/normali, visibilità ed errori identiche in tutti i campioni. Originale SHA256 invariato. Restano gli stessi quattro record importati vuoti agli indici 274/281/288/295: non sono dichiarati 1200 solidi validati geometricamente. Confronti funzionali ulteriori PASS su assieme formato 39, flacone e mouse2; nessuna scrittura dei documenti.

Le regressioni confrontano con QDataStream standard anche endian, precisioni, valori non finiti, zero con segno, subnormali, indici estremi, liste vuote/annidate e troncamenti. Dieci test CTest mirati PASS dopo la modifica.

Dati: [rapporto JSON](cache-read-optimization-2026-10-25.json). Binari storici, profili e campioni completi restano in `forgecad-linux-solver/benchmarks/cache-read/`. Le sonde sono state estese successivamente: gli hash del rapporto identificano la versione realmente misurata, non necessariamente il runner finale.

Riproduzione con un binario di riferimento conservato:

```sh
python3 tools/benchmark_document_mesh.py /percorso/assieme.prt \
  --build-dir forgecad-linux-solver --reference-binary /percorso/runner-prima \
  --cached --no-profile --repeats 3 --output /tmp/confronto-cache-nuovo
```

Il codice è comune a Mac e Linux; queste misure sono solo Linux. Non misurano framerate, disco freddo o latenza completa dell'interfaccia.
