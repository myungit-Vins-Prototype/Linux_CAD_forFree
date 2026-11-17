# K10 — accumuli della quadratura, 17 novembre 2026

La quadratura poteva bloccarsi anche dopo la correzione della norma delle derivate: una B-spline rettilinea di **1 mm**, con intervallo parametrico `h=2^-1023`, restituisce una derivata finita `2^1023`. La somma interna di due campioni traboccava prima della moltiplicazione per l’ampiezza dell’intervallo. Il candidato recupera il risultato **1 mm**. La correzione è integrata nei percorsi condivisi scalare e vettoriale; K10 nel suo complesso resta parziale e il conteggio storico dei 33 rilievi non cambia.

## Riscontro e intervento

Riproduttore nativo `BSplineCurve<3>` grado 1, nodi `{0,0,h,h}`, poli `(0,0,0)` e `(1,0,0)`, nessun CAD originale modificato. Baseline Linux attuale: derivata `8.9884656743115795e+307` finita e watchdog esterno da 0,3 s, exit 124. Non è una misura dell’intero albero: i miliardi di pannelli sono una possibilità dedotta staticamente dalla profondità 30. Il candidato torna con `RETURNED 1`.

`fk_quadrature.h` conserva formule e ordine delle operazioni ordinarie. Memorizza i 15 campioni già richiesti; se gli accumuli traboccano, li risomma in scala e ripristina insieme campioni, semiampiezza e somma con `frexp/scalbn`. Valori e grandezze vettoriali hanno scale separate. I test contano 15 callback esatte anche nel ripiego: non vengono ripetute valutazioni potenzialmente costose o con stato.

Centro e semiampiezza usano formule alternative solo quando somma o differenza degli estremi traboccano. Parametri e callback non finiti, pannelli invalidi e somme dei figli non rappresentabili producono `std::domain_error`. I pannelli vettoriali sono validati prima dell’accettazione, anche a profondità 30. Sono inoltre rifiutati gli intervalli con semiampiezza nulla per estremi distinti e i tentativi di suddivisione senza avanzamento. Restano invariati pesi, tolleranze, criterio di rumore e limite di profondità.

Per la massa, i tre contatori degli avvolgimenti venivano intenzionalmente esclusi dall’adattamento tramite una grandezza infinita. Ora il numero delle componenti che guidano il raffinamento è esplicito (`controlled`, tutte per default): la massa ne controlla 11 su 14, conserva i tre contatori e verifica comunque la finitezza di ogni componente. I test forzano la ricorsione e confrontano callback e componente attiva con la quadratura della sola componente attiva.

La correzione riguarda lunghezze/cache del loft, profili, massa del kernel e integrali delle curve nell’applicazione. CMake protegge i chiamanti di produzione coinvolti con `-fno-fast-math` e contrazione FMA disabilitata. Aggiunto il runner indipendente `forgekernel_quadrature_validation_tests`; aggiornate le descrizioni curate della mappa.

## Verifiche

| Verifica | Esito |
| --- | --- |
| Baseline nativa, watchdog 0,3 s | timeout 124, derivata finita |
| Candidato nativo | lunghezza 1 mm |
| Nuovi gruppi Release | 6/6 PASS |
| ASan/UBSan con leak detection | 6/6 PASS, ambito descritto sotto |
| Baseline SafeControls | 1/1 PASS, sorgente del gruppo identico nella TU finale |
| Golden GK scalare/vettoriale | 26 righe hexfloat bit-identiche |
| Golden lunghezze/cache | 8 righe hexfloat bit-identiche |
| Golden massa pubblica | 3 corpi, volume/area/baricentro/inerzia bit-identici |
| App completa e runner mirati, seriali | PASS, 691,103 s |
| CTest selezionati | 4/4 PASS |
| Regressioni storiche filtrate | 10 casi PASS su 9 filtri |
| Mappa seriale e --check | 802 nodi/11741 relazioni/408 TU, zero errori; 864,220 s |

