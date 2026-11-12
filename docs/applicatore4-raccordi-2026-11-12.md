# Applicatore4 — verifica dei raccordi elicoidali, 12 novembre 2026

Richiesta: controllare `Applicatore4.prt` e l'errore di chiusura del raccordo mostrato nella foto, valutando se occorra un nuovo caso per le intersezioni fra superfici.

## Stato corrente

**Estensione generale del kernel integrata, app e runner ricompilati.** Il file modificato dall’utente x=0,75/R0,15 produce un solido valido di 34 facce; superate anche variazioni del passo, raggi diversi, anteprima e rigenerazione dopo riapertura. Nessun CAD originale è stato riscritto. Algoritmo, prove e limite distinto sulle selezioni isolate sono descritti nell’ultima sezione; le sezioni iniziali conservano la cronologia della prima verifica.

## Prima verifica sullo snapshot x=0,65 — storico

La **Differenza 6**, indice B7 nel documento attuale, è un solido valido (28 facce, 78 spigoli), anche con il controllo degli incroci dei loop. Il fallimento è riprodotto sulle selezioni parziali dei bordi elicoidali: la curva di chiusura terminale non raggiunge entrambi gli appoggi. Non è stato dimostrato un difetto della geometria a monte; il messaggio sulle micro-geometrie è un suggerimento generico dell'interfaccia.

**R0,1 mm riesce completando i percorsi lungo i solchi, inclusi i tratti verso la punta.** Il kernel già disponibile costruisce e rifila le pezze; per questo risultato non è stata necessaria una modifica del kernel. Il caso generale delle catene parziali che terminano su queste superfici curve rimane aperto e non viene dichiarato corretto.

Variante consegnata: [Applicatore4-raccordato-R0.1mm.prt](../output/Applicatore4-raccordato-R0.1mm.prt), immagine [PNG](../output/Applicatore4-raccordato-R0.1mm.png). Contiene il prefisso parametrico fino a Differenza 6 e il nuovo raccordo: 9 funzioni, 3 schizzi. Identità indipendente. Le funzioni successive dell'originale, già in errore dal Raccordo 23 nella prova di lettura, non fanno parte di questa variante. L'originale conserva tutta la sua storia e non è stato modificato.

## Riproduzione e selezione

Percorso originale: `/home/myungit/Documenti/File_Esempio/Applicatore4.prt`.
Snapshot: `/tmp/applicatore4-raccordo-2026-11-12/Applicatore4.prt`.
SHA256 iniziale e finale di entrambi: `99809a857c88e6ede250c2ec77c0644b1ecd2114e1adb7997b7c7d47287ec0d3`.

Bordi, **riferiti esclusivamente al B7 di questo snapshot**, tutti con propagazione tangente:

- Selezione parziale `E5 E18 E8 E24 E11 E30 E14 E36`, R0,1: fallisce con `chiusura dell'estremo: estremi della curva di taglio non sulle due superfici`.
- Stessa selezione con `E40 E43`: riesce; anche la coppia simmetrica `E45 E48` consente il risultato.
- Selezione usata nella variante: `E5 E18 E8 E24 E11 E30 E14 E36 E40 E43 E45 E48`, R0,1 mm. Il riferimento persistente di ciascun bordo viene creato sul B-rep esatto.
- Singolo E8 a R0,1: riproduce l'errore della foto. E5 a R0,01 e R0,05 riesce, a R0,1 fallisce nella chiusura; E5+E18+E19+E20 a R0,1 riesce. Non si tratta quindi di una ricerca monotona del raggio massimo.

Diagnostica temporanea in `fk_blend_surface.cpp`: alcuni punti terminali proiettati sui vecchi bordi non appartengono alla pezza definitiva del raccordo (per esempio ~0,0493 mm nel tentativo su E8 a R0,1). I recuperi Newton non possono far coincidere un estremo incoerente con la pezza richiesta. I giunti cono/cilindro presso y=3 mm sono inoltre distinti dalla propagazione strettamente tangente. Per le selezioni parziali servirebbe gestire/riorganizzare i raccordi dei supporti coinvolti, o una diversa chiusura locale validata. Non sono state rilassate le tolleranze. Una prova temporanea di riordino delle catene per lunghezza non ha risolto il caso ed è stata scartata.

## Verifiche eseguite

