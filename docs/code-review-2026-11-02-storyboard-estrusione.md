# Storyboard, visibilita' ed estrusione fino al prossimo corpo

Verifica del 2 novembre 2026 sullo snapshot condiviso delle correzioni. Nessun
documento CAD originale modificato. Le prove descritte qui usano fixture nuove
e file temporanei; le verifiche della build completa sono coordinate separatamente.

## Difetti della storyboard confermati e corretti

1. **Rimappatura incompleta delle estrazioni multiple.** `removeBodies` e
   `removeSketchAt` enumeravano a mano i riferimenti, omettendo
   `curveExtraction.sources`. Eliminare un elemento precedente lasciava indici
   riferiti a un altro corpo o schizzo; il grafo e la rigenerazione incrementale
   potevano quindi seguire la sorgente sbagliata. Entrambi usano ora
   `visitGeometryRefs`, come il riordino. La regressione controlla gli indici,
   Undo/Redo e una curva esatta che passa da lunghezza 5 a 6 dopo la modifica
   dello schizzo rimappato.
2. **Identita' persa nelle copie usate dagli schizzi.** Ridurre il conteggio di
   una ripetizione eliminava una componente usata soltanto da un collegamento
   associativo o da Pierce: il controllo delle dipendenze vedeva solo feature
   e appoggi. `synchronizePatternResults` visita ora tutte le famiglie dei
   riferimenti degli schizzi e conserva la componente assente con diagnostica.
   Ripristinare il conteggio recupera la stessa identita', anche dopo
   Undo/Redo e salvataggio/riapertura.
3. **Ripetizione di funzione ricollegata alla lavorazione sbagliata.** In una
   ripetizione `featureOnly`, `firstBody` e' la funzione esplicita da ripetere,
   mentre `secondBody` e' lo stadio corrente su cui applicarla. Inserire Scala
   in rollback prima della ripetizione sostituiva `firstBody` con Scala,
   facendo fallire una funzione prima valida. Inserimento, riordino ed
   eliminazione ricollegano ora soltanto lo stadio implicito. Provati
   geometria reale, eliminazione, riordino, Undo/Redo e codec.
4. **Corpo nascosto quando ritorna una geometria valida.** Se tutti gli stadi
   erano soppressi o falliti, `tipFeatureId` diventava zero e i flag delle
   feature erano spenti. Al recupero, `normalizeModelHistory` copiava quel
   flag spento nella preferenza del corpo. Conserva ora `ModelBody.visible`
   quando il tip ritorna da zero. Regressioni: copia ripristinata e radice
   soppressa/riattivata, con preferenza manuale sia visibile sia nascosta,
   Undo/Redo e salvataggio/riapertura.
5. **Consumo scambiato per Nascondi manuale.** La conferma di una booleana
   spegneva il flag dell'utensile; la normalizzazione lo registrava come
   preferenza permanente. Il rollback lasciava quindi nascosto un corpo che
   prima era visibile, finche' l'utente eseguiva Mostra. La normalizzazione
   identifica ora i corpi consumati e conserva la loro preferenza. Il consumo
   continua a nasconderli nella scena finale. Provate preferenze true/false,
   conferma, rollback, Undo/Redo.

Le nuove regressioni hanno fallito sui rispettivi percorsi precedenti e sono
passate dopo le correzioni; non sono deduzioni dalla sola lettura dei sorgenti.

## Irrobustimento B8

Le due chiamate a `rebuildBody` che passavano un riferimento dentro
`extrusions_` lavorano ora su copie locali e pubblicano il risultato dopo il
loop responsivo, verificando l'identita' della feature. Una callback accodata
che inietta il detach del QVector riproduceva la perdita del risultato della
soppressione; dopo la correzione passano sia soppressione sia datum.

Questa e' **una prova di reingresso iniettato**: non e' stato identificato un
normale evento dell'interfaccia che inneschi B8. Non va presentata come
riproduzione del sintomo riferito dall'utente.

## Contratto di «Fino al prossimo corpo»

- Nuova condizione `extent=4`, aggiunta ai valori 0–3 senza cambiarli. Un solo
  verso; il segno di `distance` sceglie positivo/invertito. Occorrono soltanto
  contorni chiusi solidi: superfici, profili aperti o misti e due versi sono
  rifiutati esplicitamente.
- I candidati sono i risultati solidi precedenti, attivi e non consumati.
  La visibilita' manuale non li esclude. `booleanTools` contiene gli utensili
  limite soltanto per questa condizione; `mergeBodies` mantiene il suo ruolo
  distinto. Il layout del formato nativo 60 resta invariato.
- La lista candidati viene ricalcolata anche durante la rigenerazione, dopo
  aver pubblicato la geometria precedente. Una modifica della lista forza il
  ricalcolo: serve anche quando un limite viene soppresso o consumato e
  quindi scompare dalle dipendenze nuove. Inserire un nuovo solido in
  rollback puo' cambiare il primo limite senza riaprire il pannello.
- Bbox e proiezioni servono a dimensionare il prisma di ricerca. Il taglio
  usa booleane B-rep sulle superfici esatte, non mesh. Le facce di chiusura
  mantengono la genealogia dei corpi limite, che non vengono consumati.
