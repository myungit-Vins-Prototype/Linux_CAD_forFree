# Revisione del codice — 10 ottobre 2026

**Aggiornamento successivo:** implementate e verificate le correzioni asincrone,
del tip durante il rollback e delle prestazioni delle booleane. Questa revisione
conserva le rilevazioni e lo stato al momento dell'audit. Stato finale, benchmark
prima/dopo e problemi ancora aperti nel
[rapporto di implementazione](performance-improvements-2026-10-10.md).

Base: `f0dfbb5`, working tree inizialmente pulito. Richiesta: revisione generale,
con attenzione a booleane, raggi dei raccordi e risparmio di RAM senza rallentamenti.
Consultati `CLAUDE.md`, la revisione del 9 ottobre e la documentazione delle
prestazioni. Questa è una revisione mirata dei percorsi critici, non una verifica
formale di ogni riga del repository. I difetti della revisione precedente già
corretti non sono ripresentati come problemi attuali.

## Problemi aperti

### 1. [P1] Destinatario distrutto durante un calcolo in background

`forgeCad2026_gui.cpp:11732-11760`, anche `10830-10889`; distruttore a `307`.
Anteprima ed evidenziazione catturano un `QObject *receiver` figlio del viewport
e `this`. Quando il worker termina invocano `QMetaObject::invokeMethod(receiver, …)`.
Il distruttore non attende questi worker e non ne protegge il destinatario.
La distruzione del figlio cancella gli eventi già accodati, ma non rende valido
un puntatore passato a una nuova `invokeMethod` dopo la distruzione.

Conseguenza: accesso a un oggetto distrutto, con possibile crash quando il viewport
viene distrutto mentre il raccordo, la booleana o l'evidenziazione sono ancora
in calcolo. Rilevazione statica della sequenza di vita; **non riprodotto un crash
dell'app**. Un esperimento Qt minimo con destinatario liberato non è andato in
crash: non viene usato come prova di un crash riproducibile.

Correzione proposta: consegnare il risultato al thread GUI tramite il destinatario
stabile dell'applicazione, poi controllare un `QPointer<CadViewport>` dentro la
callback prima di accedere al viewport. Lo stesso schema è già usato da
`DocumentFilePreview::startLoading` in questo file. Non aggiungere un'attesa
sincrona nella GUI per calcoli lunghi. Stato: **da correggere**.

### 2. [P2] Il gruppo Corpi seleziona lo stadio futuro dopo il rollback

`forgeCad2026_gui.cpp:799-804`, chiamato dal clic sul gruppo Corpi a `20011`.
`modelBodyTip` risolve sempre il `tipFeatureId` finale, senza il limite della
barra di posizione. Il corpo mostrato nel viewport è invece lo stadio precedente.

Riproduzione sul viewport reale, Qt offscreen: cubo di lato 4, Scala ×2,
`setHistoryPosition(1)`. Risultato:
`history=1 tip=1 base_visible=1 future_visible=0`. Il tip selezionato è Scala,
indice 1, mentre il corpo visibile è Base, indice 0. `selectObject` accetta
questo indice e lo conserva nella selezione. L'evidenziazione del corpo e le
operazioni avviate dalla selezione possono quindi riferirsi allo stadio nascosto.

Correzione proposta: distinguere il tip finale della storia dal risultato attivo
alla posizione corrente; usare quest'ultimo per selezione e proprietà visuali.
Conservare il tip finale per salvataggio e struttura delle dipendenze.
Stato: **riprodotto, da correggere**. Probe: `/tmp/code-review-viewport.cpp`, log
`/tmp/code-review-viewport.log`. Non aggiunta una regressione intenzionalmente
fallente alla suite ordinaria.

### 3. [P2] Cache quadratica delle coincidenze nelle booleane

`kernel/fk_boolean.cpp:1451-1454`, cache e mutex a `241-242`, inserimento a `302`.
Per classificare ogni faccia viene confrontata la sua superficie con tutte
quelle dell'altro corpo. Anche i confronti negativi restano nella `std::map`.
La selezione iniziale delle coppie a `1644-1647` usa i box, ma questa successiva
classificazione confronta tutte le superfici, anche tra corpi molto lontani.
Il numero di confronti/cache può crescere come `2 × facceA × facceB`; il mutex
serializza l'accesso dei worker alla mappa.