- Runner applicativo esistente `forgecad-release/forgecad_view_tests`: anteprima asincrona, creazione, controllo B-rep, solido chiuso, mesh completa, salvataggio e rilettura PASS sulla selezione completa.
- Risultato: **114 facce, 298 spigoli, 186 vertici**, nessun bordo libero; `checkBody` con `loopCrossings=true`: 0 problemi; `tessellate`: 0 facce fallite; roundtrip binario: 0 problemi. Tolleranza locale massima di edge/vertici nel risultato: ~0,000109136 mm; nessuna modifica ai budget del kernel.
- `--document-regression baseline`: **stable**, 0 errori di rigenerazione; 0 funzioni in errore, 0 geometrie non valide, 0 metriche incomplete in tutti gli stadi. Due rigenerazioni complete e una riapertura con rigenerazione: confronti vuoti, anche fra cache iniziale e primo ricalcolo.
- Rendering nativo Wayland della copia PASS e immagine ispezionata. Il rendering iniziale all'interno del sandbox non accedeva alla sessione grafica; la verifica è stata ripetuta con accesso alla sessione, senza chiudere l'app dell'utente.
- Helper e prototipi diagnostici soltanto in `/tmp/applicatore4-raccordo-2026-11-12/`; nessun sorgente kernel/app modificato, nessuna ricompilazione dell'eseguibile, nessun commit/push. Non è stata modificata la mappa architetturale: nessun cambiamento strutturale al codice.

Prove diagnostiche locali, non una certificazione globale di tutte le possibili chiusure o del raggio massimo. La variante risolve il raccordo completo a R0,1; l'estensione delle selezioni parziali resta da implementare.

## Seguito — indagine preliminare sulla variazione parametrica (storico)

Il file consegnato è stato modificato dall'utente: centro del cerchio S1 x=0,65→0,75 mm e raccordo R0,1→0,15 mm. Snapshot della modifica in `/tmp/applicatore4-elica-2026-11-12/modificato.prt`, SHA256 `49979f0016cdb5036c3bb71425948d0a4a9153472a6baa6d90ae738bcf4bc595`. La base passa a 30 facce/84 spigoli ed è valida anche con loopCrossings. La riuscita precedente vale per lo snapshot iniziale, non per questo nuovo file.

Con la genealogia caricata i dodici riferimenti restano distinti: E8,E20,E10,E26,E14,E33,E16,E39,E44,E47,E49,E52. Una prima sonda senza registrazione genealogica indicava erroneamente una duplicazione: rilievo corretto, non un difetto attribuibile al percorso reale dell'app.

Isolate due difficoltà: il tracciatore della mitra confronta parametri lineari che non identificano punti corrispondenti (scarto ~3,09e-7 mm, distanza geometrica dopo inversione ~7,2e-12 mm); inoltre il raffinamento terminale può prendere una radice su una cucitura distante dalla curva di contatto (~0,15 mm). Prototipi di inversione del supporto e controllo della radice soltanto in /tmp, non ancora integrati o dichiarati risolutivi. I tentativi di solo Newton smorzato e chiusura terminale stabile non hanno risolto l'intero caso.

### Esito delle prove sul file variato

Con i dodici bordi effettivamente risolti dalla genealogia (8,20,10,26,14,33,16,39,44,47,49,52), il kernel originale fallisce a R0,01/R0,05/R0,1/R0,15: gli errori comprendono mitra non tracciata, contatti laterali mancanti e tratto terminale insufficiente. Anche aggiungere i nuovi bordi del fondo non risolve il raggio R0,15. Questi esiti non determinano un massimo fisico del raggio.

Un prototipo che verifica la coincidenza geometrica tramite inversione dei parametri supera la prima mitra (distanza fra i supporti ~7,2e-12 mm, senza allargare la risoluzione), ma fallisce poi una seconda curva di chiusura. Il suo punto terminale dista ~0,0567 mm dall'altro appoggio, mentre la curva richiesta attraverserebbe ~0,1504 mm in un intervallo v di ~1,7e-6: il recupero numerico iniziale non basta per una costruzione valida. Alla partenza compaiono passaggi fra le facce 20/25/20; le sezioni del vecchio algoritmo differiscono fino a ~0,0181 mm in quei giunti. Occorre gestire correttamente il passaggio oltre le piccole facce/il loro eventuale consumo, non soltanto rendere meno severa l'intersezione della mitra.

Scartati anche i prototipi di sola verifica dei punti terminali, prolungamento tangente della guida, scelta alternativa della coordinata monotona, guida intrinseca generalizzata e raffinamento 3D del contatto: nessuno produce il risultato completo richiesto. Non integrati sorgenti sperimentali, non ricompilata l'app e nessun CAD sostituito. I sorgenti e gli eseguibili diagnostici restano esclusivamente in `/tmp/applicatore4-elica-2026-11-12/`.

**Esito dell’indagine preliminare, superato dall’estensione descritta sotto:** il raccordo costante sul file variato non era ancora corretto. La prova precedente copriva la rigenerazione della definizione iniziale, non la stabilità rispetto a questa variazione parametrica. Per il file corrente non è stata consegnata una nuova variante presentata come risolta.


## Estensione generale del kernel — implementata il 12 novembre

