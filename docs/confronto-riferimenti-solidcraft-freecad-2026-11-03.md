# Riferimenti degli schizzi: confronto SolidCraft, FreeCAD e ForgeCAD

Analisi del 3 novembre 2026, richiesta dall'utente prima di scegliere un cambiamento architetturale.
È un esame diretto dei sorgenti e dei test presenti, non un benchmark o una ricertificazione dei due programmi.
Non sono state applicate migrazioni dei riferimenti di ForgeCAD in questo intervento.

## Versioni e metodo

- SolidCraft: `138e3478e66d9e5b9297a6e3b5431301fa567fbd`, main letto dal repository ufficiale il 3 novembre,
  checkout di studio `/tmp/solidcraft-sketch-review`.
- FreeCAD: release stabile **1.1.4**, `4fd3bf320d9566a27e60069fc8387448aaa3a094`, pubblicata il 28 settembre 2026.
  Sorgenti ufficiali mirati in `/tmp/freecad-reference-review`; main rilevato come
  `80df03cf5ba501b3a9b80a7b1cc5e0406aa67bc3`, ma non equiparato alla versione stabile analizzata.
- ForgeCAD: base `4fafbaa`; esaminati tipi, risolutore, proiezione/refresh, genealogia e pulizia delle cache.
  I fix degli schizzi di quel commit erano già compilati e verificati prima di questa indagine.

Le conclusioni derivano dal codice indicato sotto. La presenza di un test esterno è distinta
qui dalla sua esecuzione: non sono state compilate o eseguite le suite Rust/FreeCAD.

## SolidCraft: identità, geometria collegata e solver

