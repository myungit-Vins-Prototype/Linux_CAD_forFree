# Review: memoria temporanea e fit delle superfici — 17 novembre 2026

Liberati **15.065.226.318 byte, circa 14 GiB**, rimuovendo 284 ELF/archivi ricreabili dagli staging delle review concluse in `/tmp`. Corretto inoltre un bypass dei controlli del fit: una callback che restituiva `NaN` in una sonda interna poteva produrre una superficie dichiarata valida. Nessun commit o push. K10 globale resta parziale e i 33 rilievi funzionali precedenti non vengono incrementati per questa prosecuzione.

## Memoria e cartelle di diagnosi

Su questo host `/tmp` è un filesystem `tmpfs`: i file occupano memoria/swap. Prima della pulizia risultava occupato per circa 15 GiB su 16. Sono stati eliminati solo binari ELF e archivi statici da almeno 1 MiB, presenti in un elenco esplicito di staging conclusi. Prima della rimozione sono stati registrati percorso, dimensione, inode, data e SHA256, controllati i riferimenti dei processi accessibili e verificati tutti i 32 pacchetti di prove precedenti, 4.501 artefatti. Nessun processo è stato chiuso; nessun sorgente, log, JSON, CAD, eseguibile di produzione o pacchetto di prove è stato eliminato.

Subito dopo la pulizia `/tmp` risultava occupato per circa 890 MiB. La memoria disponibile osservata è salita da circa 9 a 16 GiB e la swap usata è scesa da circa 19 a 7,4 GiB. Sono misure globali del sistema, variabili con gli altri processi; il dato esatto attribuibile alla rimozione è il numero di byte sopra. Non è un memory leak dimostrato nel CAD.

L’utente ha chiesto anche se può cancellare le cartelle di diagnosi in `forgecad-release`: le sole cartelle `*-qa` occupavano circa 16 GiB e non servono al lavoro corrente. Contengono però copie `.prt`/STEP e risultati storici: vanno conservati prima se desiderati. `architecture-cache`, circa 9,5 GiB, è ricreabile ma la sua rimozione rallenta la prossima analisi. `kernel`, `CMakeFiles`, `*_autogen` e l’eseguibile appartengono alla build corrente. **Non è stato cancellato nulla dentro `forgecad-release`.**

Per il seguito sono state usate compilazioni seriali, analisi architetturale con un solo worker e link diretto alla libreria esistente, senza copie dell’archivio in RAM. Il massimo RSS di processo figlio misurato con GNU time è stato circa617MiB nella build e362MiB nell’analisi, non il consumo globale del sistema. A verifiche concluse sono stati rimossi anche i nuovi ELF/oggetti privati, mantenendo sorgenti, hash, comandi e risultati.

## Difetto e correzione

`fitMappingSurface` accumulava gli errori con `std::max(accumulatore, distance(...))`. Con `NaN` come secondo argomento il confronto è falso: il massimo precedente resta invariato e il fit può apparire entro budget. Il costruttore della superficie non rileva il problema se i poli costruiti dai corner/stencil sono tutti finiti e solo una sonda di accettazione restituisce `NaN`.

Riproduttore tramite API interna reale: mappa `(u,v,0)` su `[0,1]²`, jet numerico predefinito, salvo `NaN` in X quando `u=v=0,125`. La baseline restituisce `ACCEPTED surface=1`, deviazione circa `1,57009e-16`; il candidato restituisce `REJECTED` con errore di valore non finito e deviazione iniziale conservata. Il test include anche jet analitico, grado quintico e infiniti. Non viene attribuito un nuovo fallimento a un documento utente.

La correzione controlla ogni valore della callback, tutti e quattro i vettori del jet analitico, i poli intermedi delle celle e la finitezza delle distanze prima dei massimi. Rifiuta tolleranze non finite/non positive e griglie non finite, non strettamente crescenti o con ampiezza non rappresentabile. Pubblica la deviazione solo dopo la costruzione riuscita della superficie. Non introduce un nuovo vincolo sull’accumulatore iniziale: i test mantengono sia il valore negativo già ammesso sia un massimo precedente positivo.

Restano invariati formule, sonde, raffinamento, gradi e budget geometrici dei casi finiti ordinari. Il fit resta campionato: rifiutare `NaN` non prova che un picco finito fra le sonde sia assente, né certifica intento globale della mappa, Newton, iniettività o topologia.

## Verifiche e limiti

| Verifica | Risultato |
| --- | --- |
| Nuovi gruppi sulla baseline | 3/6 PASS; falliscono sonde non finite, campi del jet ignorati e preflight. Non tutti i controlli negativi erano assenti prima. |
| Candidato Release | 6/6 PASS, inclusi controlli finiti cubici/quintici, corner/stencil, callback/jet, griglie/tolleranze, errori e cancellazione. |
| Sanitizer mirati | 6/6 PASS con ASan/UBSan e rilevazione leak attiva; una TU produzione, nuova TU test e main strumentati, restante libreria Release. Nessuna prova di memoria dell’intera app. |
| Build completa dell’app e due target kernel | PASS in 19.30s, compilazione seriale; eseguibile aggiornato, app aperta non riavviata. |
| CTest mapping nonPlane e surface mapping | 2/2 PASS senza skip in 1.02s; runner nonPlane12/12, inclusi i sei nuovi gruppi. |
| Audit indipendente | Diff completo e comportamento IEEE di `std::max` ricontrollati da Sol; nessun blocker. L’auditor non ha compilato né eseguito C++. |
| Architettura e conservazione | PASS: 792 nodi, 11636 relazioni, 402 TU; hash sorgenti/configurazione/HTML e chiamate/include/riferimenti nei due versi verificati.32 pacchetti precedenti preservati. |

Le sei regressioni nuove sono incluse nel runner mapping nonPlane: i loro conteggi e quelli CTest si sovrappongono. Nessun test o avvio UI, suite generale 483 o nuova CI. L’app aperta non è stata riavviata. La pulizia non modifica la validità storica delle prove archiviate: elimina solo binari ricreabili di staging.

[Prove, piani di pulizia e sorgenti inerti](review-k10-mapping-fit-finiti-prove-2026-11-17/README.txt). [Memoria precedente](history/claude-prima-k10-mapping-fit-finiti-2026-11-17.md). Restano aperti carrier rispetto alla mappa completa, Newton e famiglie geometriche non riconosciute.
