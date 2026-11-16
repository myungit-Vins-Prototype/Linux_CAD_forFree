# Audit indipendente K10 boundary

Policy e AGENTS/CLAUDE, rapporto mitre e grafo consultati prima del codice. Questo agente scrive soltanto in /tmp/boundary-review/audit; root mantiene la memoria condivisa.

## Baseline: difetti provati

1. `standardBezierPieces` applica una trasformazione di Moebius alle coordinate omogenee razionali e mantiene i knot di parametro. `extendSplineBoundary` usa questa funzione sul bordo originale: il luogo geometrico di ogni campata può restare uguale ma l'uguaglianza allo stesso parametro originale non è preservata. Testimone algebrico elementare: poli0/1, pesi1/4, parametro1/2; originale4/5, risultato1/2, scarto3/10. Il testimone non è una esecuzione di produzione.
2. Esportazioni reali del tester sul vecchio helper: quattro casi razionali, incluso posa/scala2.5, versoUV negativo e trim, falliscono il confronto Fraction allo stesso parametro con witness esatto. Il polinomiale passa. Le pcurve esportate sono dello staging Body, non fin post-build: nessun risultato pubblico del comando è certificato da queste esportazioni.
3. I controlli ISO e accordo curva originale su33punti accettano un bump B-spline stretto tra campioni. Il tester ha eseguito il vecchio riconoscitore e ottenuto guida presente con scarto nascosto0.01mm. Il testimone Fraction separato prova residuo1/4mm per una costruzione equivalente.
4. La regola `max(input tolerance, 2*observed extensionDeviation)` amplia il budget a posteriori. L'aritmetica ammette worst1.5cap sotto soglia2cap e produce tolerance3cap. L'esempio è source-derived, non una misura continua del massimo.

## Altri limiti distinti

- I cinque punti per campata della guida non coprono i knot dell'altro carrier. Un testimone indipendente usaS0 piano conU[0,2] singola campata eS1 con bump stretto centrato19/16, nullo ai cinque campioni1,1.25,1.5,1.75,2. Il puntoE(19/16) ha distanza dal carrier>1e-7mm: qualsiasi punto entrocap deve avere|u-t|<cap, ma tutta questa striscia ha z>=1/2-8cap>cap. Lo script distingue prova derivata dal codice da esecuzione reale. Il tester ha poi eseguito questo setup esatto sul vecchio helper: accettazione fuori supporto e mutazione diBody/Model confermate in baseline-hidden-support.log. La prima fixture del tester usava gli stessi knot stretti anche suS0 e veniva correttamente rifiutata: fixture corretta prima di attribuire la riproduzione al codice.
- Il margine5% del dominio e Newton non forniscono un certificato continuo. La selezione campionata può restare euristica se l'accettazione supportata richiede il bound continuo; le famiglie non supportate devono restare esplicitamente compatibilità campionata.
- Ricostruire automaticamente le pcurve dopo il commit non eredita un certificato della guida. Servono controllo/installazione su fin effettive, range dopo moveEnd, carrier effettivi e cap fissato prima della costruzione.
- Le superfici estese modificano carrier condivisi con altri bordi: una prova della sola ISO non implica preservazione globale del dominio originale. Il verificatore tensoriale indipendente controlla l'intero rettangolo originario.

## Prove indipendenti finora

`baseline-fraction-results.json`: cinque input reali del tester, dieci carrier sul dominio originale tuttiPASS (uppermax4.874831182328089e-14mm); confronto originale quattroFAIL/unoPASS; mappe staging8FAIL/2PASS. Sono controlli sui coefficienti IEEE originali, non su estrazioni del provider di produzione.

`verifier-selftest.json`: bilineare/knot originale interno, tensor razionale positivo, alterazione di un polo emesso rifiutata. Nuovo verificatore curve: identità upper0; negativoMoebius rifiutato con squared9/100 al parametro1/2. I campioni possono mostrare l'alias del vecchio codice; non sono mai presentati come certificato.


## Candidato congelato v1: esito definitivo dell'audit

Review dei sorgenti temporanei: section73e6e130afa8fcfb804a90fcc4c34080261f09a159dbb9a8a6178238e5d5f639; surface392bc742f3b5c455938bcc21882ecf57d2ca1459d5b570e56c0d99c8026d0e96; header13e09f031b0c6acb7f43c47c6f6a9bb2886f3d79c7209372f040f812b5768966. Copie recensite e hash in questo pacchetto. Nessuna modifica di produzione o documentazione condivisa da questo agente.

Il nuovo certificato usa un witness cubico soltanto ausiliario. La somma outward dei due bounds, costruiti sui coefficienti ORIGINALI della curva e della composizione, limita la distanza effettiva tramite disuguaglianza triangolare. Il witness non sostituisce l'edge. La concatenazione omogenea con una scala comune per pezzo conserva il parametro; un nuovo controllo copre il risultato effettivo contro l'originale. Il cap nasce prima della costruzione: max(2*scaledAccuracy(1e-9),source edge tolerance), qui3e-9mm. Il ramo riconosciuto non accresce la tolleranza dell'edge.

