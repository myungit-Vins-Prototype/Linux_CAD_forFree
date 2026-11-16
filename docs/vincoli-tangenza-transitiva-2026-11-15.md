# Tangenza tramite coincidenze transitive — 15 novembre 2026

## Richiesta e correzione della diagnosi precedente

L'utente osserva correttamente che una tangenza gia' vincolata deve essere rispettata,
anche disegnando a mano. La precedente correzione analitica dei punti di Schizzo 4
era un aggiramento sul documento. Non risolveva il difetto generale del risolutore.
Frenet su una retta resta un problema distinto, gia' gestito dalla sweep a torsione minima.

Il file originale e' `/home/myungit/Documenti/Cad_Example/exhaustMainfold.prt`;
si usa la copia congelata dell'indagine precedente, senza scrivere sull'originale.

## Causa accertata

`cad_constraints.cpp::tangentContact` cercava esclusivamente un vincolo Coincident
diretto tra un punto della prima entita' e un punto della seconda. In Schizzo 4:

- Arco 1, estremo 2 -> Segmento 7 di costruzione, estremo 0 -> Segmento 11, estremo 0.
- Arco 2, estremo 1 -> Segmento 8 di costruzione, estremo 0 -> Segmento 13, estremo 0.

I riferimenti qui sono nomi UI: gli indici dei segmenti nei dati sono rispettivamente
6/10 e 7/12. Sono connessioni esplicite, non coincidenze ricostruite per vicinanza.

Il contatto non riconosciuto sceglieva l'equazione globale retta/cerchio
`abs(distanza(centro,retta))-raggio`. Vicino alla soluzione, quando il contatto e'
gia' imposto dalle coincidenze, questo residuo dipende quadraticamente dall'errore
angolare: un residuo piccolo non garantiva la tangenza direzionale richiesta al giunto.

Prova precedente alla correzione, sia solver sparso sia denso:

- `solveSketch` dichiara successo a zero iterazioni, residuo 3,7417180465e-11.
- Errori direzionali (valore assoluto del prodotto fra direzione e raggio unitari):
  sinistra 1,9546009525e-6, destra 1,7160207758e-6.
- Sweep rifiutata dal controllo G1 interno.
- Aggiungendo soltanto le due coincidenze dirette, senza muovere coordinate a mano,
  la versione precedente risolve in due iterazioni; angoli circa 1e-16 e sweep riuscita.

## Correzione generale

`tangentContact` conserva la precedenza dei contatti diretti e cerca anche nel grafo
dei vincoli Coincident validi fra punti. Una visita con insieme dei riferimenti gia'
raggiunti segue le catene esplicite e identifica i due punti propri delle entita'.
Nessuna associazione per coordinate o prossimita', nessuna modifica delle tolleranze,
nessun vincolo nuovo aggiunto al documento e nessuna regola per questo specifico CAD.

Il contatto riconosciuto usa gia' l'equazione direzionale delle tangenti presente nel
solver. La stessa risoluzione e' usata dalla misura dell'errore, dai blocchi del
Jacobiano, dall'analisi dei gradi di liberta' e dai controlli sulle maniglie.

Consultate prima della modifica descrizioni e chiamate nella mappa: viewport schizzo
-> solveSketch; tangentContact -> equazioni/buildBlocks/gauge; constraintError ->
diagnostica e buildGeometry. Il nuovo header di test e le descrizioni del catalogo
architetturale sono aggiornati insieme ai sorgenti.

## Risultato sul modello, senza modifiche manuali

La chiamata ordinaria `solveSketch` sullo Schizzo 4 originale, con gli stessi vincoli,
riesce in due iterazioni su entrambi i solver. Residuo 2,8421709430e-14;
errori direzionali sinistra 1,3357679968e-16, destra 6,6788399841e-17.
La successiva ricostruzione delle curve e la sweep hanno esito positivo.

Prodotta una nuova copia `outputs/exhaustMainfold-sweep-risolutore.prt` nella chat:
solo risoluzione ordinaria dello schizzo originale, rigenerazione delle sette feature
e aggiunta di Sweep 1 a torsione minima, corpo nuovo. Nessuna formula correttiva sui punti.
Salvataggio/riapertura/ricostruzione di tutte le otto feature senza cache riusciti;
quattro facce, checkBody vuoto e zero facce fallite nella tassellazione.
La prima copia analiticamente corretta resta disponibile e non e' stata sovrascritta.

## Verifiche

- Nuovo test fallisce sulla versione precedente: il vincolo e' dichiarato soddisfatto,
  ma le direzioni geometriche non sono tangenti.
- `tests/test_tangent_contacts.h`: 96 combinazioni fra solver denso/sparso, scale
  0,1/1/10/100, due pose, contatti diretti e catene con uno/tre intermedi, ordine
  inverso dei riferimenti/vincoli; confronto indipendente delle direzioni geometriche.
- Conflitto con entita' entrambe fisse segnalato senza mutare il documento;
  nessun contatto inventato per vicinanza; codec nativo e Undo/Redo verificati.
  Fixture con vettori legacy neutrali allineati, come i documenti reali.
- Il test viene eseguito anche da `--solver-partition`; nessuna soglia ridotta nel test.
- Runner dedicato con la correzione: PASS. App e runner generale completi compilati.
- **4/4 CTest PASS**: solver_partition (include i 96 casi), handle_constraints,
  sweep_reference e curve_extraction; 2,47 secondi totali, offscreen.
  Nessuna nuova prova UI nativa; le istruzioni manuali sono verificate nel codice dei comandi.
- Mappa: 723 file, 10397 relazioni, 352 TU, zero diagnostiche; --check PASS.
  Descrizioni, nuovo header, chiamate bidirezionali e leggibilita' verificati nel browser.

## Uso a mano

Nella versione precedente, il rimedio verificato consiste nell'aggiungere Coincidente
direttamente fra gli estremi Arco 2/Segmento 13 e Arco 1/Segmento 11; Tangente e' gia'
presente. Non occorre trascinare i punti o modificarne le coordinate.

Con l'app corretta, anche le catene di coincidenze gia' presenti devono funzionare:
si puo' riapplicare Tangente sui due giunti per richiamare il risolutore. Non occorre
aggiungere coincidenze ridondanti. La versione gia' aperta richiede riavvio per
caricare il nuovo eseguibile; nessun processo utente e' stato chiuso.

## Integrita' e modifiche concorrenti

Snapshot iniziale usato in tutte le prove: SHA256
`9acfe5a752b96675633e5e094e2c69243f4f8bab25b43749af088b623f9b9cdd`.
Nessuna scrittura eseguita sul CAD originale. Alla verifica finale il file sorgente
ha SHA256 `5fbfa5d55b02188b297f14b7c19026eaf79d41b39a7c6c65c8140623d4e6e73e`
e mtime 15/11 09:56:15 del filesystem: salvataggio esterno durante il lavoro,
non sostituito con lo snapshot. La nuova copia si riferisce quindi allo stato iniziale.
SHA256 copia risolta dal solver:
`cab3f823daedca5c01ccb7381001c17578a81974d50a478b7f28cdbbd0cd6665`.
Nessun commit/push.
