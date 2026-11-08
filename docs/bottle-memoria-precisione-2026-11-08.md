# Bottle — memoria, snapshot e precisione del documento

8 novembre 2026. Memoria Bottle, test snapshot e precisione configurabile completati e verificati.

## Snapshot e misura iniziale

Copia congelata `/tmp/bottle-memory-2026-11-08/Bottle.prt`, 94.835.104 byte,
SHA256 `63da67a96448063175b7b374f18bc46fb814db4cd2d14138601065ea0a8a50fb`.
33 feature; risultato Raccordo33: 182 facce, 2.567.022 poli NURBS logici.
Nessuna scrittura sul documento originale o chiusura dell'app utente.

La precedente riduzione dei nodi e' descritta in
[riduzione griglie loft](riduzione-griglie-loft-step-2026-11-04.md): eliminazione di
molteplicita' superflue, con conservazione dei nodi necessari e delle tolleranze.
Qui la riduzione riguarda la duplicazione dei dati, senza fit delle superfici.

## Pool di lettura esatto — implementato

- `kernel/fk_body_io.h/.cpp`: pool locale al documento, thread-safe; hash come indice,
  confronto completo bit per bit prima di condividere coefficienti o istanze NURBS.
- Nessuna fusione per vicinanza. Nodi, pesi, poli e gradi rimangono identici.
  I record distinti nello stesso body restano distinti, preservando la codifica binaria.
- `cad_document_io.cpp`: lettura parallela con pool comune e string_view sui blocchi
  gia' letti, senza la precedente copia di ciascun record in std::string.
- Nessun cambiamento del formato del file o dei parametri geometrici per questa parte.

| Misura sulla stessa copia, Qt offscreen | Prima | Dopo |
| --- | ---: | ---: |
| Poli logici nell'intera storia | 40.900.569 | 40.900.569 |
| Superfici materializzate alla lettura | 839 | 149 |
| Poli memorizzati alla lettura | 34.138.673 | 4.438.230 |
| Stima memoria poli/pesi | 1.041,83 MiB | 135,44 MiB |
| RSS dopo la lettura | 3.181.952 KiB | 2.177.972 KiB |
| RSS dopo viewport e cache pronte | 10.432.268 KiB | 2.882.020 KiB |
| Picco RSS del processo | 10.432.268 KiB | 3.401.928 KiB |

RSS a caldo circa 9,95 → 2,75 GiB, -72,4%. Il picco include decompressione,
record della storia e costruzione degli indici. Misure locali sotto carico variabile.
Le cache di selezione beneficiano delle stesse superfici condivise; non sono eliminate.

Prime regressioni kernel BodyReadPool 3/3 PASS: uguaglianza binaria, identita' dei record,
istanze, dati quasi uguali e zeri con segno non condivisi, vita oltre il pool,
lettura concorrente. App completa e runner aggiornati; verifiche estese sotto.

## Test snapshot — corretto e verificato

`Mouse.prt` formato55/cache7 precede le migrazioni semantiche: il lettore scarta
la cache della copia di lavoro e restituisce le definizioni. Il test misurava
blocchi vuoti. Rigenerare il vecchio modello oggi fallisce a Raccordo12 e non
e' una preparazione corretta per una prova di conservazione degli snapshot.

Il test costruisce quindi una fixture immutabile in memoria: cambia solo UUID
di check-in e hash del payload, conservando gli stessi dati B-rep, poi rimuove
l'identita' archiviata per le successive prove di salvataggio. Il file originale
rimane invariato e la politica delle copie di lavoro non cambia.
Nella sonda:27.296.037 byte condivisi /99.354.050 non condivisi=27,47%.
Il requisito del dimezzamento e i confronti byte per byte non sono indeboliti.
La rigenerazione parametrica del vecchio Mouse resta un problema distinto.

## Precisione per documento — implementata e verificata