`SPoint`, `Curve` e `Constraint` hanno ID locali persistenti. `Sketch::fresh` usa contatori
che evitano il riuso ordinario degli ID dopo la cancellazione. I riferimenti verso altri schizzi
contengono l'ID dello schizzo e l'ID della curva o del punto.
**Le formule interne dei vincoli usano comunque indici nei vettori**: avere ID stabili non
significa eliminare gli indici da ogni calcolo. La cancellazione passa da `remove_curves`,
che rimappa i vincoli e ripulisce punti orfani, collegamenti e pattern.
Fonti: [model.rs, identità e dati](https://github.com/tangency-labs/solidcraft/blob/138e3478e66d9e5b9297a6e3b5431301fa567fbd/crates/sketch/src/model.rs#L677),
[cancellazione e rimappatura](https://github.com/tangency-labs/solidcraft/blob/138e3478e66d9e5b9297a6e3b5431301fa567fbd/crates/sketch/src/model.rs#L1661).

Una `Link` conserva la sorgente e le entità locali generate; punti collegati sono costanti per il solver,
mentre la valutazione del documento risolve nuovamente la geometria esterna. `LinkSource` distingue
bordi, facce, vertici, schizzi, assi, piani e altri casi; per bordi/facce la Link può conservare un nome persistente.
Il nome mancante non autorizza a prendere un bordo vicino. I percorsi legacy senza nome possono invece
usare una ricerca geometrica e segnalare il riaggancio.
Fonti: [link.rs](https://github.com/tangency-labs/solidcraft/blob/138e3478e66d9e5b9297a6e3b5431301fa567fbd/crates/sketch/src/link.rs#L1),
[refresh del documento](https://github.com/tangency-labs/solidcraft/blob/138e3478e66d9e5b9297a6e3b5431301fa567fbd/crates/doc/src/project.rs#L1020).

`update_link` conserva le identità se può riconoscere il layout; alcuni percorsi riordinano
contorni compatibili, altri rifiutano ambiguità conservando l'ultima geometria. Un cambiamento di layout
può ricreare la proiezione e rimuovere vincoli, con avviso. I nomi topologici seguono la genealogia
quando disponibile; esistono anche percorsi geometrici non verificati. Quindi gli ID da soli non risolvono
ogni cambiamento topologico.
Fonti: [aggiornamento Link](https://github.com/tangency-labs/solidcraft/blob/138e3478e66d9e5b9297a6e3b5431301fa567fbd/crates/sketch/src/link.rs#L774),
[test identità e ambiguità](https://github.com/tangency-labs/solidcraft/blob/138e3478e66d9e5b9297a6e3b5431301fa567fbd/crates/sketch/src/link_identity_tests.rs),
[naming](https://github.com/tangency-labs/solidcraft/blob/138e3478e66d9e5b9297a6e3b5431301fa567fbd/crates/doc/src/naming.rs#L1).

## FreeCAD: tre livelli distinti

Lo SketchObject distingue `ExternalGeometry`, collegamenti agli oggetti e ai sottoelementi,
da `ExternalGeo`, geometria derivata dello schizzo. Mappe tra riferimento, ID geometrico persistente
ed indice corrente consentono di conservare identità durante il refresh.
Nel solver gli indici locali restano presenti: positivi per geometria interna e negativi per assi e geometria esterna.
Gli assi H/V sono inizializzati come riferimenti propri. La geometria esterna entra nel solver come geometria fissa.
Fonti: [SketchObject.h, dati](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/SketchObject.h#L91),
[mappe ID/riferimenti](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/SketchObject.h#L1185),
[Sketch.cpp, preparazione del solver](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/Sketch.cpp#L185).

L'appoggio dello schizzo è un altro livello: `AttachExtension` conserva `AttachmentSupport`, `MapMode`
e offset; calcola il posizionamento dal supporto. Un piano usato come appoggio non è la stessa relazione
che proiettare la sua traccia per vincolare una linea. Il refresh esterno gestisce esplicitamente
Datum, piani, linee/assi, punti e forme Part; risolve proiezione o intersezione nel frame locale dello schizzo.
Fonti: [AttachExtension](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Part/App/AttachExtension.cpp#L338),
[risoluzione dei tipi esterni](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/SketchObject.cpp#L9260).

`ExternalGeometryExtension` distingue stato associativo e funzione della geometria: definente,
congelata, staccata, mancante o da sincronizzare. Il refresh segnala i riferimenti mancanti e può
conservare la geometria precedente; una cache conservata non implica che il riferimento sia valido.
La cancellazione elimina i vincoli che dipendono dalle entità rimosse e rimappa gli altri.
Anche i vincoli hanno un tag UUID, usato per seguire rinomine/rimozioni delle espressioni.
Fonti: [stati esterni](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/ExternalGeometryExtension.h#L89),
[delete](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/SketchObject.cpp#L7641),
[refresh](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/SketchObject.cpp#L9452),
[tag dei vincoli](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/PropertyConstraintList.cpp#L138).

## FreeCAD: oggetti, sottoelementi e dipendenze

`PropertyLinkSub`/`PropertyLinkSubList` conservano oggetto e percorso ai sottoelementi. Il sistema
aggiorna i nomi degli elementi, partecipa alle dipendenze del documento e al ripristino.
`TopoShape::makeShapeWithElementMap` costruisce nomi attraverso la storia delle forme generate/modificate:
un semplice numero di bordo non basta per seguire la topologia.
Questo è un principio già presente nella nostra genealogia `fk_history`/`cad_element_names`, da conservare.
Fonti: [PropertyLinks](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/App/PropertyLinks.h#L111),
[ElementMap e storia](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Part/App/TopoShape.h#L1846).

In 1.1.4 `isExternalAllowed` verifica compatibilità col DAG e limiti di documento, Body e Part.
I riferimenti diretti fuori ambito richiedono un passaggio esplicito. `SubShapeBinder` usa `PropertyXLinkSubList`,
contesto e trasformazioni relative, con modalità sincronizzata, congelata e staccata.
Questo intermediario è utile per collegamenti tra parti/documenti, non necessario per ogni vincolo locale.
Fonti: [ambiti e cicli](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/Sketcher/App/SketchObject.cpp#L4207),
[SubShapeBinder](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/PartDesign/App/ShapeBinder.h#L103),
[modalità](https://github.com/FreeCAD/FreeCAD/blob/4fd3bf320d9566a27e60069fc8387448aaa3a094/src/Mod/PartDesign/App/ShapeBinder.cpp#L386).

Non si deduce che FreeCAD risolva universalmente il problema dei nomi topologici. Per esempio, i prodotti
multipli di una proiezione hanno anche un ordinamento interno; esistono mappe e ricostruzioni con euristiche.
Il codice geometrico FreeCAD dipende da OCCT: si studia il contratto architetturale, senza introdurre
OCCT nel kernel di produzione ForgeCAD.

## Confronto con ForgeCAD

| Aspetto | SolidCraft | FreeCAD 1.1.4 | ForgeCAD attuale |
| --- | --- | --- | --- |
| Geometria esterna nel solver | Geometria locale collegata e fissa | ExternalGeo separata e fissa | Cache collegate, fisse per il solver |
| Identità di entità/quote | ID testuali; indici nei vincoli | ID/tag, mappe e indici nei vincoli | ID proprietario persistente; molte entità/quote locali per indice |
| Storage esterno | Entità nei vettori dello schizzo con Link | Lista esterna distinta | Entità nei vettori ordinari con SketchLinkedReference |
| Sorgente 3D | Nome topologico, con percorsi legacy geometrici | LinkSub ed ElementMap | GeometryRef, nome e genealogia propria |
| Piano dello schizzo | Collegamento del documento | AttachmentSupport e MapMode | support/datumPlane/frame, già distinti dalla proiezione |
| Cancellazione | Rimappatura centralizzata; pulizia orfani | Rimappatura e rimozione vincoli dipendenti | removeSketchEntities più visitatori/rimappature; residua responsabilità distribuita |
| Riferimento mancante | Errore/lost e ultima geometria | Missing e ultima geometria | error/referenceError; reset della curva implicita non più risolvibile |

Fonti locali: `cad_types.h`, `cad_sketch_refs.cpp`, `cad_constraints.cpp`, `cad_sketch_edit.cpp`,
`cad_sketch_reference_usage.cpp`, `cad_geometry_refs.h`, `cad_topology_ref.cpp`, `cad_element_names.cpp`.
Il refresh corrente controlla quantità e tipo dei prodotti; identità distinte per ogni prodotto sarebbero
più robuste contro riordinamenti a parità di quantità. È un rischio individuato dal codice,
non una causa già dimostrata di tutti i comportamenti segnalati dall'utente.

## Proposta dopo entrambi i confronti

Il modello associativo di base è adeguato: entrambi i programmi usano geometria esterna derivata,
risolta dal modello e resa costante al solver. Il punto da rafforzare è il contratto di identità
tra documento, geometria derivata e consumatori.

1. Introdurre ID persistenti per entità di schizzo e vincoli. Conservare gli indici come dettaglio
   dell'adattatore del solver, con risoluzione ID→indice prima del calcolo. Migrare i file esistenti
   senza ricreare identità a ogni apertura, preservando Undo/Redo e riferimenti a valle.
2. Separare selezioni temporanee, collegamenti espliciti e riferimenti impliciti realmente usati.
   Visibilità, costruzione e durata del riferimento devono restare proprietà distinte.
   Una conversione esplicita può essere utile anche senza vincoli: non eliminarla come orfana.
3. Studiare un archivio di geometria esterna separato, sul principio di ExternalGeo, senza duplicare
   il grafo di dipendenze e i resolver esistenti. I vincoli dovrebbero indirizzare un'entità locale
   o esterna mediante un riferimento tipizzato stabile; il solver può continuare a ricevere vettori compatti.
4. Dare identità ai singoli prodotti di proiezione/intersezione mediante provenienza e ramo.
   Riordino innocuo: identità conservata. Split/fusione o ambiguità: stato esplicito e richiesta di riparazione;
   evitare il riaggancio automatico per sola vicinanza.
5. Concentrare mutazioni, controllo delle dipendenze, cleanup e commit Undo in un percorso condiviso.
   Riutilizzare i visitatori e i resolver attuali. Introdurre un intermediario tipo Binder soltanto
   quando servirà un confine esplicito tra parti/documenti o un riferimento congelato intenzionale.

Questa è una proposta, non una migrazione già applicata. Prima dell'integrazione servono casi di accettazione
per cancellazione/inserimento prima di un'entità referenziata, riordino dei prodotti, modifica del piano,
riferimento scomparso/ambiguo, recupero dopo Undo, save/reload, proiezioni 3D e cicli vietati.
L'obiettivo non è sostituire solver o kernel, ma rendere persistente l'identità senza dipendere
posizionalmente da ogni operazione sui vettori.