Riproduzione: due prismi regolari di raggio 10 e altezza 10, separati di 100
lungo X, sottrazione. Nessun contatto, volume e B-rep del risultato corretti.
Misure iniziali macOS Release, processi separati, `getrusage`:

| Facce per corpo | Thread | Picco prima | Picco dopo | Tempo booleana |
|---:|---:|---:|---:|---:|
| 258 | 1 | 3,75 MiB | 15,59 MiB | 0,029 s |
| 514 | 1 | 4,73 MiB | 43,38 MiB | 0,144 s |
| 1.026 | 1 | 6,84 MiB | 146,83 MiB | 0,717 s |
| 1.026 | automatici | 6,72 MiB | 147,16 MiB | 1,406 s |

I picchi riguardano l'intero processo, non soltanto la mappa, e non misurano
la memoria trattenuta dopo l'operazione. La crescita quadratica è coerente
con il percorso individuato; non è stata misurata con un profiler di heap.

Correzioni proposte, in ordine: scorciatoia per solidi con box disgiunti
(preservando validazione e `unifySameDomain`), filtro dei box anche durante la
classificazione, indice spaziale per le coppie candidate. Verificare identità
dei risultati seriali/paralleli e i contatti tolleranti prima dell'integrazione.
Stato: **collo di bottiglia riprodotto, ottimizzazione non implementata**.

### 4. [P2] Lancio dei thread più costoso del lavoro sui casi piccoli

`kernel/fk_parallel.h:44-48`; chiamate senza soglia nelle booleane a
`kernel/fk_boolean.cpp:1655` e `1842`, nei raccordi a `kernel/fk_blend.cpp:958`.
Ogni passaggio crea e riunisce un nuovo gruppo di thread. Per i lavori analitici
brevi l'overhead può superare il tempo geometrico; non conviene disattivare
globalmente il parallelismo, utile sulle intersezioni B-spline.

Misura finale con il probe permanente: 5.000 sottrazioni fra due box sovrapposti,
verifica di volume/B-rep fuori dal tratto cronometrato, mediana di 3 esecuzioni:
seriale **1,0975 s**, automatico **1,6882 s**, circa **54% più lento**. Anche il
caso disgiunto della tabella peggiora. Log `/tmp/code-review-permanent-measurements.log`.
Una prima misura che controllava il volume a ogni giro dava 1,573/2,095 s
(+33%): i protocolli non vanno mescolati. Tempi specifici della macchina e del carico.

Correzione proposta: scegliere il parallelismo in base al costo stimato delle
superfici e dei job, oppure riutilizzare worker persistenti. Misurare sia i casi
planari sia eliche/loft; una soglia basata solo sul numero di facce non basta.
Stato: **rallentamento misurato, ottimizzazione non implementata**.

### 5. Raccordo del flacone ancora rifiutato

`tests/test_viewport.cpp:5970`, rifiuto a `kernel/fk_blend_surface.cpp:579`.
Confermata la regressione già registrata in `CLAUDE.md`: raccordo da 0,3 mm
sull'elica e sul fine-filetto di `File_Esempio/flacone.prt` rifiutato per
«discontinuità interna della superficie lungo il raccordo». La suite kernel
può passare interamente mentre questo caso applicativo fallisce.

Non isolata una nuova causa del difetto. Il controllo evita di accettare una
superficie incoerente: non va rimosso, né vanno allargate le tolleranze per far
passare il test. Restano da studiare i contatti e la continuità delle facce
nel punto terminale. Stato: **limite preesistente confermato, non corretto**.

## Risparmio di RAM implementato

`kernel/fk_bspline_surface.cpp:324-333`: `knotIso` costruiva la cache completa
delle isoparametriche U/V ai nodi prima di scoprire che il parametro richiesto
non era un nodo e quella cache non poteva servire. Ora, solo quando la cache
non esiste, controlla prima l'appartenenza al vettore dei nodi e al dominio.
La curva richiesta continua a essere costruita con gli stessi poli, pesi e
calcoli. Il percorso di lookup a cache già pronta rimane identico.

Verifica finale con il probe permanente su superficie cubica 512×512, richiesta
`uIso(0.123456789)`, 5 processi nuovi per versione: mediana prima chiamata
**6,421 ms → 0,0127 ms**. Picco circa **29,8 → 7,7 MiB**, quindi **circa 22 MiB
evitati** in questo caso. Campioni della curva verificati contro la superficie
esatta. Log `/tmp/code-review-permanent-measurements.log`. Non è una misura del
consumo complessivo dell'applicazione.