Menu Opzioni / Precisione di calcolo del documento; campo in mm persistito nel
formato67, default1e-7 e intervallo1e-10..1e-7. Preset1e-7/1e-8/1e-9 e valore
personalizzato. Cambiare il valore ricalcola tutte le funzioni, comprende Undo/Redo
e rispetta la sola lettura. Formati precedenti:default storico; impronta66 delle
tavole conservata tramite il confronto dedicato.

Scope numerico per lavoro, propagato a rigenerazione, anteprime e parallelFor;
il ciclo GUI resta al default e le finestre non si contaminano. I diversi budget
conservano i rapporti, con soglie UV/condizionamento separate. I profili piu' fini
possono richiedere piu' poli e RAM o rifiutare geometria prima accettata. La geometria
importata e il double mantengono i loro limiti. Non e' una certificazione globale
dell'errore massimo. Dettagli e copertura:[politica](tolerance-policy.md).

Sonda kernel2/2 PASS: scope annidati/eccezioni/thread/parallelismo e confronto
geometrico offset NURBS su38x38 punti; scarto sotto2volte il budget scelto,
profilo1e-9 con scarto oltre10volte inferiore al default1e-7.

## Verifiche finali

- App completa `forgecad-release/forgecad` e runner compilati; serve riaprire l'app
  per caricare il binario nuovo. Nessun riavvio imposto al processo dell'utente.
- **32/32 CTest PASS** (13,96s): snapshot Mouse, precisione, lettura, revisioni,
  unita', cache/storia/Undo, istanze, schizzi/snap, spessore/superfici e kernel
  offset/loft/raccordi/pool/tolleranze. Log `tests-verified.log`.
- Precisione anche su Wayland PASS: dialogo, profilo per finestra/worker, Undo/Redo,
  codec67 e lettura66, dati invalidi atomici, cerchio di raggio1e-8mm conservato
  in cache nel profilo1e-9. La precisione diversa invalida le firme storiche delle
  tavole come modifica del modello, senza un errore di lettura.
- Bottle: confronto fra due lettori dello stesso build, con e senza pool:
  **30/30 B-rep identici in dimensione e SHA256**. Anche codec67/roundtrip di ogni
  B-rep PASS. I tre restanti passi sono datum senza B-rep.
- Bottle nativo Wayland/OpenGL: 33 funzioni/182 facce finali/nessun errore,
  cattura framebuffer verificata; nuovo schizzo non ricostruisce i solidi.
  A riposo:1.902ms di parete,0,000114s CPU,0frame. Questa e' la prova isolata,
  non una misura del processo utente ancora aperto col binario precedente.
- Nel processo QA nativo, dopo roundtrip e preparazione grafica, RSS3.656.896KiB
  (3,49GiB), picco5.026.868KiB; il confronto72,4% sopra usa lo stesso percorso
  offscreen prima/dopo e non comprende il roundtrip aggiuntivo della QA nativa.
- SHA256 del Bottle originale identico alla copia congelata al termine.
- Mappa647file/9.254relazioni/0diagnostiche, `--check` PASS; blocchi pool,
  precisione, codec e worker verificati in entrata/uscita e HTML nel browser locale.
- `git diff --check` PASS. Nessun commit o aggiornamento remoto richiesto/eseguito.

Limiti: il ricalcolo completo di Bottle ai profili1e-8..1e-10 non e' stato
certificato; sono verificati il default sul modello reale e il controllo effettivo
del profilo nelle regressioni geometriche. Resta il fallimento parametrico di
Raccordo12 nel vecchio Mouse, separato dalla conservazione degli snapshot.

Sonde, log, hash per funzione e catture:
`/tmp/bottle-memory-2026-11-08/`. Le misure Bottle riguardano la copia sopra.

Verifica finale dopo la correzione Shower Head: app/33CTest correnti e
Wayland Bottle PASS. Provata anche l’ultima copia Bottle salvata nel frattempo
(33funzioni/8schizzi/182facce), zero ridisegni a riposo e nuovo schizzo senza
ricalcolo dei solidi. Misure, hash e distinzione dei percorsi memoria nel
[rapporto Shower Head](shower-head-spessore-2026-11-08.md).
