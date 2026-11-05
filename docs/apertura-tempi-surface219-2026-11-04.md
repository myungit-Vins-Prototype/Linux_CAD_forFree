# Apertura di SurfaceModellingExample219 — 4 novembre 2026

## Risultato

L'apertura con snapshot valido e il ricalcolo parametrico hanno costi diversi.
Il salvataggio dell'utente esaminato (11:48, hash 17f72219) si apre fino al primo
fotogramma in **3,870 s**, riusando lo svuotamento salvato.

Il ricalcolo era invece lento per le proiezioni NURBS e le quadrature di massa.
Sono integrati due riusi di dati matematici invarianti: coefficienti combinatori dei
prodotti Bernstein e contrazione della base U nelle quadrature a u fisso.
Il confronto kernel isolato passa da **224,30 a 72,64 s (3,09×)**, con risultati
B-rep byte-identici. La rigenerazione nativa completa delle **26 feature senza
cache richiede 92,463 s**, contro circa 270 s nella precedente prova storica.

Il modello resta costoso: il ricalcolo completo non è istantaneo. Non sono state
semplificate le superfici né aumentate le tolleranze; le proprietà di massa restano
complete. Build app/runner, regressioni, test del nuovo modulo, sanitizer e mappa
architetturale verificati come dettagliato sotto.

## Documenti distinti

- Vecchia copia congelata `input-current.prt`: SHA256 `a501f4fddff8768ae9855a7443ba00502096549ca3095cbcdd90f561412c8f57`.
  Cache precedente alla versione semantica 12: la build attuale scarta atomicamente i
  vecchi risultati dei documenti di lavoro con loft/raccordi attivi. Restano 26 definizioni,
  zero snapshot B-rep accettati. Riaprire questa copia senza salvarla dopo la rigenerazione
  ripete il calcolo a ogni apertura. La sola lettura impiega 78 ms perché non decodifica i corpi scartati.
- File corrente `/home/myungit/Documenti/Cad_Example/SurfaceModellingExample219.prt`:
  **84.293.603 byte**, SHA256 `17f72219f9202ac892f9609221b5f23570f9b1a8eacd78db519eb8084978f453`.
  È cambiato rispetto alla copia congelata: conserva già 15 snapshot B-rep per le
  feature che producono corpi, oltre alle definizioni di piani/curve, per 26 feature totali.
  Nessuna modifica di schizzo pendente; guscio finale di 62 facce, nessun errore.
- Copia verificata `SurfaceModellingExample219-svuotamento-corretto.prt`: 84.289.327 byte,
  cache accettata con i limiti standard. È distinta dal nuovo salvataggio dell'utente.

Il file originale è stato solamente letto. La prova del comando Apri completo usa una
copia byte-identica del salvataggio 17f72219 in `/tmp`. Dopo le prove è stato rilevato
un ulteriore salvataggio esterno, SHA256 `f695dd9815ce4daaef1306ea7037b592afaca1f40e7e27314991ad32e8668463`.
I tempi si riferiscono alla copia 17f72219, senza estendere la verifica alla revisione
salvata nel frattempo. Nessun comando di questa indagine scrive il percorso originale.

## Misure nuove sulla build corrente

Linux/Qt6, Wayland/OpenGL nativo, impostazioni QA isolate, lettura con i limiti standard
(produttivo: cache espansa cumulativa di 1 GiB). Nessun aumento dei budget nella sonda.

| Percorso | Lettura | Ripristino viewport | Primo fotogramma |
| --- | ---: | ---: | ---: |
| File corrente, codec + viewport | 568 ms | 2.294 ms cumulativi | 2.459 ms cumulativi |
| Copia corretta, codec + viewport | 1.027 ms | 3.987 ms cumulativi | 4.082 ms cumulativi |
| File corrente copiato, comando `PdfWindow::openDocumentPath` | inclusa | 3.852 ms fino al ritorno | 3.870 ms cumulativi |

