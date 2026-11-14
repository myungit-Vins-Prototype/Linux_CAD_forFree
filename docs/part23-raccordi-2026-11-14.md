# Part23 — raccordi sulle catene e tempo di Raccordo 21

Richiesta del 14 novembre 2026: gestire il raccordo da 2 mm sui due contorni indicati nell'immagine di Part23 e spiegare il calcolo molto lungo di Raccordo 21.

## Risultato implementato e verificato

Il kernel costruisce entrambi i raccordi da 2 mm, singolarmente e nella stessa operazione. Sulla copia congelata il risultato combinato contiene **170 facce e 452 spigoli**, senza bordi liberi. Anteprima, conferma, controllo B-rep, triangolazione completa e salvataggio/rilettura della geometria sono riusciti nel runner applicativo. Riaprire l'app per usare il nuovo eseguibile.

- [Copia di prova con i due raccordi](../output/Part23-raccordi-R2mm.prt).
- [Rendering nativo della copia](../output/Part23-raccordi-R2mm.png).
- Il limite distinto della rigenerazione parametrica dopo riapertura è descritto sotto: questa copia non è dichiarata stabile a ogni ricostruzione della storia.

## Cause e modifiche generali

`kernel/fk_blend_surface.cpp`: due attraversamenti tangenti su supporti opposti possono scambiarsi d'ordine quando il raggio consuma il tratto breve fra i loro vertici. Il vecchio montaggio conservava un tratto percorso al contrario; il bordo di contatto risultava quindi orientato contro gli altri e il loop rimaneva aperto. Adesso si ricostruisce la coppia intermedia di supporti e si continua sulla guida adiacente effettiva, avanti o indietro. Sono gestiti anche il passaggio fra ultimo e primo tratto della catena chiusa e la rifinitura dell'attraversamento che si era fermato sul vecchio vertice. Non si aggiungono selezioni per prossimità e non si aumentano le tolleranze.

`kernel/fk_blend.cpp`: Raccordo 21 è un contorno piano chiuso di quattro bordi tangenti. Il percorso analitico costruiva gli utensili e applicava una booleana all'intero solido, coinvolgendo le facce libere dei raccordi precedenti. Per i contorni piani completamente selezionati, chiusi e tangenti, il rifilo locale già presente ha ora la precedenza; gli altri percorsi restano disponibili se questo non riesce.

`kernel/fk_profile.cpp`: le prove sulla variante scalata hanno esposto una seconda lentezza nel controllo dell'area orientata. Il prodotto `x*y' - y*x'` può essere quasi nullo per cancellazione; la quadratura scalare continuava a suddividere fino alla profondità massima. L'area usa ora la quadratura esistente che segue anche la grandezza dei due prodotti e riconosce la stagnazione dell'errore numerico. La tolleranza assoluta richiesta rimane `1e-13`; non cambiano i budget geometrici.

`kernel/tests/test_blend_transitions.cpp`, fixture `blend_opposed_splits.body` e target CMake delle cavità: regressioni su dieci punti di selezione, chiusura ciclica, selezione invertita, riflessione, scala 2,5, rototraslazione e raggi 1,5/2/5 mm. Si controllano raggio delle sezioni entro 1e-7 mm, geometria/loop, genealogia, input immutato, codec e triangolazione. La fixture conserva soltanto le dieci facce del caso locale.

## Misure sul caso congelato

| Operazione | Prima | Versione finale |
| --- | --- | --- |
| Raccordo 21, raggio 2 mm | 102833 ms | 64 / 65 / 63 ms |
| Due catene evidenziate, raggio 2 mm | loop aperto | circa 441 ms, 170 facce |

Tempi del kernel isolato, con geometria già caricata, non tempi complessivi di apertura o rendering. La selezione di Raccordo 21 era E9 sul predecessore B19; la propagazione completa E8/E9/E10/E11. Le due catene sono state riprodotte scegliendo E130/E131/E230/E231 e E144/E145/E240/E241 su B20. Questi indici valgono esclusivamente per lo snapshot descritto sotto; il codice non contiene regole su Part23 o su questi numeri.

