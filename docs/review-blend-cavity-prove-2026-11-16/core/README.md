# Raccordo cavità: consegna finale congelata

16 novembre 2026. Fonte `kernel/fk_blend_surface.cpp` SHA256 `dfad2aa7ca0996210a2882a349d518e9746989ea19b132ed8a459dfe53909ca8`, consegnata come `fk_blend_surface.cpp.txt`. Nessuna scrittura di produzione dell'autore di questa consegna. Patch/base hash descrivono soltanto questo modulo. Build integrata e aggiornamenti di catalogo/memoria spettano al parent. Nessun CAD o binario nel pacchetto.

## Causa misurata

La fixture pubblica `blend_cavity_loft.body`, intero contorno da Edge10, raggio0.5mm, fallisce;1.1 e1.675 passano. Prima del rifiuto, fin3 della faccia0 usa edge3 (identità curva corrispondente a model51) per una chiusura `traceBetween`. Non è un difetto del fit dei contatti RowFit: i loro scarti sono nell'ordine1e-8 o inferiori.

La vecchia chiusura contiene32campate cubiche,97poli. Nell'ultima campata[0.99999999906867743,1] il controllo misura0.272740619754mm, contro fitTolerance3.9746646702461784e-9mm, ma la soglia minima del parametro1e-9 autorizza comunque il tratto. Il valore non entra nel gap pubblicato. Una transizione geometrica di circa0.323mm è compressa in9.3e-10 di parametro.

Inoltre `solve` controlla soltanto la coincidenza S/T alla fine; manca il controllo del quarto vincolo. Il nodo s=0.99999999906867743 ha S.v=0.99999999999976596: scarto9.31e-10, maggiore della soglia1e-13 del vincolo. Due rami con S.u115.071/114.749 vengono uniti senza un controllo valido.

Il risultato BAD è realmente distante dal supporto: residuo2.0192233220685673e-5mm al testimone t0.99999999930150807, oltre edgeTol3.8118941363275445e-7. La proiezione cade internamente al dominio UV. Ricerca più profonda e verifica razionale indipendenti del parent/auditor confermano separazione, attribuite ai rispettivi pacchetti; qui non si dichiara una nuova prova continua del proiettore. La vecchia diagnosi «contatto esce dal dominio» non identificava la causa.

## Correzione e budget

- `traceBetween` conserva la firma privata esistente con un ultimo argomento opzionale fixedAxis=1. La continuazione può fissare V come prima oppure U originale. Nessuna trasposizione, nuovo carrier, nuovo fit o API pubblica.
- La coordinata nota viene mantenuta esatta durante i passi Newton. Alla fine si verificano finitezza, residuo S/T e quarto vincolo con le soglie precedenti, nelle rispettive unità.
- Una campata minima non autorizza più un errore noto oltre fitTolerance. Resta il controllo fisico tangente già esistente; ogni quantità è controllata per finitezza prima di max/evaluate. Gli ulteriori midpoint devono essere distinti e nella direzione corretta.
- I nuovi fallback della delta e della kappa operano sugli stessi supporti e gli stessi endpoint, dopo il fallimento dei tentativi precedenti. Per la delta si tenta prima la parametrizzazione corda già usata altrove, poi U stabile/smorzata. Il fit resta campionato: questa correzione NON certifica tutte le curve implicite e NON migra un ulteriore sito K10.
- Il nuovo U mantiene ESATTAMENTE gli endpoint originali e il check endpoint originale entro fitTolerance. Un prototipo ammetteva un solve alternativo al seed: eliminato prima del freeze. Le quattro misure originali sono `direct=1`, displacement0, max gapS/T1.017e-11mm contro fit circa4e-9. Il precedente endpoint BAD spostato da Newton non è usato per giustificare budget più larghi.
- La curva corretta torna indietro in V: delta1 Vmin0.997869001000697<w0.999768268880264; delta2 Vmin0.997694486013981<w0.999748677141111; Vmax1. Questo spiega perché la parametrizzazione V non è valida sull'intera connessione. Entrambe restano nel carrier[0,1]. Il nuovo tentativo deltaU usa ESATTAMENTE il filtro UV1e-6 della kappa; non impone monotonia V. `Cut.sV` controlla nodi della continuazione, NON è prova continua di assenza di estremi tra nodi. I contorni reali finali vengono verificati separatamente dai test.
- Limite algoritmico cumulativo200000 passi di continuazione, oltre alla guardia di raffinamento preesistente; controllo di avanzamento e cancellationPoint nei cicli Newton/continuazione/raffinamento. È un limite di lavoro in passi, non una garanzia di tempo globale. OperationCancelled resta distinto da domain_error e attraversa i fallback senza essere convertito.

