# K10: controllo continuo sui carrier NURBS istanziati — 17 novembre 2026

Corretto il bypass del controllo continuo quando il carrier è una `BSplineSurface` istanziata, come quelle create da pose e ripetizioni. Il caso con picco fuori budget torna a essere rifiutato anche mantenendo una base condivisa. **Budget di accettazione invariati; K10 globale resta parziale e il totale dei rilievi funzionali rimane 33. Nessun commit o push.**

Sono stati eseguiti soltanto test kernel pertinenti. Nessun test o avvio UI, nessuna ripetizione della suite generale 483, nessuna nuova CI. App ricompilata e precedenti modifiche locali conservate.

## Problema e correzione

Il provider e la costruzione Q escludevano ogni S con `basis()!=nullptr`, riportando `Unsupported`. Il mapping tornava quindi ai campioni anche per una semplice istanza identità di una NURBS già supportata. Nel riproduttore interno esistente `mapBodyByFunction`, un carrier bicubico con picco U∈[0,1003;0,1013] veniva accettato: `checkBody` senza errori, finTolerance=1e-14 mm, ma residuo effettivo circa 0,00075 mm contro il cap prefit 8e-8 mm. È un test interno sintetico, non una nuova riproduzione su un CAD utente. Fraction conferma il testimone e il contenimento di Q nel dominio.

Lo snapshot legge ora i poli della **base nativa originale** e le **matrici/traslazioni IEEE effettivamente memorizzate** nell’istanza, con intervalli arrotondati verso l’esterno. Applica i placement dall’interno verso l’esterno e centra l’ultimo passaggio come `M·P+(t−origine)` prima delle somme. Per S razionale si conserva W e si costruisce il numeratore centrato equivalente `M·H+(t−origine)·W`. Non si ricostruiscono poli world con `s->pole()` già arrotondati, non si aggiunge una composizione double, non si usa il fattore approssimato di similitudine come norma certificata e non serve un’inversa.

Il costruttore pubblico ammette similitudini finite non singolari, incluse riflessioni, e collassa già le trasformazioni successive su una base nativa con placement composto. La prova riguarda questi coefficienti memorizzati, senza pretendere di recuperare la storia matematica precedente agli arrotondamenti del costruttore. Un percorso limitato a 32 collegamenti, cancellabile e con verifica dei valori finiti, precede gli accessori ricorsivi di nodi/pesi/poli. Esaurimento o dati inconclusivi nella famiglia riconosciuta danno `Failed`, mai un recupero campionato. Il ramo oltre 32 link non è esercitabile con il normale costruttore canonico e non è stato testato forzando oggetti invalidi.

La costruzione privata della candidata Q usa il dominio della base prima dell’unica prova iniziale. E/S/range/cap restano conservati; Q viene assegnata solo dopo certificazione. Il registro finale del mapping, già introdotto nel passo precedente, ricontrolla le vere fin dopo assemblaggio anche con history utente nulla. Il codice del mapping e del ledger non cambia in questo intervento.

Un secondo test impedisce una correzione apparentemente semplice ma errata: entro il campo nominale, una traslazione X=2^18 fa perdere δ=2^-36 nei poli restituiti in double. La superficie originale conserva però uno scarto δ, pari a 16 volte il cap esplicito 2^-40. Fraction prova il testimone; il nuovo snapshot rifiuta l’originale e distingue il diverso carrier ottenuto appiattendo i poli arrotondati. Non è descritto come un falso certificato della precedente produzione: è un controesempio alla tecnica di estrazione da evitare.

## Verifiche

