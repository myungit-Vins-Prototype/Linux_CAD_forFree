# Costruzione degli schizzi visibile su richiesta

## Comportamento

Aggiunto **Visualizza → Mostra linee di costruzione**. Mostra o nasconde linee e curve
di costruzione degli schizzi già visibili, senza doverli aprire in modifica.
La preferenza è salvata in `view/sketchConstruction` e ripristinata nelle nuove sessioni.
È spenta per default; durante la modifica dello schizzo la costruzione resta visibile.
L'opzione non modifica il ruolo delle entità: continuano a essere escluse dai profili delle lavorazioni.
Le cache associative automatiche rimangono nascoste.

Il precedente intervento aveva scelto di nasconderla sempre fuori modifica. L'utente ha chiarito
che serve un comando per mostrarla al bisogno: questa modifica completa quel comportamento.

## Implementazione

`forgeCad2026_gui.cpp` aggiunge il comando con stato spuntabile, icona, spiegazione e preferenza.
Il viewport conserva uno stato di vista, distinto dal documento. Il setter invalida bounds,
cache dei candidati esterni ed evidenziazioni; non aggiunge Undo né richiama documentChanged.
`isSketchEntityDisplayed` concentra la decisione visiva, usata da rendering, limiti,
picking, selezione rettangolare, riferimenti storyboard e snap agli altri schizzi.
La policy geometrica delle sezioni loft continua a escludere la costruzione, anche quando è mostrata.

## Verifiche

- Applicazione e runner Release completi compilati. Corretto nel primo tentativo un qualificatore
  di namespace; warning preesistenti dei moduli tavole/dialoghi, senza nuove soglie geometriche.
- **10/10 CTest pertinenti PASS**: costruzione, uso dei riferimenti, collegamenti/visibilità,
  workflow associativo, quote costruzione, storyboard, GUI/3D e suite base della vista.
- Estesa la regressione esistente: on/off, disegno, bounds, picking e storyboard coerenti,
  cache automatiche nascoste, stesso documento serializzato, nessun falso Undo/dirty,
  profili loft invariati e lettura della preferenza nelle nuove finestre.
  Nella fixture il picker storyboard deve avere un callback; per le prove native il punto
  di selezione viene calcolato sulla camera corrente dopo l'assestamento della finestra.
  Le prime prove incomplete sono distinte dai log finali.
- **Wayland e XCB**: rendering on/off/in modifica e comando del menu verificati; per ciascuna
  piattaforma due processi indipendenti verificano il ripristino della preferenza accesa e spenta.
  Popup reale del menu acquisito su XCB. Su Wayland l'apertura automatizzata del popup non è
  riuscita: catturato il widget del menu; le prove di comando, disegno e persistenza sono riuscite.
  Le impostazioni dei test sono isolate; nessun file CAD originale utilizzato o modificato.
- Mappa aggiornata: **566 file, 7827 relazioni, 268 TU, zero diagnostiche AST**, check PASS.
  Verificate cinque chiamate richieste e nove moduli in entrata/uscita, distinti call/include/reference;
  HTML dei blocchi scena e menu renderizzato e ispezionato. Gli output HTML/JSON restano ignorati.

[Evidenze e immagini](history/visualizzazione-costruzione-2026-11-03-verifica.zip).
L'archivio conserva le prove finali e distingue gli errori iniziali della fixture;
non contiene eseguibili, preferenze utente o documenti CAD.
