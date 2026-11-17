Superfici scalato/ruotato: indagine read-only del17novembre2026

Limite confermato, nessuna soluzione proposta o integrata. Fixture pubblica anonima kernel/tests/data/shell_stitched_tangent.body SHA f266d6fd50ebb5c69b97038dcf05c787fa2ff763426946930d4093b4425af7f9, byte-identica alla copia storica. Nessun .prt originale letto o scritto.

Riproduttore storico: scaleBody2.5, poi rotation(center(12,-23,7),axis(1,2,.3),angle.42), shellBody senza aperture, spessore1.25. Nuovo probe fresco collegato alla libreria corrente: inputcheckBody(loopCrossings)=0issue, fallimento in3.60556s con 'fitCurve: impossibile rispettare la tolleranza entro il limite di raffinamento'. Il log storico riportava limite visite: il testo attuale differisce, la causa puntuale del fit non e stata isolata in questa indagine.

Archivio docs/history/claude-2026-11-02.md:62 afferma esplicitamente 'Un prototipo con base locale del solver non risolve e NON e integrato'. Il log rotation-scale-local conferma il fallimento. Quindi il prototipo non e una patch risolutiva gia verificata da recuperare. La sua base locale cambia la risoluzione del sistema regolarizzato sulle normali, senza fornire una nuova prova del fit. Non e stato rieseguito su sorgenti attuali.

Per priorita del coordinatore, indagine fermata a riproduzione e diagnosi breve; nessun override di tolleranza, nessun nuovo build/modifica del kernel, nessuna ricerca di fix qui. Prossimo intervento significativo selezionato: K10 carrier Srazionale nativo grado2/3 con E/Q polynomiali, con caso pubblico riproducibile e certificato indipendente da verificare. I quattro svuotamenti non trasformati riusciti sono prove storiche e non sono stati rieseguiti qui.

Compilazione effettiva del probe: c++ -O3 -DNDEBUG -std=gnu++17 -Wall -Wextra -Wpedantic -ffp-contract=off -Ikernel -x c++ probe_scale_rotation.cpp.txt -x none forgecad-release/kernel/libforgekernel.a -o probe-scale-rotation. Esecuzione subprocess timeout120s, nessun timeout verificatosi. Il wrapper Python conclude exit0 dopo aver registrato returncode1 del probe: il risultato e FAIL, non PASS. Nessun processo o finestra utente controllati.
