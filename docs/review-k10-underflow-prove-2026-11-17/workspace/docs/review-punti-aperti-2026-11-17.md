# Punti ancora aperti della review — 17 novembre 2026

Inventario consolidato su richiesta dell’utente, dopo le correzioni numeriche K10 e la segnalazione del sottomenu della cache. I rapporti precedenti conservano fotografie storiche: un punto descritto come aperto il 16 novembre può essere già risolto nel seguito. Questo elenco distingue lavoro tecnico residuo, lacune di verifica e miglioramenti proposti; non assegna nuovi ID di bug senza riproduzione.

Il conteggio passa a **34 rilievi funzionali corretti**: 33 precedenti più UI11, riprodotto e corretto nel sottomenu cache e nell’overflow ribbon; due siti della stessa causa, un solo nuovo rilievo. K10 è un punto ampio ancora parziale: i 24 siti iniziali hanno ricevuto interventi almeno parziali, ma questo non significa 24 percorsi integralmente certificati. I successivi casi K10 non sono conteggiati come altrettanti nuovi rilievi indipendenti.

## K10 — sei filoni ancora aperti

| Ordine di lavoro proposto | Punto | Stato effettivo e criterio per chiuderlo |
| --- | --- | --- |
| 1 | Quadratura: criteri residui e rappresentabilità | Overflow e prodotti pesati subnormali/azzerati recuperati e verificati su scalare, vettore, B-spline nativa e cache; le scale miste ancora non risanabili sono rifiutate. Anche la profondità massima ora segnala il fallimento se i criteri non sono soddisfatti: corretto il falso risultato su un segmento razionale di1mm. Restano arrotondamenti della normalizzazione, underflow finale non rappresentabile, criterio legacy del rumore e supporto delle parametrizzazioni difficili. La quadratura resta una stima numerica, non un certificato continuo. [Nuove prove](review-k10-underflow-2026-11-17.md). |
| 2 | Superficie costruita rispetto all’intera mappa richiesta | Il controllo continuo dei bordi non dimostra che l’interno della superficie rispetti la trasformazione voluta. Il fit controlla ancora sonde finite. Occorre un riproduttore finito fra le sonde e un controllo globale per la famiglia scelta; il rigonfiamento C3 annotato in precedenza è solo una proposta non eseguita. |
| 3 | Famiglie geometriche ancora senza controllo continuo | Altri gradi, superfici periodiche e rappresentazioni analitiche/procedurali o quintiche restano fuori dal provider attuale. Alcune conservano il percorso campionato. Identificare per ogni chiamante il comportamento `Unsupported`, aggiungere provider mirati o un rifiuto esplicito quando necessario. |
| 4 | Newton, inversioni e scelta del ramo | Controllare il residuo finale non dimostra convergenza globale o scelta del ramo desiderato. I riporti manuali, incluso `nearPole`, richiedono prima una riproduzione pubblica del problema. Le guardie aggiunte a invertPoint e alla lunghezza delle estensioni restano valide e non vengono riaperte come difetti. |
| 5 | Geometria finale dopo costruzione, ritaglio e unificazione | L’uguaglianza bordo/superficie nel parametro correlato non certifica appartenenza alla regione ritagliata, topologia, iniettività o assenza di autointersezioni. Restano da verificare i casi fuori dai registri di genealogia già supportati, incluse ricostruzioni o riparametrizzazioni successive. |
| 6 | Rifiuti conservativi e costo dei casi complessi | Limiti di lavoro, catene di wrapper lunghe e riparametrizzazioni non supportate possono rifiutare geometrie valide. Sono limiti dichiarati, non automaticamente errori. Migliorare supporto, diagnosi e costo su casi misurati, mantenendo i budget geometrici originari. |

Fonti aggiornate: [quadratura](review-k10-quadratura-2026-11-17.md), [fit finito](review-k10-mapping-fit-finiti-2026-11-17.md), [wrapper e perimetro attuale](review-k10-wrapper-curve-2026-11-17.md), [Mapping nonPlane](review-k10-mapping-nonplane-2026-11-17.md), [inversione periodica](review-k10-inversione-periodica-2026-11-17.md), [lunghezza delle estensioni](review-k10-estensione-lunghezza-2026-11-17.md).

Già incluse nel perimetro riconosciuto: curve E cubiche e Q lineari/cubiche a pesi positivi variabili; carrier NURBS nonperiodici di grado 2/3 per direzione, nativi o istanziati; Trim E/Q e trasformazioni affini E fino al limite documentato 32. Non vanno rimessi nell’elenco generico «razionali, istanze e wrapper da implementare».

## Altri punti della review

