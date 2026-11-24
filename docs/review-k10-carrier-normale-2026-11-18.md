# K10 — carrier a quota costante e normale orientata, 18 novembre 2026

Prosecuzione del filone2, dopo il controllo del carrier a quota zero. Conteggio34 invariato, K10 ancora parziale. Consultati prima delle modifiche descrizioni e archi tipizzati nei due versi: chiamante applicativo, wrap pubblico, callback del provider, fit e regressioni. Tre agenti Sol riutilizzati per implementazione in staging, audit matematico/statico e test. Build/analisi seriali, nessun test UI previsto.

## Riproduzione prima della correzione

Baseline locale congelata con hash, inclusi lavoro numerico e provider quota zero del17 novembre. Box1×1×0,001mm con origine Z=0,002mm; entrambe le facce parallele hanno quota nonzero rispetto al piano sorgente. Target cubico3/3 nativo9×9 con il rilievo interno del rapporto precedente, anchor(0,0), tolleranza1e-6mm.

L'API pubblica restituisce6 facce senza problemi al checker, input invariato e deviazione1,221797765e-10mm. A(3/16,3/16) fondo/cima restano piatti alle quote0,002/0,003mm, con residuo1,0000000000000227e-4mm rispetto alla mappa richiesta:100 volte la tolleranza e5000 volte il cap locale2e-8mm. Tutte le facce parallele erano escluse dal provider quota zero.

Tre gruppi pubblici sulla baseline:2/3 PASS, unico fallimento il rifiuto del solido errato e la conservazione delle sentinelle, sia con history sia senza. Passano il controllo piatto a quote positive/negative e il wrap quadratico ordinario. L'oracolo del rilievo deriva dalla base cardinale cubica B4(3/16)=2/3 e gradiente zero: il picco vale4/9 del poloZ, la normale lì è verticale.

## Correzione integrata

Estende il provider alla quota costante riconosciuta, anche negativa. Nuova API interna `constantHeightMappingResidualBound`, senza alias legacy. Quota zero mantiene il confronto tensoriale precedente. Per quota nonzero usa D=F−T, C=Tu×Tv naturale e n=C/||C||. Prova lower||C||>0, verso h(D·C)≥0, limite B della componente tangenziale e R di | ||D||²−h² |. Il limite posizionale è `sqrt(B²+((R+B²)/min|h|)²)`.

La normale deriva dai poli e nodi originali prima della composizione UV: scambio degli assi e rate negativi non devono invertirla. Prodotti tensoriali Bernstein e intervalli outward, nessuna conversione arrotondata usata come fonte del certificato. La seconda candidata aggiunge la verifica della politica di regolarità esistente in `Surface::normal`, con lower della normale maggiore di `angularResolution()*max(||Tu||²,||Tv||²)` racchiuso outward. Non è una prova bit per bit del valutatore floating point.

Restano fissi cap geometrico, visite16384, profondità24, limite poli e celle. Failed riconosciuto resta terminale; famiglie escluse: quote variabili, UV obliqui, istanze, pesi variabili, periodicità e altri gradi. Non è una prova d'iniettività, trim o topologia.

Audit statico Sol della prima candidata: nessun blocker; verificati derivate originali e indici, segno per h negativo, copertura delle celle e formula. Audit finale della seconda candidata, inclusa la guardia di regolarità: nessun blocker. **18/18 gruppi Release PASS** nello staging in5,141s, dieci precedenti e otto nuovi; build completa app e runner PASS in25,983s. **7/7 CTest PASS**,11,53s. **18/18 ASan/UBSan PASS**,54,370s, build strumentata25,538s. Sette TU strumentate (provider, chiamante, main e quattro file di regressione), resto kernel Release; leak detection attiva. Non è una verifica LSan dell’intera app. Mappa rigenerata con un solo processo in780.998s: 810/11837/413 (file/archi/TU), zero errori; `--check`, hash HTML/JSON e collegamenti tipizzati nei due versi PASS.

Oracolo Python Fraction indipendente: T=(u,v,3u/4), Tu×Tv=(-3/4,0,1), norma5/4, N=(-3/5,0,4/5). Per h positivo e negativo, F=T−hN annulla B e R ma dista2|h| dalla mappa richiesta; il segno lo rifiuta. Nei controlli positivi, scaleU=1,25 e UV scambiati non cambiano la normale naturale. Il polo del driver iniziale e quello costruito con `(9/4)*1e-4` differiscono di un ULP; i due oracoli esatti sono conservati separatamente.

## Limiti e prosecuzione

La correzione rifiuta conservativamente il solido con rilievo perso; non ricostruisce un offset generale e non aumenta la tolleranza per farlo passare. I controlli obbligatoriamente riusciti dimostrano che non è un rifiuto indiscriminato delle superfici curve. Nelle famiglie escluse resta il comportamento precedente; quote variabili, UV obliqui/rango1, istanze, pesi variabili e altri gradi rimangono aperti per il carrier globale. I certificati E/S/Q delle fin già disponibili restano distinti.

Proposta statica per il passo successivo, **non eseguita né implementata**: Plane con quota affine di segno costante e UV separabili a rango2. Rappresentare h come tensor Bernstein1×1; R usa h² e il segno usa h(D·C), con gradi massimi6×6/9×9 già sotto il cap10×10. La stessa formula vale con hmin del hull. Quote che attraversano zero restano fuori dal delta proposto.

Candidato pubblico da provare: box0,9×1×0,001mm, origine(0,0,0,002), frame ruotato attornoY di0,001rad, target con il medesimo bump. Fondo e top hanno quota affine positiva e UV separabili; il picco cade a circa0,20833 della direzione lunga, fra le sonde. Bordi e griglia differenziale sembrano fuori dal supporto. Alcune facce laterali hanno rangoUV1. Non è un nuovo bug accertato: il runtime può raffinare o rifiutare prima per altre cause.

Pulizia: rimossi262528056byte (250,4MiB) di ELF/oggetti esclusivamente dello staging concluso, preservando sorgenti e log. Verificati invariati43 pacchetti precedenti/6099 payload. Nessun CAD originale modificato e nessun processo utente riavviato. Nessun nuovo commit/push.

Prove testuali inerti: [archivio](review-k10-carrier-normale-prove-2026-11-18/README.txt), [verifica finale](review-k10-carrier-normale-prove-2026-11-18/FINAL-VERIFICATION.json). Hash app corrente `f0a613de2f36da545105be83434fe0e812d148b3b14d4bea989f3289fb9cbd16`; nessuna riapertura dell’app utente. La CI verde precedente riguarda soltanto il software già pubblicato.
