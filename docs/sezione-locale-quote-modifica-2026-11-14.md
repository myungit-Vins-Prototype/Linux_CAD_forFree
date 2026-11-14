# Sezioni locali quotate: falso blocco nella modifica delle viste — 14 novembre 2026

## Segnalazione e causa

La finestra «Sono presenti quote di sezione: conservare i riferimenti del piano.» compare anche creando una sezione locale su un'altra vista o cambiando una scala. Non indica un danno nel modello.

`DrawingSheetEditor::Impl::changeViews` controllava tutte le quote del foglio: per una vista proiettata ammetteva le intersezioni solo con `sectionReferences=true`. La sezione locale, invece, richiede quel flag disattivato e genera comunque intersezioni esatte del proprio piano di taglio. Una quota locale valida veniva quindi rifiutata anche durante modifiche a viste indipendenti.

`DrawingSheetEditor::updateView` conteneva lo stesso controllo incompleto per le quote della vista modificata, inclusi i supporti degli assi quotati. Un altro controllo analogo riguardava i richiami.

La copia corrente di Part23 ha cinque viste, due sezioni locali, 29 quote, un asse e cinque centri; una quota di «Vista derivata 2» usa un'intersezione locale. Questo spiega il blocco su «Vista 1» nella schermata. Il file `.drw` è più recente di quello usato per la precedente verifica degli agganci.

## Correzione implementata

- `cad_drawing_editor.cpp`: i controlli di modifica riconoscono `localSection` come sorgente valida delle intersezioni, coerentemente con la validazione già presente nel documento geometrico.
- Quote legacy, agganci espliciti, supporti degli assi e richiami conservano le identità. Rimangono vietati la rimozione del taglio che supporta quote/richiami e l'esclusione di una sorgente quotata.
- Una modifica effettiva del piano che fa scomparire il foro conserva la quota e la segnala mancante; Annulla recupera il riferimento. Nessuna associazione automatica per vicinanza.
- Nessuna variazione del formato dei file o del kernel geometrico.
- `tests/test_drawing_local_tools.cpp`: regressione su foro cieco, quota radiale e lunghezza legacy della circonferenza, asse, richiamo e centro. Copre creazione su un'altra vista, cambio di scala, modifica di contorno/piano iniziale/profondità/titolo, Annulla/Ripristina, rimozione atomica protetta, piano oltre il fondo e riapertura.
- Descrizioni e collegamenti architetturali aggiornati in `tools/generate_architecture.py` e nella mappa locale.

## Verifiche eseguite

1. Nuova regressione compilata sul codice precedente: fallisce con **lo stesso messaggio** della schermata.
2. Regressione dopo la correzione: PASS; successiva esecuzione nella suite finale: PASS.
3. Applicazione completa, runner viewport e runner tavole ricompilati. **34/34 CTest 2D PASS**, 37,19 s.
4. Part23 su copia congelata e workspace isolato, interfaccia Wayland e oggetti dell'applicazione completa:
   - apertura: **29/29 quote pronte**, modello invariato;
   - scala di Vista 1 e Annulla: **29/29**;
   - modifica della sezione locale esistente (piano iniziale e profondità traslati insieme, stesso piano geometrico), Annulla/Ripristina: **29/29**;
   - nuova sezione locale su Vista 1 e Annulla: **29/29**;
   - salvataggio e riapertura della modifica alla sezione: **29/29**, stessa firma del modello, cinque viste/due sezioni/un asse/cinque centri conservati;
   - esportazione PDF e cattura dell'interfaccia riuscite.
5. Mappa: **714 nodi, 10287 relazioni, 349 TU**, solo due TU rianalizzate, zero diagnostica; `--check`, equivalenza HTML/JSON e collegamenti test/editor nei due versi PASS.
6. `git diff --check` PASS. File CAD originali confrontati byte per byte con le copie iniziali: invariati.

Gli avvisi precedenti del PDF sui contorni apparenti e su un piano locale che non attraversa materiale nel contorno rimangono: sono diagnostiche geometriche distinte dal falso blocco corretto. I warning di indentazione del coordinatore preesistevano alle tre condizioni modificate.

App SHA256: `2c1ca52e6a117b59f9404b39fab42bb299d0cf085e24faa388ef94ee036ed5d7`.
Runner SHA256: `94ac5b3e209a58afbfab312fb19224fd720a32db8a19e43438cdfaef33558e87`.

Part23.drw originale: `9435e12ecabe58e37deb79061acd93e5edde53604be1e36213487a2ebfde3ef5`.
Part23.prt originale: `90577c6d772d1841581ac1b06ef9ddbbad5b7efedd06faa4bef52a1e269099f6`.

Log, riproduttore nativo, copie, screenshot e PDF: `/tmp/local-section-edit-2026-11-14/`.

Per usare il binario aggiornato occorre riavviare ForgeCAD. Selezionare la vista e usare **Inserisci → Sezione locale…** per modificarne il contorno e i parametri.

Riferimenti: [comandi e prove iniziali](tavole-centri-raggi-sezione-locale-2026-11-14.md), [recupero quote/cache](part23-quote-associative-cache-2026-11-14.md).
