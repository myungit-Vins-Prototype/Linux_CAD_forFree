# Proprieta di massa: facce indipendenti in parallelo — 26 ottobre 2026

Prosecuzione su `solver-mesh-gpu`, lasciando main invariato come richiesto. Il riferimento di questo intervento include gia la cache UV delle cuciture e il solver a banda: il beneficio riportato sotto misura solo il nuovo intervento.

## Calcolo conservato

`massProperties` integrava una faccia alla volta. Su mouse2, il solido cucito richiede15facce: alcune costano circa2secondi ciascuna, per~8,5s complessivi nella scelta dell'orientamento. Il calcolo continua a produrre tutte le proprieta di massa, con le stesse quadrature e tolleranze; non si usa una stima del solo segno.

In `kernel/fk_mass.cpp` si eseguono le quadrature indipendenti tramite il parallelFor esistente del kernel se il corpo ha almeno8facce, la macchina ha piu di un thread e il chiamante non e gia un worker parallelo. Corpi piccoli e calcoli annidati rimangono seriali; niente moltiplicazione dei thread dentro le operazioni gia parallele.

I risultati si raccolgono per indice di faccia e si sommano poi nello stesso ordine originale. Anche gli errori geometrici si raccolgono per indice e viene rilanciato il primo secondo quell'ordine, dopo il join dei worker. La cancellazione usa il token condiviso, interrompe i worker e viene rilanciata dopo il join senza essere confusa con un errore geometrico. Lo spazio temporaneo e lineare nel numero delle facce e si libera al ritorno: nessuna cache globale o nel documento.

Il codice di FaceIntegrator e faceArea e rimasto identico, verificato anche confrontando i sorgenti prima/dopo. Nessuna nuova API o modifica a fk_sew, CUDA, Metal, formati o tolleranze.

## Tempi sul documento reale

Quattro processi nuovi per versione, ordine bilanciato prima/dopo, qualità1 CPU e ricostruzione completa di mouse2. Profilazione disattivata; firme B-rep/mesh fuori timer. Timer di apertura = lettura+load con geometria e mesh; niente rendering GL o framerate. Nessuna build/mappa/sonda contemporanea. Clock, temperatura e altri carichi non controllati; filesystem non svuotato.

| Versione | Mediana | Minimo–massimo |
|---|---:|---:|
| Precedente, con cache UV | 32.937s | 32.346–34.004s |
| Nuova, massa per facce | 26.803s | 26.702–27.098s |

**18.6% di tempo in meno**, rapporto precedente/nuovo **1.229×**. I campioni non si sovrappongono. Questo risultato non va sommato direttamente alle percentuali di lotti precedenti eseguiti in condizioni diverse.

Nel singolo profilo diagnostico separato, la fase volume scende8,471→2,140s; ricostruzione33,640→27,448s. Questi profili hanno strumenti diagnostici attivi e non sostituiscono il lotto senza profiler della tabella.

## Correttezza

Tutti i16B-rep, mesh49.279triangoli, visibilità/errori sono identici bit per bit tra le versioni e le ripetizioni. Originale mouse2.prt SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617` invariato.

Nuove regressioni in `kernel/tests/test_mass.cpp`:

- Volume, area, baricentro e9componenti d'inerzia bit identici fra percorso automatico e seriale annidato; prismi17lati, altezze positive/negative/sottili e origini distanti, con formula indipendente del volume.
- Due richieste concorrenti sullo stesso corpo, input binario immutato.
- Dieci facce da8archi NURBS razionali estrusi: volume/area/baricentro del cilindro noti, tutte le proprieta bit identiche fra percorsi.
- Primo errore dello stesso tipo/messaggio su corpo con piu facce invalide; token di cancellazione gia richiesto e successivo recupero senza token.

10/10MassPASS con gli stessi test finali collegati alla libreria precedente e alla nuova; comprende i confronti analitici/OCCT preesistenti.9/9Sew e19/19OffsetPASS sul kernel finale.42/42CTest applicativiPASS in11,55s; app/viewport e standalone CUDA/broad phase/export ricompilati o ricollegati al kernel finale prima dei test. Runner kernel mirati con flag CMake, non eseguita l'intera suite kernel.

QA OpenGL nativa dopo ricostruzione completa mouse2: tre riaperture anteprima, prima immagine e cache ricreata uguali (zero pixel diversi), zero errori GL e B-rep/input conservati.

## Altri carichi e limiti

Una sonda separata sui prismi piani (un processo,21ripetizioni alternate per caso) non rileva regressioni:6facce0,129→0,127ms;8facce0,195→0,129ms;14facce0,397→0,233ms;26facce0,827→0,361ms;66facce2,116→0,855ms. Sono microbenchmark diagnostici, non una statistica generale del CAD.

La fixture curva gia versionata `kernel/tests/data/mouse_sewn.body` ha10facce: tutti i14campi delle proprieta di massa sono bit identici, ma il singolo campione e4,699→4,617s, senza un vantaggio significativo. Una singola faccia dominante limita il parallelismo: non si promette accelerazione su ogni corpo.

Mappa aggiornata:494file/6685relazioni,235TU senza diagnostiche; --checkPASS e79577siti entro le righe. Chiamate massProperties→parallelFor e test→massProperties esaminate in entrata/uscita, include separati. HTML/JSON della mappa ignoratiGit.

Implementazione C++17 comune anche a macOS, ma compilazione/QA nativeMac non eseguite qui. Main, installazione desktop, commit e push invariati. Prossimi costi: rimappature UV ancora~13s e Loft6~6–7s nel documento, da analizzare separatamente.

[Dati, campioni e hash](mass-parallel-2026-10-26.json); profili/runner precedenti, sonde e log in `forgecad-linux-solver/benchmarks/mass-parallel-2026-10-26`. Sorgenti dei piccoli probe conservati con i risultati.

## Riproduzione

```sh
cmake --build forgecad-linux-solver --target forgecad forgecad_view_tests forgekernel_tests -j4
forgecad-linux-solver/kernel/forgekernel_tests Mass
python3 tools/benchmark_document_mesh.py /percorso/mouse2.prt \
  --build-dir forgecad-linux-solver --reference-binary /percorso/runner-prima-della-massa-parallela \
  --output /percorso/nuova-directory --no-profile --repeats 4
```
