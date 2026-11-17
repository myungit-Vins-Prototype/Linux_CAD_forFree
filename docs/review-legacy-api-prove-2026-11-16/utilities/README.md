# Tre utility orfane — staging su HEAD47c7c19

Rimozione applicativa richiesta: `forgeIntersectRay`, `nearestBodyEdge`, `appendFixedReferenceLine`. `after/` contiene i quattro file finali; `utilities.patch` applica solo le dichiarazioni/definizioni e i commenti propri. Nessun include ripulito e nessun altro algoritmo cambiato.

Ricontrollati AGENTS/CLAUDE, piano legacy, politica tolleranze e descrizioni/archi del grafo nei due versi. Ricerca su tutti i sorgenti HEAD: sei sole occorrenze, dichiarazioni/definizioni; grafo: zero call/function-reference in ingresso per tutte e tre. Hash dei quattro file allineati a HEAD e grafo. `git apply --check` PASS, nessuna applicazione effettuata.

Preservati byte per byte i corpi di forgePickFace, appendSegment, appendProjectedCurve, sketchPlaneReference/sketchAxisReference e appendLinkedSketchReference/refreshLinkedSketchReferences. firstRayHit, projectPoint e makeConstraint/Fix restano nei loro moduli esterni invariati. Le chiamate Fix ancora presenti nei percorsi di proiezione/contatto non sono eliminate. Nessuna riassociazione per vicinanza introdotta; riferimenti associativi e genealogia intatti.

Per root: integrare dopo review; build app completa e tre CTest esistenti in selected-existing-tests.json. Nessun test nuovo, app/test/build non eseguiti da questo agente. Queste sono regressioni dei percorsi conservati, non esercitano API rimosse. Header non installati come SDK, ma consumatori esterni/non tracciati non sono esclusi; la ricerca non è prova universale di assenza d'uso. La vista AST riflette la configurazione CMake locale, non ogni configurazione/plugin esterno.

Responsabilità finali root: aggiornare CLAUDE e mappa strutturale dopo integrazione; mantenere distinti staging staticamente verificato e compilazione/regressioni effettive. Nessuna modifica repository/Git, nessun commit/push né CAD letto da questo agente. Tutti gli output di questo sottodelta sono in /tmp/legacy-api-review/utilities.
