# Review: proiezione Helix e funzioni private orfane — 16 novembre 2026

Prosecuzione con tre agenti **GPT-6.1 Sol**. Integrato e verificato il percorso continuo per le eliche trasformate proiettate nello schizzo; eliminate cinque funzioni private/locali senza usi. **29 rilievi funzionali corretti complessivamente; K10 resta aperto: 19/24 siti iniziali parzialmente trattati, cinque interamente da migrare e fallback ancora presenti.** Le cinque rimozioni non sono cinque nuovi bug funzionali.

## Difetto effettivo e correzione

Nella API Qt `appendProjectedCurve`, una `TransformedCurve(HelixCurve, identity)` con raggio 0,0001 mm, passo 0,000001 mm e un milione di giri veniva accettata contro una risoluzione di 0,0000001 mm. Sul piano YZ diventava una Converted cubica a quattro poli e perdeva un dettaglio di circa 0,0001 mm; sul piano XZ diventava un segmento e perdeva circa 0,0002 mm. Due testimoni razionali indipendenti, con sin/cos racchiusi rigorosamente, superano rispettivamente circa 1000 e 2000 volte il budget. Non è una prova basata soltanto sul callback del fitter: è l'output reale della funzione Qt.

Il nuovo modulo `kernel/fk_helix_projection` copia gli originali: HelixSpec, coefficienti IEEE rise/slope già memorizzati, matrici di ogni wrapper e assi del piano dello schizzo. Il target è `frame.toLocal(source(t)).XY` in mm, nel parametro originale. Riduzione della fase con un intervallo di pi/2, Taylor outward e resto di Hermite limitano continuamente il residuo. Per ogni cubica si sommano l'errore dei quattro controlli rispetto all'Hermite ideale e `M4*h^4/384`; `M4` comprende trasformazioni e proiezione originali. Il candidato proposto da libm e differenze finite non autorizza da solo l'accettazione.

Dopo `QPointF` e `recalculateCurve`, il percorso ricostruisce `curveGeometry` e verifica i poli, i nodi e il dominio realmente emessi. Pubblica solo dopo il controllo; salta la scorciatoia collineare campionata. Errori e cancellazione preservano schizzo, vincoli e lista `created`, anche quando contengono già elementi. I due casi da un milione di giri vengono rifiutati esplicitamente prima di allocare milioni di campate.

Il cap di lavoro conservativo è 16384 campate uniformi stimate; il traversal è iterativo e limitato a 256 wrapper prima delle query delegate. Queste guardie possono rifiutare geometrie valide: il rifiuto non significa impossibilità matematica. Non è stato dimostrato un crash dei vecchi traversal: il probe con 20000 wrapper e stack 256 KiB rifiuta anche sul precedente codice Release.

**Portata:** il nuovo ramo riguarda le Helix contenenti Transformed, con Trimmed annidati, che prima entravano nel fit generico. Helix diretta e solo rifilata mantengono il percorso quintico C2 preesistente; questo intervento non ne certifica il vecchio fitter. Il contratto del nuovo risultato cubico garantisce posizione e schema C0. Gli altri tipi Other mantengono il fallback. Nessuna tolleranza aumentata, nessuna promessa globale su autointersezioni o validità B-rep.

## Cinque funzioni private/locali rimosse

Ricontrollati 767 file attivi, inclusi test fuori CMake, backend opzionali, friend e callback Qt. Zero chiamanti o riferimenti per:

- `CadViewport::setSelectedPointPosition`;
- `StoryboardTree::firstFeature`;
- `Corner::otherFace`;
- `ChainEdge::inPlane`;
- `BandMatrix::band`.

Sette file, 35 righe eliminate complessivamente, inclusa la riga vuota finale segnalata da `git diff --check`. Classi, dati, enum, formati e API pubbliche preservati. Gli omonimi usati in altri contesti non sono stati eliminati. Le **17 API pubbliche senza usi interni restano da valutare**. Non sono stati aggiunti test che replichino getter inutilizzati. [Inventario storico e aggiornamento](review-orphans-refresh-2026-11-16.md).

## Verifiche sulla versione finale

