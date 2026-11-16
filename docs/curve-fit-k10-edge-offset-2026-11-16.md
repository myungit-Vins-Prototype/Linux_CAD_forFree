# K10 — offset dei bordi di estrusione, 16 novembre 2026

Prosecuzione della revisione dopo l’[ellisse per estremi](ellisse-estremi-2026-11-16.md), su richiesta dell’utente. Tre agenti GPT-6.1 Sol hanno preparato implementazione, regressioni e certificazione matematica indipendente. Trattato un ulteriore sito iniziale del fitter, l’offset generico dei bordi, per singola fin su estrusione e SP-curve affine. **K10 rimane aperto: 15/24 siti iniziali parzialmente trattati, 9 interamente da migrare, oltre ai fallback.**

## Difetto dimostrato

Un’estrusione regolare con generatore NURBS polinomiale di grado27 viene offsettata di 0,001 mm, con tolleranza 1e-7 mm. Sorgente e risultato precedenti superano `checkBody`, ma i due bordi longitudinali perdono un dettaglio fra i parametri controllati dal fitter: scarto misurato circa **5,74247e-7 mm**, oltre cinque volte il budget.

Una prova indipendente tratta gli input IEEE come frazioni esatte. A un parametro razionale, un punto dell’offset originale ha Z maggiore del massimo globale dei poli di **tutti e quattro i bordi restituiti** di almeno **5,7424740886275e-7 mm**. I pesi positivi garantiscono l’inviluppo convesso: è un limite inferiore di distanza geometrica dall’unione dei bordi, indipendente dalla corrispondenza dei parametri. Non è soltanto una differenza fra campioni omonimi.

La baseline privata ricompila `fk_offset.cpp` precedente e lo collega al resto del kernel Release; il modulo precedente non invoca il nuovo provider. Gli otto scenari pubblici della nuova regressione falliscono per errore geometrico sulla baseline e passano con la correzione. I dieci casi del nuovo contratto sono verifiche aggiuntive, non dieci difetti indipendenti.

## Contratto geometrico

Il bersaglio certificato è `F(t)=E(t)+d*normalize(G'(U(t)) cross D)`:

- E è il bordo 3D originale, con i suoi poli, pesi, nodi e wrapper.
- G è il generatore originale della superficie di estrusione; D è la direzione effettivamente memorizzata dal supporto.
- U(t) deriva dalla SP-curve originale; il segno di d comprende il senso della faccia.

E e G rimangono indipendenti: una tolleranza della fin non autorizza a sostituire il bordo con il generatore o con una curva ricostruita sul supporto. Invertire U cambia il percorso, senza invertire la normale del supporto. La coordinata V viene validata ma non modifica la normale dell’estrusione.

Il nuovo `extrusionEdgeOffsetErrorBound` conserva coefficienti posseduti. Supporta rette e NURBS a pesi positivi, wrapper Trimmed/Transformed e SP-curve Line2 o degree1 con pesi uguali per tratto, anche Trimmed. La partizione include nodi del bordo, giunti UV e preimmagini dei nodi del generatore. Le trasformazioni e i pullback mantengono gli originali con intervalli arrotondati verso l’esterno; nessun polo trasformato intermedio arrotondato diventa la fonte del certificato.

La formula del residuo conserva separatamente posizione e normale. Con E=H/W, candidato cubico Q e numeratore normale A, vengono limitati `J=H-QW`, `J cross A`, `J dot J-d²W²` e il segno di `d*(J dot A)`. Occorrono W positivo, normale non nulla e ramo regolare di `Surface::normal`; il fallback singolare numerico non viene certificato. I dati riconosciuti ma invalidi o inconclusivi vengono rifiutati, senza ricadere nel fit campionato.

La prima versione privata del pullback era troppo larga vicino all’estremo del parametro e rifiutava il caso valido. Una diagnosi sui coefficienti ha individuato l’amplificazione delle larghezze intervallari nella forma alle differenze. Il pullback dell’offset usa ora de Casteljau convesso per le restrizioni e la forma all’estremo più vicino per i piccoli sconfinamenti outward, senza clamp, aumento delle risorse o delle tolleranze. I provider preesistenti rimangono invariati.

## Risultato effettivamente pubblicato

Il percorso certificato è limitato a `!trimSeams` e una sola fin utilizzata su supporto Extrusion. Dopo il fit vengono controllate le cubiche effettive. Dopo `assembleBody`, `AssemblyMap` collega esplicitamente il raw edge al bordo emesso: si ricontrollano intervallo e poli finali prima di pubblicare il risultato o la storia. Non viene usata la sola vicinanza geometrica per la provenienza di produzione.

Coefficienti originali E/G/UV, normali e tangenti condividono un budget di due milioni di intervalli scalari; eventi e visite sono limitati a200000. Ricerche ordinate e cancellazione nei loop evitano prodotti incontrollati fra tutte le partizioni. Il fitter conserva i limiti precedenti di raffinamento. I rifiuti non modificano il corpo sorgente né pubblicano una `deviation` parziale.

