# Estendi: rimozione coerente di tre API legacy

Staging su HEAD `47c7c19c4faf490995e8ff64f10d83d8a6abb077`, dopo lettura AGENTS/CLAUDE, piano e descrizioni/archi del grafo. **Nessuna scrittura nel repository, mutazione Git, commit o push.** Il divieto attuale prevale sulla vecchia autorizzazione.

`after/` contiene soltanto `forgeCad2026_viewport.h` e `forgeCad2026_viewport_selection.cpp`; patch minima `extend-api-cleanup.patch`. Rimosse dichiarazioni/definizioni di `setExtendPickCallback`, `setEdgePickLinear`, `resumeExtendPick`, membro `extendPickFinished_` e consegna morta del callback. L'uscita esplicita `if (extend) return;` mantiene il mancato inoltro a raccordo/modifica dopo la pulizia del picker.

Sul baseline: zero call/reference entranti per le tre API e otto occorrenze lessicali totali, soltanto dichiarazioni/definizioni/campo/consegna. `beginExtendPick`, `startEdgePick`, `edgePicked`, `cancelEdgePick` ed `edgePickEligible` identici byte per byte nel testo delle funzioni. Stati Extend/Linear, preview, callback changed/key, altri picker e relativi dati preservati. Nessun include, formato persistente, dato, preferenza o kernel modificato.

`git apply --check` PASS; simboli rimossi assenti dagli afterfiles. Queste sono verifiche statiche: **nessuna build o esecuzione di test da core**, nessun test speculare nuovo. Root integra e verifica le suite già esistenti: `forgecad_extension_panel_tests`, `forgecad_chamfer_preview_state_tests`, `forgecad_surface_tools_tests`; build completa richiesta per il cambiamento dell'header. Dopo integrazione rigenerare/check mappa e aggiornare CLAUDE al coordinatore.

Limiti: rimozione di interfacce C++ pubbliche applicative e di un membro privato: non si promette compatibilità sorgente/ABI per consumatori esterni, che questo repository non dimostra assenti. Non sono tre nuovi bug funzionali; il conteggio dei rilievi resta distinto. Grafo incluso è baseline, non un nuovo grafo del candidato. Prove in `graph-evidence.json`, ricerche testuali e `static-verification.json`; SHA in `source-manifest.json` e `manifest.json`.
