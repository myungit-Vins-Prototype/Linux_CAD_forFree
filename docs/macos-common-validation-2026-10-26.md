# Parti comuni Linux: verifica macOS del 26 ottobre 2026

Le modifiche comuni sviluppate su Linux nel commit `27e2bc0` sono state compilate e verificate anche su macOS ARM64. Il lettore cache accelera l'apertura dei due documenti locali; la prova ha inoltre individuato e corretto un difetto OBJ dipendente dalla contrazione FMA. I sorgenti CUDA restano quelli della sessione Linux.

## Apertura con cache

Mediane di tre processi nuovi per binario e documento, qualità 1 e pipeline CPU differita in entrambe le versioni:

| Documento | Prima | Attuale | Rapporto | Picco RSS prima → attuale |
| --- | ---: | ---: | ---: | ---: |
| `flacone_recuperato.prt` | 0,620 s | 0,388 s | 1,60× | 290 → 276 MiB |
| `2550_ASSIEME-ST5674-COMPLETO.prt` | 10,889 s | 4,027 s | 2,70× | 4.068 → 3.690 MiB |

Intervalli completi: flacone 0,618–0,637 s prima e 0,380–0,403 s attuale; assieme 10,579–10,898 s prima e 3,716–4,427 s attuale. La sola lettura dell'assieme passa da 10,799 a 3,910 s; il successivo caricamento della vista resta circa 0,1 s. Risparmio mediano sull'apertura: 37,5% e 63,0% rispettivamente. Queste sono misure di apertura con cache utilizzabile, non di ricostruzione completa o rendering.

Il riferimento è la sonda finale del precedente [confronto Mac](benchmarks/mesh-parallel-2026-10-25.json), già dotata della pipeline mesh parallela. È stato conservato prima della compilazione e identificato tramite l'hash già pubblicato `3e5dd716e40b5801b2c254c9949ac06b0ff7e1a470e83dda36afe0cb538d0368`. La stessa sorgente della sonda, con SHA256 CommonCrypto fuori timer, è stata ricollegata agli oggetti attuali. Il confronto riguarda soprattutto il lettore a blocchi e le copie evitate, senza attribuire il guadagno a CUDA.

Ordine prima/attuale, attuale/prima, prima/attuale; 12 processi sequenziali, senza build o generatore architetturale concorrenti, profilazione disabilitata e `caffeinate` limitato alla prova. Picco RSS fino alla fine del caricamento tramite `getrusage` macOS, convertito da byte a MiB; firme calcolate dopo il timer. Limiti della prova: 8 GiB/300 s, mai raggiunti. Nessuna forzatura del disco freddo o controllo di frequenze, temperatura e carico esterno. Il primo tentativo nel sandbox è stato interrotto dal divieto di `ps` prima di produrre campioni completi; conservato separatamente ed escluso. La serie completa ha eseguito il monitor dei propri processi fuori sandbox.

## Identità dei file e risultati funzionali

Il flacone è quello precedente: formato 50, 24 feature/11 schizzi, SHA256 `f037619a69380b330c313169812769235f092a4d06c9726995c5b24910c36c44`. Entrambi i lettori recuperano 16 B-rep e quattro mesh dalla cache; la vista finale contiene 19.626 triangoli visibili, senza errori feature.

L'assieme locale è cambiato: formato 54, 1.203 feature, SHA256 `709d629c8703d5c62157c14a2b3683146f28093411a1c3362f0b5037bb0dab81`. La cache dichiara 969.408.197 byte, entro il limite di 1 GiB. Entrambi i lettori recuperano 1.199 B-rep e 1.191 mesh; 11.746.419 triangoli visibili e gli stessi quattro record importati vuoti. Non sono 1.203 solidi validati e questo documento non coincide con gli assiemi dei precedenti rapporti Mac/Linux.

In tutti i processi: B-rep binari identici per tutti i corpi; buffer dei vertici e delle normali visibili identici; stessi conteggi topologici, copertura delle facce, visibilità ed errori. La sonda non confronta punto per punto polilinee, mappe triangolo-faccia, etichette o matrici. Non sono stati eseguiti massa, `checkBody` generalizzato o QA OpenGL. I documenti originali sono stati soltanto letti e i loro hash finali coincidono con quelli iniziali.

Sul flacone, anche ricostruzione completa e modifica in memoria del raggio 10 → 10,01 mm producono B-rep/vertici/normali identici prima e dopo, senza errori. Sono controlli funzionali aggiuntivi, un'esecuzione per binario e scenario, esclusi dalle conclusioni prestazionali.

## Correzione OBJ emersa su ARM

Il nuovo test CPU di export, superato su Linux, falliva sul Mac: una faccetta aveva indici di posizione `1902/2129/2129`, ma il prodotto vettoriale restituiva area non nulla, circa `2,58e-19`. Un programma separato conferma che `cross(v,v)` può lasciare un residuo con la contrazione FMA predefinita del compilatore ARM; con `-ffp-contract=off` torna zero.

`buildQuadObj` ora esclude esplicitamente triangoli e quadrilateri con indici di posizione ripetuti dopo la saldatura. Il parser indipendente dei test controlla anche che ogni poligono abbia coordinate distinte. Il caso B-spline polinomiale e quello NURBS con foro passano dopo la correzione, senza aumentare tolleranze né cambiare il kernel. Questo intervento non corregge i 574 triangoli degeneri del viewport segnalati nel [rapporto mouse2 Linux](mouse2-mesh-cpu-cuda-2026-10-26.md).

## Verifiche e seguito Linux

Applicazione `forgecad-release/forgecad`, runner e suite export ricompilati. Regressioni finali: 17 CTest PASS, zero FAIL, un test Metal SKIP 77 perché il dispositivo non è accessibile nel sandbox. Le due richieste esplicite CUDA sullo stub restituiscono 77 prima di caricare il documento o esportare. Comparatore numerico: nove test PASS con Python/NumPy del runtime disponibile; nessuna dipendenza installata. La build iniziale segnala warning preesistenti delle tavole, esterni al commit Linux; la ricompilazione finale del fix non introduce warning.

Mappa locale aggiornata: 482 file, 6.615 relazioni, 233 unità senza diagnostiche; `--check` PASS. Verificati siti entro le righe e quattro collegamenti di chiamata nei due versi: test→export, export→adattatore CUDA, export→tassellatore, lettore documento→lettore a blocchi. I file generati restano ignorati da Git. Il grafo Mac non prova i rami AST CUDA.

Su Linux/NVIDIA resta da ripetere la suite export dopo questo fix comune: CPU, CUDA generico e specializzato. Restano aperte altre GPU, componenti grandi connesse del solver, degenerazioni viewport e migrazione generale del kernel sulla GPU. Nessun risultato NVIDIA nuovo è dedotto dalle prove Mac e nessuna installazione desktop è stata sostituita.

Dati, hash dei sorgenti/binari e tutti i campioni: [rapporto JSON](macos-common-validation-2026-10-26.json). Sorgente/compilazione della sonda, script del confronto, log, JSON completi e riproduzione FMA sono conservati localmente in `forgecad-release/benchmarks/macos-common-2026-10-26/`; CAD e dump geometrici non sono inclusi nei rapporti.