Richiesta esplicita: risolvere nel kernel la classe geometrica, senza ripristinare parametri, cambiare manualmente la selezione o introdurre regole legate al file o agli indici degli spigoli.

La piccola faccia terminale e le altre pezze dello sweep condividono bordi G1 reali. Il contatto della palla può attraversare l’intera pezza; vincolarlo alla vecchia carta produce sezioni differenti e una falsa mitra. La chiusura sul fondo può inoltre oltrepassare più tratti consecutivi dello stesso contorno. Non bastava migliorare il Newton della mitra.

- `kernel/fk_nurbs_join.*`: concatenazione omogenea delle carte NURBS, elevazione del grado, scambio U/V, inversioni e allineamento affine della base trasversa. La coincidenza del bordo e la relazione proiettiva dei jet `D_B = alpha D_A + beta H`, `alpha > 0`, si verificano sui coefficienti, al solo arrotondamento double. Non è un fit delle superfici e non si levigano creste reali. Carte non certificabili restano separate.
- `kernel/fk_unify.*`: operazione esplicita `unifyTangentNurbsSupports`, separata dall’unificazione ordinaria delle booleane. Rimuove le cuciture rese interne e concatena i trim polinomiali tangenti. Le micro-campate del marching sono sostituibili solo con un limite di Bernstein entro metà del budget di fit; l’altra metà copre la giunzione. Il budget accumulato è conservato e limitato alla risoluzione. Provenienza di facce, bordi e vertici dichiarata durante la costruzione; tolleranze ereditate conservate.
- `kernel/fk_blend.cpp`: se il percorso ordinario non costruisce il raccordo costante, ripete sui supporti composti. Rimappa esclusivamente tramite discendenti genealogici, con deduplicazione dei bordi concatenati. Una cucitura G1 selezionata e resa interna viene riconosciuta dalla fusione delle sue due facce. Non cambia i raggi o le selezioni persistite.
- `kernel/fk_blend_surface.cpp` e `fk_blend_model.h`: ricerca e rifinitura della chiusura lungo il contorno topologicamente connesso e tangente delle stesse facce; consumo esplicito dei tratti superati, senza ricerca di altri bordi per prossimità.

### Risultati geometrici già verificati

Sullo snapshot modificato x=0,75/R0,15: **34 facce, 90 spigoli, 58 vertici**, 0 problemi con `loopCrossings=true`, 0 bordi liberi, 0 facce non triangolabili; serializzazione B-rep e rilettura valide. Tolleranza locale massima ~0,0000207325 mm, dovuta alle costruzioni preesistenti delle mitre; non è stata aumentata alcuna soglia del solver o di `checkBody`.

Nuovo target indipendente da OCCT `forgekernel_nurbs_support_tests`, con fixture `blend_tangent_sweep.body`: carte razionali e orientate diversamente, rifiuto delle creste, conservazione dei trim entro 1e-9 mm e delle tolleranze, genealogia completa, input immutato; sul modello reale R0,05/R0,1/R0,15, rototraslazioni, scala 2,5 e rinumerazione delle facce, con misura del raggio, B-rep, SP-curve, mesh e codec. Esecuzione finale del target CMake: **3/3 PASS** in 114,45 s, inclusi domini affini, cuciture selezionate rese interne, cuspidi da rifiutare e derivate unilaterali dei trim.

Compilazione Release completa di `forgecad`, `forgecad_view_tests` e target di regressione PASS. Impronta geometrica del runner: `a31843bb3aad43775acbf72ed4d4955733fdef623dc3ab77fa52ac58993a4581`. L’app già aperta conserva il kernel precedente: riavviarla per caricare l’eseguibile ricompilato.

| Passo dell’elica | Raggio | Facce | Ricalcolo e salvataggio/riapertura/ricalcolo |
| --- | --- | --- | --- |
| 3,325 mm | 0,15 mm | 110 | PASS |
| 3,5 mm | 0,15 mm | 34 | PASS |
| 3,675 mm | 0,15 mm | 40 | PASS |

