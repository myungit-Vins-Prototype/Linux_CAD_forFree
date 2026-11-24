# Riparazione CI dopo il checkpoint — 17 novembre 2026

Stato remoto finale: **CI44142011489 PASS**, Linux121/121 e macOS124/124 CTest, entrambe le compilazioni complete riuscite sul software78d7268. Le prove locali successive restano separate da quel run.

Il commit `18d0adcc599335407942f7f7acc5d5266071a402` è stato pubblicato su richiesta dell’utente. [La prima CI](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/44132066010) ha compilato applicazione e test su **Linux e macOS**, ma la fase test è fallita su entrambi. I fallimenti non sono errori del compilatore.

## Esito della prima CI

- Linux:119 test CTest configurati; timeout del runner `forgekernel_shell_pose_tests` a180,06s; runner generale con482/483 casi superati, unico errore `ProjectedCutBandAlongExistingEdges`.
- macOS:122 test CTest configurati; stesso timeout a180,04s e stesso unico caso fallito nel runner generale,482/483.
- Errore della proiezione: `quadratura: campione non finito`. Log completi scaricati dall’API GitHub e privati delle sequenze ANSI; nessuna ipotesi di incompatibilità specifica attribuita al sistema.

## Proiezione su un bordo: causa e correzione

Riprodotto localmente con il caso esistente: **0/1 FAIL** con la stessa eccezione. `insideProfile` invoca il winding prima di verificare che la faccia sia la prima superficie incontrata dal raggio. La precondizione documentata in `fk_profile.h` vieta punti sul loop: il winding integra `cross(C(t)-q,C’(t))/|C(t)-q|²` e produce0/0 quando il punto coincide con il campione.

Nel caso reale la parete frontale riceve `q=(5,0)`, esattamente sul centro del bordo inferiore del rettangolo. La traccia privata registra `face=2, first=0, q=(5,0)`: il raggio incontra prima la faccia superiore. Anticipato in `fk_project.cpp` il medesimo `firstRayHit`, con le stesse tolleranze e lo stesso confronto del residuo; solo le facce ammesse passano al winding. Non sono stati modificati il significato del winding, le guardie finite o i budget.

Candidato **1/1 PASS** nel caso prima fallito e **5/5 PASS** sull’intera suite di proiezione. Traccia privata esclusa dalla produzione. Il caso permanente esistente verifica conteggi, area tolta, topologia e tessellazione: non è aggiunto un duplicato dello stesso test.

## Svuotamento: budget dei test e diagnosi

I180s erano condivisi da tre regressioni geometriche. Il log mostra i primi due casi conclusi prima della terminazione, ma non misura la durata di ciascuno e non prova un ciclo infinito nel terzo. Lo stesso runner storico Linux completava tutti e tre in56,0058s: non è un valore di riferimento garantito sui runner GitHub.

Registrati come tre CTest separati, usando lo stesso eseguibile e gli stessi filtri di caso. Ogni geometria mantiene il limite180s; tutti i casi e tutti i controlli restano attivi. La nuova CI potrà attribuire correttamente tempi ed eventuale timeout alla singola posa. Non si dichiara un miglioramento dell’algoritmo o la risoluzione di un eventuale rallentamento specifico del runner prima di verificarla.

## Verifiche e pubblicazione

Build completa app e runner PASS,19,854s, seriale. **7/7 CTest PASS**,63,813s: tre pose (ciascuna esegue1/1 caso), derivata B-spline, estensione, lunghezza e quadratura. Suite di proiezione **5/5 PASS** sul runner integrato. Tempi delle singole pose conservati nel log. Mappa804file/11741relazioni, zero errori,407TU da cache e solo `fk_project.cpp` rianalizzato; generazione3,221s, `--check` PASS. Hash sorgenti/configurazione e collegamenti nei due versi verificati.

Il worktree locale contiene anche i nuovi controlli numerici K10 (13 gruppi), che hanno passato di nuovo la verifica nell’integrazione; **non verranno inclusi nel commit di riparazione CI**. Le prove numeriche restano nel registro locale della review e non devono essere attribuite al codice del checkpoint o alla sua CI.

Riparazione pronta per commit/push di `fk_project.cpp`, registrazioni CMake, questo rapporto e prove inerti. Il nuovo esito remoto richiede una CI successiva; non si dichiara ancora verde. [Prove](review-ci-repair-prove-2026-11-17/SHA256SUMS).

Aggiornamento locale dopo la pubblicazione: commit `78d7268`, push e SHA remoto verificati. [Nuova CI](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/44142011489) avviata, esito ancora pendente. La copia nelle prove conserva il rapporto precedente al commit, senza alterare il sigillo.

Controllo finale del catalogo: copie CMake archiviate rinominate come testo inerte, con contenuti e sigilli originali conservati e verificati tramite `REPACK.json`. Non cambiano software, log o risultati delle prove.

Verifica remota successiva: CI44142011489 conclusa con **successo su Linux CPU e macOS CPU/Metal**, compilazione e tutti i CTest configurati PASS. Il run verifica il software78d7268; non include il lavoro numerico locale successivo.
