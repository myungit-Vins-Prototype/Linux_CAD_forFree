# Sezioni loft proiettate ed estratte — 3 novembre 2026

Richiesta: usare curve proiettate ed estratte come sezioni della loft, eliminando il rifiuto «tipo di curva non gestito».

## Causa e correzione

Riprodotti entrambi gli errori su una copia congelata di `SurfaceModellingExample219.prt`:

- `superficie rigata: tipo di curva non gestito` scegliendo la proiezione implicita;
- `rationalBezierPieces: tipo di curva non gestito` scegliendo la sua estrazione limitata.

Il percorso delle sezioni del kernel richiedeva curve razionali. Una proiezione schizzo/schizzo può invece essere un'intersezione implicita non razionale; l'estrazione ne conserva correttamente il tipo esatto. L'interfaccia accettava già questi riferimenti: il rifiuto era durante la costruzione della superficie.

Il nuovo adattatore privato `kernel/fk_loft_sections.*` prepara una rappresentazione NURBS della **sola costruzione della loft**, prima dei controlli dei contorni e della corrispondenza delle sezioni. Viene usato da `loftSheet` e `loftSolid`. Curve razionali e sorgenti associative restano sul percorso precedente, senza sostituzione o modifica. Si gestiscono anche le immagini esatte richiamate dentro gli schizzi, oltre ai wrapper di estrazione, taglio e trasformazione.

La costruzione usa i jet valore/D1/D2 e i breakpoint esatti delle sorgenti con il modulo Hermite quintico esistente. Il budget, fissato prima del calcolo, è **1e-9 mm**, lo stesso già usato per la costruzione guidata. Il fit interno usa un ottavo del budget; un controllo successivo sui tratti NURBS assemblati valuta punti indipendenti da quelli del raffinamento. Uno scarto fuori budget fa fallire l'operazione con l'indice della sezione per la diagnostica esistente; non si allarga la tolleranza.

Il fit avviene nelle coordinate intrinseche della sorgente, prima delle trasformazioni. Per una similitudine si contabilizza il fattore di scala; per un'affinità generale si usa il limite conservativo di Frobenius, con verifica finale nelle coordinate del modello. Questo evita che una collocazione ruotata/traslata produca suddivisioni quasi coincidenti per rumore numerico. Ai corner si usano separatamente i jet destri e sinistri, compreso l'estremo finale di un taglio.

Il risultato è una superficie B-rep NURBS serializzabile con il codec esistente. Nessuna nuova enum persistita, nessun cambio di formato o cache. Non si dichiara identità algebrica fra una NURBS finita e qualsiasi intersezione implicita: sono immutate le curve sorgenti esatte, mentre la superficie derivata segue il budget di costruzione dichiarato. I controlli del fit sono adattivi e campionati, non una dimostrazione simbolica globale dell'errore.

## Riproduttore reale

Originale letto senza modificarlo: `/home/myungit/Documenti/Cad_Example/SurfaceModellingExample219.prt`, 28.232 byte, SHA-256 `e4d269cf0200c078da27e4b6ecdd34bc93acd7e4c30d3d5c7a3a62c56b2a55fd`.

La copia corrente contiene 11 schizzi e 10 funzioni, senza loft salvate. Sono state provate le coppie:

1. `Curva proiettata 1` (funzione id 2, prima componente) e `Curva estratta 3D 3` (id 7);
2. `Curva estratta 3D 5` implicita (id 9) e `Curva estratta 3D 3` (id 7).

Prima entrambe fallivano con gli errori sopra; con la correzione entrambe creano una lamina valida. Nei replay nativi finali Wayland e XCB: uno scarto massimo misurato di **9,58694e-11 mm** e **5,00918e-11 mm**, rispettivamente, su 72 punti per tratto confrontati con le superfici B-rep. `checkBody` senza errori. Le sorgenti restano implicite, con puntatori e 61 valutazioni identiche prima/dopo la creazione. Conferma dal pannello, Undo/Redo, salvataggio/lettura con e senza cache e ricostruzione completa riusciti.