- 15 CTest pertinenti PASS (raccordi, supporti G1, genealogia, cache della storia dichiarata, anteprima/riapertura e precisione), più `forgecad_geometry_sources_tests` PASS: **16 CTest** distinti. Il nuovo modulo è incluso nell’impronta che invalida le cache geometriche.
- Suite OCCT/kernel esistenti: **62/62 Blend, 38/38 History, 5/5 Unify PASS**; OCCT resta escluso dalla produzione.
- `--document-regression baseline` sia sulla copia iniziale x=0,65/R0,1 sia su quella modificata x=0,75/R0,15: **stable**, 0 errori di rebuild, 0 funzioni in errore, 0 geometrie non valide e 0 metriche incomplete. Due ricalcoli e una riapertura con ricalcolo: `repeat_changes=[]`, `roundtrip_changes=[]`. Lo snapshot iniziale conserva 114 facce.
- Percorso GUI nativo Wayland sul modello modificato: anteprima asincrona riuscita, conferma R0,15, solido/mesh/codec e copia temporanea PASS, 34 facce. Rendering Wayland finale ispezionato: `/tmp/applicatore4-elica-2026-11-12/kernel-raccordo-R015.png`.
- Mappa aggiornata: **684 file, 9862 relazioni, 336 TU, 0 TU con diagnostica**; `--check` PASS. Browser Chrome locale: dettagli e relazioni in entrata/uscita di `fk_nurbs_join.cpp`, `fk_unify.cpp`, `fk_blend.cpp`, `fk_blend_surface.cpp`: **4/4 HTML PASS**, nessun errore JavaScript.

Rapporti e riproduttori locali in `/tmp/applicatore4-elica-2026-11-12/`: `ctest.log`, `blend-existing.log`, `history-existing.log`, `unify-existing.log`, `native.log`, `preview.log`, `baseline-original.json`, `baseline-modified.json`. Gli originali CAD e il file modificato dall’utente conservano gli SHA256 riportati sopra. Nessun commit/push.

### Ambito e selezioni parziali

L’estensione riguarda raccordi costanti con propagazione e carte NURBS G1 compatibili, anche con scambio U/V, versi e dominio trasverso affine. Le carte periodiche o prive del certificato richiesto restano separate; la concatenazione dei trim è limitata alle B-spline polinomiali. Non è un allargamento delle tolleranze né una certificazione di ogni possibile supporto o del raggio massimo.

Ulteriori prove sul kernel finale:

- Snapshot x=0,65, R0,1, selezione parziale della prima indagine `E5 E18 E8 E24 E11 E30 E14 E36`: **PASS**, 20 facce, 54 spigoli, solido chiuso e mesh completa.
- Snapshot modificato x=0,75, R0,15, solo `E8`: **PASS**, 13 facce, 33 spigoli, solido chiuso e mesh completa.
- **Limite distinto ancora aperto:** solo `E8` dello snapshot iniziale x=0,65, R0,1, rimane rifiutato perché il contatto terminale esce dal supporto F3. Non viene accettata una chiusura geometricamente incoerente. La selezione parziale della prima indagine e la definizione effettivamente modificata dall’utente sono invece riuscite come indicato sopra.


## Conferma durante l'anteprima — 12 novembre

Richiesta: il pulsante Conferma dei raccordi deve chiudere il pannello anche con
anteprima pendente e attendere lo stesso risultato, riutilizzandone il puntatore.

Implementato: rimosso il rifiuto nel pannello e nei percorsi di creazione/modifica.
`createBlend` e `updateBody` acquisiscono il risultato tramite `takePreviewResult`,
che attende la definizione corrente, condivide il B-rep e completa solo la mesh
quando l'anteprima mostra una patch locale. Le richieste superate conservano la
normale cancellazione per generazione.

I runner comuni hanno un'opzione, attivata dai raccordi, che nasconde il pannello
senza emettere `finished`: il worker e i riferimenti restano vivi fino alla
conferma. Il runner modale attende esplicitamente `finished`, perche' `QDialog::exec`
termina anche su `hide`. Un errore effettivo riapre i controlli e permette la
correzione. La finestra principale resta protetta dal lock del comando.

Regressioni aggiunte alla suite di selezione raccordi: timer pendente, worker
bloccato in coda, definizione superata durante modifica, errore geometrico seguito
da correzione; varianti modale/incorporata. Verificano chiusura prima del risultato,
identita' del puntatore pubblicato, assenza di lavori duplicati, mesh completa,
pulizia e singolo passo Undo/Redo.

App ricompilata. Sonda collegata agli oggetti dell'app: tutti gli otto scenari
PASS offscreen e su Wayland/OpenGL nativo, inclusa la visibilita' effettiva del
pannello e il controllo GL. Runner finale ricompilato: le stesse prove native
PASS; **13/13 CTest PASS** (selezione/rifinitura/raccordi avanzati, pannelli,
genealogia, cache storica, storia raccordi, prestazioni anteprima, mesh storica,
precisione, rilettura raccordi e durata asincrona/documento).

Mappa aggiornata: **684 file, 9862 relazioni, 336 unita' analizzate, nessuna
diagnostica**; `--check` PASS. HTML verificato in Chromium per pannello raccordi,
runner comuni, documento e anteprime, con collegamenti in entrambi i versi.
Nessun CAD dell'utente modificato. Riaprire l'app per usare l'eseguibile aggiornato.


## Velocita' e nuovo file corrente — indagine successiva del 12 novembre

