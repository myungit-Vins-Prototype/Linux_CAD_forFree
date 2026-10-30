# Roadmap: export B-spline e trascinamenti validi (26 ottobre 2026)

Completati due punti della [roadmap precedente](solver-cuda-roadmap-2026-10-26.md): regressioni permanenti CPU/CUDA per gli export STL/OBJ e selezione di trascinamenti applicabili sul documento reale mouse2.

## Correzioni degli export

I nuovi parser indipendenti hanno rilevato difetti presenti anche sul percorso CPU: un quad sottile al bordo diventa invertito dopo saldatura, triangoli OBJ collassano dopo la scrittura a 12 decimali e triangoli STL collassano dopo la conversione float.

`cad_export.cpp` controlla ora la geometria rappresentabile nel file:

- STL: conversione float prima di calcolare area e normale, rifiuto delle coordinate fuori intervallo, omissione delle faccette degeneri e verso coerente con le normali della superficie.
- OBJ: saldatura e controllo sulle coordinate effettive a 12 decimali; controllo della convessità dopo saldatura; i quad non validi conservano i triangoli, orientati sulle coordinate finali. Le faccette degeneri non vengono scritte. La tolleranza di saldatura preesistente non è stata aumentata.

La suite `tests/test_mesh_export_backends.cpp` usa gli scrittori di produzione su una patch cubica curva polinomiale e una NURBS con foro. Salva e rilegge i file in una directory temporanea, controlla conteggi/indici/normalizzazione/numeri finiti/orientamento/area non nulla, verifica un campione coperto e il centro del foro vuoto, confronta i campioni CPU/CUDA e controlla che il B-rep resti invariato.

Contatore CUDA verificato per ogni formato: 8847 punti nel caso polinomiale, 9472 in quello razionale. STL: 5403/6381 triangoli; OBJ: 3017/3624 poligoni. Nei due fixture, scarto massimo dei punti confrontati nullo e delle normali circa 1e-12, sia con CUDA generico sia specializzato. Il confronto è simmetrico sugli insiemi di campioni, non una prova di identità byte per byte o di equivalenza universale delle topologie. Tolleranze nel JSON.

CTest include CPU sempre, CUDA generico e CUDA specializzato opt-in quando compilabili. Senza dispositivo i test GPU restituiscono SKIP77. Verificato anche il collegamento al vero stub CPU, senza librerie runtime CUDA: export riusciti e richiesta GPU restituisce77. Le prove restano Linux; non dichiarano una verifica macOS. Nessun benchmark di velocità degli export.

## mouse2: sette trascinamenti riusciti

Nuova opzione `--admissible-targets`: ricerca fuori timer su un campione limitato di punti delle entità non completamente definite, provando piccoli spostamenti x/y/diagonale. Ogni target deve essere applicabile da entrambi i solver e deve spostare effettivamente il punto. Nessun vincolo rimosso o allentato. Per ogni ripetizione si verificano target, coordinate entro1e-7, struttura/pesi/nodi delle curve, tutti i constraintError finiti e inferiori a1e-7, rango/DOF ed entità definite equivalenti.

Trovati target in7/13schizzi, indici0/2/6/8/9/10/11. Sono inclusi i due schizzi da1304/1754variabili. Gli altri6casi sono esplicitamente esclusi dal totale, senza concludere che ogni loro modifica sia impossibile: la ricerca è limitata. La risoluzione senza target riesce su tutti13gli schizzi; i rifiuti della prima prova dipendevano dai target incompatibili.

Build finale,3processi nuovi sequenziali, ciascuno con5ripetizioni alternate e copie ripristinate:

| Operazione sui 7 casi selezionati | Denso (ms) | Partizionato (ms) |
|---|---:|---:|
| Risoluzione, mediana | 0.574433 | 0.516177 |
| Risoluzione, intervallo tra processi | 0.539538–0.611785 | 0.471180–0.639360 |
| Analisi DOF, mediana | 9.983892 | 0.435495 |
| Analisi DOF, intervallo tra processi | 9.342897–10.433474 | 0.403826–0.457562 |

Risoluzione: circa10.1%meno tempo sulla mediana, ma intervalli sovrapposti e risparmio assoluto inferiore a0,1ms: nessun guadagno stabile o generale dimostrato nell'editing. Analisi: circa22.9× sui casi selezionati, eseguita sugli schizzi originali. Non sono misure dell'intera rigenerazione, del rendering o della latenza completa dell'interfaccia. Clock, temperatura e carico esterno non controllati. La serie preliminare, misurata prima dell'ultima ricompilazione, è conservata separatamente nel JSON e non mescolata alla serie finale.

Documento originale invariato: SHA256 `e0c53c6aeff405c16dcb85660b303590e8fa255b752cc37acb2ab2cbc0b39617`. Runner e hash dei sorgenti sono registrati. La sonda confronta il percorso denso di riferimento e quello partizionato nello stesso eseguibile del branch; non sono due build separate di main.

## Riproduzione e stato

```sh
ctest --test-dir forgecad-linux-solver -R 'forgecad_mesh_export.*_tests' --output-on-failure
python3 tools/benchmark_document_solver.py /percorso/mouse2.prt \
  --build-dir forgecad-linux-solver --output /tmp/mouse-confronto-nuovo --repeats 3
```

18/18 regressioni mirate PASS dopo la compilazione finale di applicazione, runner viewport e suite export. Mappa aggiornata480file/6620relazioni,231unità senza diagnostiche; controllati collegamenti in entrata/uscita dei moduli interessati. CUDA e Python restano soggetti ai limiti AST documentati nella mappa. Dati: [rapporto JSON](solver-cuda-export-drag-2026-10-26.json). Log e binari locali in `forgecad-linux-solver/benchmarks/roadmap-export-drag/`.

Il confronto punto per punto delle mesh reali CPU/CUDA di mouse2 è stato completato successivamente nel [rapporto numerico](mouse2-mesh-cpu-cuda-2026-10-26.md). Restano aperti: verifica su macOS e altre GPU, ulteriori classi di trascinamento e misure sulle componenti grandi connesse del solver. Specializzazioni CUDA ancora opt-in. Installazione desktop invariata; nessun commit o push.

Il confronto numerico successivo individua anche 574 triangoli degeneri comuni nella mesh viewport, da trattare separatamente.
