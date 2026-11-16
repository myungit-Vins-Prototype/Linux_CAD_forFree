# Verifica GitHub Actions, 16 novembre 2026

Richiesta dell'utente: dopo le correzioni fare commit/push e controllare gli ultimi commit falliti su GitHub. Analizzati i quattro run falliti del12–14novembre e lo stato dei singoli job tramite GitHubCLI. **La compilazione riesce su Linux e macOS; fallisce sempre il test `BlendCavityWholeRimKeepsRadiusHistoryAndClosedTrims`, nella suite `forgekernel_blend_cavity_tests`.** Eccezione: contatto fuori dominio del fiancoF0. Il test generale del kernel passa. Non serve modificare il workflow per mascherare quel fallimento.

| Commit | Run | Compilazione | Fallimento |
| --- | --- | --- | --- |
| ce03a6d | [43636595574](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/43636595574) | Linux/macOS PASS | WholeRim |
| 2b180e4 | [43629615411](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/43629615411) | Linux/macOS PASS | WholeRim |
| 8914fd1 | [43496897328](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/43496897328) | Linux/macOS PASS | WholeRim |
| d58c235 | [43308279339](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/43308279339) | Linux/macOS PASS | WholeRim |

La correzione K12 già implementata durante questa review riguarda questo percorso. Il test e la fixture sono byte-identici all'ultimo commit ce03a6d: `test_blend_cavity.cpp` SHA256 `0e0cc0c8cfbeb77a6e43d83d29c51cdc258adb6e3a06ac5e429a0547379b73a5`, fixture `blend_cavity_loft.body` SHA256 `8d7d8d50e0bcbfb7e392cb86a17516547f874ee6a355954601f3a473164d39bc`. Raggi0,5/1,1/1,675, controllo raggio1e-7,18facce, dieci facce generate, codec/genealogia e tassellazione conservati. Nessuna soglia allargata, nessun test disattivato. [Correzione e prove K12](review-blend-cavity-2026-11-16.md).

Sulla build corrente finale: **8/8cavity PASS**, incluse WholeRim e transizioni; **28/28CTest PASS**, più regressioni mirate e Wayland. Compilazione completa app/test exit0. Un agente GPT-6.1 Sol ha inoltre configurato da zero con le opzioni CI e Clang23 Linux: controllo sintattico di14TU,8kernel/6app, tuttiPASS. Questo non è una compilazione macOS/Metal: SDK Apple assente. Controllate387TU e closure di656dipendenze locali: nessun sorgente mancante o nuovo file ignorato,50dipendenze nuove committabili da includere.

Il workflow Ubuntu26.04/macOS15, CUDAOFF, VIEWOFF/RIBBONOFF e confrontiOCCT invariato. I probe negativi `FORGEKERNEL_HAS_FLOAT_FROM_CHARS` su Apple selezionano il fallback previsto, non sono errori di compilazione del job. Tutti i nuovi moduli e test devono essere inclusi nel commit.

Remoto main controllato prima del commit: ce03a6d, uguale aHEAD locale. Commit e push autorizzati sul main corrente; la conferma CI del nuovo codice sarà registrata dopo il push. Non si attribuisce alla sola build locale il successo remoto. [Prove](ci-verifica-prove-2026-11-16/README.txt).

## Pubblicazione e verifica corrente

Commit codice e correzioni `c44975e56931c559ea3da3c794eaea66a41a304e` creato e push su origin/main riuscito. Albero pulito subito dopo il push. Nuovo [run43917116955](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/43917116955), sullo stesso SHA: in coda/in corso, esito ancora da verificare. I metadati del monitor sono conservati separatamente dalla diagnosi storica sigillata prima del push.

## Esito del primo push

Il run43917116955 è terminato con fallimento dei test su entrambe le piattaforme, dopo compilazioni riuscite. **Il precedente WholeRim/K12 è ora superato.** Nuovo unico fallimento nella suite generale: `OffsetLoftExtendedSeamsTrimBoth`, eccezione `extendSheet: giunzione fuori dal budget continuo`. Linux109/110CTest e macOS112/113CTest superati; suite generale482/483casi. La regressione si riproduce anche localmente con GCC e Clang: non è specifica di GitHub o del compilatore. Si corregge prima di un ulteriore push; nessuna CI verde dichiarata per c44975e. [Indagine e correzione](ci-sheet-join-2026-11-16.md).
