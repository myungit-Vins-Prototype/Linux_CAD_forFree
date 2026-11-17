# K10 quadratura GK: fallback scalato con callback uniche

Solo staging; niente repository, compilazioni/esecuzioni C++, UI, commit/push o copie di librerie/ELF. AGENTS/CLAUDE/politica e grafo descrizioni+call/include/reference entrambi versi letti prima del codice. graph-focus.json conserva solo nodoquadratura e archi incidenti. Root gestisce flags, build/test, rigenerazione mappa e memoria.

## Finding e modifica

Per f=2^1023 su[0,2^-1023] l'integrale è1, ma left+right e gli accumuli grezzi GK possono overfloware; Inf-Inf dà erroreNaN e il confronto adattivo scende fino all'albero2^30. Root riferisce timeout0.3s anche nella chiamata PUBBLICA arcLength su BSpline grado1 nativa (derivata2^1023 finita dopo normahypot). Nessuna prova runtime effettuata da core.

Solo fk_quadrature.h. Ogni pannello conserva15campioni, valutati UNA volta nello stesso ordine/agli stessi parametri ordinari. Accumuli K/G/assoluto/grandezza e formule ordinarie sono mantenuti. Se un accumulo/prodotto relativo ai valori non è finito, ricalcola soltanto quel gruppo normalizzando i campioni e ripristina scala*half*somma con frexp/scalbn; la grandezza vettoriale ha una scala INDIPENDENTE, così un grande estimatore non annulla un valore piccolo già calcolato. Se la normalizzazione annulla completamente un campione non nullo, domain_error conservativo. Non si promette normalizzazione esatta o supporto di ogni combinazione di scale.

Centro e semiampiezza usano le formule originali quando somma/differenza sono finite; solo per overflow passano a mezze coordinate. Midpoint condiviso anche dalla ricorsione. Semiampiezza arrotondata0 su range non degenere e bisezione che non avanza su pannello NON accettato sono rifiutate, senza cap arbitrari o cambi alla profondità30.

Callback nonfinite (anche magnitude negativa) rifiutate con std::domain_error; intervalli reversi restano integrali orientati. I pannelli vettoriali si validano prima dell'accettazione, anche a depth30; le somme errori noise e i risultati dei figli si controllano prima dell'uscita. Nessun Failure comune disponibile nel kernel; include stdexcept diretto, coerente con gli errori numerici di arcLength. Le eccezioni della callback non vengono intercettate.

## Compatibilità e limiti

Nessuna modifica alle tolleranze/cap, al criterio di roundoff o alla profondità30; tol0/negativi e altri valori legacy non ricevono un nuovo contratto. Le callback normali restano15 per pannello, nessun replay in fallback. Il normale percorso mantiene ordine dei prodotti e delle somme, ma la bit-identità va verificata dal root con flags appropriati: sotto fast-math isfinite può essere eliminato; root gestisce i consumatori produttivi cad_mass.cpp/fk_mass.cpp/fk_profile.cpp/fk_curve_algo.cpp/fk_loft.cpp(cache).

Resta stima GK numerica, NON certificato continuo/globale contro picchi fra sonde. Resta aperto l'underflow dei termini pesati quando tutti gli accumuli restano finiti, ad esempio campioni denorm_min su grande intervallo: il fallback non è attivato da quel caso. La semiampiezza non rappresentabile è rifiutata anziché recuperata. Integrale/magnitudine reale non rappresentabile, campioni già nonfiniti o scale miste perse possono essere rifiutati anche se qualche combinazione finale per cancellazione sarebbe rappresentabile. Nessun nuovo algoritmo di integrazione, normalizzazione delle fasi o soluzione generale di range arbitrari.

Core ha effettuato soltanto review statica, ricerca dei chiamanti e verifica di hash/baseline. La candidata finale048c8007 e patchf7fdd060 sono congelate; runtime/regressioni/SAN/app sono PENDING root/tester al freeze. Nessun risultato inventato o trasferito da vecchi header.