Famiglie riconosciute prima delle prove: entrambe le pcurve LineISO o B-spline grado1, singola campata, pesi uguali, anche wrapperTrimmed; entrambi i carrier B-spline. Anchor e intervallo delle pcurve originali conservati; tratti esterni con anchor propri. Se una famiglia non è riconosciuta rimane la compatibilità campionata esplicita. Un fallimento nel ramo riconosciuto è terminale e non ricade in Newton. La ricerca di una finestra più corta può provare un'altra geometria ma richiede di nuovo il certificato.

Installazione per indice edge/face esplicito prima di build; verifica sulle identità effettive di curva, superficie, pcurve, cap e range dopo trim. Controllo prima e dopo computePCurves. Esclusione dal ratchet finale delle tolleranze. Preflight sui budget finiti/nonnegativi e sugli indici modello, allocazione dei piani prima del commit. Cancellazione all'ingresso e nei loop Newton.

Esecuzione personale del runner privato fresh: **14/14PASS, exit0**, logcandidate-independent-tests.log. Hash del binario eseguito e del sorgente test registrati; nessun binario viene consegnato. Nessun vecchio runner di confronto OCCT è stato eseguito da questo agente. L'indagine root su possibili oggetti ABI vecchi è esterna a questo audit: i risultati qui derivano dalle copie fresh del tester e dai JSON.

Fraction sui sei export finali con metadata reali: **6/6 preservazioni del parametro originalePASS**, uppermax9.561278618102041e-15mm; **12/12 fin effettive /60campatePASS**, uppermax1.1916127230620075e-10mm sotto cap3e-9mm; **12/12 carrier /12celle sul rettangolo UV originalePASS**, uppermax4.874831182328089e-14mm. Due fin effettive percorrono[-0.05,1.05], oltre il range originale[0,1], e coprono50campate; le altre dieci fin verificano il bordo originale nel Body costruito. Queste prove si sovrappongono: i numeri non rappresentano casi pubblici distinti.

Tre controlli negativi sui coefficienti IEEE originali emessi (traslazione edge, traslazione carrier, cambiamento UV entro il dominio) sono rifiutati con witness razionale esatto. Il controllo matematico separato del vecchio hidden-support produce al parametro19/16 il residuo quadrato1/4.

Limiti: i dodici carrier preservati globalmente sono una verifica indipendente sui coefficienti di queste fixture, non un nuovo provider runtime per ogni rettangolo sorgente. La garanzia runtime riguarda le curve originali preservate e le fin/carrier effettivi del ramo supportato. Tutte le fixture finali qui sono Body sintetici costruiti mediante helper di produzione, model.build e computePCurves; non costituiscono una prova pubblica dell'intero comando raccordo. Newton, ISO non supportate e compatibilità campionata restano fuori dal certificato. Nessuna garanzia globale di topologia, trim-membership, iniettività, volumi, G1/G2, autointersezioni o pixelUI è inferita.


## Freeze finale v2

La sola modifica v2 riguarda la selezione della faccia emessa: plan.face indica l'ordinale esplicito di costruzione, con controllo del range e dell'identità del carrier. Verificati BlendModel::build (keptFaces=faces) e Body::buildShells (newFace in ordine f). Questo evita il falso rifiuto di due facce distinte con lo stesso puntatore carrier. Il percorso non unifica o riordina facce dopo questa costruzione. Proof matematico, UV, range e cap rimangono identici; nessun lookup per vicinanza.

Sorgente section finaleSHA3c2fece62fbf033de91693b520e82055aae2e63a6cac7011b314a1f7f74dbd7d; header e surface invariati. Test finaleSHA8d603d500f9c9348ecb9ae8d258a0d557cfc8e0a01fb9128dc027bc3bd328c85. Copie e hash finali in v2-reviewed-execution-hashes.json. Confronto personale con i tre sorgenti integrati nel repository: tutti identici byte per byte (v2-integrated-source-byte-comparison.json).

Esecuzione personale del runner V2 fresh: **15/15PASS, exit0**, incluso SharedCarrier, in v2-independent-tests.log. Rilanciati Fraction sulle sei esportazioni V2: stessi esiti numerici dei precedenti, 6preservazioni/12fin60campate/12carrierPASS con cap effettivo3e-9mm. Tutti i sei JSON originali V1/V2 sono identici byte per byte (v1-v2-export-byte-comparison.json). Rilanciati i tre negativi sui coefficienti effettivi V2: rifiuto mediante witness esatti, v2-negative-controls.json. I risultati definitivi sono v2-fraction-results.json; quelli candidate-* restano prove storiche V1 esplicite.

L'audit non ha eseguito build completa, sanitizers, prove pubbliche del comando o UI: root e tester documentano separatamente quelle prove. Nessuna certificazione Newton viene dichiarata. Il problema ABI ipotizzato durante il lavoro non modifica alcuna di queste attribuzioni: tutti i runner audit eseguiti sono fresh, firmati per hash; nessun runner storico di confronto è stato usato.