- Un cap artificiale residuo segnala un profilo non interamente coperto.
  Attraversare il piano iniziale produce un errore distinto. Il certificatore
  controlla il segno delle normali per impedire che il risultato abbia
  oltrepassato il primo ostacolo passando lateralmente. Una verifica B-rep
  finale interseca il risultato con ciascun corpo limite: materiale comune
  produce un errore, senza ripieghi che ignorino eccezioni.
- Piani, cilindri, sfere e coni hanno bounds analitici conservativi. Per
  B-spline polinomiali si controllano tutte le coppie dei poli derivati delle
  pezze di Bezier: la bilinearita' del prodotto vettoriale e la proprieta'
  di inviluppo convesso certificano il segno sull'intera pezza. I pesi
  razionali variabili non vengono ignorati: i casi non certificabili
  restituiscono un errore. Non si dichiara supporto universale a superfici
  libere o a configurazioni con sottosquadri.

## Prove effettive

Le due suite nuove `--storyboard-review` e `--extrusion-next-body` sono passate
in una sonda Qt offscreen collegata a oggetti aggiornati dei moduli interessati
e agli altri oggetti della build Release esistente. Questo **non aggiorna
l'eseguibile dell'app** e non dimostra la resa OpenGL.

Per la nuova estrusione sono stati verificati: volume esatto dei casi piani;
verso positivo e invertito; corpo piu' vicino disgiunto, corpo dietro e limite
piu' lontano; piano inclinato; cilindro con confronto al volume analitico;
sfera; cono con primo incontro esatto; tetto bilineare B-spline con volume
analitico; due utensili complanari; errori per assenza di limite, copertura
parziale, intersezione iniziale, profilo aperto/misto e sottosquadri;
visibilita' manuale del limite; inserimento in rollback; soppressione e
riattivazione; candidato disgiunto che diventa il primo ostacolo; consumo dei
limiti da una booleana; Undo/Redo, codec e rigenerazione dopo riapertura.

Le verifiche GUI/preview native e le suite comuni sono svolte dagli altri
agenti e riportate nel rapporto generale; non sono attribuite a questa sonda.

## Correzione emersa durante la lettura finale: cavita' interne

Una sonda separata ha confermato che il filtro per shell puo' scartare una cavita'
interna senza contatto con il piano iniziale, riempiendo di nuovo il primo
ostacolo. Con un secondo limite completo piu' lontano il certificatore vede
allora un solido semplice e puo' accettarlo erroneamente.

Riproduttore: profilo quadrato `[0,2] x [0,2]` a z=0; primo box interno
`[0.5,1.5] x [0.5,1.5] x [2,3]`; secondo limite
`[-1,3] x [-1,3] x [4,6]`. Esito della sonda: corpo valido, nessun errore,
volume 16; l'inviluppo dei primi incontri avrebbe volume 14. Occorre
preservare le cavita' del componente iniziale oppure rifiutare esplicitamente
questa configurazione.

**Corretto e verificato:** la nuova modalita' interseca ora il risultato
finale con ogni corpo limite e rifiuta l'ostacolo interno se la selezione
delle shell ne ha riempito il volume. Le eccezioni dell'intersezione
producono il normale errore della feature, senza un falso risultato valido;
il generico `forgeKeepShells` delle altre operazioni non e' stato modificato.
Regressioni permanenti PASS per un ostacolo interno, due ostacoli interni
successivi e un profilo disgiunto con ostacolo interno. PASS anche il profilo
disgiunto coperto senza ostacoli (volume 32) e tutti i casi precedenti,
inclusi cilindro, sfera, cono, B-spline, consumo, Undo/Redo e codec.

Il limite residuo e' esplicito: gli ostacoli interni non certificabili sono
rifiutati, non viene costruita una chiusura approssimata del loro inviluppo.

## Verifica delle suite comuni: proprietario e componenti separate

Le due segnalazioni della suite comune sono state riprodotte in una sonda
collegata agli oggetti della build finale. La normalizzazione assegnava
erroneamente il corpo proprietario della base a tutte le componenti di
`DeleteFace`, cancellando l'identita' nuova gia' assegnata alle componenti
successive. Ora solo la prima componente eredita il proprietario: il test
preesistente ritrova due corpi visibili distinti nella storyboard.

Il test di cambio proprietario dipendeva invece dal vecchio effetto del
consumo sulla preferenza di visibilita': attendeva nascosto un utensile che
non era mai stato nascosto manualmente. La fixture ora esegue esplicitamente
Nascondi sulla prima base prima del consumo. Tutte le asserzioni restano
invariate; la nuova suite continua a verificare che il solo consumo conservi
la preferenza visibile e che il rollback la ripristini.

Esiti prima/dopo: entrambi i test preesistenti fallivano prima; la sonda ora
passa il test completo del proprietario (cache, riapertura, modifica,
cascata, Undo/Redo) e il blocco delle componenti separate. Ripetute con esito
PASS anche `--storyboard-review` e `--extrusion-next-body`. Sono verifiche
differenziali con oggetto `cad_model_history.cpp` ricompilato e fixture
aggiornata nella sonda. Anche il runner generale preesistente, ricollegato
con il solo oggetto `cad_model_history.cpp` aggiornato, termina con exit 0
e con il riepilogo di assi, piani, datum, Undo/Redo, riferimenti, vincoli,
salvataggio e cache. La nuova build dell'eseguibile definitivo resta alla
verifica del coordinatore.
