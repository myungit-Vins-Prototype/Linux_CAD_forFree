# Loft: punti di corrispondenza fra sezioni — 4 novembre 2026

L’utente ha precisato che i controlli richiesti sono i punti di corrispondenza dei profili per correggere le torsioni, non i poli NURBS della superficie.

## Uso e comportamento

Nel pannello Loft, sotto le sezioni, attivare **Modifica corrispondenze fra sezioni**. I punti `S · C` e le linee colorate rappresentano sezioni e collegamenti. Trascinare un punto sul proprio profilo oppure selezionare sezione e collegamento nei due elenchi e modificare la posizione percentuale. La percentuale si riferisce alla lunghezza della sezione originale, prima dell’orientamento automatico.

- I contorni chiusi iniziano con un collegamento di cucitura. Le sezioni aperte iniziano con i due estremi, che non possono scorrere all’interno del profilo.
- **Aggiungi collegamento** aggiunge un punto su ogni sezione, tra il collegamento selezionato e il successivo. **Rimuovi collegamento** rimuove l’intera colonna, conservando almeno una cucitura o i due estremi.
- **Inverti verso sezione** cambia il verso rispetto all’orientamento automatico. Conserva la cucitura chiusa e inverte l’ordine degli altri collegamenti; sui profili aperti scambia anche gli estremi.
- Durante il trascinamento si aggiornano i punti; al rilascio si ricalcola l’anteprima. **Esc** ripristina la posizione precedente al trascinamento. **Automatico** elimina i controlli manuali; **Annulla** non modifica il documento.
- Il riordino della lista conserva i dati associati a ciascuna sezione. Aggiungere una sezione priva di controlli reinizializza la tabella in modo coerente. Le altre modalità di picking del pannello hanno precedenza sui punti.

## Implementazione

`cad_loft_correspondence.*` risolve i profili con la stessa geometria esatta impiegata dalla costruzione: schizzi interi senza costruzione, catene, bordi e curve 3D. `cad_loft_correspondence_editor.*` gestisce stato temporaneo, campi, overlay e trascinamento. Il kernel fornisce la preparazione automatica condivisa e le conversioni fra ascissa curvilinea e punto esatto.

Il picking usa campioni solo per trovare un intervallo sullo schermo, poi minimizza sulla curva esatta. Le coordinate float del viewport non diventano geometria: il dato persistito è un’ascissa double sul profilo. Non vengono creati riferimenti associativi nascosti né curve guida fittizie.

Il kernel combina gli ancoraggi manuali e le intersezioni delle guide in una mappa comune delle sezioni. Le guide reali continuano a governare traiettoria, G1/G2 e condizioni trasversali. Corrispondenze incrociate o contraddittorie vengono rifiutate con indice della sezione. Una cucitura manuale su una curva periodica completa trasla il dominio di un periodo, senza lasciare un piccolo segmento alla vecchia cucitura. Questo corregge il fallimento riprodotto con tre cerchi e una cucitura a circa `4,88623e-9` della lunghezza del terzo profilo. Anche una NURBS chiusa non periodica cambia cucitura tramite concatenazione algebrica dei tratti; i pesi sono scalati uniformemente per preservare la curva razionale. Il profilo risultante è verificato entro `1e-9 mm`. Questa gestione è limitata alle corrispondenze manuali, senza cambiare l’allineamento dei documenti precedenti.

I controlli appartengono alla feature e seguono le sue sezioni. Non aggiungono nuovi oggetti al grafo delle dipendenze. Il formato `.prt` è **64**; i file precedenti conservano l’allineamento automatico. Le impronte delle tavole possono riprodurre anche il formato63, ma rifiutano quel formato se ci sono corrispondenze manuali, senza perderle silenziosamente. Chiave dell’anteprima, salvataggio e Undo comprendono verso e posizioni.

## Verifiche e limiti

- **Build Release completa dell’app e del runner ufficiale: PASS**, Linux/Qt6. L’eseguibile aggiornato è `forgecad-release/forgecad`; un’app già aperta deve essere riavviata.
- **CTest finale: 26/26 PASS**, nessuno skip, circa 11 secondi: corrispondenze, loft da bordi, tangenza aperta, forma, guide/sezioni proiettate, complessità, condizioni trasversali, lettura documenti, revisioni native, tavole collegate, ripetizioni variabili e regressioni degli schizzi/snap Intersezione.
- Il nuovo test kernel comprende **7 casi PASS**: punti interni su sezioni curve, cucitura con guida reale, verso e ascisse spaziali, cucitura periodica vicina all’originale, guide duplicate, NURBS chiuse e direzione degli estremi. Controlli B-rep e profili entro `1e-9 mm`; corrispondenze incoerenti rifiutate.
- Test app: tabella, riordino, verso, invalidazione anteprima, codec64/lettura63, dati non finiti, Undo/Redo, trascinamento esatto, Esc, riapertura e Annulla PASS.
- **Wayland/OpenGL: PASS** sullo stesso flusso, con immagine del viewport e del pannello reale ispezionate. I controlli sono sotto le sezioni nella zona scorrevole. Il test usa fixture sintetiche, senza documenti originali. Nessuna verifica macOS o CI effettuata in questo intervento.
- **Mappa: 592 file / 8218 relazioni / 284 unità AST / zero diagnostiche**; freschezza PASS, 7 confini verificati e collegamenti entranti/uscenti distinti per call/include/reference. Rendering dell’overview e di 5 dettagli HTML PASS; dettaglio dell’editor ispezionato.
- `git diff --check`: PASS. Log, immagini e controlli della mappa in `forgecad-release/loft-correspondence-2026-11-04-qa/` (ignorati da Git).

I collegamenti permettono di correggere la corrispondenza; non garantiscono l’assenza globale di auto-intersezioni per qualsiasi insieme di profili. Non impongono continuità aggiuntiva fra le facce della loft. Nessuna tolleranza globale aumentata, nessun CAD originale modificato, nessun push effettuato per questo intervento. La correzione della divisione delle facce con tratti coincidenti resta separata e rinviata.
