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
CAD esterni, non inclusi nel repository; questa prima CI non la esegue. I runner
autonomi delle tavole restano abilitati e usano Qt offscreen.

La CI non certifica macOS, CUDA/Metal, rendering OpenGL nativo o percorsi su
documenti personali. Queste verifiche restano distinte. Non produce né pubblica
pacchetti installabili.

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
  -DFORGECAD_BUILD_VIEW_TESTS=OFF \
  -DFORGECAD_BUILD_RIBBON_PREVIEW=OFF
cmake --build build-ci --parallel 2
QT_QPA_PLATFORM=offscreen QT_QPA_OFFSCREEN_NO_GLX=1 \
QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion LIBGL_ALWAYS_SOFTWARE=1 \
  ctest --test-dir build-ci --output-on-failure --no-tests=error \
    --parallel 2 --timeout 900 --output-junit "$PWD/build-ci/ctest.xml"
```

Usare una cartella nuova, senza cache provenienti da altre macchine. Su una
macchina con toolkit CUDA CMake può abilitarlo automaticamente: il risultato
non rappresenta la stessa configurazione CPU del runner GitHub.
