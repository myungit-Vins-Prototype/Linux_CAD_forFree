# Supporto.prt — prove del 16 novembre 2026

Il [rapporto](../supporto-booleana-2026-11-16.md) descrive causa, correzione, risultati e limiti. Questa cartella contiene testi: sorgenti/probe privati con suffisso `.txt`, comandi, log, coefficienti esportati, metadati e manifest. Nessun CAD originale, oggetto compilato o eseguibile è distribuito qui. I percorsi temporanei documentano la sessione precedente e vanno ricreati per una nuova esecuzione.

- `root/`: baseline dei 3115 file e 1125 archi consultati; snapshot/delta dei cinque file esistenti, sorgenti finali, build/24CTest/38Boolean, replay Release normalizzato, metadati output, architettura e audit finale. Script `.py.txt` documentano i controlli. `input-observation-after-resume.json` distingue il file attuale dalla versione congelata, perché l’originale è cambiato durante l’interruzione. Nessuna sovrascrittura.
- `initial/`: diagnosi congelata, codice di misura, trace del fallimento e identificazione degli operandi.
- `tests-baseline/`: 176 controlli baseline, con i fallimenti attesi separati dalle esecuzioni positive.
- `core/`: sorgenti privati finali, contratti, prove ed evoluzione della correzione. L’integrazione mantiene gli stessi sorgenti; il test permanente è soltanto riformattato e verificato per token.
- `independent/`: bound Fraction dai coefficienti originali, audit matematico/contrattuale e contributo UV; distingue le prove indipendenti dal codice implementato dallo stesso autore.
- `tests/`: gruppi baseline/stage/Release/sanitizer/retry/esplorazione distinti; 424 controlli finali senza overlay kernel, 65 di consegna, 15 retry iniettati, 51 contratto permanente e 78 driver esteso. Il manifest del gruppo copre 86 file, escluso sé stesso.

ASan/UBSan/LSan coprono due TU kernel e il driver; il resto della libreria è Release. L’esecuzione unrestricted dello stesso binario supera LSan senza soppressioni; il primo limite ptrace è conservato e non contato come successo. Offscreen esercita comandi reali, Undo/Redo, storia e codec, senza certificare il renderer GL. I testimoni campionati della frontiera non sostituiscono i certificati continui dei nuovi contatti/UV e non provano globalmente l’intera booleana.

La copia consegnata è indicata in `root/output-manifest.json`, con hash e nuovo UUID; il `.prt` rimane nell’area `output/` ignorata da Git. Le prove precedono l’interruzione e si riferiscono all’input SHA79887e91; il file originale corrente ha un’altra impronta e non è modificato dalla ripresa. Le directory temporanee precedenti non sono più disponibili. I sorgenti archiviati richiedono la copia dell’input esatto per riprodurre il CAD, mentre il test permanente è autonomo senza Qt o CAD.

`SHA256SUMS` alla radice copre tutti gli artefatti testuali finali, escluso sé stesso. I manifest dei sottogruppi sono verificati senza riscriverli. I tre manifest delle fasi precedenti sono ricontrollati separatamente.
