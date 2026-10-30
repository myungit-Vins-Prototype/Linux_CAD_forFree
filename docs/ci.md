# Controlli automatici su GitHub

Il workflow [CI Linux](../.github/workflows/ci.yml) parte a ogni push e pull
request. Dalla scheda **Actions → CI Linux → Run workflow** si può avviare anche
manualmente, dopo che il file è stato pubblicato sul branch predefinito.

## Cosa controlla

- Configurazione Release e compilazione dell'intera applicazione `forgecad`.
- Compilazione di tutti i target di test abilitati nella configurazione CI.
- Esecuzione di tutti i test CTest così configurati, inclusi kernel geometrico
  e confronti con OpenCASCADE, documenti/compressione, revisioni, dipendenze,
  esportazione CPU e runner autonomi delle tavole 2D.

L'ambiente è **Ubuntu 26.04 x86-64**, con Qt 6, OpenGL, zlib, Zstandard e
OpenCASCADE dai pacchetti della distribuzione. Ubuntu 26.04 fornisce OCCT 7.9;
i nomi delle librerie OCCT usati attualmente dai confronti del kernel richiedono
OCCT 7.8 o successivo. I runner GitHub standard non forniscono una GPU NVIDIA
e questa build usa il backend CPU senza installare CUDA.

`FORGECAD_BUILD_VIEW_TESTS` e `FORGECAD_BUILD_RIBBON_PREVIEW` sono esplicitamente
disattivati. La suite viewport comprende anche prove che richiedono documenti
CAD esterni, non inclusi nel repository; la CI non la esegue. I runner
autonomi delle tavole restano abilitati e usano Qt offscreen.

Un secondo job su macOS 15 Apple Silicon installa le dipendenze con Homebrew e compila anche il backend Metal. Entrambi i job impostano `FORGECAD_ENABLE_CUDA=OFF`; le prove Metal richiedono un dispositivo disponibile e distinguono SKIP da PASS. La CI verifica il codice sui runner ospitati; rendering OpenGL nativo, CUDA/NVIDIA e documenti personali richiedono prove separate. Non produce pacchetti installabili.

## Come leggere il risultato

Il badge del README legge lo stato reale del workflow da GitHub. **Passing**
significa che configurazione, compilazione e test previsti sono riusciti;
**failing** richiede di aprire l'esecuzione e controllare il passaggio fallito.
Prima della prima esecuzione non esiste ancora un risultato verificato.

Un errore di compilazione o test fa fallire il job. CTest rifiuta anche una
configurazione senza test. I log di configurazione, compilazione e CTest e il
rapporto JUnit vengono conservati per sette giorni negli artifact dell'esecuzione,
anche in caso di errore quando quei file sono stati prodotti. Due compilazioni
simultanee e due test simultanei limitano il consumo di memoria. Un nuovo push
sullo stesso branch annulla l'esecuzione precedente ancora in corso.

Il limite generale è di quindici minuti per test CTest: la suite generale del
kernel è una singola voce che racchiude centinaia di casi. I limiti più brevi
impostati esplicitamente da CMake per le singole suite restano applicati.

Il workflow dispone della sola lettura dei sorgenti e non richiede segreti
personali. Il token temporaneo fornito da GitHub viene usato dalle azioni;
la procedura non esegue push, pubblicazioni o installazioni sulle macchine
degli utenti.

## Riprodurre la verifica su Linux

Installare le dipendenze elencate nel workflow su Ubuntu 26.04, quindi:

```sh
cmake -S . -B build-ci -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFORGECAD_ENABLE_CUDA=OFF \
  -DFORGECAD_BUILD_VIEW_TESTS=OFF \
  -DFORGECAD_BUILD_RIBBON_PREVIEW=OFF
cmake --build build-ci --parallel 2
QT_QPA_PLATFORM=offscreen QT_QPA_OFFSCREEN_NO_GLX=1 \
QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion LIBGL_ALWAYS_SOFTWARE=1 \
  ctest --test-dir build-ci --output-on-failure --no-tests=error \
    --parallel 2 --timeout 900 --output-junit "$PWD/build-ci/ctest.xml"
```

Usare una cartella nuova, senza cache provenienti da altre macchine. Su una
macchina con toolkit CUDA, `FORGECAD_ENABLE_CUDA=OFF` seleziona esplicitamente il backend CPU senza avviare nvcc. Per verificare CUDA configurare una build separata con l’opzione `ON`.


### Coerenza numerica tra processori

Il target `forgekernel` richiede `-ffp-contract=off` a GCC/Clang/AppleClang,
anche per i chiamanti C++ e Objective-C++ che compilano le sue funzioni inline.
Le operazioni scritte separatamente mantengono arrotondamenti separati: una
specializzazione B-spline e il percorso generale devono dare gli stessi prefissi
di derivate, anche quando il processore dispone di FMA. Restano attive le
ottimizzazioni, la vettorizzazione e le `std::fma` esplicite dei predicati esatti.
La regola non viene passata al compilatore CUDA.

Su un processore compatibile AVX2/FMA, per riprodurre anche il percorso
`x86-64-v3`, aggiungere `-DCMAKE_CXX_FLAGS=-march=x86-64-v3` alla configurazione
in una cartella dedicata. Non usare questa opzione sulle altre architetture.

Il test di trascinamento costruisce e consegna ogni evento del mouse prima di
creare quello successivo, evitando che lo stato globale condiviso del puntatore
in Qt 6.10 alteri la posizione della pressione. Il volume del raccordo dell'intero
bordo arco/segmenti è verificato con sezioni analitiche integrate indipendentemente:
per questo caso OCCT può restituire un volume superiore a quello del pezzo iniziale.
I controlli geometrici, di topologia e di tassellazione restano attivi.

Il raccordo che svanisce alla tangenza dei due appoggi raffina il fit in base
all'incertezza dei campioni interni: la stima singolare di un estremo non deve
autorizzare uno scarto grande su tutto l'intervallo. La regressione verifica
anche la distanza dei bordi dagli appoggi cilindrici, oltre al volume rimosso.