Il terzo percorso include il comando applicativo effettivo: accesso al documento,
caricamento, fingerprint ove richiesto, rigenerazione/ripristino, storyboard e visualizzazione.
La finestra principale era già avviata. Cache del filesystem non svuotata: non sono
misure di avvio a freddo dell'applicazione o del sistema.

Il puntatore del B-rep finale è identico prima/dopo `CadViewport::loadDocument` nelle
prime due prove: lo svuotamento viene riusato, senza un nuovo calcolo. La sonda applicativa
conferma 26 feature, 62 facce e zero errori. Il profilo per feature attribuisce a
`Specchio 16` 1,508 s nella prima prova e 1,875 s nel comando completo; le altre
costruzioni sono sotto la risoluzione di 1 ms del cronometro. La madre dello specchio
ricrea i risultati derivati anche all'apertura (`cad_feature_build.cpp`): è un costo
residuo misurato, non la spiegazione dei minuti di rigenerazione.

## Evidenze e limiti

Sonde C++, script di collegamento e log conservati nella cartella ignorata
`forgecad-release/shell-nonfinite-2026-11-04-qa/opening/`. Le sonde riusano gli oggetti
della build completa; nessun sorgente di produzione né eseguibile dell'app modificato.
Il comando di build si arresta al primo errore. Prove nuove entrambe PASS; nessuna
suite generale rieseguita per questa indagine senza modifiche al codice.

Log storici di confronto nella cartella QA superiore: `native-wayland.log`
(`REBUILT 37946`, `RELOAD cache=1 282697`, `RELOAD cache=0 552939`),
`position-jet-shell.log` (`SHELL_SUCCESS ... ms=217304`).

La migrazione della cache spiega la rigenerazione del vecchio documento. Un eventuale
ritardo ancora presente sulla versione corrente **non è riprodotto** da queste prove:
non va attribuito automaticamente alla cache o alla sola dimensione del file. Rimane
aperta la profilazione delle fasi interne dello svuotamento per accelerarne il ricalcolo.


## Ricalcolo: cause e ottimizzazioni

Su richiesta successiva dell'utente, l'indagine si concentra sul ricalcolo effettivo,
indipendentemente dalla cache. Campionamento GDB di un processo QA appositamente
avviato: 91 campioni ogni 3 s, conclusione normale con 62 facce e zero issue.
Il tempo sotto debugger non è un benchmark comparabile con l'esecuzione normale.
I contatori perf sono indisponibili per i permessi del sistema; nessuna impostazione modificata.

Primo spreco individuato e corretto in `fk_bernstein.cpp`: i coefficienti binomiali
invarianti del prodotto venivano ricalcolati dentro ogni coppia della convoluzione.
Ora vengono precalcolati per le due righe, con la stessa ricorrenza e lo stesso
ordine delle somme. Nuova prova matematica dei prodotti fino al grado 101.
Build app completa e runner riuscite; Bernstein 6/6, Shell 18/18, Offset 26/26 PASS.
I filtri iniziali CurveAlgo/SurfaceAlgo non corrispondevano a nomi di test (0/0):
non sono evidenza di verifica delle proiezioni; usare il filtro Project.
Il B-rep dello svuotamento prima/dopo questa sola modifica è byte-identico,
SHA256 `50c523e5b154ecf1f445e9d0b76baf9ddca666d39c6b0250e9a14a79e82252c4`.

Altro costo misurato: le quadrature complete di massa nella cucitura e nel controllo
finale dello svuotamento. Una prova **soltanto temporanea** del volume scalare passa
il caso in 163 s; la variante Bernstein con proprietà complete in 242 s. I processi
hanno avuto carichi concorrenti: nessuno speedup definitivo ricavato da queste durate.
La variante scalare non è integrata e non sostituisce le proprietà di massa dell'app.
È stato integrato il riuso della contrazione a u fisso della NURBS nelle quadrature
interne, con proprietà complete e tolleranze conservate.


