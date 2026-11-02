# Unione fino al corpo e riferimenti alla curva proiettata

Correzioni del 2 novembre 2026, successive al primo rilascio `317b8cb`.
Richiesta: l'estrusione fino al corpo deve poter fondersi con il corpo raggiunto;
la curva da due schizzi deve essere utilizzabile come riferimento negli altri schizzi.

## Uso

Riaprire l'app per caricare il nuovo eseguibile. Per il documento già salvato,
aprire **Modifica** su Estrusione 18 e confermare **Fino al prossimo corpo**, verso
**Invertito**, operazione **Unione**. Sono supportati ricerca automatica e
selezione manuale del destinatario. La conferma forza il ricalcolo delle vecchie
unioni automatiche che avevano salvato il solo prisma senza destinatari.
La sola apertura del documento può mostrare ancora la vecchia cache.

Per la curva proiettata, aprire lo schizzo destinazione e usare **Converti spigoli**
sulla componente desiderata. Il riferimento è associativo anche per la curva
implicita esatta. Snap, quote e **Punto sull'entità** usano la stessa geometria;
**Pierce** usa l'attraversamento esatto del piano dello schizzo. La curva deve
attraversare il piano per definire un Pierce isolato. Una curva complanare non
definisce un punto Pierce univoco.

## Unione: causa e correzione

La selezione automatica dei corpi provava l'unione ordinaria e, dopo un'eccezione
nell'intersezione delle superfici, trattava il corpo come estraneo: il comando
riusciva producendo soltanto il prisma. La selezione manuale mostrava lo stesso
fallimento del kernel. Le superfici estese al di fuori dei trim provocavano
una ricerca dei semi troppo profonda pur essendo già disponibili le superfici
di contatto generate dalla differenza.

Il percorso nuovo usa `uniteDisjointBodies` soltanto quando la costruzione corrente
dell'estrusione ha già provato che il suo interno è disgiunto da quello dei
medesimi limiti B-rep. La prova è locale, legata agli stessi oggetti geometrici,
e non viene ricostruita per sola appartenenza a `booleanTools` o da una cache.
I corpi nella fascia positiva fino alla risoluzione lineare non vengono attestati
come disgiunti. Il filtraggio delle shell iniziali resta subordinato al controllo
delle cavità; un sottoinsieme del risultato di una differenza conserva la prova.
Destinatari eventualmente sovrapposti vengono prima aggregati con la booleana generale.

Il kernel divide i contatti di area sui carrier coincidenti, quindi usa
classificazione, assemblaggio, controllo B-rep e genealogia ordinari. Non è un
fallback per unioni arbitrarie fallite, né un verificatore generale di disgiunzione.
La guardia di classificazione rileva alcune violazioni, senza sostituire la
precondizione. La booleana generale conserva il percorso completo per gli
operandi sovrapposti o privi di prova. Le SP-curve si riusano solo con identica
mappatura UV provata dai dati esatti; ogni curva conserva le associazioni a tutte
le coppie di superfici e i budget originari. Nessun campionamento o aumento
delle tolleranze definisce la geometria.

Il grafo della storia mostra distintamente il ruolo di limite e quello di
destinatario della fusione; soltanto il destinatario viene consumato.

## Riferimenti: causa e correzione

La conversione di una curva implicita in NURBS finita era correttamente rifiutata,
ma il rifiuto impediva anche il riferimento associativo. Ora una curva esatta 2D,
immagine affine della sorgente 3D, alimenta solver, quote, vincoli e display.
Il riferimento persistito permette di ricostruire questa cache dopo il caricamento,
anche senza cache B-rep. Gli estremi sono punti reali della curva, mai poli di una
spline di ripiego. Il marcatore procedurale è accettato dal decoder solo in
presenza di un collegamento associativo valido.

La QA nativa ha inoltre riprodotto la perdita di PointOnCurve al decode:
la migrazione legacy classificava come inesistente la curva procedurale prima
della ricostruzione della cache. La forma è ora classificata strutturalmente,
così entrambi i vincoli persistono; geometria e solver continuano a rifiutare
il riferimento finché la cache esatta non viene risolta. La regressione verifica
numero, tipi, riferimenti e valore di PointOnCurve e quota prima/dopo decode
e refresh, con e senza cache, anche con una cache nascosta interposta. Una
sorgente mancante conserva i vincoli ma lascia un errore esplicito.

Le componenti hanno chiavi ricavate dalle sorgenti e dai rami, senza dipendere
dall'ordine della lista. Se il ramo manca, oppure la sorgente è soppressa/in errore,
il collegamento diventa non risolto: non viene riassociato al ramo vicino né
conservata una curva valida obsoleta. La rigenerazione segue entrambi gli input,
comprese modifiche interne con estremi invariati. Il formato resta **60**; i nuovi
membri geometrici sono transitori. Versioni precedenti possono lasciare questi
nuovi riferimenti non risolti; non li devono interpretare come rette di ripiego.

Rimangono i limiti delle curve implicite per sweep, conversione libera in NURBS
ed esportazione STEP/IGES. Le NURBS prodotte esattamente conservano gli usi
compatibili. Le modifiche topologiche che eliminano una chiave richiedono una
nuova scelta esplicita della sorgente.

## Modello utente e verifiche