Richiesta: velocizzare R0,1 sui bordi elicoidali e correggerne il fallimento.
L'utente ha precisato che il raccordo va subito dopo **Differenza 6**.
Il documento corrente ha 21 funzioni/7 schizzi; non e' la precedente copia a 9 funzioni.
Snapshot `/tmp/applicatore4-raggio-velocita-2026-11-12/Applicatore4.prt`,
SHA256 `04a093b0d11071005395abc5a42ef90e1b1c13e11799957bb1da7e23bf292802`.
B7: 30 facce/84 bordi, elica passo3,5; il raccordo a monte della punta ha R1,7.

Tempi del kernel Release prima della modifica, esclusi mesh e check finali:

- Dodici semi E7,E20,E11,E28,E14,E32,E18,E40,E44,E47,E49,E52: R0,1 PASS,
  74 facce/194 bordi, 15,46s.
- Primi otto semi: R0,1 FAIL in14,95s, chiusura dell'estremo non sulle due superfici.
- Composizione certificata anticipata:0,73s; dodici semi R0,1 PASS in1,45s aggiuntivi;
  otto semi ancora FAIL in1,57s aggiuntivi. R0,01 parziale riesce, R0,05 fallisce.
- La ricerca di chiusura usa un punto a circa0,075mm dal contatto del raccordo.
  I tentativi sulle carte separate ripetono fit e mitre prima del supporto composto.

Implementata in `fk_blend.cpp` la preparazione anticipata degli appoggi G1 se i
semi toccano una NURBS e la genealogia dimostra che le facce scelte sono state
composte. Se il tentativo fallisce resta il percorso ordinario; gli appoggi gia'
preparati si riusano e il medesimo tentativo non si ripete. Nessuna selezione
estesa automaticamente, nessun raggio/tolleranza modificato.

Verifica Release ripetuta in tre coppie alternate: mediane15,193s prima e2,415s
dopo, guadagno6,29x. B-rep serializzato identico (SHA256
`7ba154717b083adec55cebe7e3a0ff669abcd0d58e8ab23f8e87686f4ed1050b`).
B-rep chiuso/valido e tutte le facce tassellate. Suite NURBS3/3 PASS
(raggi, pose, scala, ordine e genealogia). App e runner in ricompilazione.

Indagine preliminare sulla chiusura parziale: raffinamento del contatto, guide intrinseche
e guida assiale sono per ora sonde esclusivamente in `/tmp`; i candidati che non
superano i controlli non sono integrati. I tempi dei prototipi non sono benchmark
Release. Nessun file CAD originale riscritto.


### Consumo del bordo terminale verso una quarta faccia

Individuata la causa della selezione parziale: il contatto supera il vertice
del bordo corto tra fianco e cono. Cercare ancora l'intersezione sulle stesse
tre superfici porta a un punto fuori dal raccordo. La nuova gestione segue
l'adiacenza al vertice successivo e identifica la quarta faccia, senza scegliere
nuovi bordi da raccordare. Richiede un vertice di grado3, frontiere coerenti e
un candidato univoco con intersezione entro la tolleranza sorgente; rifiuta
l'interferenza con altri bordi scelti. La pezza d'angolo sostituisce la frontiera
consumata usando i budget gia' esistenti. Il tentativo e' atomico e successivo
ai percorsi precedenti; raggi variabili e smussi non sono estesi da questo caso.

Implementato in `fk_blend_surface.cpp`, simmetricamente sui due contatti.
Primo controllo Release sul B7 corrente: otto semi, R0,1 PASS2,928s,
54facce/140bordi, nessun errore B-rep, bordo aperto o faccia non tassellabile.
Fixture congelata `blend_terminal_fourth_face.body` e nuova regressione su
R0,05/0,1, pose, scala2,5, ordine delle facce, raggi e bordi della punta esclusi.
Le prime prove geometriche passano; la verifica ha evidenziato che i bordi e
vertici nuovi del percorso precedente non erano dichiarati: ora la provenienza
e' ricavata dall'incidenza alle pezze generate, senza ricorso alla vicinanza.
Verifica completa e ricompilazione app ancora in corso.

La composizione anticipata lascia prioritario il percorso specializzato dei
coperchi piani. Prima della nuova estensione:62/62Blend,38/38History,5/5Unify,
13/13CTest PASS e prova Wayland sul contorno completo PASS (puntatore e Undo/Redo).


Verifiche sull'estensione integrata: **4/4NURBS PASS**, inclusi bordi e vertici
nuovi dichiarati, doppia pezza d'angolo, assenza di raccordi aggiunti sulla punta
e conservazione dell'ingresso. **13/13CTest PASS** sulla build finale. App e
runner ricompilati. Wayland/OpenGL sul B7: parziale anteprima2,925s/conferma0,140s,
54facce; completo2,437s/0,120s,74facce. Puntatore riusato, Undo/Redo e B-rep/mesh PASS.
La selezione equivalente alla foto (E8,E22,E11,E28,E15,E34,E18,E40) riesce in3,101s.
Il risultato completo della build finale conserva lo stesso hash del benchmark.