| Verifica | Esito e perimetro |
| --- | --- |
| Build app e sei target kernel | PASS, circa 31,87 s; app finale SHA256 `1ce44fceec73cbda6c911cb6d35d2305eced931dd025f7bf5ec68b860a798720`. |
| CTest pertinenti | 6/6 PASS, zero skip, circa 48,75 s. |
| Nuove regressioni | 6 casi: pose composte/simmetrie/scale, perdita da poli arrotondati, Quilt razionale pubblico con history nulla/codec, picco nascosto con fallimento atomico, esclusioni E/Q, cancellazione. |
| Driver Release | 46/46 PASS, circa 0,39 s: sei nuovi casi più i 40 provider/ledger/mapping precedenti. |
| ASan/UBSan/LSan | Stessi 46/46 PASS, circa 4,77 s; una TU produzione, cinque TU test e main strumentati, resto kernel Release. LSan calibrato fuori ptrace con perdita reale di 19 byte ed exit 23. Nessuna certificazione dell’intera app. |
| API e audit Fraction indipendente sulla libreria finale | 4/4 fin del mapping pubblico PASS; massimo limite continuo 8,5168271986e-11 mm contro 8e-8 mm. |
| Mappa e conservazione | PASS: 788 nodi, 11557 relazioni, 398 TU; hash sorgenti/configurazione/HTML, chiamate e dipendenze in entrambi i versi verificati. 28 pacchetti di prove precedenti conservati integralmente. |

Le sei suite sono `forgekernel_surface_curve_bounds_tests`, `forgekernel_mapping_nonplane_validation_tests`, `forgekernel_surface_mapping_tests`, `forgekernel_sheet_join_validation_tests`, `forgekernel_surface_instances_tests` e `forgekernel_tolerance_tests`. Il numero dei casi del driver e quello delle suite non vanno sommati: sono livelli di verifica sovrapposti.

La nuova TU finale contiene sei casi. Un primo tentativo con un settimo caso ridondante del costruttore è conservato separatamente: baseline 3/7 e candidato 47/47. Eliminato quel solo controllo già presente nella suite esistente delle istanze, la baseline finale è 2/6 e il candidato passa tutti i sei nuovi casi e i 40 precedenti. La sola aspettativa preesistente aggiornata è S istanza identità, da `Unsupported` a `Certified`: pesi variabili e wrapper E/Q mantengono le precedenti aspettative di esclusione.

L’auditor collega la libreria root finale `57c11033779f33893b07d166c682a0c5e8e3d24c14c98abfee43c7253ca62fa7`, verificandone l’hash prima e dopo il link. Il caso pubblico trasforma il target NURBS razionale 2/2 con posa e scala, poi usa `SurfaceQuiltMap::bend`. Le quattro fin finali conservano il carrier istanziato originale, la genealogia dichiarata, range/sense e input/target immutati. Il risultato con history nulla è byte-identico; la rilettura del codec è coperta dal nuovo test. Fraction usa coefficienti della base e placement originali esportati dalle vere fin, senza chiamare il provider di produzione.

E/S/Q/range sono identici fra baseline, staging e risultato finale. **Il metadato finTolerance cambia**: dichiara ora il cap prefit 8e-8 mm, anziché la precedente stima campionata di circa 5e-11–7e-11 mm. Non è un aumento del cap di accettazione; edgeTolerance resta zero. Non si dichiara il corpo serializzato identico alla baseline.

## Limiti

Sono aggiunte le istanze condivise di S NURBS nonperiodiche di grado 2/3 per direzione, con pesi finiti positivi. E resta BSpline cubica nativa; Q resta Line/BSpline grado 1/3 con pesi assenti o costanti positivi. Pesi variabili E/Q, relativi wrapper, superfici periodiche, altri gradi e famiglie restano `Unsupported`.

Il certificato riguarda il residuo posizionale E−S(Q) delle fin. Non prova la conservazione globale del carrier rispetto alla mappa richiesta, il ramo Newton, trim/topologia, iniettività o assenza di autointersezioni. I limiti di lavoro già esistenti del provider restano invariati e possono rifiutare conservativamente geometrie valide; non è una deadline temporale rigida. Nessuna modifica al costruttore o al kernel delle trasformazioni.

Implementazione root, test e audit completati con agenti GPT-6.1 Sol; il ramo di lettura preliminare ha fornito indicazioni ma si è arrestato per capacità del modello. Nessun CAD utente aperto o modificato. [Prove riproducibili, log, coefficienti e manifest](review-k10-carrier-istanze-prove-2026-11-17/README.txt). La CI storica non certifica queste modifiche non committate.