### Confronto isolato sullo stesso B-rep

Vecchio binario kernel e variante integrata eseguiti in sequenza, fuori dal debugger,
sullo stesso `final-native/native-source.body`, F0/F14 e 1 mm. Nessun altro lavoro QA
pesante concorrente. I tempi comprendono lettura, svuotamento, controllo B-rep e scrittura.

| Variante | Tempo totale | CPU utente | Volume nella cucitura |
| --- | ---: | ---: | ---: |
| Kernel precedente | 224,30 s | 702,65 s | 21,554 s |
| Bernstein precalcolati + contrazione U | 72,64 s | 271,23 s | 6,861 s |

Guadagno **3,09×**, tempo ridotto del **67,6%** in questa singola coppia di prove locali.
Entrambe producono 62 facce e zero issue; confronto binario completo PASS, stesso hash
`50c523e5b154ecf1f445e9d0b76baf9ddca666d39c6b0250e9a14a79e82252c4`.
Nessuna nuova approssimazione della superficie e nessun incremento delle tolleranze.
La contrazione è riusata soltanto quando la quadratura attraversa diverse campate V;
restano completi volume, area, baricentro e tensore. La variante sperimentale del solo
volume non è integrata. I 73 secondi restano un limite del caso, non un ricalcolo istantaneo.
La rigenerazione nativa delle 26 feature e le regressioni finali sono riuscite.

### Verifica applicativa e regressioni finali

Build completa `forgecad` e `forgecad_view_tests` riuscita. Gruppi finali:
nuovo isoline 3/3, Bernstein 6/6, Mass 11/11, Project 25/25, Shell 18/18,
Offset 26/26, Boolean 38/38 PASS. I filtri condividono alcuni test: i conteggi
non rappresentano un totale di prove tutte distinte. La nuova fixture di massa usa
due box disgiunti (almeno 8 facce), esercitando davvero il percorso parallelo.
Controlla volume, area, baricentro e traccia del tensore contro formule analitiche,
identità seriale/parallela e immutabilità del B-rep.

QA nativa Wayland, `CadViewport::loadDocument`, definizioni sole in `roundtrip-0.prt`,
zero snapshot prima del calcolo: **26 feature, 92,463 s di ricalcolo, primo fotogramma
92,669 s**, nessun errore. Raccordo 24: 4,443 s; Raccordo 25: 8,964 s;
Svuota 1: 71,110 s. Mesh visibili: 542 ms. Anche il B-rep della rigenerazione completa
è byte-identico al risultato del kernel congelato. Il confronto con i circa 270 s
nativi precedenti è storico, distinto dal benchmark kernel isolato della tabella.

Le prime compilazioni della nuova fixture erano incomplete (target non ancora
riconfigurato, poi nome enum correggibile solo nel test); non sono state conteggiate
come prove riuscite. La pipeline finale arresta il lavoro al primo errore di build/test.
Nessun CAD originale modificato; tutte le prove di ricalcolo usano copie congelate.
AddressSanitizer/UBSan: **3/3 PASS** sul nuovo modulo e le sue fixture, inclusa la
massa dei due box NURBS; il resto della libreria collegata non è stato ricompilato con
sanitizer. Non è una dichiarazione di copertura dell'intera app.
Vista architetturale aggiornata: **579 file, 8023 relazioni, 278 unità AST,
zero unità con diagnostica**; `--check` PASS. Verificati entrata/uscita di cinque
moduli e sei confini di chiamata/include; overview e cinque dettagli HTML resi e
controllati, compreso il nuovo valutatore. Chiamate, include e riferimenti restano
separati. HTML/JSON generati restano ignorati da Git.

Le modifiche sono locali nel working tree; nessun nuovo commit o push in questo
intervento. L'app locale e il runner sono ricompilati; la sessione già aperta
continua a usare il precedente eseguibile finché viene riavviata.
