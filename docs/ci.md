# Controlli automatici su GitHub

Il workflow parte sulle pull request e sui push a `main`. Si può avviare
manualmente da **Actions → CI Linux e macOS → Run workflow**.
[Configurazione del workflow](../.github/workflows/ci.yml)

## Integrare una modifica

1. Lavora su un branch e fai i controlli locali mirati ai file modificati.
2. Apri una pull request verso `main` per avviare la suite completa su
   Linux e macOS. Aggiorna lo stesso branch per correggere eventuali problemi.
3. Verifica che entrambi i job sulla revisione da integrare siano verdi,
   poi usa **Squash and merge** per pubblicare un unico commit su `main`.

L'aggiornamento di una pull request avvia la suite una volta. Il push a
`main` dopo il merge verifica lo stato pubblicato.

La CI di `d1fa1b2` è riuscita su Linux con 121 test superati e 3 skip
e su macOS con 125 test superati e 2 skip.
[Esito della CI](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/45351370347)

## Cosa controlla

- Configurazione Release e compilazione dell'intera applicazione `forgecad`.
- Compilazione di tutti i target di test abilitati nella configurazione CI.
- Esecuzione di tutti i test CTest così configurati, inclusi kernel geometrico
  e confronti con OpenCASCADE, documenti/compressione, revisioni, dipendenze,
  esportazione CPU e runner autonomi delle tavole 2D.

L'ambiente è **Ubuntu 26.04 x86-64**, con Qt 6, OpenGL, zlib, Zstandard e
OpenCASCADE dai pacchetti della distribuzione. Ubuntu 26.04 fornisce OCCT 7.9.
OCCT serve solo a `forgekernel_tests` (`kernel/occt_tests`, opzione
`FORGECAD_BUILD_OCCT_TESTS`): l'applicazione non lo collega. I toolkit di scambio
si scelgono secondo la versione (`TKDE*` da OCCT 7.8, nomi storici prima). I runner GitHub standard non forniscono una GPU NVIDIA
e questa build usa il backend CPU senza installare CUDA.
L’installazione Linux sostituisce i mirror cloud Azure ed EC2 del runner
con il mirror principale Ubuntu via HTTPS, conservando le verifiche delle firme APT. Aggiorna sia le
sorgenti `.list`/`.sources` sia `apt-mirrors.txt` e `apt-security-mirrors.txt`,
utilizzati dai runner tramite `mirror+file:`. Ogni connessione ha
un timeout di 30 secondi e fino a tre tentativi; il passaggio delle dipendenze
ha un limite complessivo di 15 minuti. Anche `dependencies.log` viene conservato,
per distinguere problemi di download dagli errori del compilatore e dei test.

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
simultanee e due test simultanei limitano il consumo di memoria. Aggiornare
una pull request o fare un nuovo push a `main` annulla la relativa esecuzione
ancora in corso.

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

Il kernel rileva con una prova di compilazione e link la disponibilità di
`std::from_chars` per i decimali. Le versioni di libc++ Apple che non lo offrono
usano `strtod_l` con locale C privato e un token limitato al buffer ricevuto.
Le regressioni STEP verificano anche subnormali, overflow e input non terminati
da NUL, senza modificare il locale globale dell'applicazione.

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


### Sezioni circolari dello sformo e letture concorrenti del catalogo

I fallimenti del 2 novembre 2026 avvenivano durante i test, dopo la compilazione
riuscita su Linux e macOS. Lo sformo conserva ora analiticamente le sezioni
circolari dei supporti sostitutivi; le letture coerenti del catalogo revisioni
non prenotano il lock del writer. Le regressioni verificano sformi ripetuti e
pubblicazioni concorrenti senza allargare tolleranze o disattivare prove.
Diagnosi, riproduttori e risultati sono nel
[rapporto di riparazione](ci-riparazione-2026-11-02.md).


### Cuciture dei raccordi su coperchi spline

