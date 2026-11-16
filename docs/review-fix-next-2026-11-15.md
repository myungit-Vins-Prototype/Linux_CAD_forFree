# Prosecuzione della review — 15 novembre 2026

> Aggiornamento successivo: **K2 è stato completato e integrato in produzione**. [Implementazione, verifiche e limiti](sweep-k2-completamento-2026-11-15.md). Il testo seguente conserva lo stato di questo intervento precedente.

Continuazione autorizzata della review generale con gli stessi criteri e due agenti **GPT-6.1 Sol**, riutilizzati per kernel, solver e lifecycle dei pannelli. Il coordinatore ha integrato le patch, corretto unità/stili/anteprime e svolto la verifica combinata. Nessun commit/push, nessun CAD originale modificato e nessun processo utente chiuso.

## Stato dei rilievi

Questo intervento corregge **14 ulteriori rilievi**: K4/K5/K6, S1/S2, UI1–6 e PRE1–3. Con M1/M2 e K1/K3 già corretti, sono **18 dei 19 difetti confermati nella review iniziale**. Resta aperto **K2**, controllo delle autointersezioni globali degli sweep. Le 22 funzioni senza chiamanti restano candidati manutentivi separati: la review non le conta come bug e questo intervento non le elimina automaticamente.

| Rilievo | Causa e comportamento corretto | Prova principale |
|---|---|---|
| K4 | Un polo di controllo esterno non è un punto del profilo razionale. La positività del controllo locale viene verificata sulla curva mediante coefficienti Bernstein e suddivisione; i veri piegamenti restano rifiutati. | 18 costruzioni equivalenti per rappresentazione, frame, scala e posa; 12 piegamenti reali rifiutati; sweep 19/19 |
| K5 | La soglia di chiusura cresceva con la distanza dall'origine. Ora usa la tolleranza di connessione scalata dall'accuratezza del documento. | Sezioni 2D/3D aperte a X=0/1e4/1e6, scale0,1/1/10; gap sotto/sopra soglia, due accuratezze; area e checker |
| K6 | reverseEdge assumeva due fin, modificando il bordo prima dell'eccezione sulla lamina. Ora prepara curve/pcurve prima della mutazione e aggiorna soltanto le fin presenti. | Lamina e box, tutti i bordi invertiti due volte; geometria delle fin, pcurve, checker e ripristino |
| S1 | wrapAngle sottraeva ripetutamente un giro anche quando il double non cambiava più. Ora riduce con fmod e converte in radianti dopo modulo360. | ±1e20, ±DBL_MAX, ±180, ±540 e molti giri: soluzione e codec, denso/sparso |
| S2 | max ignorava NaN nei residui. Guardie su definizione, residui, Jacobiani e passi impediscono falsi successi; il commit geometrico avviene su copia validata. | NaN/±inf, overflow da coordinate finite, blocchi senza incognite, target/Fix/NURBS/pattern/Pierce; codec e fallimento atomico |
| UI1 | I tre fattori del Riempimento ereditavano la conversione di lunghezza. Ora sono adimensionali. | Dialogo reale in mm/cm/m/in/ft: digitare1 restituisce1 |
| UI2 | I loop annidati in documenti diversi ritardavano distruttori e sblocco del primo. La guardia centrale impedisce il secondo runner cross-document. | Due workspace e DocumentTabsWindow reali: cleanup del comando rifiutato, primo ancora attivo, sblocco e riapertura del secondo |
| UI3 | Mapping/pattern cancellavano callback senza terminare la selezione. Cleanup idempotente su finished e ritorno, con cancelReferencePick. | Pannelli reali, annullamento durante selezione; callback/marks/picker e lock puliti |
| UI4 | Cambiare continuità/ruolo lasciava il vecchio picker attivo. Il cambio lo termina e azzera il pulsante. | G1-facce→G0 e piano→facce, nuova selezione avviabile |
| UI5 | Colori locali chiari restavano uguali nel tema chiaro. Ruoli semantici comuni aggiornano informazioni, successo, errore e suggerimenti. | Contrasto minimo4,5:1 e cambio scuro→chiaro→scuro su pannello aperto |
| UI6 | Il refresh conservava soltanto la riga corrente. Ora ripristina separatamente selezione multipla e current per identità del riferimento. | Refresh, riordino, rimozione, current non selezionato e punto di appoggio aggiornato |
| PRE1 | Estrusione senza nuovo riferimento lasciava forma/chiave pronte. Ora cancella il lavoro e mostra l'attesa del riferimento. | Preview valida→nuovo job→limite mancante; invalidazione immediata e nessuna ripubblicazione tardiva |
| PRE2 | Loft incompleta lasciava messaggi di calcolo/pronta. Ora elimina la preview e chiede almeno due sezioni. | Apertura con1sezione, passaggio a2 e ritorno a1 dopo risultato valido |
| PRE3 | Mapping sostituiva sempre la prima sorgente. Ora sviluppo/rivestimento usano hiddenOperands come il risultato finale. | Modi3/4, copia sì/no e sorgenti singole/multiple |