## Verifiche finali

- Build Release completa di `forgecad` e `forgecad_view_tests`: PASS, non soltanto kernel/runner. Restano warning di inizializzatori `LoftContact` già presenti nel runner.
- **62/62 Blend**, **38/38 History**, **7/7 Profile** nel runner kernel ricompilato: PASS.
- **10/10 CTest applicativi**: selezione e rifinitura dei raccordi, raccordi avanzati, storia dichiarata, storia raccordi, anteprime, riuso in modifica, ricaricamento e impronta delle sorgenti geometriche: PASS.
- **3/3 nuove regressioni**: PASS. Il target delle cavità complessivo è **7/8**, per il fallimento preesistente documentato sotto.
- Sessione nativa Wayland, renderer Mesa Intel UHD P630, OpenGL con 4 campioni: anteprima e conferma dei due contorni PASS; copia salvata/riletta con 170 facce, nessuna faccia non triangolabile e nessun bordo libero. Rendering ispezionato. Riapertura dell'anteprima della funzione salvata: `valid=1`, nessun errore, 44102 triangoli.
- Mappa consultata prima delle modifiche e aggiornata: **696 file, 10059 relazioni, 340 TU, zero diagnostiche**. `--check`, corrispondenza HTML e collegamenti in entrata/uscita dei moduli modificati: PASS.
- `git diff --check`: PASS. Nessun commit o push.

## Limiti osservati, distinti dal caso corretto

**Test della cavità già fallente nel codice iniziale.** `BlendCavityWholeRimKeepsRadiusHistoryAndClosedTrims` fallisce a R0,5 con il contatto fuori dal dominio del fianco F0. Il risultato è stato riprodotto anche compilando in O2 i tre moduli originali di HEAD `8914fd1` (`fk_blend.cpp`, `fk_blend_surface.cpp`, `fk_profile.cpp`) e collegandoli al posto di quelli modificati. Nessun test è stato indebolito o saltato per mascherarlo. Log `cavity-baseline.log` e `cavity-tests-final.log` nella cartella diagnostica.

**Rigenerazione della storia di Part23 dopo riapertura.** Due ricostruzioni consecutive in memoria sono stabili e non riportano errori o B-rep invalidi. Dopo salvataggio, riapertura e ricostruzione cambiano però già Estrusione 17 ed Estrusione 18, a monte dei raccordi, e il cambiamento si propaga a valle. Si osserva sia sulla copia di prova con il nuovo raccordo sia sullo snapshot originale senza Raccordo 22. La diagnosi di questa deriva dei riferimenti/parametri a monte resta aperta: non si dichiara stabile il roundtrip parametrico completo. JSON `document-baseline.json` e `original-document-baseline.json`; zero errori di rebuild, zero geometrie invalide, `repeat_changes=[]`, `status=unstable` per le differenze successive alla riapertura.

## Sorgente e riproducibilità

Il percorso del contesto `/Users/myungit/Linux_CAD_forFree` non esiste su questa macchina. Repository effettivo: `/home/myungit/Linux_CAD_forFree`; originale individuato in `/home/myungit/Documenti/Cad_Example/Part23.prt`.

Prove sullo snapshot `/tmp/part23-raccordi-2026-11-14/Part23.prt`, SHA256 `3ea2176bbae7671b574345fce5169dfd713f70e45f90f4880ded7c44bc9e383f`. Durante l'intervento il file nella cartella degli esempi ha ricevuto aggiornamenti concorrenti: nella lettura successiva comprende già Raccordo 22 e ha SHA256 `a63eb512986a5bafaa610a3969a52df5fa65ea7fdc5afb1c21fba0198b554fd8`. Le operazioni di questa chat hanno letto tale file e scritto le copie separate; nessuna sostituzione dell'originale è stata eseguita. Le misure riportate si riferiscono allo snapshot congelato, non certificano le successive modifiche del documento attivo.

Sonde, log, confronti del codice originale e JSON: `/tmp/part23-raccordi-2026-11-14/`.