Il fallimento del 6 novembre era nel test del raccordo ruotato, dopo build
riuscite su entrambe le piattaforme. Il raffinamento delle due sezioni sulla
cucitura riduce lo scarto prima del controllo di coincidenza; non cambia le
tolleranze di accettazione. La regressione verifica anche una seconda
rototraslazione. [Diagnosi e prove](ci-raccordi-2026-11-06.md).


### Spessore interno e chiusure tangenti dei raccordi

I fallimenti dell’8–10 novembre avvenivano nei test dopo build riuscite. La suite
offset aperto ora dispone di 900 secondi: comprende costruzioni NURBS dense che
richiedono circa 600 secondi CPU, oltre il vecchio limite di 180 sui runner con
pochi core. Tutti i casi e i controlli geometrici restano attivi. La chiusura
tangente dei raccordi smorza i passi Newton e usa la continuazione stabile anche
per bordi analitici, con gli stessi limiti di scarto; una rotazione perturbata
di 1e-14 rad riproduce il problema anche su Linux.
[Diagnosi, correzione ed esiti](ci-riparazione-2026-11-10.md).


### Raccordo cavità: fallimenti del 12–14 novembre 2026

I quattro run falliti compilavano correttamente su Linux e macOS. Falliva
`BlendCavityWholeRimKeepsRadiusHistoryAndClosedTrims` per il contatto fuori dal
dominio del fianco F0. La correzione K12 mantiene invariati test, fixture e
soglie; suite cavity8/8 e controlli locali correnti PASS. La conferma sul nuovo
commit remoto è distinta dalle verifiche locali.
[Diagnosi e stato della verifica](ci-verifica-2026-11-16.md).

### Quadratura portabile

Il 24 novembre 2026 entrambe le build del commit `1a450e0` sono riuscite.
I test hanno trovato cinque fallimenti nelle prove delle
booleane e uno nel calcolo effettivo delle proprietà di massa di un raccordo.
La bisezione assegnava metà budget a ogni figlio anche quando la difficoltà
era concentrata vicino a una tangente. Il figlio che ha già raggiunto
l'accuratezza richiesta cede ora il budget inutilizzato. Se entrambi devono
essere raffinati, la ripartizione segue le rispettive stime d'errore.
La tolleranza complessiva e il rifiuto a profondità esaurita restano attivi.

Su macOS tre richieste di lunghezza per ciascuna di due B-spline regolari
restituivano zero con una velocità subnormale. Il calcolo della velocità
riscalata usa ora la componente massima prima di applicare la norma.
Il test verifica sia l'API delle lunghezze sia la cache. Il nuovo caso
dell'estremo a radice quadrata copre la quadratura scalare e vettoriale
in entrambi i versi. [Esecuzione GitHub iniziale](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/45338480279).

Su Linux locale la correzione supera i sei casi riprodotti, tutti i 488
casi del runner generale del kernel e le suite di quadratura e quantizzazione.
La build Release locale è riuscita.

Il primo job Linux di `1665232` ha raggiunto il limite di 15 minuti
durante il download dei pacchetti su `us-east-2.ec2.archive.ubuntu.com`.
Il workflow estende anche ai mirror EC2 il reindirizzamento già usato
per Azure. I pacchetti e le verifiche delle firme APT sono gli stessi.
[Log del job Linux](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/45344148977/job/138274546494).

Il run di `b0a9407` ha completato la CI Linux e superato il runner
generale del kernel anche su macOS. Su macOS restavano quattro
asserzioni fallite nella suite di quadratura, tutte sulle richieste
alla cache. La lunghezza d'arco diretta era corretta. La cache del
loft e la lunghezza diretta condividono ora `arcLengthSpeed` per
riscalare la derivata e verificare i valori finiti. Il test conserva
sia la richiesta iniziale sia quella ripetuta dalla cache.
[Run di b0a9407](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/45346555868).

In locale passano i 16 casi di quadratura e i 6 di lunghezza d'arco.
