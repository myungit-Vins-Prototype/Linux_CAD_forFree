# Ribbon del viewport e runner separato

La ribbon approvata è ora inclusa nell’app normale e visibile per default. Le toolbar classiche restano agganciabili e personalizzabili; al primo avvio della nuova interfaccia vengono spente, conservando posizioni e layout. Le successive scelte di visibilità sono rispettate.

L’app normale e il runner usano **Fusion** come stile comune, senza bisogno di `-style Fusion`. Colori e accenti provengono da due palette dell’app: dal sistema viene seguita soltanto la scelta **chiaro/scuro**, anche mentre le finestre sono aperte. Il cambio mantiene il disegno dei controlli. Con Qt 6.5+ viene osservato `QStyleHints::colorSchemeChanged`; se lo schema non è disponibile, viene usata la luminosità della palette ricevuta da Qt. Sui backend meno recenti l’aggiornamento dipende dalle notifiche di palette fornite dalla piattaforma. Font, scala dello schermo e sfondo del viewport conservano le impostazioni esistenti.

Per la verifica nativa del tema (Qt 6.8+, preferenze isolate) usare `forgecad-ribbon-preview --test-application-theme <cartella-catture>`: cambia lo schema della sola app, verifica ribbon e foglio stile nella finestra aperta e salva `ribbon-light.png`/`ribbon-dark.png`. La prova non modifica l’aspetto globale del sistema. Il test CTest `forgecad_application_theme_tests` verifica anche il fallback e distingue richieste native da segnali simulati su offscreen.

In **Opzioni → Barre degli strumenti** si possono riattivare le toolbar, mostrare/nascondere la ribbon e aprire **Personalizza ribbon…**. Anche il clic destro sulla ribbon apre il menu; sul pulsante Compatta apre direttamente l’editor.

L’editor permette, per ogni scheda, di aggiungere, rinominare, mostrare/nascondere, rimuovere e riordinare sezioni; aggiungere, rimuovere e riordinare comandi e separatori; inserire un separatore tra sezioni e scegliere quanti pulsanti promuovere nella ribbon. Il menu della sezione contiene tutti i comandi, anche quelli non promossi. Termina schizzo è protetto dalla rimozione e dall’overflow. Conferma salva, Annulla scarta, Ripristina ricrea la disposizione iniziale solo dopo Conferma.

Preferenze: `ribbon/layout` (JSON), `ribbon/visible`, `ribbon/compact`; `ribbon/toolbarDefaultsApplied` applica una sola volta i nuovi default. Le precedenti preferenze di visibilità sono conservate sotto `ribbon/previous/`. Il nome sorgente `ribbon_preview` è mantenuto per continuità, ma il componente è condiviso con l’app normale.

## Avvio

Dalla radice del progetto, con la build già preparata:

```sh
./tools/run_ribbon_preview.sh
```

Eseguibile separato:

```text
forgecad-release/ribbon-preview/forgecad-ribbon-preview
```

La prova usa `QSettings` con organizzazione `ForgeCADPreview` e applicazione `RibbonExperiment`. Le impostazioni di `ForgeCAD/ForgeCAD` restano separate. La finestra indica **ForgeCAD — Prova ribbon**. Le barre tradizionali partono nascoste come nell’app; la ribbon è un adattatore delle azioni reali della finestra e i menu restano disponibili. Il pannello Revisioni conserva la propria preferenza.

Il target del runner condivide la ribbon dell’app e conserva preferenze separate. Il target non viene installato e resta escluso dalla build predefinita.

Per ricompilarla esplicitamente:

```sh
cmake -S . -B forgecad-release -DFORGECAD_BUILD_RIBBON_PREVIEW=ON
cmake --build forgecad-release --target forgecad_ribbon_preview -j4
```

L'opzione CMake è disattivata per default. La sua attivazione rende disponibile il target, che va comunque richiesto esplicitamente.

## Cosa provare

