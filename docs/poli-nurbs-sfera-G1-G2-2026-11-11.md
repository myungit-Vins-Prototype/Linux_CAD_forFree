# Poli NURBS: continuità della sfera e identità del comando

## Riproduttore e causa

Copia congelata di `Sphere_deformed.prt`: SHA-256 `a80a7aedea5c8a9c4866301b02f16a218606f959945e0d6e30e1e84c8ce4d941`.
Due funzioni, sfera primitiva e Modifica poli NURBS. F0 unica, griglia razionale di grado 2×2, 9×5 poli; nodi interni doppi ai quadranti e all'equatore. G1 legacy attivo, 24 poli già modificati.
Il vecchio vincolo copriva giunti topologici, cucitura e collasso dei vertici, ma non i nodi interni a bassa continuità parametrica né il jet delle calotte. La deformazione può quindi produrre vere pieghe anche nella stessa faccia.

## Implementazione

- Vincoli G1/G2 sui limiti unilaterali esatti delle basi razionali, pesi fissi e riparametrizzazione costante ricavata dal supporto neutro. Non si sostituisce la geometria con normali grafiche smussate.
- Fasce dei vertici degeneri: trasformazione affine comune del jet locale, con vertice mobile.
- G2 sui giunti compatibili; fasce protette nei casi non rappresentabili. Spigoli preesistenti conservati.
- Modi persistiti 0/1/2 legacy invariati; nuovi 3=G1 completo,4=G2, schema estensioni16. La modifica di una funzione G1 legacy aggiorna i vincoli nella sessione, ricalcolando i poli risolti come preferenze; Annulla non altera il documento.
- Titolo Modifica poli NURBS anche in modifica; proprietà esplicita `ribbonCommand` al posto del riconoscimento ambiguo dalla parola superficie.

## Verifiche effettive

Kernel: 20/20 test SurfacePoles, inclusa regressione sfera, normali e curvatura normale sui due lati dell'equatore, replay dei poli risolti, pesi protetti, integrità B-rep e sorgente immutata.
File utente caricato attraverso il normale viewport: vecchia deformazione con residui G1=0,0309775 e G2=0,403937. Ricalcolo G1: residuo G1=3,34e-13, G2=0,0111102; ricalcolo G2: G1=2,33e-12, G2=2,88e-12. Tutti i 45 poli partecipano alla compensazione della griglia rada. Copie salvate in /tmp, originale invariato.
Build completa GCC dell'app e del runner riuscita; 11/11 CTest pertinenti PASS. Suite kernel superfici 40/40 PASS, di cui 20 per i poli. Sei sorgenti modificati verificati anche con Clang `-fsyntax-only` (non una build completa Clang).
Runner ufficiale Wayland/OpenGL: G1/G2, ribbon, Undo/Redo, schema16 e rigenerazione senza cache PASS a scala1× e2×. Selezione, occlusione e poli multifaccia PASS. Il test preserva anche i comandi manuali compatibili all'attivazione dei vincoli e rifiuta le opzioni sconosciute/nuove su schemi precedenti.
Ribbon: il comando attivo viene mostrato anche se normalmente oltre i primi pulsanti promossi del gruppo, senza modificare il layout persistente; Modifica poli appartiene a Modifica.
Architettura: 681 file,9800 relazioni,334 unità C++,0 diagnostiche; check e quattro schede HTML verificate anche visivamente. Commit funzionale `ddc02f90a0bedfac589ceb3cd4b2ead53df75590` su origin/main. CI42982288794 Linux/macOS in corso.

Limiti: conserva la continuità esistente; non ripara automaticamente gli spigoli presenti nel supporto a monte. Il residuo G2 relativo misura i jet rappresentabili e non certifica regolarità globale o assenza di auto-intersezioni. Le griglie quadratiche rade possono richiedere spostamenti estesi; più poli danno più libertà locale.

Approssimazione: una griglia refittata con passaggi/cuciture incompatibili viene rifiutata con indicazione di disattivare Semplifica. Riproduttore sfera12×16: incompatibile; griglia razionale originale9×5 verificata. Nessun aumento della tolleranza per consentire la conferma.

Verifica supplementare della ribbon: il self-test completo nativo Wayland si arresta sul fondale dei popup (`glassBackdropReady`), riprodotto anche sostituendo solo il modulo ribbon con la versione HEAD precedente. Il percorso G1/G2 e i suoi controlli della ribbon passano; il test generale dei popup non è dichiarato riuscito né attribuito alla modifica NURBS.

Confronto grafico nativo del file reale e della copia G2: calotta appuntita e fascia visibili nell’originale, eliminate con G2; immagine locale `/tmp/forgecad-sphere/compare.png`. L’originale conserva lo SHA-256 iniziale. La CI precedente richiedeva circa25–30min complessivi; quella nuova è ancora nella compilazione, senza esito dichiarato.
