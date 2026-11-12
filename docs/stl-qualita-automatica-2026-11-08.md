# Qualita' automatica dell'esportazione STL/OBJ — 8 novembre 2026

Richiesta: migliorare il calcolo automatico, che produceva un modello STL
troppo approssimato. La modifica riguarda soltanto la mesh esportata;
geometria B-rep, precisione del documento e operazioni CAD restano indipendenti.

## Cause e implementazione

- Il suggerimento precedente usava i vertici `QVector3D` della mesh di display
  e la diagonale dell'intera scena: scarto diagonale/2000, angolo 10 gradi.
  Corpi distanti potevano peggiorare il dettaglio; la qualita' dipendeva anche
  dalla triangolazione visibile.
- Il recupero automatico poteva aumentare sia lo scarto sia l'angolo fino a
  16 e 4 volte i valori di partenza. Una mesh completa non implicava fedelta'
  sufficiente alle superfici.
- `cad_export::suggestedStlOptions` usa i box delle facce del kernel in double,
  includendo sfere/tori senza bordi e l'interno delle NURBS. Scarto automatico
  `clamp(diagonale_corpo_piu_piccolo / 10000, 1e-6, 0.01)` mm e angolo 2 gradi.
  Lato massimo iniziale: diagonale del corpo piu' grande/50, minimo 1e-6 mm.
  La distanza fra corpi non entra nella formula.
- Il builder automatico applica la qualita' suggerita anche a vecchi parametri
  troppo grossolani, preservando richieste gia' piu' precise. I recuperi possono
  aumentare soltanto il lato massimo; scarto e angolo restano entro il budget
  iniziale. Lo scarto si raffina per passi di 1,25, conservando il valore
  raggiunto anche quando occorre aumentare il lato: prima ogni tentativo
  ripartiva dai parametri iniziali. L'angolo resta costante, evitando una
  densita' eccessiva sui raccordi. Restano al massimo nove tentativi e i limiti
  del tessellatore non vengono aumentati.
- Prima di accettare una mesh automatica, si confronta il punto esatto della
  superficie al baricentro UV di ogni triangolo curvo con il piano della
  faccetta. Uno scarto eccessivo richiede un nuovo tentativo o un errore
  esplicito. I triangoli con area entro l'arrotondamento `64*epsilon*lato^2`
  non definiscono un piano affidabile: il controllo evita la divisione per
  quell'area; i writer controllano la degenerazione sulle coordinate salvate.
  Il controllo e' campionato e non certifica un massimo globale.
- Viewport e dialogo riusano il calcolo del modulo export. Opzioni manuali e
  formato persistente restano compatibili. STL e OBJ condividono preparazione
  e recupero, senza una seconda tassellazione per l'anteprima.

## Verifiche geometriche gia' eseguite

Lettore STL indipendente: misura radiale nei baricentri e nei punti medi dei
lati delle faccette realmente serializzate, includendo l'arrotondamento float.
Confronto con la formula del vecchio automatico, eseguito sulla CPU:

| Raggio sfera (mm) | Triangoli prima | Triangoli dopo | Scarto prima (mm) | Scarto dopo (mm) | Budget nuovo (mm) |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0,05 | 13.436 | 52.376 | 0,0000298419 | 0,00000767305 | 0,0000173205 |
| 5 | 13.428 | 52.440 | 0,00298405 | 0,000767341 | 0,00173205 |
| 50 | 13.298 | 65.906 | 0,0298409 | 0,00761388 | 0,01 |

Circa 3,9 volte meno errore nei tre casi; aumento dei triangoli di circa 4–5
volte. Il risultato dipende dalla geometria, non e' una promessa generale
su dimensioni del file o tempi di calcolo.

Patch NURBS cubica polinomiale: scarto verticale massimo ai centri dei
triangoli STL 0,00118715 mm, budget 0,00146969 mm. Anche patch razionale con
foro: STL/OBJ automatici completi, foro e copertura conservati. Verificati
corpi distanti 10.000 mm, compresenza di corpi grandi/piccoli, tetto 0,01 mm
e B-rep serializzato invariato. Regressioni permanenti nel runner
`forgecad_mesh_export_backend_tests`.

## Modelli reali e regressioni

Sonde temporanee collegate agli oggetti di produzione, su copie congelate
senza scrivere nei documenti originali. Parametri uguali a quelli suggeriti
dal dialogo; tutti i B-rep esportati verificati byte per byte dopo il calcolo.

- Shower Head salvato l'8 novembre alle 13:18, tre corpi visibili: STL
  **821.845 triangoli / 41.092.334 byte / quattro tentativi / 5,5 secondi**;
  OBJ **353.582 quad + 114.810 triangoli / quattro tentativi / 10,4 secondi**.
  Scarto iniziale 0,00655064424838 mm, angolo 2 gradi. Mesh complete e B-rep
  invariati; misure su una sola esecuzione CPU, non un benchmark generale.
- Mouse storico: snapshot recuperato con la regressione immutabile esistente,
  per conservare i B-rep precedenti senza rigenerare la geometria con il kernel
  attuale. Due corpi visibili: la faccia F1 di Raccordo 18 resta fuori dal
  budget di 0,00975613901808 mm anche nei recuperi; STL/OBJ rifiutati con errore
  esplicito. Limite aperto della triangolazione di quel trim; nessuna qualita'
  inferiore viene dichiarata riuscita. Originali e B-rep invariati.
- App `forgecad-release/forgecad` e runner completi compilati. **10/10 CTest
  pertinenti PASS**: export automatico/manuale, backend CPU/CUDA generico e
  specializzato, disponibilita' tassellazione, sorgenti geometrici, lettura
  documento, snapshot Mouse, mesh display e tolleranze.
  Dopo l'estensione dei casi NURBS automatici anche ai backend GPU, ripetute
  le tre regressioni CPU/CUDA generico/specializzato: tutte PASS.
- Il test generale `forgecad_view_tests` fallisce sul B-rep finale dello
  snapshot Applicatore. Stesso fallimento riprodotto sostituendo con quelli
  di HEAD tutti i sorgenti di produzione modificati, a parita' di fixture e
  ambiente: problema preesistente, non corretto in questo intervento.
- Avvisi di compilazione preesistenti in `forgeCad2026_dialogs_sketch.cpp`
  (indentazione) e `tests/test_viewport.cpp` (inizializzatori LoftContact).

Mappa finale aggiornata: **652 file / 9.346 relazioni / zero diagnostiche AST**;
`--check` e verifica HTML export/viewport nei due versi PASS, zero errori
JavaScript; schermate ispezionate. Gli artefatti generati restano ignorati.
Nessun collaudo manuale o certificato globale dell'errore dichiarato.
Eseguibile aggiornato e destinazione dei collegamenti desktop verificata:
`forgecad-release/forgecad`. Per usare la nuova build occorre riaprire l'app.