- Fondo delle due carte con lo stesso blur GPU dei pannelli parametrici. Opacità, sfocatura e raggio seguono le impostazioni dei pannelli nella prova, anche durante la sessione (entro mezzo secondo). Lo spazio tra le carte resta libero; la sfocatura segue resize e modalità compatta e viene rimossa nascondendo la ribbon. Senza il compositore GPU resta il fondo traslucido.
- Ribbon sopra il viewport: 123 punti logici, oppure 95 con **Compatta**, più il padding superiore/inferiore calcolato dal raggio. Il margine segue la geometria del quarto di cerchio e si aggiorna insieme all’arrotondamento; anche il blocco laterale si allarga per conservare lo spazio interno dei comandi. Le schede sono ancorate a sinistra indipendentemente dall’allineamento interno del tema Qt e restano a filo del bordo superiore per mantenere continua la linea blu sul raggio; il margine superiore viene applicato ai gruppi di pulsanti.
- Schede Modello, Superfici, Schizzo, Analisi e Vista sempre visibili. Schizzo include un gruppo Crea schizzo utilizzabile prima di iniziare; strumenti e vincoli si abilitano creando o aprendo uno schizzo. La scheda viene selezionata automaticamente entrando in modifica.
- Fondale blur GPU con curva arrotondata analitica e antialias per pixel, senza segmenti poligonali. Contorno e maschera delle carte generati dalla stessa sagoma a risoluzione quadrupla e ridotti alla risoluzione del display; il fondo viene ritagliato una sola volta. Accento blu di 3 punti logici, con sfumatura lungo l’arco e spessore costante. Un contorno bianco di 0,35 punti logici circonda entrambi i blocchi e viene disegnato prima dell’accento blu, che rimane in primo piano.
- Etichette entro due righe, elisione e tooltip completi; carattere adattato allo spazio mantenendo le icone. I nomi dei gruppi conservano spazio per la freccia del menu.
- Gruppi con pulsanti principali, menu completi e menu `»` quando lo spazio non basta.
- Termina schizzo resta fuori dall'overflow.
- Dopo l'ingresso nel contesto si può cambiare manualmente scheda senza essere riportati continuamente su Schizzo.
- Le azioni sono le stesse dei menu: stato selezionato, abilitazione, scorciatoie e comandi operativi sono condivisi. Il gruppo Zoom include le impostazioni già implementate.
- I pannelli parametrici originali si aprono sotto la ribbon, con i campi numerici e le conferme/annullamenti originali.
- Gli eventi di attivazione da pulsanti, menu e scorciatoie preparano la scheda pertinente; le riaperture dei pannelli note vengono riconosciute anche dal titolo.

## Limiti della prima versione

Il catalogo deriva dai menu italiani e il riconoscimento delle feature riaperte usa anche i titoli dei pannelli. È un ponte sperimentale che evita modifiche al dispatcher dell'app attuale; non è ancora il catalogo definitivo con identificatori stabili e notifiche esplicite di ogni fase del comando.

Il contesto schizzo è osservato tramite il getter pubblico ogni 50 ms senza sostituire i callback originali. Il ciclo di vita completo, le personalizzazioni avanzate della ribbon e la riapertura di tutte le famiglie di feature restano da completare dopo la valutazione.

Lo Zoom tutto conserva il calcolo originale sull'intero viewport. La gestione delle aree coperte per l'inquadramento e per tutti i navigatori non è ancora il servizio completo proposto nello studio. Il layout dei pannelli molto alti su viewport piccoli richiede ulteriori verifiche. La prima prova riguarda la finestra 3D; le tavole mantengono la propria interfaccia.

## Verifiche

```sh
ctest --test-dir forgecad-release -R '^forgecad_ribbon_preview_tests$' --output-on-failure
QT_QPA_PLATFORM=xcb FORGECAD_IGPU=1 ./forgecad-release/ribbon-preview/forgecad-ribbon-preview --self-test
```

Il primo comando usa preferenze temporanee e profilo offscreen. La prova nativa richiede una sessione grafica disponibile; su altri sistemi va lasciato il relativo backend Qt. Copertura: contenimento delle etichette in tutte le schede, dimensioni normale/compatta, schede contestuali, scelta manuale, compressione/overflow, uscita schizzo, pannello sotto la ribbon, QAction reali e abilitazione della rotella. La prova nativa confronta anche il framebuffer con ribbon nascosta/visibile: il blur deve modificare la scena sotto le carte e lasciare invariato il distacco tra loro. Verifica inoltre una sfumatura grigio–bianco dopo aver contaminato esplicitamente il blending come QPainter, su otto fotogrammi con viste diverse, blur presente/assente e antialiasing0/4, confrontando i pixel con i colori attesi. Apre e annulla inoltre il pannello reale di una primitiva senza creare geometria.

Cattura della sola finestra della prova, con chiusura automatica:

```sh
./tools/run_ribbon_preview.sh --capture /tmp/forgecad-ribbon-preview.png
```