Durante il lavoro e' stato rilevato un nuovo salvataggio esterno del `.prt`
(SHA256 `e39e44ce1e153ee6e942e55a4a999351cb80977bcb97e44541403e81360fa07c`):
B7 serializzato e' identico, SHA256
`f0a6bdab7cf8da3e61f61e5ce4fdabec799eb201ced294661824cdb981414789`.
Anche la nuova copia congelata supera la prova nativa parziale (3,136s/0,108s).
Nessuna sonda salva sul percorso originale.

Mappa finale:684file/9863relazioni,336unita' e nessuna diagnostica; `--check` PASS.
HTML verificato in Chromium per blend, unify, superfici, genealogia e regressioni,
con collegamenti in entrambi i versi. Suite finali **62/62Blend,38/38History,
5/5Unify PASS** dopo l'estensione e la dichiarazione per incidenza. `git diff --check` PASS.

Intervento concluso: R0,1 sul contorno completo e sulla selezione parziale
dopo Differenza6 verificato nel kernel e nell'app, senza riscrivere il CAD
o ampliare la selezione. Eseguibile `forgecad-release/forgecad` ricompilato;
un processo gia' aperto richiede la riapertura per caricare il kernel aggiornato.


## Indagine sul limite oltre R0,14 — 12 novembre, file salvato alle12:46

Richiesta: spiegare perche' non si riesce ad aumentare il raggio oltre0,14 sul
file gia' raccordato. Congelata una nuova copia, originale mai riscritto:
`/tmp/applicatore4-r014-2026-11-12/Applicatore4.prt`, SHA256
`3a4bfa6c1022551ecd732a124f72af878f9726c989caff7e78a4b70d1811edeb`.
Il file contiene19funzioni/6schizzi: Raccordo22 (indice8, base7/Differenza6),
24bordi salvati, R0,14, risultato47facce/124bordi; Raccordo23 (indice9,base8),
16bordi salvati, R0,14, risultato74facce/194bordi. Gli ID salvati si risolvono
senza perdita. B7 e' identico alla fixture del precedente intervento.

Prove Release sulle due selezioni effettive, ciascuna sulla propria base salvata:

| Raggio | Raccordo22 | Raccordo23 |
| --- | --- | --- |
|0,14|PASS|PASS|
|0,1401|PASS|PASS|
|0,145|PASS|PASS|
|0,146–0,149|non provati|FAIL|
|0,15|FAIL|FAIL|
|0,16|FAIL|FAIL|
|0,20|FAIL|FAIL|

Tutti i PASS includono checkBody con incroci, assenza di bordi aperti e
tassellazione completa. I FAIL riportano sempre
`blendEdges: passaggio tangente sul primo supporto non coincidente`,
in `fk_blend_surface.cpp`, classificazione del giunto dopo i passaggi Split.
Non esiste un limite configurato a0,14. Il blocco riguarda un giunto SplitA
alla transizione fra cono e calotta arrotondata a monte (R1,7). Nel supporto
composto sono F9→F5 sulla prima elica e F8→F6 sulla seconda, con il fianco
elicoidale invariato. La cucitura e' a y=12,788: superficie conica e NURBS
coincidono entro1,1e-10mm e le normali geometriche sono parallele (versi UV
opposti). A R0,15 le sezioni preliminari discordano di0,0141683mm e0,00595406mm.

Causa isolata: la ricerca preliminare del contatto restituisce t=0 prima di
raggiungere la cucitura corretta. Raffinando in3D il contatto esatto della
sezione contro la curva della cucitura, t diventa circa0,569751 e il residuo
scende da0,147799mm a1,9e-14mm. Nelle sonde R0,15 riesce su entrambi i
raccordi testati separatamente, con47/74facce, B-rep chiuso valido e mesh completa.
Questo dimostra che0,14 non e' il massimo geometrico di quelle operazioni.
Non e' stato provato il ricalcolo concatenato dell'intera storia con entrambi
i raggi a0,15.

Sonde esclusivamente in `/tmp`: semplice generalizzazione della mitra su A
non basta e viene respinta; raffinamento Gauss-Newton e riuso di
`meetBlendContacts` risolvono R0,15 senza allargare tolleranze. A R0,20 la
cucitura viene raffinata correttamente ma resta un successivo errore nella
mitra (scarto0,041804mm contro budget0,002mm): non e' un raggio certificato.
Nessuna di queste sonde e' integrata nel kernel o nell'eseguibile; nessuna
modifica funzionale di produzione in questo intervento. Restano valide le
prove precedenti solo per la build e i casi gia' certificati.