SHA256 eseguibile finale: `710e9bb2243e00fa232da9a628918e60c071ea40001022c1fe30f1842d5e7137`.
Runner: `c7acbf61794f07f6f723b74ea2283de98f689b3d827d3c5cfee2538632982f02`.


## Aggiornamento: piano di Estrusione 17, Raccordo 23 e storyboard

Nuova segnalazione dell'utente: corretto manualmente l'appoggio di Schizzo 10/Estrusione 17; Raccordo 23 appena salvato lento, cosi' come l'evidenziazione delle feature nella storyboard. Nuovo snapshot congelato in `/tmp/part23-piano-r23-2026-11-14/Part23.prt`, SHA256 `61c1468f07ef0ea87db8f8527027a38a196705464a276ef5eca0145566dd4e7c`, 23 feature. L'originale attivo continua a ricevere salvataggi dell'utente: hash successivo `4db541bd6cd72f67a16b005e5b96631c260257b8723866dda46aeae5123c64f3`. Nessuna scrittura della chat sul CAD originale.

### Piano dello schizzo

Nel vecchio snapshot Schizzo 10 ha `plane=3`, origine `(0,0,60)` e appoggio nominato `f16:cap.end`, sulla feature Specchio 16. Nel nuovo ha piano XY, origine zero e nessun appoggio su faccia. Tuttavia la copia `ExtrusionObject::plane` di Estrusione 17 conserva ancora `3`: non veniva sincronizzata dopo il cambio di supporto. Il calcolo geometrico usa lo schizzo, non questa copia legacy. `cad_feature_build.cpp` la sincronizza ora sia nel caricamento da cache sia nella costruzione parametrica di estrusioni/rivoluzioni.

Il percorso `followDatum -> resolveGeometryRef -> resolveFaceReference` accettava anche il ripiego per indice/geometria quando il nome salvato non era piu' ritrovato; l'avviso non diventava un errore dell'appoggio. Questo puo' spostare silenziosamente uno schizzo su un altro piano. La nuova `resolveSketchSupport` in `cad_datum.*`, usata anche dal cambio esplicito di supporto, richiede una risoluzione genealogica univoca per le facce nominate. Se fallisce mantiene il frame precedente e segnala l'appoggio perso, bloccando le feature dipendenti. Facce legacy senza nome e piani di origine/costruzione conservano la compatibilita'. Non cambia la politica generale di risoluzione degli altri consumer.

**Limite della diagnosi storica:** il cambio automatico di piano riferito dall'utente non e' stato riprodotto sul vecchio snapshot: frame e coordinate dello schizzo restano invariati nella sonda load/rebuild/codec/reload/rebuild. Sono dimostrati l'incoerenza del campo legacy e il ripiego silenzioso, non il nesso causale con la specifica sequenza di clic del documento originario. Il nuovo documento corretto dall'utente passa il baseline completo: zero errori/invalidi, `snapshot_changes=[]`, `repeat_changes=[]`, `roundtrip_changes=[]`, `status=stable`.

`tests/test_sketch_support.h`: la stessa faccia nominata segue la modifica dell'altezza della base; nome perso senza salto di piano, errore del consumatore; cambio esplicito su XY con definizione sincronizzata, Undo/Redo, codec senza cache, riapertura/rebuild e compatibilita' del riferimento legacy. Le prove sono incluse nel test storyboard esistente.

### Lentezza di Raccordo 23

Sul predecessore B21, raggio 2 mm, riferimento E60: kernel aggiornato **509 ms**, adattatore completo con genealogia registrata **774 ms**, tassellazione **64 ms**, curve U/V **24 ms**. Finestra Wayland/OpenGL: modifica effettiva a R2,01, geometria e display pronti in **639 ms**, nessun errore. Apertura della modifica invariata gia' riusa B-rep e mesh senza ricalcolo.

Con i tre moduli kernel originali di HEAD `8914fd1`, compilati in O2 e sostituiti nel medesimo eseguibile della sonda, il solo kernel supera **150 secondi** (timeout). Rimettendo **soltanto `fk_profile.cpp` originale**, con raccordo e transizioni gia' aggiornati, supera **100 secondi** (timeout). Quindi la quadratura dell'area orientata, gia' corretta nel primo intervento, spiega anche il nuovo Raccordo 23: il controllo del rumore evita la suddivisione fino alla profondita' massima per cancellazione numerica. I tempi con timeout sono limiti inferiori, non durate finali misurate.

