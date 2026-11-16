# Review: cucitura Plane e anteprima Smusso — 16 novembre 2026

**Completato questo blocco con tre agenti GPT-6.1 Sol.** Correzione della ricostruzione delle SP-curve durante la cucitura su Plane e del difetto PRE8 nello Smusso. **29 rilievi confermati corretti complessivamente. K10 resta aperto: 18/24 siti iniziali parzialmente trattati,6 interamente da migrare, oltre ai fallback.** Nessun CAD originale modificato; l’app è stata ricompilata completamente. Riaprire l’eseguibile aggiornato per usarla.

## Cucitura: difetto riprodotto

Due rettangoli piani adiacenti hanno lo stesso bordo geometrico, ma parametri differenti. Il rappresentante conserva una NURBS monotona di grado 128, con una piccola variazione Bernstein tra i vecchi campioni; il bordo dell’altra faccia è una retta. Gli input hanno pcurve coerenti, `checkBody` non segnala problemi e la cucitura passa da 8 a 7 bordi.

Sulla baseline il ramo non affine emette una cubica a 4 poli e il checker continua a passare. A `t=3/128`, però, `||S(PC(t))-E(t)||=1,120011104e-7 mm`, oltre il cap preesistente di 1e-7 mm. I controlli uniformi 16/64 non vedono il massimo. Non si tratta di un volume perso: è errata la corrispondenza nello stesso parametro, richiesta dalla fin. Una traccia privata dimostra l’esecuzione del vecchio fit non affine; due ulteriori varianti riproducono anche i riusi con parametro identico e rimappatura affine.

Sei regressioni geometriche falliscono prima della correzione. Scala 1000 e pesi razionali non uniformi erano già compatibili sulla baseline e rimangono controlli positivi; la cancellazione costituisce il nono gruppo baseline. I fallimenti dei test non sono conteggiati come sei nuovi difetti separati: estendono la correzione K10 già aperta.

## Correzione e contratto

- Per le sole fin realmente rimappate con supporto Plane e bordo Line/NURBS a pesi positivi, inclusi wrapper Trimmed/Transformed, `sewnPCurve` ricostruisce la pcurve nel parametro originale del bordo rappresentativo, prima delle scorciatoie campionate. Gli altri supporti/famiglie restano fallback.
- Le API interne `detail: : fitPCurveOnPlane` e `requirePCurveOnPlane` riusano il provider duale originale: bound continuo della proiezione per il fit in mm, poi residuo completo contro il bordo 3D. La soluzione del sistema Gram produce soltanto candidati UV; sono i coefficienti originali e l’oracolo Bernstein outward ad autorizzarli. Il fit mantiene 1e-8 mm.
- Il cap rimane numericamente quello preesistente: `original(sampled)+max(1e-7,10*tolerance)`. Non è sostituito da un upper bound più largo. La nuova `pcurveTolerance` pubblica l’upper bound continuo effettivo, entro quel cap. Il residuo normale fuori piano è incluso e, se troppo grande, la costruzione fallisce senza pubblicare una deviazione parziale.
- Due campi transitori di `RawFin` trasportano l’obbligo e il cap all’assemblaggio. Non cambiano il formato persistente. `AssemblyMap` identifica faccia e bordo effettivi; l’identità dell’oggetto pcurve condiviso distingue le fin migrate nella stessa coppia faccia/bordo. Ogni obbligo deve ritrovare esattamente identità, numero e budget. Il controllo legge comunque i coefficienti, il bordo, il parametro e il Plane realmente emessi dopo i passaggi di assemblaggio.
- Le fin rappresentanti non rimappate rimangono fuori dal nuovo contratto. Un primo prototipo contava tutte le fin della coppia e poteva rifiutare una coppia mista: è stato corretto prima dell’integrazione. La prova privata verifica il caso positivo e rifiuta copia della pcurve con identità diversa, budget aumentato, polo alterato nella stessa identità e intervallo cambiato: 5/5 PASS.

Sorgenti: `kernel/fk_exchange.cpp/.h`, `kernel/fk_offset.cpp`, `kernel/fk_curve_fit_bounds.h`. Regressione permanente: `kernel/tests/test_sew_pcurve_validation.cpp`, target `forgekernel_sew_pcurve_tests`,12 gruppi. `fk_curve_fit_bounds.cpp` non è cambiato in questo blocco.

## PRE8: Smusso dopo digitazione e Tab

Digitare 1,25 al posto di 0,5 e premere Tab aggiorna il campo, ma sulla baseline anteprima, chiave, generazione e messaggio «pronta» rimangono sul vecchio valore. Il caso è riprodotto con veri eventi di tastiera e `editingFinished`, anche con un worker pendente. **La conferma costruisce già il valore corretto**: la chiave protegge il riuso del B-rep, quindi il difetto dimostrato riguarda anteprima/stato e ricalcolo superfluo.

I due campi erano collegati soltanto al callback di Invio/frecce. Due connessioni `editingFinished` alla funzione di aggiornamento esistente coprono ora Tab e perdita del focus. Nessuna modifica al componente generale `ExpressionSpinBox`.

