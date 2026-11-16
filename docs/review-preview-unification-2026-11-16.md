# Anteprime, pannelli e ricontrollo delle orfane — 16 novembre 2026

Corretti due ulteriori difetti degli stati delle anteprime e completata la copertura della correzione UI2 su undici comandi che usavano ancora un ciclo eventi proprio. Il totale dei rilievi confermati corretti sale da 24 a **26**; l’estensione di UI2 non è contata come un nuovo difetto. K10 resta aperto: 15 dei 24 siti iniziali sono parzialmente trattati, 9 interamente da migrare, oltre ai fallback.

Lavoro svolto con tre agenti GPT-6.1 Sol: implementazione del coordinatore, verifica indipendente dei pannelli e ricontrollo delle 22 candidate orfane. Le modifiche pregresse a ellisse e kernel sono preservate.

## Problemi e comportamento risultante

| Rilievo | Prima | Dopo |
|---|---|---|
| PRE5 | Svuotamento con spessore zero cancella geometria e worker ma conserva «pronta»; tornando a uno spessore valido resta il vecchio errore. | Stato esplicito di input non valido, nessuna geometria residua; al recupero si elimina il vecchio errore e si calcola la nuova anteprima. |
| PRE6 | Filettatura senza faccia o durante la selezione dichiara un calcolo in corso senza worker. | Il testo richiede la faccia o indica l’attesa della selezione nella vista. |
| UI2, copertura residua | La guardia tra documenti era nel runner comune, ma undici runner manuali la aggiravano. Un comando della scheda A poteva restare bloccato fino alla chiusura del comando in B. | Tutti gli undici passano dal coordinatore comune; setup di picker e anteprima avviene dopo la guardia. Consultare le altre schede resta possibile. |

Runner migrati: Ripetizione, Estrusione, Trasforma, Filettatura; Offset/Svuotamento, Rigata, Boundary, Riempimento, Planare; Datum; Misura. Il coordinatore conserva l’API a tre argomenti e aggiunge un overload con callback di inizializzazione. Collega `finished` prima di `show()` e dell’inizializzazione, evitando un’attesa infinita se uno dei due chiude subito il pannello. I sotto-dialoghi dello stesso documento restano consentiti.

Il messaggio d’anteprima resta nel footer fisso sopra i pulsanti anche quando `PreviewScope` viene creato dopo l’apertura: un helper comune estrae la riga dal form scorrevole sia all’apertura sia alla creazione tardiva. L’operazione è idempotente e conserva i widget. Ripetizione conserva il picking nello storyboard e ne ripristina callback, abilitazione e barra della storia alla chiusura.

## Verifiche effettive

- Build Release completa di applicazione e runner riuscita; **24/24 suite CTest pertinenti PASS**, compresa l’ellisse per estremi.
- Probe privato con componenti Qt e geometrie effettive: **115 asserzioni PASS**, **11 pannelli realmente aperti e annullati con cleanup verificato**, sia offscreen sia su **Wayland**. Il replay finale collega gli oggetti correnti dell’app sostituendo soltanto il `main`; nessun oggetto di produzione privato sovrapposto.
- 79 asserzioni verificano stati, recupero dello spessore, filettatura, inizializzazione, callback, picker, lock, annullamento, schede in entrambi i sensi e chiusure sincrone. Altre 36 verificano il footer su nove runner: parent fuori dal form, ordine prima dei pulsanti, scorrimento reale e riapertura. Datum e Misura non creano quel messaggio e sono esplicitamente esclusi dalle sole verifiche del footer.
- Controlli negativi conservati: sulla baseline precedente 7 asserzioni funzionali falliscono; la variante privata prima della correzione del footer fallisce tutte le 36 asserzioni aggiunte. I tre controlli della nuova API non sono applicabili alla baseline. Non sono 115 difetti distinti.
- Il primo replay Wayland dava 110 PASS/5 FAIL: il compositor sostituiva le dimensioni richieste dal fixture e cinque form non avevano overflow. La diagnostica conserva dimensioni, esposizione e scroll; la sola attesa dell’esposizione non basta a escludere configure successivi. Il fixture finale limita l’altezza del vero `QScrollArea` di prova, mantenendo le asserzioni su overflow, movimento effettivo del contenuto e posizione del footer. La produzione resta identica. Esiti iniziali, diagnostica e replay finale sono distinti negli artefatti.
- Mappa rigenerata: **762 file, 11023 relazioni, 380 unità C++**, zero diagnostica; `--check`, hash dei sorgenti e della configurazione, HTML/JSON e collegamenti entranti/uscenti verificati. Ricontrollati gli undici passaggi al coordinatore e le due chiamate all’helper del footer.
- **2929/2938 file iniziali invariati**; nove file esistenti modificati nel blocco. Il kernel e i manifest delle prove precedenti dell’ellisse e dell’offset sono preservati.

Le prove native verificano la vita dei widget e il layout. Non costituiscono una certificazione dei pixel OpenGL, di tutti i temi/sistemi operativi o dell’assenza globale di leak; non è stata aggiunta una nuova campagna sanitizer in questo blocco.

## Funzioni orfane e lavoro aperto

Le **22/22 candidate** restano prive di chiamanti o riferimenti a funzione nel repository controllato: **5 private/locali** candidate concrete alla rimozione e **17 API pubbliche** da valutare separatamente. Nessuna rimozione effettuata; non sono 22 bug funzionali. Il [rapporto delle orfane](review-orphans-refresh-2026-11-16.md) conserva definizioni, visibilità, ricerca sui test anche fuori CMake e limiti dell’analisi statica.

L’uniformazione delle anteprime non è dichiarata esaustiva: restano le differenze documentate fra patch e corpo completo, avvio manuale/automatico e riuso alla conferma nelle altre famiglie. K10 mantiene i limiti indicati in apertura. Su richiesta successiva dell’utente, è ora in indagine la booleana di `Supporto.prt`, in particolare Corpo 2/Corpo 3: questa consegna dei pannelli non ne anticipa l’esito.

## Artefatti e riproduzione

[Prove](review-preview-unification-prove-2026-11-16/README.md), con sorgenti dei probe e script salvati come testo, log, manifest degli agenti e audit del coordinatore. Gli esiti degli agenti descrivono la loro fase privata; il replay in `root/` documenta gli oggetti Release effettivamente integrati. `SHA256SUMS` sigilla gli artefatti del blocco. Nessun CAD originale modificato e nessun processo utente chiuso.
