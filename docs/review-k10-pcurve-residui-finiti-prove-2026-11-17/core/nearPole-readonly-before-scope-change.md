# NearPole: audit iniziale in sola lettura

AGENTS/CLAUDE e grafo795nodi/11651archi consultati PRIMA dei sorgenti; descrizioni e call/include/reference entrambi versi in graph-evidence.json. Nessun repoedit/compile/eseguibile/copia archive/UI/commit/push. Solo dati testuali staging; root cura gli eventuali runtime.

API effettive: fitPCurve/exactPCurve/computePCurves (fk_pcurve.h), non esiste un buildPCurve pubblico né un parametro guess passato dall'utente a fitPCurve. sampleParameters (fk_pcurve.cpp110–137) parte dalla proiezione globale del centro della curva, in UV canonici, poi procede con invertPoint, ora dotato del controllo finale2b385. Un range del parametro3D di ordine1e12 NON si trasforma da solo in guessUV1e12: le due quantità sono distinte.

Fitter::node nearPole (fk_pcurve.cpp245–253) fa projectPoint, controlla projection.distance, riporta u vicino al guess con un multiplo di periodo, ma non verifica immediatamente il residuo in quei nuovi UV. Largephase può comparire internamente per un forte overshoot dell'Hermite (derivataUV elevata vicino a un polo), per una curva con molte rivoluzioni risolte, o per estrapolazione degli estremi al polo; non è legittimo iniettarla artificialmente in un oggetto privato e chiamare quello un riproduttore pubblico.

Controllo importante dopo il nodo: fitHermitePCurve (360–400) costruisce i poli realmente emessi, applica baseShift e chiama pcurveDeviation sul fitted finale. Le partizioni includono i breakpoint della curva3D e della pcurve; i nodi condivisi entrano nella griglia di controllo. Un nodo prima del shift che abbia errore oltre cap non implica quindi che l'API pubblichi una pcurve errata. fitLinearPCurve esclude le superfici con poli analitici; non esiste una via lineare che possa recuperare automaticamente questo caso sfera/cono. La verifica resta campionata, non certifica il continuo e non dimostra impossibile qualsiasi controesempio: impedisce solo di dedurre un bug pubblico dalla sola omissione locale.

Esito attuale: nessun riproduttore pubblico oltre cap accertato per il solo nearPole manualunwrap. Nessuna patch applicata o congelata e nessun nuovo bug contato. Un eventuale guard locale conservativo dovrebbe controllare finitezzaUV e residuo fisico DOPO il riporto, con lo stesso cap, senza canonicalfallback che rompa il ramo; ma senza witness o nuova istruzione resta proposta di hardening, non correzione dimostrata. Il vecchio arcParameter/globale non è stato riaperto.

File/linee e hash si riferiscono allo snapshot letto; nessuna prova runtime attribuita a questo agente.
