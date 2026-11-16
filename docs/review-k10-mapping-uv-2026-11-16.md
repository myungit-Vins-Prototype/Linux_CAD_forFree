# K10 — trasferimenti UV Mapping/Quilt

Correzione integrata e verificata il 16 novembre 2026 con tre agenti **GPT-6.1 Sol**. App completa ricompilata; riaprirla per usare la nuova build. **K10: 21/24 siti iniziali parzialmente trattati, tre interamente da migrare.** Il totale della review resta **31 rilievi corretti**: le due famiglie ampliano il rilievo K10 già aperto.

## Problema riprodotto

`SurfaceQuiltMap::bend` può rifiutare una piegatura valida su Plane quando cambia soltanto il riferimento delle coordinate UV. La geometria del target resta vicina all’origine, nel dominio nominale, ma l’origine del supporto e le SP-curve/anchor sono traslate coerentemente fino a U≈10.000. Il fit dei contorni UV per costruire l’offset richiedeva `1e-12` nelle coordinate parametriche e finiva il raffinamento. L’offset Plane è invece una traslazione analitica e non usa quei bounds.

Riproduttori reali: sorgente NURBS razionale con trim, quota 0,2 e offset normale 0,3, nelle pose identità e trasformata. Baseline: entrambi rifiutati. V3: entrambi riescono con `checkBody` senza errori, intervallo originale e genealogia conservati. Le altre 18 varianti erano già valide e rimangono verifiche di compatibilità: non sono 18 bug.

## Correzione e contratto

- `kernel/fk_quilt_mapping.cpp`: per offset Plane usa direttamente la costruzione analitica con il carrier originale. Il fit del dominio resta nel fallback non Plane.
- `kernel/fk_surface_mapping.cpp`: quando `MappingFaceImage.surface` è Plane, ricostruisce la SP-curve dal **bordo 3D effettivamente emesso**, con `fitPCurveOnPlane`, stesso parametro originale e metrica fisica. Non converte parametri angolari in NURBS assumendo che rimangano uguali.
- Dopo `Body::build` / `Body::buildSheet`, identifica le fin tramite genealogia dichiarata di faccia/edge e multinsieme del puntatore SP-curve con il budget esatto pianificato. Ricontrolla carrier, range originale, tutte le campate dei coefficienti realmente emessi e limite pubblicato. Nessun riconoscimento per sola vicinanza.
- Storia e deviazione vengono pubblicate dopo tutte le verifiche e l’ultimo controllo di cancellazione. Nei fallback senza obblighi Plane e senza richiesta di storia non aggiunge la relativa ricostruzione.
- Il CMake aggiunge `fk_surface_mapping.cpp` ai moduli compilati con `-fno-fast-math;-ffp-contract=off`; Quilt era già incluso. Il confronto dei comandi mostra **una sola modifica a un comando esistente**, più il nuovo test.

Budget fissati prima della costruzione: `connectionBudget=min(tolerance,1e-7)`, fit bordo `0.2*connectionBudget`, cap fin `0.8*connectionBudget`. Il fit proiettato Plane ha target fisico `min(1e-10,0.05*fitBudget)` mm; la fin pubblica il limite continuo ottenuto dentro il cap, senza il fattore aggiuntivo 1,01 del vecchio massimo campionato. UV e millimetri sono grandezze diverse: non si dichiara equivalenza generale delle rispettive soglie per frame arbitrari.

Il candidato V2 privato usava `1e-9` come target fisico: è stato scartato **prima dell’integrazione**. La V3 mantiene il massimo 1e-10 mm; di conseguenza i casi a UV≈100.000/1.000.000 restano rifiuti conservativi e atomici. Non sono successi rivendicati né errori nascosti aumentando una tolleranza.

## Prove eseguite