Durante il lavoro il file originale e cambiato indipendentemente dalle prove. E stata congelata e controllata anche la revisione successiva: 27.675 byte, SHA-256 `944f6cbebef71ba2f3c092db3516d64640355d12a91df70f8686b248ef2f3867`, stessi id delle funzioni. Anche questa versione costruisce entrambe le coppie: il replay finale XCB usa questa copia, quello Wayland usa la prima. Nessun passaggio delle prove scrive il file originale.

## Verifiche consolidate

- Compilazione Release **completa** di app e runner, esito 0. Una precedente esecuzione della build si e interrotta con segnale 143 senza errore di compilazione; ripresa incrementale completata. Le prove finali e gli hash si riferiscono agli artefatti completati. Rimangono avvisi di compilazione preesistenti.
- **19/19 CTest pertinenti**, nessuno skip: 6 del kernel (forma loft, guide proiettate, nuove sezioni, proiezioni, riferimenti e storia) e 13 dell'app (nuove sezioni, bordi loft, tangenza, estrazione, proiezioni/riferimenti, strumenti di superficie, pannelli/storyboard, soppressione e cache/storia/posizione).
- La nuova suite kernel esegue **15 gruppi**: due/tre sezioni, rigato, estrazioni limitate e trasformate, affinità con scala non uniforme, immagini associative in schizzo, proiezione composta con corner, taglio che termina sul corner, ordine inverso, G1/G2, guide razionali, controllo diretto del budget e sorgenti immutate, razionali senza fit e intervallo invalido con identità della sezione. Per le costruzioni: `checkBody`, confronto indipendente con le sezioni e round-trip BodyIO. Massimo misurato nelle prove della superficie: **1,01058e-10 mm**.
- Nuova regressione app: proiezioni ed estrazioni implicite come sezioni; sorgenti non sostituite, aggiornamento dello schizzo a monte, rigenerazione associativa, Undo/Redo di creazione e modifica, codec con/senza cache e rebuild dopo riapertura. PASS nel runner completo.
- **42/42 test Loft esistenti** con confronti OCCT confinati al target di test. Nessuna dipendenza OCCT aggiunta all'app.
- **4 flussi nativi PASS**: due coppie reali su Wayland e due su XCB, con contesto OpenGL valido, pannello/anteprima/conferma, B-rep, fedeltà delle sezioni, Undo/Redo, file con/senza cache e ricostruzione completa. Preferenze isolate; dock secondari nascosti nella fixture per dare spazio al modello. Screenshot del risultato verificati visivamente.
- Mappa architetturale aggiornata: **563 file, 7.785 relazioni, 267 TU**, zero diagnostiche AST. Verificati 9 confini e i collegamenti in entrata/uscita di 10 moduli, distinguendo chiamate/include/riferimenti. `--check` PASS e due viste HTML renderizzate/controllate. HTML/JSON generati restano ignorati da Git.
- Non e stata rilanciata la suite completa di 167 CTest, ne una nuova CI GitHub o una build macOS. Questi esiti sono locali Linux, GCC/Qt6, con OpenGL nativo Wayland/XCB.

Prove, riproduttori e hash degli artefatti nel [pacchetto di verifica](history/loft-sezioni-proiettate-2026-11-03-verifica.zip). Il pacchetto esclude gli originali `.prt` e i binari; i sorgenti delle regressioni sono versionati.

## Campo verificato

La modifica riguarda le **sezioni** della loft. Non estende a ogni combinazione la risoluzione di guide implicite contro sezioni non planari: quel percorso richiede ancora sezioni piane quando deve isolare intersezioni esatte con una guida implicita. Non viene dichiarata una soluzione generale a tutte le configurazioni di loft o ai contatti G1/G2 con facce arbitrarie. Le prove usano esclusivamente copie e impostazioni isolate.