| Verifica | Risultato |
| --- | --- |
| Build completa app e runner | PASS; primo tentativo fermato per correggere lo scope dell'include del test, poi build completa exit 0 |
| Nuova suite Qt e header permanente | 13/13 PASS: otto varianti, compatibilità quintica diretta, due alias enormi, atomicità e contratti |
| Baseline dello stesso driver | 9/12 PASS, tre FAIL: due geometrie negative e ripetizione dello stesso trigger sullo schizzo popolato |
| Release effettiva | 13/13 PASS, 11 export byte per byte identici al prototipo finale |
| Prova indipendente Fraction | Otto varianti, tutte le 624 campate: PASS; massimo 7.61259490419804e-08 mm; caso stretto 5,261407548553817e-9 entro 1e-8; polo alterato respinto |
| Guardie indipendenti del provider | 37/37 PASS: domini, non finiti, schema, wrapper 256/257, ownership, budget, cancellazione e poli alterati |
| ASan, UBSan e LSan mirati | 13/13 PASS, nessuna diagnostica o soppressione; tre TU strumentate, resto app/kernel Release |
| CTest pertinenti | **20/21 PASS**, un fallimento preesistente riprodotto col kernel congelato precedente; exit 8 conservato |
| Filtri kernel estesi | Helix 5/5, Fill 25/25, Blend 62/62 PASS; i filtri possono sovrapporsi, non sommati come casi distinti |
| Architettura | 772 file, 11165 relazioni, 385 TU, zero errori; check, HTML/JSON, hash, compile DB e 872 archi incidenti verificati nei due versi |
| App effettiva | Avvio isolato Wayland per cinque secondi, `/proc/.../exe` e launcher desktop allineati alla nuova build; nessun CAD aperto o processo utente toccato |

La verifica Fraction usa soltanto i valori IEEE esportati, aritmetica razionale con arrotondamento outward a griglia di 192 bit, Machin per pi e Taylor per sin/cos. Ricostruisce gli originali e verifica ogni campata finale con controlli Hermite e derivata quarta; non chiama o legge l'oracolo del prodotto. Le misure dense nei JSON sono diagnostiche e dichiarate tali.

I sanitizer coprono `cad_sketch_refs.cpp`, il nuovo provider e il driver; Qt e resto app/kernel non sono interamente strumentati. Il primo tentativo LSan nella sandbox falliva per ptrace: il log è distinto dalla riesecuzione finale esterna, exit 0 e leak detection attiva. Non è una garanzia di assenza globale di perdite. L'avvio Wayland non certifica i pixel OpenGL o tutti i flussi desktop; macOS e Windows non ricertificati.

## Fallimento preesistente isolato: raccordo della cavità

`forgekernel_blend_cavity_tests`, caso `BlendCavityWholeRimKeepsRadiusHistoryAndClosedTrims`, fallisce con «il contatto esce dal dominio del fianco F0». Stessi oggetti del test rilinkati al kernel congelato prima di questo intervento: stesso fallimento, 7/8 casi passati. Un probe separato sulla fixture `blend_cavity_loft.body` isola il raggio **0,5 mm rifiutato**; **1,1 e 1,675 mm** riescono con 18 facce e `checkBody` vuoto. La sorgente rimane identica dopo ogni prova.

La guardia è in `kernel/fk_blend_surface.cpp`, nella gestione dei contatti che cambiano supporto. La causa geometrica completa e l'eventuale correzione restano **da diagnosticare**; non si assume che basti allentare la guardia e non si indebolisce il test. Questo è un problema riemerso nelle regressioni ampliate, non una regressione attribuita alle cinque rimozioni. La suite non è dichiarata tutta verde.

## Punti ancora aperti

I cinque siti iniziali K10 ancora interamente da migrare sono:

1. `kernel/fk_blend_mitre.cpp:129`: giunto con parametri risolti sui supporti;
2. `kernel/fk_blend_surface.cpp:1655`: continuazione implicita del raccordo;
3. `kernel/fk_surface_mapping.cpp:189`: trasferimento UV sul carrier conservato;
4. `kernel/fk_quilt_mapping.cpp:713`: trasferimento UV per l'offset del carrier;
5. `kernel/fk_sheet.cpp:407`: giunzione implicita delle superfici estese.

I 19 siti affrontati restano parziali: altri Other nello schizzo, percorso quintico diretto, famiglie non Plane, matching/cap sorgente campionati e altri fallback richiedono ancora lavoro. Restano anche l'uniformazione complessiva di pannelli/stili/anteprime e le 17 API pubbliche. Prossima priorità proposta: diagnosticare il raccordo 0,5 mm appena isolato; poi i trasferimenti UV Mapping/Quilt. Nessuna nuova correzione a questi punti è dichiarata qui.

Baseline 3676 file congelata; conservazione delle modifiche preesistenti verificata separatamente in `root/final-audit.json`. Tutti i sei archivi precedenti 95/146/172/267/126/152 restano integri. Nessun CAD originale modificato, nessun commit o push. L'istanza già aperta dall'utente deve essere riaperta per caricare la nuova build.

[Prove e manifest](review-k10-helix-orphans-prove-2026-11-16/), [stato generale](code-review-2026-11-15.md), [memoria precedente](history/claude-prima-k10-helix-orphans-2026-11-16.md).
