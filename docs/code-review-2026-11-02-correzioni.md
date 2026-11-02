# Correzioni della review, storyboard e nuova estrusione

Intervento del 2 novembre 2026, sulla base `ab6fc71` del ramo `main`.

**Aggiornamento successivo:** questa review è pubblicata con `d4f15fc`. La correzione del caso reale `Superfici.prt` e le verifiche sulla nuova build sono nel [rapporto estrusione](superfici-estrusione-2026-11-02.md); la nuova proiezione da due schizzi è nel [rapporto curva 3D](curva-proiettata-due-schizzi-2026-11-02.md). Gli esiti sotto restano quelli del checkpoint della review.
La richiesta comprende la correzione dei cinque rilievi confermati, due verifiche
indipendenti con agenti su GUI/3D e storyboard, e l'estrusione fino alle prime
superfici del corpo successivo. I sintomi precisati dall'utente sono rigenerazioni
errate, corpi nascosti senza richiesta e spigoli dei raccordi selezionati da soli.

## Correzioni R1–R5

| Rilievo | Correzione e regressione |
| --- | --- |
| R1, ricorsione sintattica STEP | Limite indipendente di 256 livelli, comune a liste e parametri tipizzati. File con 100.000 livelli producono un errore controllato prima di costruire alberi profondi; profondita' ordinaria preservata. |
| R2, allocazione anticipata nel B-rep nativo | Gradi, nodi, dimensioni e byte realmente disponibili sono verificati prima di allocare poli e pesi delle superfici. Il confronto usa divisioni per evitare overflow. Gli input troncati non richiedono memoria proporzionale a conteggi non presenti. |
| R3, molteplicità STEP | Gradi e molteplicità devono essere interi finiti rappresentabili; nodi distinti crescenti, molteplicità positive e compatibili col grado, totale coerente con i poli prima dell'espansione. Coperte curve e superfici, incluse entita' complesse razionali. |
| R4, catalogo concorrente | Lettura coerente ottimistica; in caso di generazioni diverse, rilettura sotto lock della transazione con attesa limitata. Lo stato temporaneamente occupato e' distinto dalla corruzione e preserva accesso e lock gia' posseduti nei frontend modello/tavola. Una corruzione persistente continua a bloccare le modifiche. |
| R5, occorrenze STEP perse | L'identita' comprende il percorso di prodotto, NAUO e MAPPED_ITEM, senza chiavi arrotondate delle trasformazioni. Preservate occorrenze coincidenti, distanti 4e-7 mm e annidate; visite duplicate della stessa occorrenza restano deduplicate. |

Nuova suite indipendente `forgekernel_exchange_robustness_tests`: cinque casi con
guardia delle allocazioni solo nel test. Verifiche separate di store, client e
frontend coprono contesa controllata, scritture concorrenti, recupero e corruzione
persistente. Nessun limite della memoria o tolleranza e' stato aumentato per
mascherare i difetti.

## Storyboard, GUI e visualizzazione 3D

L'agente storyboard ha corretto rimappature mancanti delle estrazioni multiple,
perdita d'identita' delle copie usate da riferimenti associativi/Pierce e
riassociazione errata della funzione esplicita nelle ripetizioni `featureOnly`.
Le componenti separate da Elimina facce mantengono ora identificativi di corpo
distinti: prima la normalizzazione le riuniva sotto lo stesso proprietario,
mostrando soltanto l'ultima. Sono riprodotte e corrette due cause di visibilita' inattesa: il recupero del tip
dopo soppressione/errore e il consumo di un utensile scambiato per Nascondi manuale.
Provati rigenerazione, rollback, Undo/Redo e persistenza. La fixture storica
del cambio di proprietario nasconde ora esplicitamente il corpo prima del consumo,
cosi' continua a verificare una preferenza manuale false senza dipendere dal bug;
le sue asserzioni restano invariate e la nuova suite copre anche true.

L'agente GUI ha corretto cache di picking obsolete prima del repaint, selezione
delle facce dietro un corpo opaco e highlight di raccordi lasciati da una lista
nascosta. Rendering e picking condividono ora la visibilita' effettiva durante
l'anteprima di modifica. Corretti anche il cursore del pan con auto-repeat e due
problemi dei test preesistenti, palette e directory delle catture storyboard.
La selezione deliberata di tutti i bordi di una faccia e' conservata.

B8 e' irrobustito con copie locali e verifica dell'ID dopo il loop responsivo.
La regressione inietta un reingresso: non e' stato riprodotto un normale evento
dell'interfaccia che inneschi questa ipotesi. Non spiega automaticamente tutti i
sintomi dell'utente.

Rapporti degli agenti: [GUI/3D](code-review-2026-11-02-gui-3d.md) e
[storyboard/estrusione](code-review-2026-11-02-storyboard-estrusione.md).

## Uso e contratto della nuova estrusione

