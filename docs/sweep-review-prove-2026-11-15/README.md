# Prove correzioni sweep — 15 novembre 2026

Log della build completa, 42 test kernel distinti (43 esecuzioni per sovrapposizione dei filtri Mass/PCurve),
16 CTest, cinque riproduttori ricompilati e mappa. Hash sorgenti iniziali/finali e binari conservati.
I rapporti K1/K3 degli agenti descrivono prove isolate precedenti all'integrazione; i log `kernel-*`
e `probe_*` descrivono la build finale combinata. Il fallimento K1 nel vecchio staging K3 è superato
dal test combinato PASS. K2 e K4 riproducono ancora difetti, pur avendo exit code 0: sono diagnostici.

I sorgenti `.cpp.txt` e i driver `.py.txt` sono prove documentali, esclusi dai target di produzione.
Per ripetere, rinominare una copia in `/tmp` togliendo `.txt`; i driver indicano checkout e build
usati, adattabili ad altro host. CTest usa preferenze temporanee e Qt offscreen; non certifica OpenGL.
La prima consultazione `ctest --show-only` in sandbox è fallita per il log di build non scrivibile;
la lettura e i test successivi, autorizzati sul checkout effettivo, sono riusciti.
