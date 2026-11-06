# CT0578: regressione nell’unione del filetto — 6 novembre 2026

## Diagnosi e correzione verificata

Copia congelata di CT0578.prt, SHA256 `cf25833b681806b8b806ad13ce92c09853eda64595e445d3c72270e77502c682`, 10.671.245 byte, 36 feature. Nessuna scrittura dell’originale. Sia la lettura con budget cache 4 GiB sia quella senza cache richiedono il ricalcolo: le geometrie salvate non sono riutilizzabili nella versione corrente.

Primo errore Sweep 10: unione con Rivoluzione 6, risultato non chiuso, edge 6 senza due fin opposte. Seguono 23 errori dipendenti. Gli operandi isolati hanno 8 e 260 facce, entrambi validi. Riprodotto anche con le derivate precedenti all’ottimizzazione del 6 novembre: non dipende da quella modifica.

`FaceParameterClassifier` rifiutava subito UV appena fuori dal dominio NURBS. Le SP-curve di intersezione possono avere, per esempio, u=1,00000000028 pur individuando il bordo entro circa 3e-10 mm. La classificazione 3D correttamente restituisce Boundary, quella UV restituiva Outside, eliminando i tagli sulla faccia cilindrica. Ora il controllo della distanza fisica dal bordo precede il rifiuto dei parametri esterni. Restano invariati budget e guardia per punti realmente esterni; nessun clamp, modifica della geometria, riaggancio o ripiego mesh.

Sonda con oggetti corretti: unione 265 facce, una shell, zero problemi; ricalcolo integrale 36 feature, zero errori, 17,324 secondi. Nuovi test indipendenti da OCCT: budget fisico su domini scalati, fori, punti esterni, parametri non finiti e avvolgimenti cilindrici. La regressione di bordo fallisce sulla libreria precedente; entrambi i test passano con la correzione. Applicazione completa e runner Release compilati; verifiche finali riportate sotto.

Artefatti locali: `/tmp/ct0578-regression-20261106`.

## Verifiche finali

- Applicazione `forgecad-release/forgecad` e runner compilati completamente. 17/17 CTest mirati PASS; filtri kernel Boolean 38/38 e Sweep 13/13 PASS. Successivamente eseguita anche la suite completa `forgekernel_tests`: **468/468 PASS**, uscita 0 entro il limite di 900 s usato dalla CI. I due nuovi casi del classificatore restano nel target indipendente `forgekernel_face_parameter_tests`, incluso nei 17 CTest mirati.
- QA nativa Wayland sulla copia: apertura 18,118 s nella prima prova e 20,361 s nella seconda; 36 feature, 30 B-rep, zero errori e tutti i controlli topologici superati. Sweep 10: 265 facce; risultato finale: 353 facce, una shell. Finestra e viewport acquisiti e ispezionati; nessuna modifica alle preferenze dell’utente.
- Salvataggio in un nuovo documento indipendente in /tmp: 30 cache accettate alla riapertura, completata in 8,115 s senza errori; identità delle feature conservate. Il confronto iniziale dei byte rilevava differenze in 21 B-rep. Confronto geometrico separato: topologia identica e scarto massimo zero su 1.018.055 posizioni campionate di vertici, edge e superfici decodificati. Non si dichiara identità binaria del round-trip.
- Mappa rigenerata: 626 file, 8.858 relazioni, 305 unità C++, zero diagnostiche. `--check` PASS; verificati call/include e collegamenti nei due versi; tre viste HTML renderizzate con QtWebEngine/XCB e controllate. HTML/JSON restano ignorati.
- SHA256 originale e copia congelata ancora identici a fine QA. Nessuna scrittura del CAD originale, nessuna modifica alle tolleranze. La build aggiornata richiede il riavvio dell’applicazione.

## Pubblicazione

L’utente ha richiesto commit/push del fix e delle ottimizzazioni Surface219 ancora locali, oltre al controllo dell’ultima CI.

Run precedente [42250382792](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/42250382792), commit bbd2533: macOS compilazione e test riusciti; Linux cancellato al timeout di 60 minuti durante il download APT dal mirror Azure (OpenJDK, CMake e altre dipendenze transitive). Configurazione, compilazione e test Linux non erano iniziati. Il precedente run 42242095481 su e761f43 era riuscito su entrambe le piattaforme.

Workflow corretto: solo nei runner Linux effimeri, mirror Azure sostituito con `https://archive.ubuntu.com/ubuntu`; firme e pacchetti invariati. Connessioni con timeout di 30 secondi e tre tentativi, step massimo 15 minuti; log delle dipendenze incluso negli artifact. Non si aumenta il timeout dei test e non si modifica la macchina dell’utente. Verificati gli endpoint ufficiali (HTTP 200), sintassi YAML/Bash e frammento Python prima della pubblicazione. La nuova CI sul commit pubblicato è riuscita su entrambe le piattaforme.

Pubblicato `a6248eaef8dd5b19fb46fca91a0842c9275b90b2` su origin/main. Nuova CI [42266591981](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/42266591981) **success**, compilazione e test riusciti su entrambe le piattaforme:

| Piattaforma | Job completo | Dipendenze | Compilazione | CTest |
| --- | --- | --- | --- | --- |
| Linux CPU | 20m57s | 72 s | 734 s | 75 PASS, 2 SKIP; 436,60 s |
| macOS CPU e Metal | 15m56s | 106 s | 410 s | 79 PASS, 1 SKIP; 422,13 s |

Su entrambe sono riuscite le nuove suite del classificatore UV e delle derivate B-spline. La suite generale kernel è passata in 392,81 s su Linux e 357,78 s su macOS; anche i test Metal sono passati. Skip dichiarati: `forgecad_gpu_broad_phase_tests` solo su Linux e `forgecad_overlay_uniform_tests` su entrambe. Nessun test fallito; gli skip non vengono contati come PASS.

Il problema APT precedente è superato: installazione Linux completata in 72 secondi. Log e risultati del run sono acquisiti nella directory QA. Il successivo commit contiene soltanto la memoria operativa e questo rapporto, con `[skip ci]` per non rilanciare la stessa compilazione su modifiche documentali.