## Prove e attribuzione

Probe pubblico finale con la fonte congelata: r0.5/1.1/1.675 PASS,18facce e checkBody completo0. Compilazione privata con ESATTAMENTE il command CMake del target: `-O3 ... -O2 -g -ffp-contract=off`, quindi effectiveO2 con debug e nessuna contrazione FMA; non è una build O3 pura. Zero warning nel modulo. Dettagli in compile-command.json e compile-candidate.log. Non è una build completa dell'app.

Probe cancellazione privato:3/3 PASS (token iniziale, token armato durante continuazione alla quinta evaluate(order1), recupero). Include la fonte esatta e una Surface diagnostica delegante Plane che usa il percorso virtuale generico; nessuna API di test aggiunta in produzione. Nessuna pubblicazione parziale.

Audit statico indipendente del provider finale dfad2aa7: nessun blocco individuato; pacchetto separato `/tmp/blend-cavity-review/audit/candidate`, manifest913cd4c86e6c39972a2d86f1376cb08422297c89e65f9948b1861787bf3c0661. I log endpoint/V sono diagnostici del prototipo precedente alla sola aggiunta di guardie/formattazione; dichiarano le misure effettive, senza attribuire loro una nuova esecuzione sull'oggetto finale.

Verifiche finali dell'agente test sulla fonte dfad2aa7:

- Replay pubblico8/8 PASS:5varianti equivalenti r0.5 (ordine diretto/inverso,seed17,posa rigida,scala2.5),2controlli r1.1/1.675 e rifiuto specifico del raggio2.1 che consuma la faccia. Input immutati,18facce/10generate dichiarate,nessuna pcurve mancante,check/roundtrip/tessellazione senza errori. Ordini/seed r0.5 producono lo stesso body byte per byte; errore massimo del raggio circa1.36e-10mm.
- Regressioni permanenti6/6 PASS in23.64s; replay8varianti29.50s,entro timeout90 del nuovo target. Sulla fonte finale dei test,baseline2/6PASS e4FAIL geometrici. Il vecchio0/6 si riferisce a un'assunzione di test scartata: history è OUTPUT e viene storicamente svuotata a inizio operazione. I2test di cancellazione/rifiuto verificano corpo immutato e history vuota, compatibili con il contratto precedente; nessuna modifica a fk_blend.cpp e nessuna nuova anomalia dedotta da quel fixture.
- Audit delle fin effettive:7/7corpi,734fin,426917campate sull'unione dei breakpoint edge/pcurve,2134585punti;nessun residuo pcurve/endpoint oltre i budget originali. Rapporto massimo pcurve/budget circa1/3,endpoint/budget circa0.990099. È controllo campionato,non certificato continuo globale.
- ASan/UBSan/LSan mirati:8/8probe e6/6regressioni,entrambi exit0,539.2544s e340.3534s. Metadata finali letti in `/tmp/blend-cavity-review/tests/sanitizer-run-final.json` e `sanitizer/build-metadata.json`:detect_leaks=1,halt_on_error=1,nessuna soppressione,nessuna diagnostica. Quattro TU strumentate complessivamente (modulo blend,probe,main,test),ogni eseguibile ne collega due/tre;resto kernel Release. Nessun certificato globale di memoria/leak dell'app o dell'intero kernel.

Il parent riferisce sulla stessa fonte integrata:build completa app234.33s,34CTest,Blend62,Helix5,Fill25,replay8Release e7output byte-identici,Wayland e mappa PASS. Queste prove appartengono alla consegna del parent,non sono state rieseguite o attribuite a questo agente.

## Integrazione

Il parent ha integrato la stessa fonte dfad2aa7 dopo PASS delle prove concordate. Per rieseguire l'integrazione su una baseline:verificare base-source-hashes.json,applicare prototype.patch.txt oppure copiare la fonte .txt al percorso kernel/fk_blend_surface.cpp,quindi verificare stage-source-hashes.json. Aggiungere il test separato e CTest TIMEOUT90 secondo la consegna test/root; non serve un nuovo modulo di produzione o una nuova dipendenza. Aggiornare la descrizione di fk_blend_surface nel catalogo per il vincolo completo, rifiuto al floor e continuazione terminale U sullo stesso carrier; rigenerare/checkare graph/HTML e archi nei due versi. Build completa e runner integrati appartengono al parent.