Nel pannello Estrusione scegliere **Fino al prossimo corpo**, poi il verso
**Positivo** o **Invertito**. Occorre un profilo chiuso solido e un solo verso.
I corpi limite sono i risultati solidi precedenti, attivi e non consumati; anche
un corpo nascosto manualmente puo' delimitare l'estrusione. Non occorre scegliere
una faccia e i limiti non vengono consumati.

La ricerca dimensiona un prisma con bounds conservativi, poi usa booleane B-rep
sulle superfici esatte. Le prime superfici incontrate possono appartenere a piu'
corpi. Candidati, dipendenze e genealogia si aggiornano anche in rigenerazione,
per inserimento in rollback, soppressione, riattivazione e spostamento dei limiti.
Il formato nativo resta 60; il nuovo valore di fine e' aggiunto dopo i precedenti.

Provati piani, piano inclinato, cilindro, sfera, cono, B-spline polinomiale,
utensili multipli e profili disgiunti, con verifiche geometriche/volumetriche.
Anteprima, conferma, Undo/Redo e salvataggio/riapertura sono coperti.

Limiti espliciti: profilo non interamente coperto, intersezione al piano iniziale,
sottosquadri, ostacoli interni non risolvibili e superfici il cui primo incontro
non e' certificabile danno errore. Le NURBS con pesi variabili non sono certificate
universalmente. Nessun ripiego su mesh o chiusura approssimata. Non sono disponibili
in questa modalita' simmetrica, due versi o sola superficie.

## Verifica finale

- **Build completa finale PASS**, applicazione e runner Release aggiornati su Linux,
  GCC 16.2.1/Qt 6.12; OCCT 7.9.3 confinato ai test di confronto. Occorre riaprire
  l'app per caricare il nuovo eseguibile.
- **CTest consolidato: 151 PASS, 1 SKIP, 1 FAIL su 153.** Prima esecuzione completa
  di 153 test (406,30 s); dopo la correzione delle componenti, nuova esecuzione
  dei 152 test restanti (67,81 s), nessun fallimento e uno skip. Il binario del
  kernel completo e' invariato, verificato con SHA256: il suo risultato resta
  valido per la build finale. I due fallimenti iniziali della storyboard sono
  superati sul runner definitivo, senza rimuovere asserzioni.
- **Unico FAIL residuo preesistente:** `DraftCurvedAndConcaveSolids`,
  `kernel/tests/test_draft.cpp:134`; volume 253.5270716475676 contro
  253.5270734420086, scarto 1.794441e-6 oltre il limite 1e-6. Il kernel completo
  passa 457/458 controlli. Nessuna tolleranza o aspettativa numerica modificata;
  il difetto non e' dichiarato risolto. Lo SKIP e' `forgecad_overlay_uniform_tests`,
  che richiede un contesto grafico e non viene certificato dall'offscreen.
- Robustezza scambio **5/5 PASS**, anche con ASan/UBSan su `fk_step.cpp`,
  `fk_body_io.cpp` e test dedicato; gli altri oggetti restano Release, rilevazione
  leak disabilitata. Nessuna diagnostica dei sanitizer. Filtri STEP **20/20**,
  IGES **9/9**, BodyIO **2/2** PASS.
- Runner definitivo: **6/6 prove OpenGL reali PASS** su Wayland/Intel Mesa e
  XCB/NVIDIA, incluse due ricreazioni del contesto per backend, picking,
  raccordi e nuova estrusione con Undo/Redo. Log e immagini ispezionati
  dall'agente GUI; nessuna prova macOS o su altre versioni Qt/OCCT.
- Mappa rigenerata: **531 file, 7.305 relazioni, 249 unita' AST, zero errori**;
  `--check` PASS. Controllati in entrambi i versi i collegamenti dei moduli
  modificati, distinguendo call/include/reference, e cinque chiamate fra moduli
  relative a visibilita', candidati e importazione. Pagina HTML ispezionata in
  Firefox headless, dettaglio `cad_extrude.cpp` leggibile con entrate e uscite.
  HTML e JSON generati restano ignorati da Git.

[Archivio delle evidenze](history/code-review-2026-11-02-correzioni-verifica.zip):
log delle due esecuzioni CTest, JUnit originali e consolidato con provenienza,
metadati/hash delle build, verifiche delle fixture, controlli della mappa,
log e catture delle sei prove grafiche finali. Il consolidato usa l'ultimo esito
per ogni test; non viene presentato come una singola invocazione CTest.

Le prove usano copie congelate di `Mouse.prt`, `Applicatore.prt` e
`prova con loft.prt`; manifest CTest copiati in `/tmp` cambiano soltanto la
destinazione delle fixture, conservando comandi, timeout e proprieta'. Gli
originali non sono modificati. Nessun processo dell'utente chiuso, nessun commit
o push. Le modifiche documentali preesistenti sono preservate.
