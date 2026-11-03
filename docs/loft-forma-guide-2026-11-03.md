# Loft 9: correzione della forma con una o due guide

Verifica del 3 novembre 2026 su `SurfaceModellingExample219.prt`.

## Risultato

Corrette le creste artificiali e la deformazione aggiungendo **Curva estratta 3D 5**.
La superficie conserva le sezioni e segue entrambe le guide; le giunzioni geometricamente
G1 delle guide non diventano piu' pieghe interne. La vista frontale della lamina e' stata
ispezionata con un agente indipendente, insieme al pannello reale su Wayland e XCB.
Il confronto con SolidWorks e' qualitativo: era disponibile l'immagine, non il suo B-rep.

**Rimane il corner reale di 3,182 gradi della sezione superiore**, fra la curva e il segmento
originali dello schizzo. Il kernel ne conserva la geometria; per avere G1 anche in quel
punto occorrerebbe modificare esplicitamente quella giunzione dello schizzo.

Applicazione completa e runner compilati: `forgecad-release/forgecad` e `forgecad-release/forgecad_view_tests`.
Un processo gia' aperto deve essere riavviato per caricare il nuovo eseguibile.

## Caso riprodotto

Base dell'intervento: `6e9a5f5827ff031b070e68796ee4f008019d59c0`.
Originale letto soltanto: `/home/myungit/Documenti/Cad_Example/SurfaceModellingExample219.prt`.
Copia congelata: `/tmp/forgecad-loft-shape-fix/fixture/SurfaceModellingExample219.prt`.
SHA256 `5e42109d8e7a7c2f40b0d027b59ab2001a7b62503678d5cc6701051db95dbf48`, 82240 byte; hash originale riletto e invariato al termine.
Nessuna scrittura all'originale o al suo lock, nessun processo utente chiuso.

Loft 9, indice 8/id12: Schizzo 8 curva 2 e Curva estratta 3D 3 id10 come sezioni;
Curva estratta 3D 4 id11 come guida iniziale. La variante aggiunge Curva estratta 3D 5 id9.
Continuita' G1 alle estremita' e sulle guide, influenza 100%. La sezione id10 proviene da
Schizzo 4, curva 3 e segmento 10: il loro corner non deriva dalla curva proiettata.

## Cause e modifica

- Il vecchio campo Hermite orientava ogni polo usando il suo modulo: un'operazione nonlineare
  che cambiava il loft inserendo nodi, pur lasciando identica la geometria delle sezioni.
- Il passaggio da una a due guide cambiava anche il campo base delle righe interne.
- Le guide erano geometricamente G1 ma avevano salti di velocita' parametrica, fino a rapporti
  2,4 e 4,435. Il parametro grezzo trasformava quei salti in discontinuita' delle normali.
- Sezioni razionali con denominatori diversi amplificavano ulteriormente il difetto.

`kernel/fk_loft.cpp` usa ora un modulo comune per sezione, indipendente dai poli e non nullo
nei casi verificati di corda perpendicolare alla direzione G1. Le guide vengono percorse in
coordinata curvilinea, valutando sempre la sorgente esatta. Il nuovo modulo privato
`kernel/fk_loft_profiles.{h,cpp}` rende compatibili i profili mediante composizioni e prodotti
Bernstein algebrici e denominatore comune; non approssima le sezioni.
Le correzioni usano il prodotto esatto del denominatore con il raccordo cubico
`3u^2-2u^3`, conservando derivate trasversali nulle sulle guide interne.

Le guide sorgenti restano esatte, comprese quelle implicite. Il bordo NURBS del loft usa
il procedimento adattivo preesistente: budget 1e-9 mm e controlli indipendenti fino a 4e-9 mm,
invariati. Non si dichiara identita' algebrica con qualsiasi curva implicita.
Mesh e coordinate float sono usate soltanto per la vista. Nessun OCCT in produzione.

La review indipendente ha scartato prima dell'integrazione un prototipo lineare che creava
una cresta su tre guide. La regressione dedicata ora verifica che non ricompaia. Per i contatti
non vengono piu' aggiunti nodi ausiliari uniformi a pochi ulp da nodi gia' presenti: nessun
nodo sorgente distinto viene unito o eliminato; i casi con intervalli di un ulp restano verificati.

## Misure sul B-rep

| Misura | Prima: una guida | Dopo: una guida | Prima: due guide | Dopo: due guide |
| --- | ---: | ---: | ---: | ---: |
| Salto massimo delle normali al giunto U | 14,024 gradi | 3,182 gradi | 18,803 gradi | 3,171 gradi |
| Differenza normale ai nodi V, offset 1e-9 | 6,130 gradi | 6,62e-7 gradi | 21,680 gradi | 7,97e-7 gradi |
| Minimo seno fra Su e Sv sulla griglia interna | 0,533 | 0,881 | 0,185 | 0,854 |

Il residuo U e' presso il corner originale. La differenza V decresce con l'offset di misura:
non resta il salto finito iniziale. Le misure provengono dalle derivate del B-rep, non dai pixel.
`checkBody` e la mesh erano validi anche prima: non bastavano per rilevare la cresta.
Sulle sonde indipendenti le sezioni sono rispettate entro 7,3e-14 mm e le guide entro il
budget dichiarato. Dettagli e distribuzione dei campioni sono nei rapporti kernel/GUI archiviati.