Le verifiche locali non certificano tutti i DPI o i driver di Windows/macOS/Wayland. La prima cattura è stata ispezionata nella sessione X11/OpenGL locale.

## Regressione Blocco dopo apertura documento

Il catalogo elimina sincronicamente i riferimenti alle `QAction` distrutte, anche dai gruppi personalizzati: i menu dinamici come File recenti ricreano le proprie azioni a ogni apertura. Il self-test verifica questa rimozione prima di aprire i pannelli.

Per riprodurre il percorso con un documento reale senza modificarlo, il runner usa preferenze isolate e tre copie temporanee, carica ciascuna copia e clicca il vero pulsante Blocco, annullando il pannello:

```sh
./forgecad-release/ribbon-preview/forgecad-ribbon-preview --test-open-block /percorso/modello.prt
```

## Sfondo, angoli e ombra dei menu

Anche i pannelli incorporati, compreso **Vincoli**, aggiornano tinta e testo dalla palette della finestra principale quando cambia il tema, prima dell’apertura e mentre restano aperti. Intestazione e istruzioni seguono lo stesso tema; le impostazioni di vetro e la geometria salvata restano condivise.

I popup comuni (menu principali, sottomenu, menu contestuali e menu della ribbon) usano le stesse tre impostazioni di **Visualizza → Pannelli delle funzioni**: trasparenza, sfocatura e raggio degli angoli. Le chiavi condivise sono `view/functionPanelOpacity`, `view/functionPanelBlur` e `view/functionPanelCornerRadius`. Vengono rilette anche a popup aperto; raggio zero mantiene gli spigoli retti. Il margine interno cresce con il raggio per proteggere voci, icone e scorciatoie.

Il colore viene dalla palette della finestra applicativa, evitando l'eredità trasparente dei gruppi della ribbon. I popup sono finestre separate: acquisiscono e sfocano la parte sottostante della sola finestra proprietaria, con cache e aggiornamenti legati ai nuovi frame. Non viene acquisito il desktop; se la finestra non è disponibile resta la tinta del tema con l'opacità richiesta. Le icone e le scritte rimangono nitide, mentre il fondale usa una maschera alpha antialias.

Popup e ribbon hanno un'ombra morbida che segue gli angoli. L'ombra della ribbon è separata dalla maschera dei due blocchi e non intercetta il mouse; nei popup viene disegnata nel margine trasparente, senza duplicare quella nativa del sistema. Al cambio di tema o di foglio stile, il popup mantiene trasparente il fondo della finestra Qt e ricrea il fondale dalla palette aggiornata: il margine dell'ombra non riceve una tinta rettangolare.

## Ridimensionamento dei Vincoli

Il pannello **Vincoli** si ridimensiona trascinando la maniglia nell’angolo inferiore destro. Le dimensioni scelte vengono ricordate. La maniglia resta disponibile con ForgeCAD massimizzato e conserva uno spazio libero rispetto al contenuto e agli angoli arrotondati.


### Diagnostica del cambio tema nei popup

Il tema della piattaforma può sostituire l'intero `QStyle` quando cambia l'aspetto del desktop, oltre ad aggiornare la palette. Il controller conserva Fusion anche dopo questa sostituzione; i `CadMenu` condividono uno stile Fusion esplicito, distinto da quello applicativo, così il polish transitorio di Breeze non registra effetti del compositore sul margine trasparente dell'ombra.

Per raccogliere dati durante una prova reale avviare `tools/diagnose_menus.sh`: indica una cartella temporanea e avvia l'app compilata in `forgecad-release`. Aprire un menu, cambiare tema e riaprirlo; la prova non cambia le preferenze del sistema. `FORGECAD_BINARY` permette di indicare un'altra build. Prima chiudere normalmente la propria app se si vuole confrontare la nuova build; lo script non termina processi esistenti.

La cartella contiene `session.log`, `theme.jsonl` (sostituzioni dello stile e backend), `menus.jsonl` (stile esplicito, alpha, attributi Qt e impostazioni) e PNG separati di disegno, ombra, superficie e fondale dei menu. La diagnostica è disattivata nell'avvio normale; salva al massimo20campioni per popup e non acquisisce lo schermo o l'intera finestra. Per un approfondimento Wayland avviare con `FORGECAD_MENU_PROTOCOL_TRACE=1 tools/diagnose_menus.sh`; la traccia del protocollo riguarda soltanto quella istanza e può essere voluminosa.
