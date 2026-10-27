# Ottimizzazione B-spline su main — 27 ottobre 2026

Integrata su main la sola ottimizzazione delle basi B-spline del ramo di sviluppo. I gradi1..5 e gli ordini0..2 usano dimensioni costanti per il compilatore; i restanti gradi/ordini mantengono il percorso generale. Algoritmo A2.3, ordine aritmetico, API, tolleranze e cancellazione per chiamata invariati. Nessuna dipendenza CUDA/Metal o inclusione delle altre ottimizzazioni sperimentali di loft, proiezioni e solver.

## Verifiche

- Applicazione completa `forgecad` e runner `forgecad_view_tests` Release compilati con CMake su Linux.
- 114/114 test selezionati del kernel superati: curve e superfici B-spline, algoritmi di curve/superfici, loft, massa, offset, shell, tessellazione e cancellazione. Comprendono quattro nuove regressioni permanenti: OCCT/prefissi delle derivate, Bernstein/buffer, concorrenza/cancellazione e limiti unilateri ai nodi ripetuti. Non e' la suite completa del kernel.
- 27 regressioni applicative: 26 superate, un errore preesistente `forgecad_history_position_tests` su `suppression restores operands despite consumed scene filter`, riprodotto sul binario main precedente. Nessun test disabilitato o criterio allentato.
- Ultimo `mouse2.prt` salvato, copiato e ricostruito in memoria prima/dopo: B-rep serializzati, mesh visibile, errori e visibilita identici; zero errori. SHA256 7ffa64559f38820ed0f070fa233efc27a2b5ea82ee749e04817a756cb1ae92ce, 36 feature, 14 schizzi, 20 B-rep, 30902 triangoli visibili. Un processo per versione, prova di correttezza, non misura statistica di velocita. Nessuna scrittura di documenti, archivi o sessioni CAD dell'utente.
- Mappa aggiornata e ispezionata:469file/6554relazioni/230TU,0diagnostiche; `--check` superato,79.282siti con righe valide, quattro confini di chiamata verificati in uscita/entrata e include distinti. HTML/JSON generati ignorati da Git.

## Prestazioni e portabilita

La precedente prova isolata sullo stesso main di partenza abc74a2, quattro processi per versione con ordine alternato sulla copia congelata mouse2 SHAa45aa16453f08abdd746c21f84e5f86188b78db8bec80e255fa10d1f86f1e24c, ha misurato apertura con ricostruzione CPU qualita1: **31,006→27,017secondi**, mediana, **12,867%tempo in meno (1,148×)**. Tutti i B-rep/mesh/errori/visibilita identici. Il file successivamente salvato e usato nella prova di correttezza sopra e' diverso: non gli viene attribuito automaticamente lo stesso guadagno.

Il confronto precedente sostituiva solo fk_bspline_basis.cpp.o nella libreria main e riutilizzava gli altri oggetti main. Nessun profiler/build/analisi concorrente, nessuno svuotamento cache filesystem o controllo di frequenza/temperatura. Il risultato non va combinato con rapporti o tempi assoluti del ramo sperimentale, ne esteso automaticamente alle aperture con cache o all'intera applicazione. Obiettivo10× ancora aperto.

Il codice CPU C++17 e' comune anche a macOS; build e prestazioni native macOS non verificate. Installazione desktop esistente preservata. Binari main compilati in `/tmp/forgecad-shell-publish-main-build-20261027`; artefatti, firme e log in `forgecad-linux-solver/benchmarks/bspline-main-integration-2026-10-27` nel checkout sviluppo. Commit e push autorizzati dall'utente, limitati a questo intervento.
