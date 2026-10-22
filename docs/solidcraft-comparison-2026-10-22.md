# Confronto SolidCraft / ForgeCAD — 22 ottobre 2026

## Ambito e metodo

Analisi statica diretta dei sorgenti: ForgeCAD commit `46fd5951114fe304d864b13dedf060e38d7d8334`; SolidCraft commit `3d3dd27439995f37eaed1feaa1c53a0e42d2fe39`, scaricato dal repository pubblico in `/tmp/solidcraft-comparison`. Il checkout ForgeCAD effettivamente disponibile è `/home/myungit/Linux_CAD_forFree`, mentre il contesto iniziale indicava un percorso macOS inesistente su questo host.

Non sono stati compilati o avviati i due programmi, eseguiti benchmark o confrontati risultati geometrici su modelli comuni. I comportamenti descritti sono dedotti dal codice; velocità, stabilità visiva e accuratezza relativa restano da misurare. Nessuna modifica funzionale, build, commit o push. La modifica preesistente a `compile_commands.json` è preservata.

Conclusione: SolidCraft offre soluzioni utili per navigazione e dettaglio visivo adattivo; questo non dimostra una superiorità numerica generale del kernel. ForgeCAD ha già anteprime asincrone evolute e proprietà di massa integrate sul B-rep.

## Zoom e qualità visiva

| Aspetto | ForgeCAD | SolidCraft |
| --- | --- | --- |
| Legge della rotella | `zoom *= pow(0.9985, delta)` | `factor = exp(-scroll * 0.0015 * sign)` |
| Punto fermo durante zoom | Centro della vista; nessuna compensazione pan nel gestore della rotella | Punto sulla faccia sotto il cursore; in assenza di faccia, piano alla profondità del target |
| Camera | Ortografica nel percorso esaminato; matrici e stato prevalentemente float | Ortografica e prospettica; camera e matrici CPU f64, conversione f32 per il rendering |
| Limiti | Minimo fit/5000 e massimo 4×fit, dipendenti dagli ingombri della scena | Fattore del singolo evento limitato a 0,05–20; distanza 1e-3–1e7 |
| Input | angleDelta, altrimenti pixelDelta; applicazione immediata | Distinzione rotella/trackpad/pinch, smoothing configurabile e navigazione condivisa tra canvas |
| Mesh dei corpi sotto zoom | Livello scelto e dimensione del corpo; nessun raffinamento da zoom nel percorso esaminato | Errore in pixel, raffinamento asincrono, isteresi e budget |

