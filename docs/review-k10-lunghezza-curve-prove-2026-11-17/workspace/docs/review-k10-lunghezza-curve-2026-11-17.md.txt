# K10: lunghezze delle curve e cache del loft — 17 novembre 2026

Corretto il calcolo della velocità in `arcLength` e nella cache usata dal loft. Una retta B-spline lunga **1 mm**, con gli stessi poli e una diversa scala del parametro, poteva risultare lunga **zero** oppure portare la quadratura a un numero enorme di suddivisioni. Ora restituisce 1 mm in entrambi i casi riprodotti. Aggiunti controlli numerici e cancellazione nei campioni, senza cambiare la quadratura o le tolleranze.

Implementato e verificato localmente, senza commit/push. Prosecuzione di K10: i 33 rilievi funzionali della review rimangono invariati; K10 generale resta parziale.

## Riproduzioni native

Il coordinatore ha eseguito l’API pubblica su B-spline di grado 1, poli `(0,0,0)` e `(1,0,0)`, nodi `[0,0,h,h]`. È lo stesso segmento geometrico di 1 mm, con coordinate ordinarie, poli/nodi finiti e dominio nativo valido. L’auditor ha verificato l’algebra dyadica con Fraction, senza eseguire C++.

| Caso | Baseline | Candidata V2 |
| --- | --- | --- |
| `h=2⁶⁰⁰`, derivata finita `2⁻⁶⁰⁰` | Il quadrato va in underflow; norma e lunghezza 0 | Lunghezza 1 mm |
| `h=2⁻⁶⁰⁰`, derivata finita `2⁶⁰⁰` | Il quadrato trabocca; norma +∞, watchdog a 0,3 s, exit124 | Lunghezza 1 mm |
| Retta semplice, token di annullamento già attivo | Restituisce 1 ignorando il token | `OperationCancelled` |

Nella baseline la velocità infinita rende `Inf−Inf` l’errore stimato Gauss–Kronrod: NaN non soddisfa i criteri di arresto. Il codice può visitare l’albero fino alla profondità 30, potenzialmente `2³¹−1` pannelli; il watchdog osserva il mancato ritorno entro il limite, mentre l’oracolo statico ne spiega la causa. **Non viene chiamato ciclo infinito**: la profondità è limitata, ma il lavoro è sproporzionato.

La cancellazione della base B-spline era già presente nella valutazione: non si attribuisce alla patch un precedente blocco di annullamento per tutte le famiglie. Il caso di annullamento ignorato è stato riprodotto sulla retta. L’annullamento all’ingresso della cache esisteva già ed è conservato.

## Implementazione

Due file di produzione: `kernel/fk_curve_algo.cpp` e `kernel/fk_curve_length_cache.h`. Quando `squaredNorm(derivata)` è normale, rimane l’espressione originale `sqrt(dot)`. Per quadrati subnormali, azzerati o traboccati, con vettore non nullo, viene usato `std::hypot` 2D/3D. Una derivata realmente nulla conserva velocità zero; una derivata o una norma non rappresentabile viene rifiutata.

I callback controllano cancellazione e finitezza di parametro, derivata e velocità. Si verificano anche ampiezza dell’intervallo, suddivisione, integrali e somma prima del ritorno o dell’inserimento in cache. Non si richiede finitezza della posizione `d[0]`, che non entra nell’integrando della lunghezza. I range puntuali conservano il risultato zero.

Partizioni, ordine delle somme, algoritmo/pesi/profondità Gauss–Kronrod, chiavi, hit ed eviction della cache restano invariati. Nessun nuovo contratto per la tolleranza: sono preservati anche zero e valori negativi con il criterio storico di arrotondamento, verificati sui controlli. Nessun aumento di tolleranze o limite arbitrario alle velocità. I prefissi finiti già calcolati possono restare in cache dopo un errore; non si promette una transazione sull’intera richiesta, ma nessun risultato non finito viene inserito.

Gli header pubblici e `fk_quadrature.h` sono invariati. `fk_curve_algo.cpp`, il chiamante produttivo `fk_loft.cpp` e il nuovo target sono compilati con le opzioni numeriche esplicite che preservano controlli finiti e ordine delle operazioni. La modifica delle opzioni ha richiesto una ricompilazione completa del kernel in questa build, completata serialmente.

## Verifiche correnti