## Verifiche finali

| Verifica | Esito |
|---|---|
| Build Release | **App e runner completi PASS**, eseguibili successivi alle sorgenti |
| Nuovo target permanente | **18/18 PASS**: otto offset pubblici e dieci verifiche provider/contratti |
| Confronto baseline | **8/8 fallimenti geometrici attesi**, con sorgente e risultato validi al checker |
| CTest pertinenti | **24/24 PASS**, inclusi offset, sweep, mapping, proiezione, spessore, ellisse, pannelli e riuso anteprime |
| Regressioni kernel precedenti private | **23/23 PASS**; comprese anche nelle famiglie CTest, senza sommare i conteggi come casi distinti |
| ASan/UBSan/LSan mirati | **18/18 PASS**, nessuna diagnostica o soppressione |
| Certificazione indipendente | **40 curve, 943 poli, 301 campate complete PASS** |
| Massimo limite superiore indipendente | **4,364583310131473e-8 mm**, entro 1e-7 mm |
| Attribuzione alla Release | Esportatore collegato solo alla libreria finale: **40/40 dump identici byte per byte**, zero differenze |
| Controllo negativo | Un polo finale spostato di1e-4 viene rifiutato dal verificatore indipendente |
| Architettura | **762 file, 11023 archi, 380 TU**, zero diagnostica, check, HTML/JSON e archi entranti/uscenti PASS |
| Conservazione | **2781/2788 file iniziali invariati**, sette file esistenti modificati nello scope |

Nel caso iniziale lo scarto misurato scende a circa **3,92695e-8 mm**. Le misure dense sono regressioni, mentre il limite sull’intera campata viene dimostrato dai coefficienti. Le40 curve indipendenti comprendono tutti i32 bordi dopo assemblaggio di otto corpi pubblici e otto fit diretti con E/G distinti; non sono40 corpi o40 ulteriori bug. Coperti polinomiali/razionali, U crescente/decrescente, senso faccia, distanza negativa, pose, trim, parametri traslati e laterali con U costante.

Il verificatore Fraction indipendente usa una formula diversa dal provider C++ e non lo chiama. Esporta gli input originali e tutti i poli/nodi finali; applica trasformazioni, inserimento nodi e restrizioni con razionali esatti. Le radici sono racchiuse con certificati integer. Il suo massimo positivo e il test negativo sono conservati con i dati, non dedotti da una griglia. [Metodo e certificati](curve-fit-k10-edge-offset-prove-2026-11-16/independent/final-audit.md).

Il sanitizer strumenta `fk_curve_fit_bounds`, `fk_offset`, `fk_affine_surface_fit` e il main del nuovo test. Il resto del kernel è Release non strumentato. I18 casi passano con leak check attivo fuori dal sandbox; nessuna soppressione. Non è una certificazione integrale del kernel o dell’app grafica. Link dell’app senza OCCT, nuovo runner senza Qt/OCCT e flag floating-point rigorosi verificati: contrazione FMA disattivata nei tre TU, no-fast-math esplicito in provider/test e default rigoroso senza opzioni fast-math nel fitter offset. Il primo audit pretendeva il flag no-fast-math esplicito anche per quest’ultimo: l’assunzione è stata corretta rispetto ai comandi effettivi, senza cambiare sorgenti, build o verifiche geometriche.

## Limiti e prossimi punti

Le altre superfici, le SP-curve non affini, le normali mediate fra più facce e le cuciture rifilate conservano il percorso precedente. La certificazione è posizionale sui bordi riconosciuti: non dimostra l’assenza globale di autointersezioni, la correttezza completa della superficie offset, della topologia o delle genealogie. Risorse finite e controllo conservativo possono rifiutare anche geometrie valide.

Restano interamente da migrare nove siti iniziali: due ricostruzioni UV in `fk_offset`, due giunti/continuazioni dei raccordi, due trasferimenti UV mapping/quilt, rimappatura SP-curve nella cucitura, giunzione delle superfici estese e curve procedurali proiettate nello schizzo. Si aggiungono i fallback nei15 siti affrontati.

I24 rilievi precedentemente corretti mantengono il conteggio; le22 candidate orfane e l’uniformazione completa di pannelli, stili e anteprime sono punti distinti. K2 rimane in produzione. La nuova ellisse è preservata e la sua suite passa anche sul kernel aggiornato. Nessuna nuova prova renderer in questo blocco: la prova Wayland dell’ellisse appartiene al blocco precedente. CAD originali e modifiche pregresse conservati, nessun commit/push.

[Sorgenti, log e manifest](curve-fit-k10-edge-offset-prove-2026-11-16/README.md), [stato precedente](curve-fit-k10-projection-2026-11-16.md), [memoria conservata](history/claude-prima-k10-edge-offset-2026-11-16.md).