Hash originale ricontrollato a fine indagine: identico alla copia congelata.

## Estensione del dominio di raggio — intervento in corso

Su richiesta di raggiungere il massimo geometricamente ammissibile, integrato
in `fk_blend_surface.cpp` il raffinamento SplitA: il contatto esatto della
sezione viene incontrato con la cucitura finita usando `meetBlendContacts`.
La cucitura e il residuo devono rispettare la tolleranza originale; nessun
ID o raggio speciale. Build della sola libreria PASS; app e regressioni nuove
non ancora ricompilate.

Un secondo difetto e' stato isolato a R0,20: il parametro necessario alla mitra
cade fuori dal dominio di fit del breve tratto. Coprendo tutti i parametri
richiesti, la sonda scende da0,0418024mm a4,48e-6mm, sotto il budget gia'
esistente. Emerge poi un evento topologico: entrambi i contatti del breve
tratto si invertono, perche' i due cambi di supporto si sono scambiati di
ordine. Sono in sperimentazione la ricostruzione sulla coppia opposta di
supporti e il riordino degli eventi. Il massimo geometrico resta da dimostrare;
nessun fallimento del solver viene assunto come limite fisico.

### Intersezioni entro il dominio di fit e raggi intermedi

Integrati anche copertura di tutti i parametri richiesti dalla mitra e Newton
smorzato entro i domini delle due pezze, con ripartenza dai contatti comuni e
continuazione della stessa intersezione. Il vecchio Newton poteva seguire una
radice del prolungamento polinomiale molto lontano dal giunto:0,155 e0,16
fallivano mentre0,165–0,18 riuscivano. Le sonde corrette certificano0,155,
0,16 e0,18 con B-rep chiuso,0errori e mesh completa;0,19/0,20 raggiungono
l'evento del tratto interamente assorbito.

Helper nativo collegato agli oggetti dell'app e alla libreria aggiornata:
Raccordo22 e23 portati entrambi a0,18, ricalcolo da indice8 senza cambiare i
riferimenti salvati. Tutte le funzioni fino a indice18 PASS, ciascuna con
checkBody e mesh completi. Salvata solo una copia temporanea
`Applicatore4-R018-verifica.prt`; originale intatto. Prova ripetuta sulla nuova
libreria e verifiche di UI ancora da completare. Aggiunta regressione su
fixture con0,15/0,155/0,16/0,17/0,18, pose, scala e rinumerazione facce;
compilazione test PASS, esecuzione in corso.

### Verifica dell'integrazione numerica

Build completa `forgecad` e `forgecad_view_tests` PASS. Regressioni:
5/5 NURBS,62/62 Blend,38/38 History,5/5 Unify,13/13 CTest delle funzioni
raccordo/anteprima/genealogia/precisione/lifetime PASS. Mappa684file,
9863relazioni,336TU senza diagnostica; `--check` e HTML in entrambi i versi
superficie↔mitra e test↔blend PASS. Eseguibile aggiornato alle13:26 del12/11.

Con libreria Release definitiva: R22/R23 entrambi a0,16, storia fino a indice18
PASS e copia salvata. Copia a0,18 riletta, tutti i riferimenti salvati mantenuti,
ricalcolo da indice8 e nuova copia: PASS. SHA256 originale sempre identico.

Oltre0,18 resta sperimentale il cambio di ordine delle due transizioni di
supporto, con estensione certificata della cucitura e orientamento del nuovo
contorno. Le sonde non sono integrate: pur trovando intersezioni geometriche
locali, alcuni contorni non si chiudono; non vengono accettati come solidi.
Il massimo geometrico non e' ancora certificato.

### Certificazione dei jet NURBS — estensione generale integrata

La cucitura che genera il tratto corto separa un segmento di profilo e il
tratto curvo tangente dello sweep. L'atlante precedente lasciava quattro
fianchi lineari per elica: i jet differenziati venivano confrontati direttamente
con il limite di arrotondamento dei poli. Il residuo2,0e-13 superava128eps,
mentre la derivazione grado/campata e il rapporto dei jet amplificano lo stesso
roundoff dei coefficienti. Non era una discontinuita' geometrica del profilo.

`fk_nurbs_join.cpp` ora propaga il limite invariato sui coefficienti attraverso
le sottrazioni dei poli e i fattori ka/kb/alpha/beta. Limite del jet =
128eps × magnitudine normalizzata dei coefficienti ×
[2(|alpha ka|+|kb|)+|beta|]. Nessuna coordinata, peso o superficie viene
spostata. Evitato anche il pavimento assoluto sui jet, che avrebbe potuto
nascondere una piega cambiando solo la scala UV. Aggiunta regressione positiva
e negativa con scale UV1e-8,1,1e8 e ordine invertito.

