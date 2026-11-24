Prove locali K10: interno del carrier, 17 novembre 2026.

Baseline locale precedente al provider, con le correzioni numeriche già presenti.
Il wrap pubblico restituiva un solido piatto: residuo interno 1e-4 mm contro
1,22e-10 mm dichiarati. Baseline pubblica 3/4 gruppi; finale 10/10 Release e
ASan/UBSan, 7/7 CTest. Il provider per quota zero, UV separabili e superfici
polinomiali rifiuta conservativamente quel solido. Non costruisce né certifica
l'offset normale generale. Fonte, binario e grafo sono collegati dalla verifica
finale; 42 pacchetti precedenti rimangono invariati.

La CI44142011489 verde riguarda il software pubblicato78d7268, separato dalle
nuove modifiche locali. Sorgenti e patch sono testo inerte; nessun ELF, archivio
di libreria o CAD. Le prove dipendono dalla build descritta nei log: non si
promette un replay ermetico senza toolchain e libreria. Sanitizer su cinque TU,
resto kernel Release: non è una verifica LSan dell'intera app.

Il primo log di build è parziale: app completata, poi ricaricato il Makefile per
il nuovo target. Il comando separato dei runner è riuscito. I binari temporanei
sono rimossi come registrato in cleanup.json; la build del repository rimane.
