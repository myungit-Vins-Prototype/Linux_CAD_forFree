# K10: residui pcurve non finiti — 17 novembre 2026

Corretto `pcurveDeviation` in `kernel/fk_pcurve.cpp`: campioni mancanti o valutazioni NaN potevano produrre errore zero. `exactPCurve` e i due percorsi finali di `fitPCurve` potevano così accettare una pcurve senza una misura valida. Ora la misura non verificabile restituisce infinito positivo e l’accettazione richiede un residuo finito entro il budget già previsto. Nessun aumento delle tolleranze; il fattore storico 50 del fallback lineare rimane invariato.

La correzione è integrata e verificata localmente. **K10 resta parziale; il totale dei 33 rilievi funzionali della review rimane invariato.** Nessun commit o push.

## Riproduzioni e limiti

Tre prove sulle API reali del kernel, eseguite dal coordinatore prima e dopo la patch:

- Retta distante 1 mm dal piano: con campioni −1 o 0 la baseline restituiva 0; ora restituisce +∞. Con 1 o 16 campioni il risultato rimane 1 mm.
- Cerchio analitico con centro e raggio finiti prossimi a `DBL_MAX`: alcuni punti valutati traboccano. Prima errore 0 e pcurve esatta accettata; ora +∞ e risultato nullo. Sono coordinate fuori dall’intervallo dimensionale ordinario CAD: è una prova del contratto numerico, senza rivendicare un documento reale risolto.
- NURBS cubica nativa con quattro poli nulli, nodi `[1,1,1,1,2,2,2,2]` e pesi `[21,29,37,45]`, deliberatamente interrogata **fuori dominio** su `[0,1]`. Il denominatore estrapolato è `24t−3`, nullo in `t=0,125`: punto non finito. Prima `fitPCurve` riusciva con errore 0; ora fallisce e conserva il valore precedente dell’argomento di uscita. Il controllo positivo sul dominio `[1,2]` riesce. L’algebra razionale è stata verificata indipendentemente dall’auditor; l’esecuzione è del coordinatore.

Non è una nuova validazione generale dei domini NURBS. La misura rimane **campionata**: non certifica l’errore massimo continuo fra sonde, né il carrier rispetto all’intera mappa o la topologia B-rep. La verifica continua già disponibile per alcune famiglie resta distinta.

L’indagine iniziale sul riporto manuale UV vicino ai poli non ha fornito un testimone pubblico di uscita geometrica errata: il fit finale ricontrolla la curva effettiva. Nessuna modifica speculativa a quel ramo. Resta da approfondire insieme a Newton e ai casi globali di K10; nessun nuovo difetto pubblico attribuito a `nearPole`.

## Implementazione e controlli

La misura verifica range, quantità di campioni, breakpoint prima dell’ordinamento, parametri, UV, punti e distanza. Richiede copertura degli estremi; non incrementa `INT_MAX` e consente cancellazione durante i campioni. Eccezioni e cancellazione si propagano; nessun limite artificiale al numero di campioni. Non è una garanzia contro cicli interni dei singoli generatori di breakpoint. I tre punti di accettazione richiedono esplicitamente una misura finita anche con tolleranza infinita; il parametro `deviation` viene aggiornato solo sul successo finale.

Una sola TU di produzione modificata, nessun header pubblico. Nuova TU `kernel/tests/test_pcurve_deviation_validation.cpp`, target CMake dedicato e descrizioni architetturali aggiornate. Tre agenti GPT‑6.1 Sol per patch, audit e test; nessuna compilazione concorrente o modifica autonoma al repository. Runtime e integrazione eseguiti dal coordinatore.

| Verifica corrente | Esito |
| --- | --- |
| Nuovi gruppi, prima/dopo | 3/6 → 6/6 |
| ASan/UBSan, leak detection attiva | 6/6, nessuna segnalazione |
| Build completa app e runner pertinenti, seriale | PASS |
| CTest: deviazione pcurve, offset pcurve, sheet join, sew pcurve | 4/4 PASS, nessuno skip |
| Runner storico, solo filtro `PCurve` | 11/11 PASS |
| Mappa architetturale, hash/configurazione/HTML e `--check` | PASS, 797 nodi / 11666 relazioni / 404 TU |

I sei gruppi comprendono controlli finiti positivi, breakpoint originali di E/Q, range/campioni invalidi, overflow delle valutazioni e del calcolo della distanza, accettazione e cancellazione. ASan/UBSan strumentano `fk_pcurve.cpp`, la nuova TU test e il main; il resto del kernel è Release. Non è un’analisi completa dei leak dell’applicazione. Nessun test UI o avvio grafico, nessuna esecuzione completa dei 483 test, nessuna nuova CI. Il binario sul disco è aggiornato; l’app già aperta non è stata riavviata.

## Conservazione e memoria

Eliminati soltanto 13 ELF/oggetti privati ricreabili di questa prova, 385,704,392 byte (circa 367.8 MiB). Nessuna copia aggiuntiva della libreria statica; compilazioni e analisi seriali. Conservati sorgenti e log, build corrente, CAD e cartelle QA dell’utente. Il precedente recupero di circa 14 GiB è descritto nel rapporto RAM/fit, non viene conteggiato di nuovo.

Archivio completo della memoria precedente in [cronologia](history/claude-prima-k10-pcurve-residui-finiti-2026-11-17.md). Audit finale verifica i 7.374 file della baseline: sole cinque modifiche esistenti previste, 7.369 file preservati, più i nuovi file espliciti. Verificati integri 34 pacchetti precedenti e 4.756 artefatti; HEAD/origin `47c7c19`, index vuoto. Gli output architetturali ignorati non sono stati aggiunti a Git.

Prove, comandi, log, tempi, sorgenti inerti e hash dei binari in [pacchetto verificabile](review-k10-pcurve-residui-finiti-prove-2026-11-17/README.txt); il suo `SHA256SUMS` autentica i percorsi effettivi. La CI storica non certifica queste modifiche locali.

Eseguibile attuale SHA‑256: `7f10c1ee12b4662a691da1ef8b3be7e5f8ee9a3f7fbf6f9ad3702ae189e8a3d4`.
Kernel attuale SHA‑256: `3f663c15f980ff7f35fa68e762f3393a9298a35f7293507004243a50af9d45df`.
