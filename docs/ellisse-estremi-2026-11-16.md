# Ellisse per estremi — 16 novembre 2026

Richiesta: creare un’ellisse senza scegliere prima il centro e usare tutti i quattro estremi per quote e vincoli. Implementata con tre agenti GPT-6.1 Sol; la modalità dal centro rimane disponibile.

## Uso

Apri **Schizzo → Strumento geometrico → Ellisse per estremi**; il comando ha una propria icona ed è disponibile anche nella toolbar e nella ribbon.

1. Scegli il primo estremo di un asse.
2. Scegli l’estremo opposto dello stesso asse.
3. Scegli un estremo del secondo asse: il punto viene proiettato sulla perpendicolare che passa per il punto medio del primo asse.

Il quarto estremo viene ricavato per simmetria. L’anteprima mostra i quattro punti e i due assi. Un clic degenere viene rifiutato mantenendo la costruzione in corso; Esc annulla l’anteprima. Il comando rimane attivo per disegnare altre ellissi.

Con **Quota**, scegli uno dei quattro estremi e un riferimento per assegnare distanze o coordinate X/Y. In **Selezione**, usa Ctrl+clic sui punti e il pannello **Vincoli** per coincidenze, allineamenti, Fisso e gli altri vincoli compatibili. Il doppio clic sulla curva attiva Quota. Il centro si aggiorna di conseguenza.

## Definizione geometrica e vincoli

La nuova entità conserva quattro punti reali `[A0,A1,B0,B1]`, referenziati senza punti virtuali o appoggi al campionamento del display. Le coppie A0/A1 e B0/B1 hanno lo stesso punto medio e i loro assi sono perpendicolari. Sono tre equazioni intrinseche su otto coordinate: l’ellisse libera ha cinque gradi di libertà. Il centro non è un’incognita aggiuntiva.

Il trascinamento di un estremo conserva il controestremo dello stesso asse e adegua l’altra coppia; il solver applica poi i vincoli esistenti. La concentricità usa il centro derivato e le dipendenze di entrambe le coordinate della prima coppia. Un risultato degenere o fuori dalla precisione del documento viene rifiutato atomicamente, senza aumentare le tolleranze.

La geometria di profilo è una `Kernel::Ellipse` analitica. Taglia e Spezza riconoscono la stessa conica; gli archi risultanti vengono rappresentati esattamente come NURBS razionali. I quattro estremi sono disponibili per snap, picking e quote, compresi il terzo e il quarto.

## Persistenza e compatibilità

`DrawingTool::EllipseExtremes` è aggiunto in coda con valore 25: i valori precedenti rimangono invariati. Il formato nativo è **70**, senza modifica del layout della curva: conserva i quattro punti e tutti i riferimenti dei vincoli. Il lettore mantiene i documenti precedenti; una nuova ellisse non viene serializzata con un formato storico che non la rappresenta. La validazione geometrica usa la precisione salvata nel documento, anche nei dati Pierce.

Il controllo delle impronte delle tavole riconosce il precedente formato69, anche con precisione personalizzata e più entità nascoste. Una modifica geometrica reale o l’aggiunta della nuova ellisse resta una modifica, senza riassociazione per vicinanza né errore degli encoder storici. Il riproduttore ha fallito prima della correzione e passa sul matcher finale.

## Verifiche finali

| Verifica | Esito |
|---|---|
| App e runner Release | Build completa finale PASS; eseguibile aggiornato |
| CTest pertinenti | **24/24 PASS**, nuova ellisse, ellisse storica, editing, snap, solver, precisione, codec, revisioni, tavole collegate e regressioni del kernel |
| Interazione nuova ellisse | **12 geometrie**, **48 normalizzazioni**, **48 bersagli solver**, tutti e quattro gli estremi con quote X/Y, Fix, Coincidente e punto sulla curva |
| UI e salvataggio | Tre clic, degeneri, anteprima/Esc, quattro drag mouse, snap/picking, Undo/Redo, QAction e formato70/legacy69 PASS |
| Taglio conica | Ellisse ruotata con secondo asse maggiore: semiarco razionale, estremi, 21 punti e area del semiprofilo **5π** PASS |
| Compatibilità impronte69 | Precisioni 1e-7/1e-8, visibilità, cambi geometrici e nuova entità diretta/Pierce PASS |
| Sanitizer mirati | ASan/UBSan/LSan sui quattro TU geometrici/solver/editing e probe PASS, senza soppressioni |
| Wayland/OpenGL | Nuova suite con `--gl` PASS, contesto nativo valido |
| Architettura | **761 file, 10998 archi, 379 TU**; zero diagnostica, check, HTML/JSON e archi entranti/uscenti PASS |
| Conservazione | **2669/2687 file iniziali invariati**, 18 modifiche esistenti circoscritte e nuovi modulo/test/rapporti |

La prima build si è sovrapposta all’aggiunta dell’encoder69 e ha fallito il link per un oggetto precedente. Dopo il congelamento delle sorgenti, i file interessati sono stati ricompilati e la build finale è passata; i due log restano distinti. L’ultima compilazione e i test usano gli hash finali verificati.

Il probe sanitizer strumenta `cad_ellipse`, `cad_curve_solver`, `cad_constraints`, `cad_sketch_edit` e il main privato; kernel e dipendenze offset restano Release non strumentate. Un primo link con tutte le dipendenze GUI riportava allocazioni del driver NVIDIA: il probe finale collega soltanto QtCore e le dipendenze necessarie, senza soppressioni. Nessuna certificazione generale dei leak dell’app grafica. Le prove Wayland verificano il contesto e la suite, senza attribuire una revisione visuale completa di ogni tema.

La prima esecuzione CTest ha dato 23/24: la fixture mouse confrontava coordinate ideali con clic arrotondati ai pixel interi. L’atteso finale usa i punti effettivamente ricevuti e una proiezione calcolata indipendentemente; la tolleranza 1e-5 resta invariata e nessun codice di produzione è stato modificato per questo esito. Il log iniziale è conservato separatamente.

Il test d’area è stato corretto prima della verifica finale per usare la lunghezza del segmento come dominio della retta normalizzata; l’area attesa 5π non è cambiata. CAD originali e modifiche pregresse, incluso il blocco K10, sono preservati. K10 resta aperto come documentato nel rapporto precedente.

[Guida dello schizzo](sketch-tools.md), [prove e manifest](ellisse-estremi-prove-2026-11-16/README.md), [memoria prima dell’intervento](history/claude-prima-ellisse-estremi-2026-11-16.md).