È stata congelata una copia dell'originale corrente `Superfici.prt`, 16.074.185 byte,
SHA256 `56779a6c668258e54d686a9f1e721de03acbea84a2c16d642e1083d364203adf`.
La diversa copia storica `superifici.prt` resta distinta. Nessuna sonda scrive
alcuno dei due originali.

Sulla copia corrente, Schizzo3 fino a Corpo1 produce un prisma di
2347,969022611748 mm³. Unione automatica e manuale producono un unico solido
con **23 facce, una shell e volume 23653,78484397822 mm³**; target16 persistito.
La somma delle due integrazioni separate è 23653,784841942052 mm³:
lo scarto di circa 2,04e-6 mm³ deriva dalla diversa integrazione delle facce
suddivise, non da tolleranze aumentate. La prova kernel ripetuta dopo codec
separati dei due corpi riesce in entrambi gli ordini.

La suite dedicata al contatto comprende superfici B-spline non piane,
contatti parziali e su due facce, cavità conservate, contatti solo lungo
spigoli/vertici come componenti separate, genealogia, entrambi gli ordini
e unione generale di solidi sovrapposti. Le prove dei riferimenti comprendono
piani inclinati, identità dei rami, quote, PointOnCurve, Pierce, codec,
sorgenti mancanti e modifiche interne degli input.

Build Release completa di app e runner: **PASS**, Linux GCC16.2.1 / Qt6.12.
I manifesti registrano gli hash dei 35 sorgenti/test e dei binari effettivi.
La prima integrazione del runner richiedeva l'include esplicito di
`cad_projected_curve.h`: corretto e ricompilato prima dei test comuni.

- Sette nuove regressioni CTest: **7/7 PASS**, anche dopo il delta codec.
  Comprendono quattro percorsi applicativi e tre suite kernel: 39 casi dei
  riferimenti esatti, 4 gruppi della parametrizzazione e 7 dell'unione disgiunta.
- Suite completa prima dell'ultimo delta codec: **161 PASS, 1 SKIP, 1 FAIL su163**,
  566,53s. Dopo il delta sono stati ripetuti **tutti i151 test applicativi**:
  **150 PASS, 1 SKIP, zero FAIL**,142,39s. I12 target kernel della prima fase
  restano riferiti alla stessa libreria, invariata byte per byte.
  Il consolidato finale è161 PASS/1 SKIP/1 FAIL; non è una seconda singola
  esecuzione completa dei163 test dopo il codec.
- Unico FAIL: quello preesistente `DraftCurvedAndConcaveSolids`,
  `kernel/tests/test_draft.cpp:134`:253,5270716475676 contro253,5270734420086,
  scarto1,7944410046766279e-6>1e-6, identico al checkpoint precedente.
  Kernel generale **457/458**; test e tolleranza invariati.
- Skip `forgecad_overlay_uniform_tests` per il contesto offscreen. La verifica
  GL avviene separatamente nelle sessioni native.
- QA unione: **12/12 PASS**, quattro casi creazione/modifica × auto/manuale
  su offscreen, Wayland e XCB;8 casi hanno contesto GL nativo.
  Anteprima, conferma, volume, singolo solido, consumo del destinatario,
  genealogia, Undo/Redo e codec verificati.
- QA riferimenti: **2/2 sessioni native PASS**, Wayland/Intel P630 Mesa e
  XCB/Quadro RTX3000 NVIDIA. Converti spigoli, cache nascosta/rendering,
  snap, PointOnCurve e quota, modifiche interne di entrambi gli input,
  riordino dei rami, Undo/Redo, Pierce su X=1, decode e riapertura con/senza
  cache. Entrambi i vincoli persistono; errore massimo dell'immagine affine
  su21 parametri:0. Catture ispezionate, nessun errore GL. Nessuna prova macOS.
- ASan/UBSan mirati PASS: unione7, parametrizzazione4, riferimenti39 e modulo,
  più regressione finale codec. I rapporti elencano esattamente i TU
  strumentati e i supporti non strumentati: nessuna build sanitizer dell'intera
  app attestata. LeakSanitizer escluso per incompatibilità ptrace; nessuna
  verifica delle perdite di memoria dichiarata.
- Originali realmente riletti a fine prove: entrambi i Superfici e i tre
  esempi CTest corrispondono ai rispettivi snapshot iniziali di questo
  intervento; cinque copie congelate intatte. Le sole modifiche ai manifesti
  CTest copiati riguardano `FORGECAD_EXAMPLE_DIR`, con proprietà e timeout conservati.

Mappa architetturale finale: **548 file,7571 relazioni,258 unità AST,zero errori**.
`--check` PASS; verificati8 confini di chiamata e, separatamente, entrate/uscite
call/include/reference dei moduli interessati. HTML reso e ispezionato su
`cad_extrude.cpp` e `cad_sketch_refs.cpp`. HTML e JSON generati restano locali
e ignorati da Git.

[Archivio delle evidenze](history/estrusione-unione-riferimenti-2026-11-02-verifica.zip):
log/JUnit distinti per fase, manifesti e hash, sonde e rapporti degli agenti,
confronto causale codec FAIL→PASS, sanitizer mirati, catture GUI e mappa,
verifiche degli originali. Prototipi, diagnosi preliminari e risultati finali
sono distinti; non contiene CAD originali o binari.