Sonda generale, senza riordini di celle: atlante di B7 da20 a12facce, tutti i
fianchi dello stesso sweep composti esattamente. Sulle24referenze reali del
primo raccordo, R0,20 e0,30 PASS con23facce/60bordi,0errori,0bordi aperti e
mesh completa. R0,50 incontra un ulteriore errore SplitA;0,75 e oltre falliscono
la sezione e non sono assunti come limite fisico. Kernel e test ricompilati;
regressioni nuove, ricalcolo documentale e build app da completare.

### Validazione dell'atlante completo e interazioni fra funzioni

6/6 regressioni NURBS PASS: raggi fino a0,30, pose, scala, rinumerazione e
preservazione delle pieghe per scale UV1e-8..1e8. La fixture parziale ora
produce20facce/54bordi senza pezze artificiali generate da vertice (prima2).
La regressione e' stata aggiornata a richiedere esattamente0angoli artificiali,
con tutti i controlli di raggio, B-rep, genealogia e mesh invariati; il consumo
di una frontiera reale resta verificato dal caso sintetico
`BlendConsumedTerminalReachesTangentSupport`, che conserva i controlli di
volume, tangenza e patch triangolari. Non sono stati disattivati controlli.

App e runner ricompilati con il nuovo atlante;62Blend,38History,5Unify e13CTest
PASS. Mappa rigenerata/check PASS. Prove native sul file congelato:
- R22=R23=0,20: tutte19funzioni PASS,0errori e mesh complete.
- R22=0,30/R23=0,14: tutte19funzioni PASS.
- R22=R23=0,25 oppure0,30: il secondo raccordo fallisce sul passaggio SplitA.
- R22=0,40/R23=0,14: il secondo fallisce; il primo da solo e' valido.
- R22=0,14/R23=0,30 sulla base salvata: PASS; a0,40 non certificato.

I riferimenti R23 si risolvono per nome sui bordi corretti (scarti dal punto
salvato di pochi1e-7mm); le unioni sono dichiarate nella genealogia. Il problema
successivo riguarda l'interazione con il raccordo precedente vicino alla punta,
non un limite artificiale di0,14. L'estensione sperimentale della guida
intrinseca ai supporti curvi non risolve questo evento e non e' integrata.
La selezione simultanea completa a0,20 e' valida; a0,30 una pezza di lunghezza
parametrica nulla causa un'eccezione interna nel fallback. Sonda in corso per
riconoscere l'evento senza costruire una NURBS degenere e mantenere atomici i
tentativi. Il massimo geometrico resta aperto.

### Stato consegnato e scelta del parametro da portare al limite

Eseguibile completo aggiornato alle13:46:25 del12/11;6NURBS,62Blend,
38History,5Unify e13CTest PASS. Mappa684/9863/336TU,0diagnostiche,
check e6verifiche HTML dei collegamenti nei due versi PASS. Diff-check PASS.
Copia R22=0,30/R23=0,14 salvata, riletta e ricalcolata da indice8 fino alla
fine: tutte19funzioni, B-rep e mesh PASS. La copia e' solo una prova;
la correzione e' nel kernel. Originale SHA256 ancora
`3a4bfa6c1022551ecd732a124f72af878f9726c989caff7e78a4b70d1811edeb`.

La sonda che conserva gli estremi di una campata minuscola e riconosce un
intervallo parametrico esattamente nullo evita l'eccezione di costruzione
NURBS ma il raccordo simultaneo R0,30 fallisce ancora su una frontiera vicina;
non e' integrata e non viene presentata come soluzione. Non sono integrati
riordini di celle, cambi di guide o correzioni dipendenti da ID/raggi/file.

Il massimo geometrico non e' dichiarato raggiunto. I due raccordi consecutivi
hanno raggi indipendenti e si influenzano alla punta; richiesta all'utente la
scelta fra massimizzare R22 lasciando R23 a0,14 oppure aumentare entrambi
insieme, per determinare il limite della definizione effettivamente voluta.

## Verifica congiunta prima della pubblicazione

Su richiesta dell'utente, la correzione storyboard e' nel commit `ce84735`;
gli altri interventi locali sono raccolti in un secondo commit distinto.
Build completa di tutti i target PASS. Nuova verifica: 23/23 CTest mirati a
interfaccia, memoria, riferimenti, esportazione e storyboard; 2/2 CTest NURBS
(6 casi) e disegni; kernel 62/62 Blend, 38/38 History e 5/5 Unify PASS.
`git diff --check` e controllo della mappa architetturale PASS.
Report degli altri interventi e archivi sono inclusi, insieme alle fixture
di regressione; file CAD originali e vista architetturale generata restano esclusi.