| Verifica | Esito |
|---|---|
| Suite mapping precedente sulla baseline | 4/4 CTest PASS |
| Riproduttori pubblici | Baseline: due rifiuti; V3: due successi |
| Nuovo test permanente V3 | 21/21 gruppi PASS: 20 corpi, più gruppo di atomicità con due cancellazioni e quattro rifiuti conservativi |
| CTest integrati selezionati | **26/26 PASS**, 71.65 s |
| Runner kernel Release integrato | 21/21 PASS; **20 corpi byte-identici** al candidato V3 congelato |
| ASan/UBSan/LSan mirati | 21/21 PASS, senza diagnostiche o soppressioni; driver, Mapping, Quilt e `fk_curve_fit_bounds.cpp` strumentati, resto della libreria Release |
| Fraction indipendente | 80 fin finali / **27.089 campate complete** entro ciascun budget pubblicato; limite massimo ≤1,67819e-8 mm |
| Fit proiettato separato | Tutte le 80 fin ≤1e-10 mm; massimo limite ≤7,58040e-11 mm |
| Negativo del verificatore | Un polo UV alterato di 1e-4 viene respinto con testimone razionale fuori budget |
| Wayland sul runner integrato | Mapping, anteprime e 1.569 controlli del tema PASS; non è una certificazione di ogni pannello |
| App completa e avvio | Build PASS, 306.78 s; avvio Wayland isolato cinque secondi, `/proc/.../exe` e launcher verificati |
| Libreria kernel | Solo i due oggetti Mapping/Quilt cambiano; **91 oggetti identici** alla baseline |
| Architettura | 775 file / 11246 relazioni / 387 TU; zero errori, hash/HTML/compileDB e collegamenti in entrambi i versi PASS |

Il test permanente non contiene macro per disattivare il nuovo certificato. La diagnostica storica della baseline è una **copia privata separata**, che omette soltanto l’asserzione nuova del provider continuo: 19 gruppi passano e i due falsi rifiuti geometrici rimangono. I controlli fisici, di budget, range, immutabilità e codec sono uguali. La prova Fraction conferma che i 72 residui delle prime 18 varianti baseline erano già validi; non si attribuisce loro una falsa accettazione.

Il verificatore Fraction legge i coefficienti IEEE esportati come razionali esatti, inserisce i nodi e usa de Casteljau/Bernstein su tutte le campate. Non chiama il provider C++ né assume l’identità matematica di un frame normalizzato in floating point. La proiezione fisica e il residuo completo sul Plane sono certificati separatamente. Le 80 fin sorgenti sono verificate entro la risoluzione esplicita 1e-7 mm (limite massimo 4,16334e-14), senza rivendicare esattezza algebrica a budget sorgente zero.

I 26 CTest coprono Mapping, carrier, diagonali e flatten, fitter/provider/tolleranze, K12, pannelli/anteprime, storyboard/storia, schede, tema ed ellisse. Il primo lancio LSan dentro sandbox si ferma per il limite ptrace: log conservato ed escluso dai PASS. Stesso binario e opzioni fuori ptrace: PASS. Non costituisce una certificazione globale di assenza di leak.

## Limiti e prosecuzione

La relazione finale edge/SP-curve sul Plane è ora continua. **Il fit del bordo 3D rispetto alla mappa sorgente resta campionato**, così come fit generale delle superfici e guardie Jacobiano. Il nuovo certificato non dimostra fedeltà continua dell’intera mappa, iniettività, appartenenza continua al trim, topologia globale, volume o assenza di autointersezioni. Il controllo è dopo `Body::build/Sheet`: in questo percorso non viene chiamato `unify`, quindi non gli si attribuiscono garanzie.

Restano i carrier non Plane, i casi non riconosciuti come immagine Plane e le facce che attraversano più chart senza conservare il carrier. UV molto grandi possono superare le risorse o i limiti di arrotondamento del certificato e venire rifiutati. Nessuna mesh entra nei calcoli geometrici.

Tre siti iniziali K10 ancora interamente da migrare:

1. `fk_blend_mitre.cpp`: giunti con parametri risolti sui supporti;
2. `fk_blend_surface.cpp`: continuazione implicita dei raccordi;
3. `fk_sheet.cpp`: giunzione delle superfici estese.

I 21 siti affrontati restano parziali nelle famiglie/fallback dichiarati. Restano anche il caso trasformato di `Superfici.prt`, 17 API pubbliche candidate orfane e l’uniformazione dei colori letterali. Questo blocco non modifica formati persistiti, file CAD originali, interfaccia o preferenze dell’utente.

## Preservazione

Baseline di **4.232 file** congelata prima delle modifiche, descrizioni e 1.001 archi consultati prima del codice. Hash, modifiche previste e nove sigilli precedenti ricontrollati in `root/final-audit.json`. [Prove e sigillo](review-k10-mapping-uv-prove-2026-11-16/README.txt), [memoria precedente completa](history/claude-prima-k10-mapping-uv-2026-11-16.md). Sorgenti delle prove archiviati come testo inerte `.txt`; esclusi ELF, oggetti, librerie e CAD binari. Nessun commit, push o modifica di processi utente.