La legge esponenziale è sostanzialmente simile: `ln(0.9985) ≈ -0.0015011`. Gli eventi Qt ed egui hanno normalizzazioni diverse, quindi la somiglianza dei coefficienti non prova uguale sensibilità percepita. La differenza più concreta è l'ancoraggio al cursore. In SolidCraft `target' = anchor + (target-anchor) * (nuovaDistanza/vecchiaDistanza)`; il rapporto effettivo dopo il clamp evita spostamenti errati ai limiti dello zoom.

SolidCraft raffina quando l'errore del livello corrente supera 1 pixel fisico, cercando 0,75 pixel; riduce il livello sotto 0,175 pixel. Massimo quattro passi, ciascuno dimezza lo scarto cordale. Sono previsti due lavori contemporanei, sei milioni di triangoli raffinati e cache di 768 MiB; corpi sotto 32 pixel non sono raffinati. Sono parametri di SolidCraft, non valori automaticamente adeguati per ForgeCAD. Facce, bordi ed evidenziazioni usano la stessa rappresentazione raffinata; B-rep e mesh di riferimento restano separati.

ForgeCAD usa scarto cordale pari alla diagonale × 0,004 / 0,001 / 0,0002 e soglia angolare 0,5 / 0,25 / 0,1 rad. Aumentando lo zoom, lo stesso scarto in millimetri occupa più pixel: questo spiega perché una superficie numericamente regolare possa apparire sfaccettata. Non implica un difetto della superficie del kernel.

Riferimenti ForgeCAD: `forgeCad2026_viewport_interaction.cpp:1260`, `forgeCad2026_viewport_scene.cpp:285`, `forgeCad2026_camera.cpp:14`, `forgeCad2026_viewport.cpp:71`, `cad_forge.cpp:1825`.

Fonti SolidCraft fissate al commit analizzato: [camera](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/render/src/camera.rs#L335), [navigazione](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/ui-egui/src/navigation.rs#L132), [raffinamento di display](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/ui-egui/src/display_lod.rs), [qualità e soglie](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/doc/src/display_quality.rs#L81).

## Calcoli numerici del kernel

ForgeCAD ha un kernel C++ proprio con curve analitiche/NURBS e topologia B-rep. Le intersezioni esaminate includono casi analitici, isolamento di radici in Bernstein, suddivisione di pezze di Bézier e Newton; per superfici generali esiste il marching. I predicati orient2d/orient3d hanno filtro numerico e recupero con espansioni di double, entro le condizioni dichiarate sugli input.

SolidCraft usa un confine Rust che integra Truck e copie modificate di truck-geometry, truck-shapeops e truck-meshalgo, oltre a molti algoritmi propri. Non è soltanto un frontend di Truck, e la descrizione «pure Rust» non significa che tutti gli algoritmi siano stati scritti da zero.

Le costanti ForgeCAD distinguono risoluzione lineare 1e-7 mm, angolare 1e-11 rad, connessione booleane 1e-6 mm e fit del marching 1e-9 mm. SolidCraft dichiara EPS=1e-9 mm e LINEAR_TOL=1e-6 mm, ma usa budget diversi nei singoli algoritmi. Confrontare il numero più piccolo non permette di classificare la precisione: risoluzione, coincidenza, errore di fit e scarto di tassellazione sono grandezze diverse. Anche la size box ForgeCAD di lato 1e6 mm è un campo nominale: l'helper di controllo non certifica automaticamente tutti i modelli.

La differenza verificata più rilevante riguarda le proprietà di massa. ForgeCAD usa divergenza, integrazione sulle superfici e sui loop UV e quadratura adattiva Gauss–Kronrod; tolleranza relativa richiesta predefinita 1e-12. È integrazione numerica della geometria B-rep, non aritmetica esatta e non garanzia universale di dodici cifre.

SolidCraft `measure_mass` parte da una tassellazione grossolana e ne produce una fine e una intermedia; confronta i contributi per faccia e seleziona un livello secondo accordo delle aree. La tolleranza fine combina dimensione, piccoli raggi e un minimo di 1e-4 mm. Volume e baricentro sono somme sui triangoli. È una misura approssimata del B-rep, anche se le coordinate dei triangoli sono f64. Per misure indipendenti dalla mesh conviene preservare l'impostazione di ForgeCAD; l'effettiva accuratezza su casi difficili va comunque verificata.

Le booleane generali SolidCraft verificano plausibilità volumetrica e prevedono percorsi specializzati, ritentativi con tolleranze differenti e traslazioni comuni degli operandi. Il codice trasla entrambi gli operandi e riporta il risultato nella posizione iniziale: non va descritto come uno spostamento relativo arbitrario del solo utensile. Sono strategie da studiare con diagnostica e budget, non da trasferire al nostro kernel senza verificarne gli errori geometrici e la genealogia topologica.

Il documento oracle di SolidCraft nel checkout riporta 83/83 casi riusciti, con 62 riferimenti analitici e 21 FreeCAD. È un risultato pubblicato dal progetto, non riprodotto in questa analisi; il README online letto durante l'indagine riportava un riepilogo precedente. I confronti oracle hanno soglie proprie, spesso relative 1e-3: non certificano precisione assoluta nanometrica.

Riferimenti ForgeCAD: `kernel/fk_precision.h`, `docs/tolerance-policy.md`, `kernel/fk_predicates.cpp:86`, `kernel/fk_intersect.h`, `kernel/fk_marching.h`, `kernel/fk_mass.h:42`, `kernel/fk_mass.cpp`, `kernel/fk_quadrature.h`.

Fonti SolidCraft: [dipendenze e patch](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/Cargo.toml), [costanti geometriche](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/geom/src/lib.rs#L48), [misure](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/kernel/src/measure.rs#L129), [booleane](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/kernel/src/ops.rs#L914), [oracle](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/docs/oracle.md).

## Calcolo delle anteprime delle feature

ForgeCAD possiede già snapshot immutabili, QThreadPool, generazioni per rifiutare risultati superati e cancellazione cooperativa. Primo lavoro immediato; richieste successive scandite da timer di 120 ms. Durante il trascinamento lascia terminare il lavoro corrente e conserva l'ultima richiesta, evitando di cancellare continuamente ogni fotogramma.

La cache tiene al massimo quattro anteprime, con un budget stimato di 64 MiB sui buffer di display. Non è un tetto di memoria dell'intero B-rep: la stima non conta tutta la geometria e ogni allocazione accessoria. Contesto invalidato al cambio di schizzi, corpi o qualità. Le estrusioni semplici dispongono già di deformazione della mesh precedente durante il drag. Le feature invariate riusano geometria e display esistenti.

`FeaturePreviewBuilder` normalmente costruisce il B-rep attraverso buildGeometry; per raccordi ed estrusioni fuse/sottratte può disegnare soltanto le patch modificate e le facce conservate. Alla conferma `takePreviewResult` riusa il B-rep dell'anteprima corrente, eventualmente completando la mesh finale. È un vantaggio rispetto a ricostruire sempre tutto al clic su OK.

SolidCraft usa chiavi di comandi/revisione/colori, sessioni scratch e un worker desktop. Conserva l'ultima geometria buona; alcuni risultati intermedi compatibili sono mostrati durante il movimento, mentre gli errori richiedono la chiave corrente. Nel ramo web esaminato il calcolo dell'anteprima avviene sincronicamente.

Per estrusioni rettilinee compatibili ha un percorso diretto dai triangoli già disponibili. Per molte feature new/join/cut/intersect il worker costruisce il solo utensile senza eseguire la booleana completa: questa differenza riduce il lavoro richiesto per feedback grafico, ma non conferma la riuscita della feature finale. Altri comandi usano l'anteprima completa. Esiste inoltre una tassellazione provvisoria B-rep con campionamento prefissato e controlli di complessità/budget prima dell'allocazione; è un percorso specifico, non una proprietà di ogni anteprima.

Riferimenti ForgeCAD: `forgeCad2026_viewport_preview.cpp:4`, `:28`, `:48`, `:91`, `:177`, `:255`, `:313`, `forgeCad2026_feature_preview.cpp:125`.

Fonti SolidCraft: [stato e worker](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/ui-egui/src/preview.rs#L196), [utensile senza booleana](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/ui-egui/src/preview.rs#L435), [estrusione diretta](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/ui-egui/src/extrude_preview.rs), [mesh provvisoria limitata](https://github.com/tangency-labs/solidcraft/blob/3d3dd27439995f37eaed1feaa1c53a0e42d2fe39/crates/kernel/src/native/preview.rs).

## Interventi proposti e verifica

1. Zoom al cursore nella vista 3D, compensando il pan con il rapporto effettivo dopo il clamp. Verificare punto proiettato invariato entro una frazione di pixel, viste ruotate, HiDPI e limiti min/max. Separare normalizzazione di rotella e trackpad.
2. Raffinamento visivo dipendente dai pixel: mantenere la mesh attuale durante il lavoro, raffinare solo i corpi visibili, aggiungere isteresi e budget misurati sulla macchina. Far coincidere facce, spigoli e highlight; isolare mesh grafica da B-rep, misure ed esportazioni.
3. Estendere il feedback rapido già presente: utensile o trasformazione grafica durante il drag per operazioni compatibili, risultato B-rep esatto al rilascio e alla conferma. Distinguere chiaramente lo stato provvisorio dalla validazione finale; conservare il riuso del risultato esatto corrente.
4. Misurare separatamente costruzione B-rep, booleana, tassellazione, trasferimento GPU e paint; contare cache hit, lavori superati/cancellati, picco di memoria e latenze p50/p95. Confrontare la stessa modalità: anteprima utensile contro utensile, risultato completo contro risultato completo.
5. Preparare un corpus comune con primitive, piccoli raccordi su pezzi grandi, quasi tangenze, NURBS rifilate, coordinate lontane dall'origine e trascinamenti rapidi. Controllare scarto geometrico, volumi analitici, invarianti topologici e stabilità sotto traslazione/scala, oltre al solo successo dell'operazione.

Tutti questi interventi sono proposti, non implementati. Non è giustificata una sostituzione del kernel sulla base di questo confronto. La priorità suggerita è navigazione e rappresentazione grafica, preservando il calcolo B-rep e le misure indipendenti dalla mesh.


## Approfondimento successivo: unità della rotella e filtro egui

Dopo due tentativi di animazione della camera non risolutivi per l'utente,
letto anche il codice della dipendenza esatta egui0.36.2 indicata da Cargo.lock.
La somiglianza dei coefficienti esponenziali, evidenziata nello studio iniziale,
non implica sensibilità uguale: un evento desktop di una linea viene convertito
in40punti da InputOptions. Il percorso Qt usava direttamente120unità angolari
per lo scatto standard. Di conseguenza il nostro scatto valeva circa16,48% di
avvicinamento, contro5,82% nel caso una-linea/40punti di SolidCraft.

WheelState accumula l'input e applica per frame una frazione
`1-pow(0.1,dt/0.1)` del residuo, completando il90% in100ms; sotto1punto
consuma il residuo. InputState limita dt a100ms. Navigation applica soltanto
la quota del frame con `exp(-0.0015*quota)`. Non è una nuova interpolazione
con destinazione/durata fissa per ogni scatto, né impone uno zoom minimo.

Il nuovo percorso ForgeCAD normalizza Qt120->40 e applica lo stesso filtro
esponenziale al delta; mantiene lo scarto della vecchia coda in inversione,
scelta diversa dal semplice accumulo firmato di egui. Dettagli e verifiche in
[viewport-performance.md](viewport-performance.md). La percezione con la
periferica fisica dell'utente resta da confermare: non è corretto dedurla dal
solo numero di fotogrammi o dalla lettura dei sorgenti.

Fonti: [egui WheelState0.36.2](https://github.com/emilk/egui/blob/0.36.2/crates/egui/src/input_state/wheel_state.rs),
[InputOptions0.36.2](https://github.com/emilk/egui/blob/0.36.2/crates/egui/src/input_state/mod.rs#L100),
[emath exponential_smooth_factor](https://github.com/emilk/egui/blob/0.36.2/crates/emath/src/lib.rs).


## Taratura ulteriore secondo il riscontro dell'utente

L'utente, che confronta l'app desktop, continua a percepire un passo più ampio
e un movimento rapido all'inizio che frena in arrivo. Verificata l'installazione
aperta0.19.0 e il tagv0.19.0; le preferenze salvate riportano smooth_wheel_zoom
e zoom_to_cursor attivi. Il codice della versione conferma il filtro
esponenziale, quindi non viene descritto come velocità uniforme.

La taratura ForgeCAD successiva usa metà del passo precedente (20punti per
120unitàQt, fattoreexp(-.03), circa2,96%) e avanzamento uniforme in log scala
su160ms. È una scelta esplicita per il riscontro dell'utente, non una replica
matematica del filtro egui. Aggiunto l'ancoraggio al cursore, che mancava in
ForgeCAD e cambia lo spostamento apparente del modello. Dettagli correnti e
limiti della verifica in [viewport-performance.md](viewport-performance.md).


## Riscontro decisivo sul percorso reale degli eventi

L'utente porta il tempo a3secondi, ricompila e continua a non vedere differenza.
La prova fisica nella finestra diagnostica registra in Qt un dispositivo
«touchpad» di tipoTouchPad, angleDelta=-120 e pixelDelta=-15. Il nostro
riconoscimento lo instradava direttamente, scavalcando ogni animazione:
la corrispondenza delle formule non aveva effetto su quel percorso.
Ora uno scatto angolare quantizzato con pixel grandi resta animato anche se
etichettatoTouchPad, con regressione sugli stessi valori fisici registrati.
I piccoli delta continui restano diretti. Dopo la correzione e il riavvio,
l'utente conferma: «Sì, ora i tre secondi si vedono».

Questo non è una registrazione degli eventi nativi di SolidCraft: dimostra
il bypass nel nostro CAD e la correzione sulla periferica dell'utente.
La durata3secondi è rimasta quella scelta manualmente dall'utente.
