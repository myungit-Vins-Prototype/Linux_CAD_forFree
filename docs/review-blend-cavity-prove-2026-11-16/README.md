# Prove K12 — raccordo cavità

Consegna verificata: [rapporto](../review-blend-cavity-2026-11-16.md). `SHA256SUMS` sigilla tutti gli artefatti di questo archivio. `root/` contiene baseline3890file,hash,mappa prima/dopo,patch,build/CTest,verifiche Release e Fraction;`core/` il modulo corretto congelato e i probe;`tests/` baseline/regressioni/sanitizer;`independent/` diagnosi del proiettore e audit critico del candidato.

Le fonti di prova sono `.cpp.txt`/`.py.txt` e i CMake snapshot `.txt.txt`,fuori dai target. I manifest interni vengono adeguati ai nomi archivio;gli originali restano `SHA256SUMS.original.txt` quando occorre. CAD `.body`/`.prt`,oggetti e binari esclusi;hash e coefficienti IEEE per la prova matematica inclusi. Distinguere risultati eseguiti da ogni agente,verifiche root,analisi statica e limiti campionati:non è una certificazione globale del kernel o di assenza di leak.
