# Prove: K10 UV e PRE7 — 16 novembre 2026

Il [rapporto](../review-k10-uv-preview-2026-11-16.md) descrive difetti, contratti, implementazione, risultati e limiti. Questo archivio distribuisce soltanto testi; i sorgenti privati e gli script conservano il suffisso `.txt`. Nessun CAD, binario o oggetto.

- `root/`: baseline3388file,625archi consultati, copie prima/dopo, delta, hash del software e dei binari, build completa,30CTest,27Offset/18Shell, replay Release10gruppi eWayland79stati, Fraction e relativi negativi, audit finale e architetturale.
- `core/`: freezeV2 e sorgenti di produzione, probe7guardie,10gruppi privati, contratti/limiti e attribuzione della metrica. Manifest JSON del sottogruppo verificato senza modificarlo.
- `tests/`: fonte del test/exporter, baseline negativa, V2normale/sanitizer, comandi e hash. Esattamente16JSON:8baseline e8finali. Gli8export della Release finale sono identici ai finali archiviati, come attesta `root/release-replay.json`; non si duplicano i dati. I192record fin rappresentano8corpi, non192difetti.
- `ui/`: PRE7, header permanente, wrapper, prove finali offscreen/Wayland e negative, primo fixture conservato, comandi/hash e audit indipendente del prototipo kernel.

La regressione UV genera il box in memoria e non dipende da un CAD utente. Il target permanente può riesportare i coefficienti: `forgecad-release/kernel/forgekernel_offset_pcurve_tests /tmp/cartella-nuova`. Per la verifica Fraction, ricreare in una directory temporanea `fraction_common.py` e `verify_fraction.py` dai rispettivi `.txt`, quindi eseguire `python3 verify_fraction.py <cartella-json> <risultato.json> --expect-pass`. Il parser supporta le famiglie esportate e richiede pesi positivi e copertura completa del dominio. I comuni helper razionali sono riutilizzati dalla precedente prova indipendente degli offset; nessun oracle C++ è chiamato. `root/release-verify.py.txt` documenta anche confronto bytewise e controllo negativo di un polo alterato.

PRE7 si esegue col runner reale: `forgecad-release/forgecad_view_tests --preview-selection-state`; offscreen oWayland secondo ambiente. CLI eCTest isolano le preferenze. Il fixture usa callback reali dopo il normale annullamento del picking e verifica token inizialmente non cancellato. Le prove attestano stati/worker/pannelli, senza certificare il rendererGL.

ASan/UBSan/LSan coprono due TU kernel più il driver; il resto è l’archivio Release dichiarato nei metadati. Nessuna soppressione, runV2unrestrictedpositivo. Il precedente errore ptrace è conservato e non contato come successo. L’header finale differisce dal freeze solo nel commento su throw/infinito; il test è riformattato. L’audit verifica i token C++, i sorgenti di produzione e i binari effettivi.

`SHA256SUMS` alla radice copre tutti gli artefatti finali escluso sé stesso. I manifest separati `core/manifest.json`, `tests/SHA256SUMS.txt` e `ui/SHA256SUMS` restano immutati e vengono ricontrollati. I quattro blocchi precedenti sono verificati separatamente(95/146/172/267artefatti). Gli script di sessione contengono percorsi temporanei e assert riferiti a questa baseline; non eseguirli indiscriminatamente su altri checkout.
