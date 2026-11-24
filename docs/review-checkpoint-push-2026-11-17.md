# Checkpoint commit e push — 17 novembre 2026

Stato remoto finale: **CI44142011489 PASS**, Linux121/121 e macOS124/124 CTest, entrambe le compilazioni complete riuscite sul software78d7268. Le prove locali successive restano separate da quel run.

Richiesta esplicita dell’utente: pubblicare le modifiche accumulate, poi continuare la revisione.

Commit `18d0adcc599335407942f7f7acc5d5266071a402` su `main`, push completato e SHA remoto verificato. Include le correzioni locali K10 e API legacy, UI11 e le prove storiche. Controllati 40 pacchetti/5712 payload tramite checksum, hash dei sorgenti integrati e degli eseguibili già verificati, mappa aggiornata 804 nodi/11741 relazioni. Nessuna nuova build né ripetizione UI necessaria per questo checkpoint senza modifiche software.

Controllo whitespace PASS sui sorgenti e rapporti attivi; 124 avvisi riguardano solamente log grezzi/patch negli archivi sigillati, conservati byte per byte. Nessun ELF, libreria statica, CAD utente o HTML/JSON generato aggiunto.

[CI della revisione](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/44132066010) avviata: esito ancora da verificare. Le prove locali pregresse non equivalgono a una CI verde sul nuovo commit.

Dopo il push ripresa la quadratura K10: underflow finito nei prodotti con i pesi; due agenti Sol controllano il rischio e le regressioni in sola lettura. [Memoria precedente](history/claude-prima-checkpoint-push-2026-11-17.md).

## Esito e riparazione successiva

Prima CI terminata: build completa PASS su Linux/macOS; test FAIL su entrambe per `ProjectedCutBandAlongExistingEdges` e timeout cumulativo del runner delle tre pose. Corretti il chiamante della classificazione e la registrazione dei casi, senza rimuovere guardie o aumentare il limite della singola prova. [Rapporto](review-ci-repair-2026-11-17.md).

Secondo commit `78d72684e4e6dfa45ace425bb1d8e776a57d41bc` pubblicato su `main`, remoto verificato. [Nuova CI](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/44142011489) in corso: non ancora dichiarata verde. Le riparazioni CI sono versionate; le nuove correzioni di prodotti pesati e arresto adattivo K10 rimangono locali, verificate con app completa,13 gruppi+sanitizer e mappa. [Prove numeriche](review-k10-underflow-2026-11-17.md).

Metadati delle prove pubblicati in `6294aa37e63c51434fd2754ca7b1398d8a281ffe`: copie CMake rese inerti, contenuti e sigilli originali preservati. Commit soltanto documentale con CI saltata; non cambia il codice di78d7268 e non sostituisce la CI44142011489 ancora in corso. SHA remoto verificato.

Verifica remota successiva: CI44142011489 conclusa con **successo su Linux CPU e macOS CPU/Metal**, compilazione e tutti i CTest configurati PASS. Il run verifica il software78d7268; non include il lavoro numerico locale successivo.
