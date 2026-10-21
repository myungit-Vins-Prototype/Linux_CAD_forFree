# Mappa dell’architettura ForgeCAD

Per la vista aggiornata all’apertura usare il comando Python sotto. [index.html](index.html), aperto direttamente, rimane disponibile come snapshot autonomo. Il documento funziona offline senza server e contiene il grafo, i filtri e la ricerca. Ogni blocco raccoglie file con responsabilità comuni; il dettaglio di un file mostra tutte le relazioni in uscita e in entrata, con simbolo e riga sorgente. La ricerca comprende i simboli definiti rilevati dall’AST. Il grafo di dettaglio limita i vicini visibili a sette per lato; gli elenchi sottostanti sono completi per i filtri selezionati.

I blocchi sono raggruppamenti per responsabilità, non nuovi target CMake o oggetti a runtime. In particolare i diversi `forgeCad2026_viewport_*.cpp` implementano la stessa classe `CadViewport`: separano le responsabilità dell’implementazione mantenendo privato lo stato GUI. Camera, renderer, picking, compositore, cache di curvatura e calcolo delle anteprime hanno invece componenti propri. `PdfWindow` mantiene il coordinamento della finestra; le sette fasi del costruttore condividono soltanto uno stato temporaneo di inizializzazione.

## Significato delle frecce

- **Chiamata C++:** espressione di chiamata risolta da libclang nella configurazione CMake analizzata. Quando disponibile il simbolo è collegato al file che ne definisce l’implementazione; altrimenti al suo header di dichiarazione. Chiamate tra file della stessa classe possono interessare lo stesso oggetto.
- **Include:** direttiva locale `#include`, rilevata dal testo anche nei rami condizionali. Esprime una dipendenza di compilazione, non una chiamata.
- **Riferimento a funzione:** indirizzo di funzione/metodo, per esempio in una connessione Qt. Indica un possibile callback; non prova che venga invocato.

La freccia è sempre **origine → destinazione**. Le occorrenze sono deduplicate per file, riga, simbolo e contesto. Le relazioni interne allo stesso file rimangono negli elenchi. Il grafo tra blocchi aggrega le relazioni tra file; la vista generale mostra fino a due dipendenze prevalenti per blocco, mentre la selezione di un blocco ne mostra tutti i collegamenti. Il loro conteggio non misura il numero di invocazioni durante l’esecuzione.

L’analisi statica non è una traccia runtime: dispatch virtuale, connessioni dinamiche, `std::function`, plugin, macro e rami esclusi dalla configurazione possono introdurre chiamate ulteriori. I file non compilati dalla configurazione e CUDA hanno il catalogo e gli include, ma nessuna copertura AST garantita. L’elenco esatto delle unità analizzate e gli eventuali errori sono in `graph.json` → `analysis`. Le librerie esterne non sono nodi del grafo. I collegamenti ai sorgenti sono relativi al repository; l’ancora `#L` funziona soltanto negli editor che la supportano.

## Calcolo all’apertura nel browser

Dalla radice del repository:

```sh
python3 tools/open_architecture.py
```

Lo script trova l’ultima build usata dal generatore e apre automaticamente il browser. Se necessario specificarla la prima volta:

```sh
python3 tools/open_architecture.py --build-dir build-linux
```

A **ogni apertura o ricaricamento della pagina** JavaScript chiede al processo Python locale di controllare e aggiornare il grafo. La pagina mostra avanzamento ed eventuali errori e carica lo schema solo dopo il successo. Il pulsante **Aggiorna dai sorgenti** ripete l’operazione. Se i file sono invariati non viene ripetuta l’analisi AST. Non serve ricompilare l’applicazione per aggiornare lo schema di sorgenti modificati; servono comunque la configurazione CMake con `compile_commands.json`, gli header generati e gli strumenti libclang già descritti sotto.

Python legge il codice e risolve le chiamate C++; JavaScript gestisce la visualizzazione e la richiesta di aggiornamento. Un HTML aperto direttamente con `file://` non può eseguire Python o leggere liberamente il checkout: è lo snapshot offline, non l’avvio automatico.

Il server ascolta solo su `127.0.0.1`, su una porta libera, con un percorso casuale di sessione. Espone esclusivamente mappa e sorgenti del catalogo, con i sorgenti serviti come testo; rifiuta percorsi fuori dal catalogo e richieste da altri siti. Nessun servizio o avvio permanente viene installato. **Ctrl+C nel terminale** chiude server ed eventuale analisi in corso. Con `--no-browser` stampa il link senza aprire una finestra; `--port` permette di scegliere una porta. Il percorso della build è memorizzato nel file locale ignorato da Git `.forgecad-architecture.json` e non viene condiviso tra computer.

