# Storyboard nei pannelli, diagnostica loft e cancellazione specchi — 3 novembre 2026

Implementato e verificato localmente con build completa di app e runner. Le modifiche riguardano tre richieste:
selezione degli schizzi/bordi dalla storyboard nei pannelli; identificazione visuale di guida e sezione negli
errori loft; eliminazione dello specchio al primo comando, anche con schizzi associativi.

## Uso e comportamento

Attivare il campo di selezione del pannello, poi fare clic nella vista oppure nella storyboard. Il clic sullo
schizzo sceglie il contorno intero, anche quando nascosto. Espandendo lo schizzo o il corpo si possono scegliere
singole entità, punti, spigoli e bordi compatibili con il campo attivo. Le facce mantengono il riferimento
al loro contorno quando usate per raccordi/smussi; Estendi propone i bordi liberi delle superfici.
I riferimenti futuri, soppressi, non validi o incompatibili sono esclusi. La scelta non cambia la visibilità
salvata, non apre la modifica e non muove il cursore della storia. La modalità precedente si ripristina
alla chiusura. Il picker delle sorgenti delle ripetizioni continua a funzionare.

Gli errori di attraversamento loft nominano la guida e la sezione nell'ordine effettivo scelto nel pannello.
La guida coinvolta viene ridisegnata in rosso e la sezione in arancione, anche se le sorgenti sono nascoste;
ciascuna ha etichetta e freccia. I marcatori tecnici restano interni. Le segnalazioni vengono rimosse quando
si modifica la definizione, si risolve l'errore o si chiude il pannello. Si evidenzia la coppia fallita:
non si attribuisce una coppia arbitraria agli errori per cui il kernel non ne fornisce una.

Una sola eliminazione dello specchio rimuove madre e risultati e ripristina la base. Gli schizzi che
referenziavano geometria eliminata conservano un errore esplicito, senza agganciarsi ad altre copie.
Undo/Redo trattano l'operazione come un solo passo.

## Cause e implementazione

- La storyboard era disabilitata da `WindowLock`, salvo il picker dedicato delle ripetizioni.
  `cad_storyboard_selection.*` tiene disponibile il solo albero per la scelta e protegge modifiche,
  spunte di visibilità, rinomina, trascinamento e cursore. I figli di selezione sono temporanei.
- `forgeCad2026_viewport_storyboard.cpp` costruisce riferimenti esatti/genealogici e usa gli stessi callback
  dei picker del viewport. `cad_datum.cpp` risolve anche lo schizzo intero come percorso limitato.
  Il pannello loft riusa la voce canonica della guida intera, evitando duplicazioni con le entità.
- Nelle prove native il cambio di campo poteva eliminare i figli temporanei e spostare la riga prima
  dell'hit test. Il ponte conserva l'indice persistente della voce premuta, anche per Invio/Spazio.
- Il kernel forniva già gli indici della coppia fallita. Il passaggio in `cad_forge.cpp` perdeva la guida
  e, per le sezioni esatte, anche la sezione. `cad_loft_diagnostics.*` conserva i marcatori e risolve i nomi
  attraverso l'ordine effettivo delle guide, includendo schizzi interi, catene parziali e curve 3D/bordi.
  Gli errori di ordine e guida vuota ricevono anch'essi gli indici disponibili.
- `forgeCad2026_viewport_loft_diagnostics.cpp` prepara e disegna overlay temporanei, etichette e frecce.
  Il pannello lascia visibili entrambe le identità nel messaggio. I campioni sono solo per il display;
  l'intersezione resta un calcolo del kernel esatto in double, senza nuove tolleranze geometriche.
- `deleteFeatures` rimuoveva il gruppo specchio in passaggi separati. Fra due rimozioni,
  `removeBodies` poteva aggiornare gli schizzi collegati e rigenerare una copia appena eliminata.
  Eliminando poi la madre restava una copia orfana arancione. Ora il gruppo viene rimosso con una sola
  rimappatura prima di rigenerare. Si mantengono preferenze di visibilità, base implicita e passo Undo.

Il formato nativo resta 61. I CAD originali non sono stati modificati; nessun processo utente è stato chiuso.

## Verifiche effettive

| Prova | Risultato |
| --- | --- |
| Build completa CMake, app e `forgecad_view_tests` | PASS, eseguibile aggiornato |
| 18 suite CTest pertinenti: nuovo picker, storyboard, pannelli, storia/soppressione, ripetizioni, genealogia, superfici, loft, curve 3D | 18/18 PASS |
| Kernel, filtro `Loft` | 42/42 PASS |
| Selezione nativa: schizzo nascosto intero, punto, bordo esatto, contorno faccia, filtri, ripristino e sorgente ripetizioni | Wayland e XCB PASS |
| Loft reale: guida errata/sezione nominate, scelta sezioni e guida dalla storyboard, correzione, conferma e pulizia overlay | Wayland e XCB PASS |
| Finestra completa `PdfWindow`: clic sullo specchio nella storyboard, un solo Elimina, Undo/Redo con schizzo collegato | Wayland e XCB PASS |
| Riproduttore specchio con codice precedente, compilato sugli stessi header | FAIL atteso: residuo dopo un solo Elimina |
| Overlay: colori distinti, etichette/frecce, framebuffer OpenGL e controllo immagini | PASS |
| Mappa architetturale | 559 file, 7731 relazioni, 265 AST, zero diagnostiche |
| 10 confini e 17 moduli: entrata/uscita distinte per call/include/reference; `--check` e resa HTML | PASS |
| `git diff --check` | PASS |

La fixture nativa sintetica iniziale con due dock visibili ha incontrato un crash Qt in
`QPropertyAnimation` durante il primo resize, prima del picker. Per la verifica isolata il dock ausiliario
è nascosto e la geometria viene preparata prima di mostrare la finestra; i clic sono limitati alla porzione
visibile delle righe. I flussi della loft e dello specchio nella finestra reale sono verificati separatamente,
senza questa variante. Non si dichiara corretto quel crash Qt generale. Le suite complete native precedenti
hanno inoltre i limiti già registrati nel rapporto dei pannelli. macOS e la nuova CI GitHub non ricertificati
in questo intervento; gli esiti qui sopra sono locali Linux.

La mappa è generata e ignorata da Git; generatori e descrizioni sono versionati. Il controllo HTML è stato
ripetuto dopo la stabilizzazione del ponte. Le prove sono conservate in
[archivio verifiche](history/storyboard-diagnostica-loft-2026-11-03-verifica.zip).

Per caricare la nuova build occorre riaprire ForgeCAD: l'app già aperta continua a usare il codice precedente.