Nella prova iniziale la richiesta successiva restava circa 0,008 ms in entrambe
le versioni; quella prova non riservava il vettore dei poli (picchi assoluti
35,8/13,7 MiB e prima richiesta mediana 6,814/0,0187 ms). Il probe permanente
lo riserva, quindi i picchi assoluti di partenza sono inferiori. Il risparmio
della cache resta circa 22 MiB in entrambi i protocolli.

Esteso `BSplineSurfaceMatchesOcct`: le API `uIso`/`vIso` vengono confrontate
con la superficie esatta, per 300 superfici casuali, razionali e non razionali,
campioni interni e sui nodi. Non modificati precisione, geometria B-rep,
tolleranze, qualità della mesh o limite di Undo.

## Altre opportunità, non integrate

- `BooleanBuilder::computeCuts`, righe 1668-1673: copia i contenitori dei
  risultati delle coppie nei contenitori globali e mantiene gli originali
  fino alla fine di `collectCuts`. Si possono trasferire e liberare prima
  questi metadati. Curve e superfici sono già condivise: non è una seconda
  copia completa della geometria. Risparmio non misurato.
- Le cache Bezier/BVH/isoparametriche usano pubblicazione atomica ma consentono
  costruzioni concorrenti duplicate al primo accesso. Vale la pena misurare
  il picco su una grande superficie usata contemporaneamente da molti worker.
- `History` conserva fino a 200 stati. I vettori Qt e i corpi immutabili sono
  condivisi: non sono 200 copie integrali del documento. Eliminare risultati
  intermedi o cache utili ridurrebbe RAM a costo di ricalcolo/Undo; non proposto
  come risparmio senza perdita di velocità.

## Riproduzione e verifiche

Probe permanente: `kernel/tests/probe_code_review.cpp`, non collegato all'app,
nessun test con soglie di tempo. Compilazione e comandi:

```sh
c++ -std=c++17 -O2 -I kernel kernel/tests/probe_code_review.cpp \
  forgecad-release/kernel/libforgekernel.a -o /tmp/forgecad-review-probe
/tmp/forgecad-review-probe iso 512
/tmp/forgecad-review-probe boxes 1 5000
/tmp/forgecad-review-probe boxes 0 5000
/tmp/forgecad-review-probe disjoint 1024 1
/tmp/forgecad-review-probe disjoint 1024 0
```

Per confrontare la vecchia cache senza modificare il checkout: estrarre
`git show f0dfbb5:kernel/fk_bspline_surface.cpp` in `/tmp`, compilarlo con
`-std=c++17 -O2 -I kernel -c` e collegare questo oggetto prima di
`libforgekernel.a`, con lo stesso sorgente del probe.

- Build finale: PASS, `/tmp/code-review-build-final.log`.
- Test B-spline mirati dopo la modifica: 3/3 PASS.
- Kernel prima della modifica: **309/309 PASS**, `/tmp/code-review-kernel-verified.log`.
- Suite escluso il runner kernel dopo la modifica: **23/24 PASS**,
  `/tmp/code-review-app-final.log`; solo il raccordo del flacone fallisce.
- Verifica completa del kernel dopo la modifica: **309/309 PASS**, exit code 0,
  `/tmp/code-review-kernel-after.log`.
- Probe permanente compilato ed eseguito nei tre modi, tutti con `valid=1`;
  confronto finale prima/dopo collegando il sorgente di HEAD in un oggetto
  separato, senza modificare il checkout. `git diff --check`: PASS.

La prima esecuzione nella sandbox si è interrotta: test OCCT con `P_tmpdir`
scrivono in `/var/tmp`, accesso negato, poi il test tenta di usare lo STEP non
letto. Il tentativo con `TMPDIR=/tmp` è stato fermato perché non cambia
`P_tmpdir`. Le verifiche complete valide sono state eseguite con l'accesso
necessario ai file temporanei, senza cambiare le tolleranze o saltare test.
`/usr/bin/time -l` non può leggere `kern.clockrate` nella sandbox; per le
misure di memoria è stato usato `getrusage` nel probe.

Limiti: macOS/CPU/Release; test GUI offscreen, senza verifica del rendering
OpenGL nativo. Nessuna prova CUDA/Linux o benchmark completo del Mouse;
nessun documento utente modificato, installazione in `/Applications`, commit
o push eseguiti. Gli altri problemi elencati restano aperti.