## Decisioni e limiti

**Un comando modeless alla volta fra documenti.** Il cambio scheda e la consultazione restano consentiti. Se si tenta un secondo comando, esso viene annullato e la barra di stato spiega che bisogna terminare il primo. L'annidamento nello stesso workspace resta compatibile. È una soluzione esplicita al difetto UI2; non è una conversione di tutti i dialoghi a lifecycle asincrono. Sbloccare soltanto A su finished sarebbe stato insufficiente: i suoi distruttori sospesi avrebbero potuto cancellare callback e preview di un nuovo comando avviato in A.

K4 certifica la sezione continua ai parametri del percorso esaminati dal costruttore; **non certifica l'assenza di autointersezioni globali**. K2 non e stato corretto: resta necessario un validatore geometrico generale. L'[analisi dedicata](sweep-review-prove-2026-11-15/K2-analisi.md) spiega perché aumentare i campioni o usare l'assenza di intersezioni trovate come prova di validità sarebbe insufficiente.

Le guardie numeriche riguardano la definizione degli schizzi, non ogni double delle altre feature. Il formato nativo non cambia. I grandi valori finiti angolari restano ammessi; NaN/infinito vengono rifiutati in lettura e scrittura senza modificare la destinazione. constraintError valida geometria e solo il vincolo richiesto, anche esterno alla lista. La validazione aggiunge un costo: microbenchmark agente su1000vincoli, giro diagnostico completo16,1→20,9ms; non è stato dichiarato un miglioramento delle prestazioni.

Le tre incoerenze di anteprima confermate sono corrette. Rimangono le differenze di flusso descritte nella [matrice delle15famiglie](review-2026-11-15/anteprime.md): avvio manuale/automatico, ritardi, patch trasparente/corpo completo e riuso finale. Un sistema unico di stati per tutte le famiglie resta una proposta più ampia, non viene presentato come implementato da queste correzioni.

## Verifiche combinate

- **Build completa Release dell’applicazione e dei runner PASS**; confermata nuovamente dopo l’ultima correzione del test. L’eseguibile `forgecad-release/forgecad` su disco è aggiornato; occorre riaprire l’app per utilizzarlo.
- **34/34 CTest PASS** nella batteria finale, 5,01s. Include nuovi test dei pannelli/anteprime, solver e codec, compatibilità storia/Undo/Redo, unità, temi, riferimenti e costruzioni loft/sweep/superfici. Elenco e log nelle prove.
- La prima batteria era33/34: il controllo `influence->value()==55` in edge-loft fallisce anche sulla snapshot precedente, con `55.000000000000007`. ExpressionSpinBox conserva intenzionalmente la precisione interna. Corretto il solo confronto del test con errore assoluto<1e-12; nessun arrotondamento aggiunto al prodotto. Log baseline/attuale conservati.
- **Mappa724file /10429relazioni /353unità C++, zero diagnostica**. Generazione e `--check` PASS; dati HTML incorporati identici al JSON, descrizioni aggiornate e sei relazioni rilevanti verificate in entrata/uscita (codec→solver, ruoli messaggi, preview→hiddenOperands, runner e controllo Bernstein).

- Kernel: **74 test distinti /75 esecuzioni PASS**; filtri Sweep19, Loft43, ReverseEdge1, PCurve11, Euler1, con un caso in comune.
- Agent S1/S2: matrice numerica, solver partizionato,96 casi tangenti, codec e Undo/Redo PASS; stesso probe sulla baseline fallisce sul primo NaN.
- Agent UI:34 verifiche lifecycle PASS, incluse schede reali. Controllo statico indipendente delle modifiche PRE1–3/UI1/UI5 senza ulteriori regressioni concrete.
- Preferenze e cache Qt isolate in directory temporanee. Offscreen verifica eventi/widget/policy; non certifica il rendering OpenGL né la visibilità finale in pixel dei corpi.
- Non eseguita l'intera suite del progetto né una nuova sessione LeakSanitizer dell'app. I test di memoria dei precedenti interventi restano prove storiche, non nuove esecuzioni.

Sorgenti e regressioni sono nei moduli esistenti; le [prove](review-next-prove-2026-11-15/README.md) conservano log, elenco dei test, audit degli hash e dettagli degli agenti. HTML/grafo architetturale sono locali e ignorati da Git. Le modifiche preesistenti a cache, sweep, massa, tangenze e relativi test sono state conservate.