| Punto | Cosa rimane | Classificazione |
| --- | --- | --- |
| Pannelli e anteprime | Completare il confronto fra funzioni per patch/corpo completo, avvio manuale/automatico e riuso alla conferma; rendere esplicite le differenze giustificate dal costo. PRE1–8 e i runner già migrati rimangono corretti nei rispettivi perimetri. | Uniformazione residua; non una nuova lista di bug dimostrati. |
| Stili e menu | Corretto UI11 in questo intervento: sottomenu cache e overflow ribbon usano CadMenu. Resta il confronto complessivo dei pannelli e degli altri stili, senza presumere altre regressioni. | Segnalazione concreta dell’utente; altre incoerenze richiedono riscontro prima di essere dichiarate bug. |
| Memoria e GPU | Profilazione prolungata con apertura/chiusura di documenti, annullamenti e carichi GPU; verifica delle perdite dell’intera app. M1/M2 sono corretti e i sanitizer mirati non sostituiscono questa campagna. | Lacuna di verifica: nessun leak permanente dell’app dimostrato. |
| Modellazione libera | Restano limitati ai casi verificati raccordi liberi, sformo generale, collasso dei raccordi nello svuotamento e alcune geometrie di contorno apparente. Le reti U/V sono Coons G0, non Gordon globale G1/G2. | Limiti funzionali generali, distinti dai riproduttori già corretti. |
| Configurazioni e rilascio | Verifica visiva nativa macOS e stili desktop non provati; GPU/hardware e Windows fuori dal perimetro certificato. Checkpoint18d0adc: build Linux/macOS PASS, test FAIL su profilo/pose. Riparazioni pubblicate in78d7268, nuova CI in corso; il recupero underflow/arresto resta locale. | [Stato CI e checkpoint](review-checkpoint-push-2026-11-17.md); verifica visiva nativa separata. |
| Documenti aggiornati | Supporto/K11 è verificato sulla versione congelata e sulla copia consegnata; l’originale successivo con SHA 70e4ec5c… è diverso e non eredita quei PASS. Ricontrollarlo se è la versione ancora interessata dal problema. | Ricontrollo condizionato alla versione, non K11 automaticamente riaperto. |

Fonti: [pannelli e anteprime](review-preview-unification-2026-11-16.md), [UI9/UI10](review-ui-colori-api-2026-11-16.md), [memoria e copertura](code-review-2026-11-15.md#funzioni-orfane-e-memoria), [Supporto](supporto-booleana-2026-11-16.md), [booleane e tema](review-boolean-order-tabs-2026-11-17.md), [registro](../CLAUDE.md).

## Punti da non considerare ancora da correggere

- **Orfane iniziali:** 22 candidate tutte classificate; 16 rimosse, 6 query/cataloghi conservati intenzionalmente. Non rimangono 17 API legacy da decidere. Una nuova ricerca più estesa è attività di review, non prova di altre orfane. [Esito finale](review-legacy-api-2026-11-16.md).
- **Tema schede:** UI8–10 corretti, schede e tema Wayland ricontrollati con PASS il 17 novembre. Resta la copertura visiva di altri sistemi; i timeout storici non sono una regressione corrente dimostrata.
- **K2:** in produzione e verificato, non lasciato sperimentale. **Ellisse per estremi:** implementata con quattro estremi quotabili/vincolabili.
- **Superfici scala/posa:** riproduttore corretto nei casi documentati. **Flacone3 inverso:** fallimento storico non riprodotto sugli stessi record; resta esplicita la connessione tollerante, non una nuova patch da fare senza un caso negativo.
- **K11 Supporto:** correzione e copia unita verificate sulla versione delle prove; distinguere il file successivo indicato sopra.

## Ordine di prosecuzione

Dopo UI11, recupero underflow e arresto alla profondità massima, passare al controllo globale del carrier2. Il lavoro numerico1 resta limitato alle questioni residue esplicitate sopra. Successivamente famiglie non supportate/Newton e verifica del B-rep finale; in parallelo solo analisi in lettura sugli altri filoni. Test pertinenti alle modifiche, compilazioni e analisi seriali. Nessun test UI ripetuto per modifiche soltanto kernel.

La [roadmap funzionale generale](roadmap.md) contiene anche nuovi strumenti e miglioramenti (saldature strutturate, pacchetto autonomo di rilascio, ulteriori casi HLR, prestazioni e budget GPU). Sono sviluppi separati, non bug pendenti di questa review.

Checkpoint18d0adc pubblicato su richiesta dell’utente; il successivo intervento underflow ha prove kernel mirate e resta locale. Nessun CAD originale ricontrollato in questo passo e nessuna modifica alle prove precedentemente sigillate.
