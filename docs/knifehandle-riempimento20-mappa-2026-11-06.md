# KnifeHandle — corrispondenza fra appiattimento e piegatura

## Diagnosi e correzione, 6 novembre 2026

Copia congelata `forgecad-release/knifehandle-wrap20-qa/input-38de9d2d.prt`:
70 funzioni, 19.636.215 byte, SHA256
`38de9d2da8e01906dd0122d78694265ff77ff633873b56448edc63dc566a51b9`.
L’originale non viene modificato.

La funzione 17 «Distendi superficie» appiattisce F6/F5 di Solido cucito16
su Piano5, con origine UV=(1; 0,601843861038), XY=(-3;2) mm e angolo90°.
Riempimento20 usa tale contorno e genera51corpi da Estrusione19.
La vecchia centratura della piegatura sceglie invece un’origine parametrica,
un angolo e una centratura indipendenti, perdendo la corrispondenza.
La piegatura deve applicare l’inversa completa: R(-A)(q-T), conservando
origine UV e ordine delle facce dell’appiattimento.

## Implementato

- Riferimento persistente alla funzione di appiattimento nello slot del piano
  sorgente (kind11, schema8 delle estensioni superficie). Il piano effettivo,
  l’ordine delle facce, UV e R(-A)(q-T) sono risolti dalla definizione corrente.
- Il pannello riconosce la mappa dal contorno delle ripetizioni selezionate;
  «Mappa di …» compare in «Piano / mappa sorgente». I parametri derivati sono
  visibili ma non editabili; resta disponibile la modalità libera su piano.
- Facce sorgenti piane a quota costante: carrier esatto alla quota zero,
  offset locale alla quota opposta, trasferimento UV noto delle SP-curve e
  verifica dello scarto fisico. Evita il fit tensoriale delle calotte attraverso
  le linee curve dei nodi del carrier, causa dell’esplosione della griglia.
- Riempimento: più facce complanari, lista aggiungi/rimuovi, cucitura e fusione
  dei bordi interni prima del contenimento. Definizione associativa e formato
  nativo66 invariati per i contorni multipli. Restano i limiti64riferimenti,
  500copie, fori ed ingombro conservativo del seme.

Nessun allargamento del trim o dei budget, riscalatura o eliminazione di copie.
L’appiattimento di superfici a doppia curvatura resta non isometrico.

## Verifiche concluse

Build completa dell’app riuscita. Sulla copia congelata, tutte le51copie del
Riempimento20 vengono piegate con il collegamento associativo: scarto B-rep
massimo misurato4,12438e-8mm. Corpo22 isolato:2,0575e-8mm.
Kernel test mappature PASS, inclusa nuova verifica del carrier conservato.
Wayland: riempimento GUI su entrambe le facce117copie e piegatura completa
confermata,936facce, scarto4,12438e-8mm. Aggiunta e rimozione del contorno,
riconoscimento automatico della mappa, campi derivati bloccati, Undo/Redo di
riempimento e piegatura, salvataggio separato e lettura schema8 PASS.
Anteprima: aggiunto allo snapshot anche il piano indirettamente referenziato
dalla mappa; prima era escluso dal contesto compatto del worker e produceva
un falso errore «Scegli un piano valido».

Copia di prova: `forgecad-release/knifehandle-wrap20-qa/KnifeHandle-Riempimento20-due-facce-piegato.prt`.
L’originale è stato aggiornato esternamente durante l’intervento: congelata
anche `input-b8b7f553.prt`,25.606.814byte,136funzioni/117copie,
SHA256 `b8b7f5532cdc0979fe99eade9a828831ed3ae0e2e9f76f4b9f06ce99ecb22b5d`.
L’appiattimento conserva angolo90°, ma ora XY=(0;0). Anche questa seconda
copia supera il percorso completo in Wayland:117copie,936facce, scarto
misurato4,12438e-8mm, Undo/Redo e lettura schema8.

Copia aggiornata: `forgecad-release/knifehandle-wrap20-qa/KnifeHandle-Riempimento20-due-facce-piegato-attuale.prt`
(38.930.661byte). Riapertura e rigenerazione del risultato senza cache PASS:
B-rep identico alla conferma, SHA256
`c4c1ca7d6309abc8fb238f48e7487d7d6539f768a81c30c09f91ce1ac0d69959`.
65,798s per caricare/rigenerare questo test completo: non una misura del
solo fit né una promessa di prestazioni interattive.

App e runner compilati;15/15CTest PASS (5,99s), incluso il piano indiretto
nell’anteprima, contorni separati con vuoto e rifiuto di piani differenti.
Test kernel mappature PASS: sviluppo, corrispondenza reversibile, carrier
conservato, offset, genealogia, B-rep, STEP e negativi.
Mappa613file/8639relazioni/296TUs,0diagnostiche; controllo di aggiornamento,
collegamenti in uscita/ingresso e rendering HTML di panoramica+10dettagli PASS.
`git diff --check` PASS. Nessun commit/push richiesto per questo intervento.

Le versioni originali sono state lette e congelate, senza salvarvi le prove.
Un successivo salvataggio esterno produce hash `f3838efec9deed035b6c12ea25ae1592c7829b6d86c567b152d2437108a27979`;
le prove complete si riferiscono alle due copie identificate sopra.

## Uso

1. Nel riempimento premere «Scegli contorno nella vista», aggiungere le facce
   complanari, poi «Termina selezione». Il secondo clic rimuove la faccia;
   «Rimuovi» agisce sulle righe evidenziate della lista.
2. In «Piega da superfici appiattite», selezionare un risultato e «Copie».
   Se il contorno viene da un appiattimento, il pannello sceglie «Mappa di …»:
   la disposizione torna sulle facce originali senza ricentrarla né ruotarla
   indipendentemente. Il piano reale è incluso nello snapshot dell’anteprima.
3. Le modalità libere restano selezionabili tramite il piano sorgente.



## Prosecuzione

[Unione dei rilievi con il manico](knifehandle-unione-rilievi-2026-11-06.md): diagnosi e correzione della lentezza in corso.