Tre agenti GPT‑6.1 Sol per candidata, audit e test, in staging. Integrazione e tutte le compilazioni/esecuzioni del coordinatore; nessuna copia aggiuntiva della libreria statica.

| Verifica | Esito |
| --- | --- |
| Nuovi gruppi: controlli finiti, cancellazione, riparametrizzazioni, derivate non finite, range, cache dopo errore | 6/6 PASS |
| ASan/UBSan con leak detection attiva | 6/6 PASS, nessuna segnalazione |
| Confronto ordinario prima/dopo, otto righe di valori esadecimali diretti e cached | Bit-identico |
| Build completa app e target pertinenti | PASS |
| CTest: nuovo target, derivate B-spline, lunghezza estensione | 3/3 PASS, nessuno skip |
| Storici selezionati: 2 ArcLength, 1 cache, 2 SheetExtend e 3 Loft | 8/8 PASS |
| Architettura, hash/configurazione/HTML, typed due versi e `--check` | PASS, 801 nodi / 11726 relazioni / 407 TU |

La TU comprende curve 2D/3D, NURBS razionali C0 con lunghezze note, trim e trasformazioni affini, parametri `2^±600` e `2^±535`, assi e diagonali con lunghezze indipendenti 1/3/5 mm. La baseline della nuova TU ha eseguito **solo `SafeControls` (1/1)**; i casi patologici sono stati provati separatamente con watchdog. Non si attribuisce un confronto baseline 6/6.

ASan/UBSan strumentano la TU produttiva, la nuova TU con cache inline e il main; resto kernel Release. Non è un’analisi completa dei leak dell’app. Nessun test UI o avvio grafico, suite completa483, nuova CI, commit o push. Il binario sul disco è aggiornato; l’app aperta non è stata riavviata.

La mappa ha richiesto la rilettura di tutte le407TU perché la sua cache usa un’impronta globale degli header; il cambio dell’header privato invalida tutti i precedenti risultati. Anche questa analisi è seriale. Un tentativo di verifica partito prima del termine ha osservato lo snapshot precedente: ripetuto soltanto dopo la conclusione del generatore, senza modifiche software aggiuntive.

## Limiti e prosecuzione

La quadratura mantiene errori stimati e arresto a profondità30: non diventa una certificazione continua globale. Il recupero locale della norma non cambia gli altri usi di `norm` nel kernel né protegge ogni intermedio della quadratura condivisa. Restano da approfondire gli accumuli interni Gauss–Kronrod con valori prossimi ai limiti del double; un’ipotesi per il prossimo giro è salvata come proposta inerte, **non compilata o eseguita**, nelle prove.

Derivate non rappresentabili, come nel test nativo con `h=2⁻¹⁰³⁰`, sono rifiutate; non si promette di risolvere tutte le parametrizzazioni. Il campione con parametro non finito prodotto da un midpoint traboccato viene rifiutato conservativamente, senza introdurre una formula generale di midpoint robusto. Fit/carrier globale, Newton e famiglie non coperte di K10 restano aperti.

## Conservazione

Eliminati solo 14 ELF/oggetti privati ricreabili: 34,850,640 byte, circa 33.2 MiB. Sorgenti/log/hash conservati; nessun CAD o cartella QA dell’utente rimossi.

Audit finale sui7.800file baseline: sole6modifiche esistenti attese,7.794preservati, più nuovi file dichiarati. Verificati37pacchetti precedenti/5.170artefatti. HEAD/origin `47c7c19`, index vuoto. Generati architetturali ignorati non aggiunti a Git. La V1 limitata al rifiuto è rimasta in staging sigillato; la versione integrata è V2, che recupera anche i due casi rappresentabili.

[Memoria precedente completa](history/claude-prima-k10-lunghezza-curve-2026-11-17.md), [prove e comandi verificabili](review-k10-lunghezza-curve-prove-2026-11-17/README.txt), [review generale](code-review-2026-11-15.md). Prove solo testuali con sorgenti inerti e `SHA256SUMS` sui percorsi effettivi. La CI storica non certifica le modifiche locali.

Eseguibile SHA‑256: `d9fb5a313317a9d09cc19ff9ebd4d86ca5817cfc81779a291fdff7c0143728e6`.
Kernel SHA‑256: `444966aa6efc69324d8f8b6d1aeac0e3f504aaca50c5143f298f54c9d893898c`.