### Evidenziazione nella storyboard

`forgeFeatureFaces` ricostruiva per ogni faccia visibile i box e le tolleranze dei candidati in tutti gli stadi storici. `cad_feature_faces.cpp` isola picking/proprieta' delle facce e mantiene una ricerca locale: box/tolleranze preparati pigramente una sola volta e riusati sulle altre facce. Stesso ordine, confronti B-rep double e tolleranze, nessuna riassociazione topologica o modifica dei B-rep. Nessuna cache globale da invalidare.

Raccordo 23: **11714 -> 266 ms**, stesse **38 facce**. Prima prova nativa completa: R23 **289 ms**, E17 **162 ms** (2 facce), R21 **221 ms** (4), E1 **34 ms** (9). La mesh evidenziata e' estratta tramite gli ID dei triangoli, come prima; lifetime e generazione dei risultati asincroni restano protetti. `tests/test_feature_faces.h` confronta il batch con il picking singolo su base, smusso e traslazione, includendo storia filtrata e dati assenti. Screenshot verificato nella cartella diagnostica.

### Verifiche conclusive del nuovo intervento

- Build completa Release di **app e runner**: PASS; eseguibile `forgecad-release/forgecad` aggiornato. Nessun processo utente chiuso. Per usare il nuovo binario occorre riaprire l'app.
- **17/18 CTest** del gruppo eseguito: tutti i 17 test pertinenti superati (storyboard con le due nuove suite, owner, datum, schizzi/storia, riferimenti evidenziati, genealogia/cache, raccordi/selezione/rifiniture, anteprime/riuso/mesh e lifetime). Il test generale `forgecad_view_tests` si ferma su **Applicatore.prt**: il documento legacy in `/home/myungit/Documenti/File_Esempio/` perde la vecchia cache in base alla politica gia' presente nel codec; il test pretende invece il B-rep finale subito dopo `loadDocumentFile`, senza ricostruirlo. La sonda legge tutte e nove le feature ma nessun B-rep in cache. `cad_document_io.cpp` e `cad_document_read.cpp` non sono stati modificati. Questo test generale resta fallente; nessuna asserzione o cache dell'esempio e' stata alterata per farlo passare.
- La prima prova lifetime ha esposto un'ipotesi superata **nel test**: dopo il riuso immediato, una definizione invariata non crea piu' il timer, e il test chiamava `QTimer::stop` su null. Stack GDB confermato. La prova ora modifica realmente una dimensione della primitiva prima di accodare il lavoro: esercita ancora distruzione/annullamento e le stesse asserzioni, con entrambi i worker in coda. PASS dopo la correzione; riuso immediato coperto dal test dedicato.
- Nuovo Part23 con tutte le protezioni: **baseline stable**, zero errori/invalidi e nessuna differenza cache/rebuild, rebuild ripetuto o save/reopen/rebuild (`document-final.json`).
- Seconda prova nativa Wayland/OpenGL: R23 modificato a R2,01, **624 ms** dall'aggiornamento al display pronto; riapertura dell'anteprima invariata **0 ms**, nessun errore. Evidenziazione nativa e screenshot ispezionati.
- Architettura: **699 file, 10105 relazioni, 341 TU, zero diagnostiche**; `--check`, HTML/JSON identici e relazioni in entrata/uscita PASS. Controllate in particolare le chiamate dal viewport alla ricerca facce e a `resolveSketchSupport`.
- `git diff --check`: PASS. Nessun commit o push. Memoria aggiornata entro 16 KiB. Log, sonde e screenshot in `/tmp/part23-piano-r23-2026-11-14/`.

SHA256 app finale: `1349c1c8ab61f583dd09d27744885b1f07883df3f3e54638eb11753aa794764d`.
SHA256 runner finale: `fa9f121e29a8dfeff6a3d1027339407471400b47ca49367f39656701fb2f91c9`.

