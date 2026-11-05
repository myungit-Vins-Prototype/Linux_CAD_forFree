# KnifeHandle: unione tra Raccordo 10 ed Estrusione 11

Copia congelata di KnifeHandle.prt: SHA256 `2f86126ee723f031b38b3d9362c6f936a93d5b139e11825a5e3a382535181159`. Originale non modificato.

## Diagnosi e correzione

Entrambi i corpi passano checkBody: 9 facce nel raccordo, 3 nell'estrusione. Unione fallita nella classificazione della faccia NURBS F0 del raccordo. Il grafo 3D separava erroneamente i due usi della cucitura in un ciclo avanti/indietro e li proiettava sullo stesso lato UV.

`fk_boolean.cpp`: conserva i loop delle facce con cucitura non attraversate da tagli, continuando a suddividere gli edge nei punti condivisi; mantiene la fin originale, usa la sua SP-curve per individuare il ramo parametrico e la riporta nel risultato. Nessuna modifica di tolleranze o delle superfici.

`fk_unify.cpp`: non elimina le cuciture della stessa faccia quando la superficie non è periodica. Il test sintetico ha evidenziato che la vecchia pulizia lasciava il volume corretto ma una mesh incompleta.

## Verifiche concluse

- Applicazione `forgecad-release/forgecad`, runner viewport e target kernel compilati. Non viene aggiornata l'istanza già aperta: riaprire l'applicazione.
- Copia reale: unione diretta Raccordo10/Estrusione11 in entrambi gli ordini, 10 facce, volume 46784,9 mm³ (valore arrotondato), checkBody con incroci dei loop superato e zero facce fallite nella tassellazione.
- Pipeline del documento: aggiunta della feature di unione, rigenerazione, salvataggio della copia QA e riapertura superati. STEP esportato e riletto come un corpo valido. Finestra viewport Wayland separata e schermata ispezionata; nessuna prova di clic sul pannello dichiarata.
- Nuovo `forgekernel_boolean_seam_tests`: loft circolare con facce tangenti unite e perno intersecante. Volumi analitici delle tre operazioni, entrambi gli ordini, due lati UV distinti, genealogia, mesh, STEP e risultati binari identici con 1/4 thread. Sul kernel precedente la stessa configurazione fallisce con «foro fuori da ogni contorno».
- 6/6 CTest mirati PASS: cucitura booleana, genealogia applicativa, punti/piani e corrispondenze loft (kernel e app).
- Runner OCCT corrente: 38/38 Boolean, 5/5 Unify e 42/42 Loft PASS. Nessun cambiamento di soglie o test disattivato.
- Mappa aggiornata: 597 file, 8323 relazioni, 287 unità analizzate e zero diagnostiche. Check, collegamenti entranti/uscenti, chiamate/include e rendering di panoramica più tre dettagli PASS. Nessun arco reference per i tre file interessati.
- `git diff --check` PASS; SHA256 originale invariato. Modifiche locali, nessun commit/push in questo intervento.

## Ambito e riproducibilità

La conservazione del loop si applica alle facce con cucitura non attraversate da tagli: non cambia l'ordinamento angolare generale dei contatti. Non si dichiara risolta ogni intersezione possibile attraverso una cucitura NURBS. La cucitura parametrica resta nel B-rep perché necessaria al dominio UV; la superficie è la stessa, senza bordi fisici aggiunti o variazioni di forma.

Artefatti temporanei: `/tmp/KnifeHandle-frozen.prt`, `/tmp/KnifeHandle-union-qa.prt`, `/tmp/KnifeHandle-union.step`, `/tmp/knife-app-qa.cpp`, `/tmp/knife-app-qa.log`, `/tmp/knife-union-native.png`; log build/test e mappa `/tmp/knife-*.log`. File CAD dell'utente non aggiunti al repository.

Durante la diagnosi una sonda aveva rilinkato vecchi oggetti nella directory CMake precedente alla separazione `kernel/occt_tests`, causando crash da incompatibilità di layout. Questi esiti non sono validazioni del kernel corrente. La suite finale usa esclusivamente il target CMake aggiornato e passa integralmente; non si dichiara una verifica completa con sanitizzatori.