La regressione permanente `tests/test_chamfer_preview_editing.h`, CLI `--chamfer-preview-editing`, usa sei scenari: primaria, seconda distanza e angolo, ciascuno con risultato pronto o worker pendente. Controlla tasti effettivi, invalidazione, token inizialmente non cancellato, recupero, Invio/frecce, conferma unica, geometria mostrata/confermata e cleanup. Stesso header sul vecchio codice: 63 PASS/27 FAIL; corretto: 90/90 PASS. Le 27 asserzioni fallite descrivono un solo difetto PRE8. La pista adiacente sulle unità dell’angolo è stata scartata: `setSuffix` disabilita già la conversione delle lunghezze.

## Verifiche finali

| Verifica | Esito effettivo |
|---|---|
| Build completa app, kernel e runner Release | PASS, 266.54s; nessun overlay privato nei binari finali |
| CTest pertinenti, incluse entrambe le nuove regressioni | 37/37 PASS, 74.26s; è un sottoinsieme selezionato |
| Runner generale Iges / Exchange / StepRoundTrip / StepReadAssembly | 9/9, 3/3, 2/2, 1/1 PASS |
| Runner generale Offset / Sheet | 27/27, 41/41 PASS |
| Regressione cucitura Release | 12/12 PASS; 8/8 JSON identici byte per byte ai dati congelati e verificati indipendentemente |
| ASan/UBSan/LSan mirati | 12/12 PASS, leak check attivo, nessuna soppressione/diagnostica; 3 TU kernel+driver strumentati, resto Release |
| Anteprima Smusso su runner finale | 90/90 offscreen e 90/90 Wayland PASS; nessuna certificazione nuova dei pixel OpenGL |
| Guardie di provenienza | 5/5 PASS sul prototipo esatto integrato |
| Certificato indipendente Fraction | 64 fin sorgenti entro risoluzione esplicita 1e-7 mm; 64 fin finali/169 campate complete entro cap; 8 migrate anche entro budget pubblicato e fit 1e-8 mm |
| Massimo bound indipendente finale | 8.6726196538e-09 mm; sei negativi baseline confermati e polo finale alterato di 1e-4 rifiutato |
| Architettura | 769 file/11127 archi/384 TU; 0 diagnostiche; check, hash, HTML e archi nei due versi PASS |

Il verificatore Python parte dai valori IEEE originali esportati in floathex: nodi/pesi, inserimento dei nodi e restrizione razionali, residuo omogeneo Bernstein e radice razionale arrotondata verso l’esterno. Copre tutte le campate e non chiama né legge gli oracoli del prodotto. Le sorgenti dichiarano localmente zero in alcuni casi: il controllo indipendente delle sorgenti usa esplicitamente la risoluzione 1e-7 mm, senza rivendicare uguaglianza algebrica a zero. Le 8 fin rimappate finali hanno invece anche la verifica contro la propria tolleranza pubblicata.

Le prove private con oggetti sostituiti sono limitate al percorso esercitato: il layout `RawFin` è cambiato e non si attribuiscono loro le suite IGES/STEP. Queste ultime sono state eseguite dopo la ricompilazione completa, con tutte le unità allineate. ASan strumenta offset, exchange, sew e driver; non certifica tutto il kernel o la GUI contro ogni perdita.

## Conservazione e lavoro ancora aperto

Baseline 3519 file congelata, descrizioni e archi consultati prima degli interventi; 3508 file preesistenti preservati,11 modifiche esistenti attese e due nuovi test, oltre a rapporto/memoria/prove. Gli archivi precedenti 95/146/172/267/126 restano invariati e verificati. Nessuna rimozione delle22 candidate orfane e nessuna modifica ai CAD.

Restano sei siti K10 interamente da migrare:

1. `fk_blend_mitre.cpp`: giunto tra supporti con parametri risolti.
2. `fk_blend_surface.cpp`: prosecuzione del raccordo implicito.
3. `fk_surface_mapping.cpp`: trasferimento UV sul carrier conservato.
4. `fk_quilt_mapping.cpp`: trasferimento UV e bound per l’offset del carrier.
5. `fk_sheet.cpp`: giunzione implicita delle superfici estese.
6. `cad_sketch_refs.cpp`: proiezione procedurale `Other`.

Anche i 18 siti parzialmente trattati conservano famiglie fallback, compresa la cucitura non Plane. Il matching dei bordi e il residuo sorgente usato per il cap restano campionati: il nuovo certificato riguarda la pcurve del target emesso, non dimostra l’equivalenza globale dei bordi scelti né certifica retroattivamente la sorgente. Il limite del sistema Gram e le risorse possono produrre un rifiuto dichiarato.

Per il prossimo sito procedurale è conservato un probe di `TransformedCurve(HelixCurve)` con molti giri: il callback del fitter può aliasare la periodicità e perdere una deviazione. Non è ancora una riproduzione della funzione Qt `appendProjectedCurve` e non aggiunge un rilievo conteggiato separato a K10. Restano inoltre l’uniformazione complessiva di pannelli/stili/anteprime e la decisione sulle 5 orfane private/locali e 17 API senza usi interni.

[Prove e manifest](review-k10-sew-preview-prove-2026-11-16/), [stato generale](code-review-2026-11-15.md), [memoria precedente](history/claude-prima-k10-sew-preview-2026-11-16.md).