I tre corpi del golden sono parallelepipedo, cilindro ed estrusione con profilo NURBS razionale. I filtri storici riguardano ArcLength, cache, stagnazione della quadratura, massa di box/estrusioni/spline verticali, determinismo/errore/cancellazione della massa parallela e loft circolari. CTest: quadratura, lunghezze, derivate B-spline ed estensione di lunghezza delle lamine. Verificati hash sorgenti/configurazione, JSON incorporato nell’HTML e archi tipizzati nei due versi.

Eseguibile compilato SHA256 `51678833150ff31c0f76d78e88c9c0e3eab9fd3eee0838f5e4a0d5df26d5dd1e`. Ripuliti 29 ELF/oggetti ricreabili dello staging, 186413880 byte (177,8 MiB); nessuna libreria statica copiata, build e processi utente preservati.

I sanitizer strumentano la TU di lunghezza, il nuovo test e il suo main; il resto della libreria è Release. La quadratura vettoriale viene istanziata e strumentata nel test. Non è una verifica delle perdite dell’intera applicazione. Sulla baseline è eseguito soltanto il gruppo `SafeControls` della TU V1, identico nella TU finale; gli altri gruppi, potenzialmente bloccanti prima della correzione, sono eseguiti sul candidato.

I sei gruppi coprono controlli ordinari e integrazione orientata, tolleranze 0/negative legacy, integrandi estremi con integrale finito, grandezze vettoriali indipendenti, callback senza replay, lunghezza nativa/cache, rifiuti NaN/Inf anche a profondità 30, estremi grandi e integrali non rappresentabili.

## Limiti e prosecuzione

Gauss–Kronrod fornisce una **stima numerica**, non un limite continuo certificato. La normalizzazione dei campioni usa operazioni double: rifiuta la perdita completa di un campione nel riscalamento, ma non è una trasformazione simbolica esatta. Il ripiego non garantisce tutte le combinazioni di cancellazione e scale estreme. I prodotti ordinari con pesi possono ancora andare in underflow finito senza attivare il ripiego: limite individuato staticamente e lasciato esplicito per la prosecuzione, non una prova runtime o un ulteriore caso dichiarato corretto.

Un intervallo di ampiezza minima subnormale con semiampiezza arrotondata a zero è rifiutato conservativamente, anche quando l’integrale matematico sarebbe rappresentabile. Non è stato introdotto un limite artificiale di scala. Il ritorno legacy a profondità 30 conserva il significato di approssimazione, non di raggiungimento garantito della tolleranza. Restano aperti i limiti globali K10 su fit, carrier, Newton e topologia finale.

Nessun avvio o test UI, suite generale da 483 test, nuova CI, commit o push. Eseguibile su disco aggiornato solo dalla build completa; eventuali processi utente già aperti restano intatti.

## Tracciabilità

Base HEAD/origin/main `47c7c19c4faf490995e8ff64f10d83d8a6abb077`, indice vuoto. Agenti Sol separati per proposta, test e revisione statica; compilazioni, runtime e integrazione eseguiti dall’agente principale in serie. Consultati descrizioni e collegamenti call/include/reference prima delle modifiche; verificati nei due versi dopo la rigenerazione.

[Pacchetto delle prove](review-k10-quadratura-prove-2026-11-17/SHA256SUMS): sorgenti congelati, comandi/log/tempi, oracoli Python, consultazione della mappa, confronti ordinari, test e verifiche di preservazione. I file sorgente/script archiviati sono resi inerti; SHA256SUMS elenca i percorsi effettivi del pacchetto. Conservati e ricontrollati 38 pacchetti precedenti, 5367 artefatti. L’archivio della [memoria prima dell’intervento](history/claude-prima-k10-quadratura-2026-11-17.md) conserva integralmente lo stato iniziale.

La prima candidata è stata scartata come soluzione autonoma dopo che il controllo del chiamante massa ha rivelato la sentinella infinita intenzionale. La prima build è stata interrotta dall’agente principale (SIGINT sul proprio supervisore, exit 130) per integrare l’esclusione esplicita. Gli esiti di staging V1 non sono usati come certificazione della massa o dell’app finale; log e rettifica dell’audit sono conservati. La V2, con test finali anche sulla propagazione ricorsiva, è quella compilata e verificata sopra.