Il costo dell'algebra esatta e' una rappresentazione piu' grande: nel caso reale gradi 21x5,
43x341 poli con una guida e 43x711 con due; B-rep circa 0,5/1 MB. Il codec B-rep esistente
conserva i punti verificati senza scarti al roundtrip. Il denominatore ha un limite esplicito
di grado 128 e controlli di finitezza/positivita'; non viene sostituito con un fit nascosto.

## Apertura dei file e revisioni

La cache geometrica passa da 7 a 8; il formato della definizione resta **60**.
Le vecchie cache di copie di lavoro con loft attivi vengono scartate atomicamente, cosi'
si ricostruiscono anche i derivati senza mescolare risultati vecchi e nuovi.
I documenti senza loft mantengono gli snapshot portabili fra versioni del kernel.

Le revisioni check-in conservano il loro snapshot immutabile: la migrazione e' limitata
alle copie di lavoro (`checkinId` nullo). Una regressione conserva esattamente il B-rep
storico di un loft e ne verifica la disponibilita' per la validazione delle revisioni.
I test coprono cache corrente 8, vecchia copia di lavoro 7, genealogia storica 6/7,
revisione con loft e rifiuto della futura cache 9. Nessuna soglia geometrica e' stata allentata.

## Verifiche finali

- Suite generale consolidata: **164 PASS, 1 SKIP OpenGL offscreen, 0 FAIL su 165**.
  Una prima esecuzione aveva due assert ancora fissati a cache 7 e un test fixture bloccato
  dal sandbox in sola lettura; erano inoltre saltati cinque test GPU. Aggiornato il contratto
  della cache, ripetuti con successo 15 target pertinenti e i controlli che richiedevano accesso
  alla build/GPU. I log iniziali sono conservati e distinti dai risultati finali.
- Kernel generale: **459/459 PASS**, 357,97 secondi. Risultato letto nel LastTest prima del
  ritest; l'XML originale conserva successo/durata ma tronca stdout a 1024 byte. Il ritest ha
  sostituito LastTest; la nota nell'archivio distingue questa osservazione dal log troncato.
- **9 gruppi nuovi** sulla forma PASS; il vecchio codice fallisce 6 dei 9 gruppi. Inclusi
  invarianza ai nodi polinomiali/razionali, raccordi G1 nonuniformi, tre guide, continuita'
  perpendicolare alla corda, corner originale, ordine/parametri delle guide e BodyIO.
- **42 test Loft preesistenti** e **10 gruppi guide implicite** PASS sulle sonde isolate;
  entrambi i runner geometrici sono anche nella suite di produzione.
- Clang: quattro TU modificate PASS; `cad_document_io.cpp` ricontrollato dopo la guardia
  sulle revisioni. ASan/UBSan: 9 gruppi forma e 10 gruppi guide implicite PASS, strumentando
  loft, profili e relativa TU di test. Libreria di supporto non strumentata; LeakSanitizer
  escluso per ptrace dell'ambiente, senza attestazione sulle perdite di memoria.
- **Wayland e XCB nativi PASS**: migrazione automatica, pannello reale, selezione della
  seconda guida, anteprima/conferma, passaggio 1→2→1, Undo/Redo, riapertura cache8/senza cache,
  immagini di fronte/retro ad alta qualita', Rivoluzione 3 visibile, contesto GL senza errori.
  I 100 input di produzione collegati sono rimasti immutati durante entrambi i run finali.

Il singolo test uniformi OpenGL resta saltato offscreen; la QA nativa della vista e' separata.
I warning di inizializzatori delle tavole gia' esistenti sono conservati nei log di compilazione;
nessun nuovo warning nei moduli geometrici modificati. Nessuna nuova CI remota e' attestata qui:
la CI verde precedente riguardava `ec08b12`, non questa correzione.

## Mappa, evidenze e copie

Mappa aggiornata: **552 file, 7635 relazioni, 261 unita' AST, zero errori**.
Generazione/check PASS; verificati 7 confini di chiamata e, separatamente,
chiamate/include/riferimenti in entrata e uscita degli 8 moduli verificati. HTML dei blocchi
profili e codec renderizzato e ispezionato. HTML/JSON generati restano ignorati da Git.

[Evidenze riproducibili e rapporti kernel/GUI](history/loft-forma-guide-2026-11-03-verifica.zip).
Archivio con manifesti SHA, sorgenti/probe, comandi, log e immagini; esclude CAD, B-rep binari,
eseguibili, oggetti e librerie esterne. L'originale non viene distribuito nell'archivio.

Copie locali dimostrative, salvate dal pannello finale XCB:

- `/tmp/forgecad-loft-shape-fix/gui-qa/SurfaceModellingExample219-Loft9-una-guida.prt`
- `/tmp/forgecad-loft-shape-fix/gui-qa/SurfaceModellingExample219-Loft9-due-guide.prt`

Restano i limiti precedenti su sezioni realmente nonplanari/guide complanari e il corner
originale. I controlli del Jacobiano sono campionati e non certificano globalmente l'assenza
di auto-intersezioni. La correzione non modifica gli schizzi per imporre una forma diversa.