## Aggiornamento automatico

L’aggiornamento è **attivo per default** (`FORGECAD_UPDATE_ARCHITECTURE=ON`). Dopo aver riconfigurato la propria build CMake, il normale comando di compilazione aggiorna anche la mappa:

```sh
cmake --build build-linux --parallel
```

Il target `forgecad_architecture`, incluso nella build standard, verifica sorgenti, catalogo, template e configurazione di compilazione. Rigenera HTML/JSON soltanto se necessario, riutilizzando la cache AST; a vista invariata non modifica i file. Un comando dopo il link copre anche la compilazione esplicita del solo target `forgecad`; se questo target non richiede un nuovo link, usare la build standard per rilevare anche cambiamenti dei soli strumenti/template. Per aggiornare esplicitamente la mappa:

```sh
cmake --build build-linux --target forgecad_architecture
```

CMake abilita automaticamente `compile_commands.json`. Servono Python 3 e binding `clang.cindex` compatibili con libclang. Gli header Qt generati vengono preparati prima dell’analisi. La build usa un solo processo AST per contenere il consumo di memoria. Alla prima generazione o dopo cambiamenti agli header l’analisi può richiedere alcuni minuti. Le compilazioni successive invariate eseguono soltanto il controllo rapido.

Un errore di analisi fa fallire il target e conserva la vista precedente; non viene pubblicato un nuovo grafo incompleto. Le scritture avvengono tramite sostituzione di file temporanei e un lock per checkout serializza aggiornamenti da build diverse. La vista aperta nel browser va ricaricata dopo l’aggiornamento. Non è un servizio in background: le modifiche vengono recepite alla build successiva.

L’automatismo aggiorna file, simboli, chiamate e include. Le descrizioni curate e i gruppi di responsabilità richiedono ancora una revisione umana quando cambia il significato di un modulo.

Per build di distribuzione senza gli strumenti di analisi è possibile disabilitare esplicitamente il target con `-DFORGECAD_UPDATE_ARCHITECTURE=OFF`. In quel caso l’aggiornamento resta obbligatorio prima di consegnare modifiche strutturali.

## Aggiornamento obbligatorio

Per richiesta dell’utente, **ogni cambiamento strutturale richiede l’aggiornamento di questa vista**. La regola permanente è in `AGENTS.md` e `CLAUDE.md`.

1. Aggiornare i gruppi e le descrizioni curate in `tools/generate_architecture.py` quando cambia una responsabilità. Le descrizioni del kernel derivano dal commento iniziale del relativo header; le altre descrizioni non curate derivano dal ruolo header/implementazione e dal nome del file; i simboli vengono estratti dal codice.
2. Configurare la build abituale conservando le proprie opzioni; l’aggiornamento automatico esporta già i comandi di compilazione.
3. Compilare normalmente, oppure generare esplicitamente il grafo usando Python 3 e i binding `clang.cindex` compatibili con la libclang installata:

   ```sh
   python3 tools/generate_architecture.py --build-dir build-linux
   python3 tools/generate_architecture.py --check
   ```

   Su Linux i binding sono spesso forniti dal pacchetto `python3-clang`; il nome varia secondo la distribuzione. Se necessario usare `--clang-resource-dir /percorso/lib/clang/VERSIONE/include` per gli header builtin. Il generatore usa al massimo due processi per default (`--jobs 1` per ridurre il consumo di memoria). La cache AST è nella cartella di build e si invalida al cambio dei sorgenti/header o delle opzioni di compilazione. Dopo aggiornamenti di libclang o degli header di sistema cancellare `architecture-cache` dalla propria cartella di build prima di rigenerare.
4. Aprire l’HTML e verificare il blocco cambiato, i collegamenti in entrambi i versi e la leggibilità. Per richiesta dell’utente `graph.json` e `index.html` sono artefatti locali ignorati da Git: verificarli dopo la modifica, senza includerli nel commit. Versionare invece strumenti, template e descrizioni aggiornati.

`--check` non richiede libclang né una build: verifica il catalogo dei file, gli hash dei sorgenti/strumento/template, i collegamenti e la corrispondenza tra JSON e HTML. Segnala anche modifiche non strutturali: in quel caso rigenerare, così righe e simboli rimangono corretti. Non sostituisce compilazione e test. Un errore AST fa fallire la generazione e il controllo; non deve essere interpretato come un grafo completo.

File mantenuti a mano: `template.html`, questo documento e il catalogo Python. File generati e ignorati da Git: `graph.json`, `index.html`. Su un nuovo checkout vengono creati automaticamente al primo aggiornamento tramite build o avvio Python. Il generatore non esegue il codice del progetto né invia dati in rete.
