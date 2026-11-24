# Errori di costruzione nella viewport 3D

La viewport mostra la spiegazione dell'errore durante l'anteprima, alla conferma del comando e dopo la rigenerazione della storia. Selezionando nell'albero una funzione in errore si può richiamare la segnalazione. Dal riquadro parte un segmento verso ogni punto o bordo segnalato. Per un bordo il richiamo termina sul tratto visibile. Per una posizione fuori campo raggiunge una freccia sul margine della vista. I richiami seguono la geometria durante lo zoom e la rotazione.

- **Cucitura.** Se «Crea un solido» fallisce per bordi aperti, sono evidenziati i bordi rimasti liberi dopo l'abbinamento delle superfici. Se due bordi combaciano solo in parte, viene evidenziato il tratto rimasto libero.
- **Spessore e Offset.** Quando la distanza raggiunge il limite di curvatura, il kernel può restituire un punto sulla parte conservata della faccia. Per una discontinuità lungo uno spigolo può restituire la curva interessata. Il testo specifica il motivo.
- **Loft e schizzi.** Il loft mantiene i richiami a sezioni e guide. Il risolutore può evidenziare le entità di un vincolo coinvolto, il punto Pierce o gli elementi scelti per la modifica.
- **Riferimenti persi o posizione incerta.** Il messaggio distingue l'ultima posizione conosciuta, i riferimenti usati dalla funzione e il riquadro degli operandi. Se non è disponibile alcuna posizione affidabile compare la spiegazione testuale.

La diagnostica geometrica è transitoria. Dopo aver riaperto un documento, la rigenerazione della funzione ricalcola i dettagli forniti dal kernel.

## Verifica su Superfici_Riv_Tut

Prima del supporto al collasso dei raccordi, lo spessore interno di 2 mm su «Raccordo 15» segnalava la faccia 35. Nella prova del richiamo, il marcatore si trovava a circa (−18,1673; 4,5485; 65) mm, sul ritaglio effettivo della faccia.

Per riprodurre la cucitura manuale della precedente versione del modello, la prova usa le 52 facce originali, 50 delle 52 facce dell'offset e le 20 pareti di collegamento. La diagnosi di Cucitura contiene gli otto bordi liberi distinti, quattro per apertura. Ripristinando le due facce mancanti si ottiene un solido chiuso e la segnalazione si azzera.

Verifiche Linux del 21 novembre 2026. Superati 11 test di integrazione su diagnostica, schizzi, pannelli e storia, e 5 test mirati del kernel su Cucitura, Offset e Spessore. La cattura del framebuffer OpenGL su Wayland verifica il marcatore sul modello e la resa dei bordi. La verifica successiva dei segmenti di richiamo copre i due temi, più bersagli, un bordo che attraversa la vista e la presenza simultanea di bersagli in campo e fuori campo. La prova di chiusura del pannello conferma che resta visibile l'errore di una funzione a valle dopo la modifica di un operando.

I richiami vengono composti dopo la scena OpenGL. Il riquadro, i segmenti, i marcatori, le frecce di Offset/Spessore e le annotazioni Loft usano il disegno raster antialias alla densità di pixel della viewport. Il riquadro riserva per il testo l'intera dimensione misurata, arrotondata per eccesso. Nelle catture Wayland a scala 100% e 150% su Superfici_Riv_Tut il passaggio fra contorno e linea presenta coperture intermedie e il messaggio di prova è leggibile per intero. A scala 100% un tratto di richiamo nel framebuffer presenta 302 pixel di copertura intermedia fra il contorno e il colore interno, sia con il multisampling della scena attivo sia con quello spento. Sullo stesso tratto la versione precedente ne aveva zero. Superati i quattro test mirati su diagnostica e comandi Offset e Spessore.
