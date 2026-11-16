# Prossimi punti aperti della review — 16 novembre 2026

Audit in sola lettura durante la CI del commit `046685e`, con due agenti GPT-6.1 Sol. Nessuna correzione aggiuntiva, test o build attribuita a questa fase; nessun cambiamento al codice verificato da GitHub. Totale31rilievi corretti invariato.

## Colori dell'interfaccia

Confermati staticamente due gruppi di foreground UI indipendenti dal tema:

- `forgeCad2026_gui.cpp:1809`: Visualizza → Parametri della sezione, messaggio e descrizione del piano con `#9fc6e8`.
- `forgeCad2026_dialogs_analysis.cpp:565,569,618`: Analisi → Proprietà di massa, etichette, nota e errore con colori HTML inline. Il cambio della palette del pannello non sostituisce quei formati.

Il separatore Raccordo, `forgeCad2026_dialogs_fillet.cpp:499`, resta candidato: il letterale è fisso ma la resa dipende dallo stile del frame. Non è un ulteriore bug visivo dimostrato.

La propagazione globale UI8 resta corretta; questi sono override locali residui. Nessuna misura nuova di contrasto o pixel, nessuna conclusione su tutti i temi desktop. Carta, contenuto tecnico e colori scelti dall'utente sono esclusi. Il prossimo intervento proposto sostituisce gli override UI con ruoli semantici e verifica il pannello aperto durante il cambio tema.

## API candidate orfane

Ricontrollate17/17API: nessuna call/reference entrante nel grafo e nessun uso osservato negli773file attivi cercati, inclusi test/backend opzionali. Il grafo778/11369/390TU non prova l'assenza di consumatori esterni.

Valutazione proposta, non implementata: conservare6query/cataloghi utili; deprecare7ingressi legacy; valutare la rimozione di4wrapper duplicati (`CadViewport::basisMatrix`, `normalizedViewAngle`, `extrusionNames`, `ForgeCad::usesSketch`). Gli omonimi attivi della camera e `sketchesOf` restano necessari. Nessuna rimozione di strumenti o dati persistiti proposta per propagazione.

Due catene richiedono una modifica coerente: callback Estendi e provider della tavola. Togliere da sola l'action legacy della tavola lascerebbe una dereferenziazione `findChild` nel contenitore. Non è un errore corrente riprodotto: è il rischio concreto di una pulizia incompleta. Getter senza consumatori non sono bug funzionali.

[Inventario completo API](review-prossimi-punti-prove-2026-11-16/api/README.md), [rilievi UI](review-prossimi-punti-prove-2026-11-16/ui/README.txt), [provenienza e sigillo](review-prossimi-punti-prove-2026-11-16/README.txt).
