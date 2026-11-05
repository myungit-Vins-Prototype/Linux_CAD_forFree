# Ripetizioni a parametri variabili — 4 novembre 2026

Richiesta: variare i parametri nelle ripetizioni lineari/circolari, anche più parametri contemporaneamente, mediante incrementi costanti.

## Uso

Scegliere **Ripeti: Una funzione**. Nel passaggio **Parametri** le quote dei suoi schizzi diretti compaiono nella viewport anche se lo schizzo è nascosto e senza entrare in modifica. Un clic sul testo o sulla linea di quota aggiunge la variazione e porta il focus sul suo incremento; un secondo clic seleziona la riga esistente. Le quote selezionate sono verdi. I parametri numerici della funzione, come la distanza dell'estrusione, restano nel breve elenco con **Aggiungi**. Ogni riga contiene parametro, valore iniziale, incremento e valore dell'ultima istanza.

**Il corpo intero** disabilita la variazione e rimuove le quote temporanee. Anche il costruttore rifiuta variazioni su un corpo eventualmente ricevute da file/API. Cambiare sorgente svuota le righe precedenti; passare temporaneamente a corpo conserva la bozza nel widget, ma non la applica né la salva. Chiudere con OK/Annulla rimuove overlay e callback.

`q(i) = q(0) + i * incremento`, con `i = 0` per l'originale. Il valore iniziale segue quello della sorgente. Il passo lineare/angolare della ripetizione rimane indipendente. Le esclusioni conservano il numero originale: escludere l'istanza 2 non rinumera i valori delle successive. Nelle griglie avanza prima la direzione 1, poi la 2. La colonna **Ultima** mostra il valore dell'ultimo indice nominale, anche se quell'occorrenza è esclusa.

## Parametri e costruzione

Il catalogo pubblico comprende soltanto i parametri della funzione selezionata e gli schizzi che usa direttamente. In una booleana comprende gli utensili diretti e i loro schizzi, mai il destinatario fisso. Antenati, piani d'appoggio e schizzi associativi a monte continuano a servire al ricalcolo ma non diventano parametri selezionabili.

La ripetizione di funzione resta limitata ai tipi già supportati dal comando (funzioni additive/sottrattive supportate e booleane unione/differenza). I campi numerici esposti dipendono dal tipo effettivo dell'utensile. Enum, booleani e riferimenti geometrici non sono incrementi numerici.

`cad_pattern_variation` contiene un catalogo comune per elenco, lettura, applicazione e limiti dei parametri, più la ricostruzione del grafo di dipendenze. Ogni istanza parte da copie isolate dei dati; vengono rigenerati i nodi modificati e i loro dipendenti. Seguono appoggi, riferimenti associativi, Pierce e soluzione dei vincoli. Il risultato usa il B-rep e le booleane esistenti, senza ricostruzione da mesh o allargamento delle tolleranze.

Per la ripetizione di una funzione si varia lo strumento e si conserva il destinatario fisso. Unioni e differenze rispettano l'operazione attiva, ignorando campi appartenenti ad altre modalità. Risultati vuoti e assenza di occorrenze attive sono errori, salvo il caso già previsto in cui il destinatario contiene l'originale di una funzione precedente.

Gli incrementi tutti nulli riusano il percorso delle copie identiche. Nelle variazioni multiple una riga a incremento nullo non rende sporco il suo ramo. Le istanze differenti ricevono mesh proprie: la visualizzazione non riusa la mesh della sorgente. La chiave delle anteprime comprende identificatori, parametri e incrementi.

## Identità, salvataggio e limiti espliciti

- Owner persistente: `featureId` per funzioni e `storyId` per schizzi. Ogni vincolo riceve un `parameterId` locale; `nextParameterId` è monotono e persistito. Eliminare una quota e crearne un'altra non riassocia la variazione alla nuova quota. La ricreazione di una ripetizione dello schizzo conserva invece l'identità del vincolo modificato.
- Formato PRT **63**. Lettura dei formati precedenti, normalizzazione deterministica degli ID mancanti e fallback della firma 62 per le tavole. I codec delle firme precedenti rifiutano variazioni non rappresentabili. I documenti originali dell'utente non sono stati modificati nelle prove.
- Parametro eliminato/inattivo, duplicato, incremento non finito/frazionario per un conteggio, valore fuori dominio o costruzione fallita producono diagnostica. La geometria precedente e Undo restano intatti se la conferma fallisce.
- La variazione dei conteggi nello schizzo ricrea le copie e rimappa i riferimenti disponibili. Viene rifiutata se elimina entità vincolate/riferite o coinvolge percorsi parziali legacy che non possono essere rimappati in modo affidabile.
- Restano i limiti geometrici dei costruttori; non tutte le combinazioni di valori generano una forma valida. Il catalogo permette fino a 256 variazioni; il limite delle istanze resta quello della ripetizione esistente.

## Verifiche della revisione richiesta

- App Linux completa e runner aggiornati. **13/13 CTest PASS**, incluse ripetizioni esistenti/legacy, superfici, riferimenti, documenti, unità, soppressione e tavole collegate.
- Prove geometriche con volumi analitici: lineari/circolari/griglie, esclusioni, variazioni simultanee diametro/distanza, booleane unione/differenza e conteggi di schizzi.
- Catalogo della funzione e dei suoi schizzi diretti; rifiuto di corpo intero e riferimenti forzati agli antenati; sorgenti immutate, parametro duplicato/non finito e conteggio frazionario rifiutati. Identità dopo inserimento/eliminazione quote, codec63/firma62, Undo/Redo e cache anteprima.
- Test ufficiale del pannello su **Wayland PASS**: cambio funzione/corpo, clic sul testo quota, campo incremento, dati documento identici prima/dopo selezione, conferma/modifica, Undo/Redo, riapertura senza cache, rifiuto atomico di una variazione invalida e cleanup di quote/callback su OK/Annulla.
- Framebuffer nativo ispezionato: quota diametro disegnata sopra il solido, verde dopo la scelta. La cattura QWidget dell'intera finestra omette gli overlay QPainter; la verifica del rendering usa il framebuffer OpenGL.

Mappa aggiornata e verificata: **585 file, 8106 relazioni, 281 unità AST, zero diagnostiche**. Verificati 14 confini call/include e le relazioni in entrata/uscita dei moduli interessati; overview e cinque pagine di dettaglio HTML passano il rendering software offscreen. Il tentativo di render HTML con QtWebEngine/Wayland è terminato con segfault; la verifica del pannello ForgeCAD su Wayland è distinta ed è passata. Nessuna certificazione macOS/CI. I test riguardano i casi descritti, non tutte le combinazioni geometriche. Nessun commit/push; CAD utente intatti. Prove locali in `forgecad-release/pattern-variation-2026-11-04-qa/` (ignorata).
