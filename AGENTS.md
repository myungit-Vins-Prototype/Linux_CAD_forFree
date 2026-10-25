# Istruzioni permanenti del progetto

- La lingua di lavoro è l'italiano.
- Per preferenza esplicita dell'utente, non inserire `codex` nei nomi dei branch, neppure come prefisso `codex/`; usare nomi descrittivi del lavoro.
- Comunicare sempre all'utente i passaggi cruciali durante le operazioni: decisioni, modifiche sostanziali, verifiche, esiti e problemi che cambiano il lavoro, senza limitarsi alla risposta finale.
- All'inizio di ogni chat su questo progetto, leggere `CLAUDE.md` e le sezioni pertinenti al lavoro richiesto. È il registro condiviso del progetto, anche per Codex.
- Per istruzione esplicita dell'utente, aggiornare sempre `CLAUDE.md` prima della risposta finale di ogni intervento, in tutte le chat del progetto, comprese indagini, correzioni e modifiche alla documentazione.
- Aggiornare `CLAUDE.md` anche a ogni obiettivo raggiunto durante un intervento, senza attendere un promemoria dell'utente o la fine della chat.
- Registrare richiesta, decisioni, cause individuate, file modificati, verifiche realmente eseguite, risultati, limiti e lavoro ancora aperto. Distinguere implementato, verificato, sperimentale e proposto; non dichiarare aggiornato l'eseguibile se è stato compilato soltanto il kernel o il runner dei test.
- Preservare le voci delle altre chat. Rileggere la parte da modificare prima di scrivere per non sovrascrivere aggiornamenti concorrenti; aggiornare la voce pertinente o aggiungere una voce datata senza duplicare il registro. Non inserire credenziali o segreti.

## Vista architetturale — istruzione permanente dell’utente

- Per richiesta esplicita dell’utente, `docs/architecture/index.html` e `graph.json` sono generati localmente e ignorati da Git; non forzarne l’aggiunta ai commit. Versionare generatori, launcher, template e documentazione necessari a ricrearli.
- Dopo ogni cambiamento strutturale (file/moduli aggiunti, spostati o rimossi, responsabilità o confini di chiamata/dipendenza modificati), aggiornare la vista consultabile `docs/architecture/index.html` e il relativo `graph.json`.
- Per consultazione con aggiornamento all’apertura usare `python3 tools/open_architecture.py`: la pagina richiede a Python un controllo/aggiornamento a ogni apertura o ricaricamento; il solo index.html è uno snapshot offline.
- La compilazione CMake è indipendente dalla mappa: esporta `compile_commands.json`, ma non avvia il generatore. Aggiornare tramite il server Python all’apertura/ricaricamento della pagina o con il pulsante; resta disponibile il generatore esplicito da terminale.
- Aggiornare le descrizioni e i gruppi in `tools/generate_architecture.py`, rigenerare con `python3 tools/generate_architecture.py --build-dir <build-configurata>` e verificare con `python3 tools/generate_architecture.py --check`. Istruzioni e limiti: `docs/architecture/README.md`.
- Verificare il blocco interessato e i collegamenti in entrambi i versi; mantenere distinte chiamate C++ risolte, include e riferimenti a funzione. Non dichiarare conclusa una modifica strutturale lasciando la vista obsoleta; segnalare esplicitamente eventuali limiti di analisi.
