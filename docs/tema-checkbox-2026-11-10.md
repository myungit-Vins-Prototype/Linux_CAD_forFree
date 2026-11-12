# Tema e contrasto dei checkbox — 10 novembre 2026

Richieste: correggere le spunte poco visibili nel tema chiaro e aggiungere al menu la scelta chiaro/scuro/desktop.

## Implementato

- `forgeCad2026_dialogs_common.cpp`: `QListWidget` e `QTreeWidget` usano lo stesso `Base` opaco dei campi. Con il precedente fondo trasparente Fusion disegnava indicatori neri anche nel tema chiaro. Nessun indicatore sostitutivo: restano disegno, misure e stati nativi Qt.
- I normali `QCheckBox` conservano i distinti ruoli `WindowText` (etichetta sul pannello) e `Text` (spunta sul campo). La precedente regola QSS `color` rendeva chiara anche la spunta, riducendo il contrasto su fondo chiaro.
- `forgeCad2026_application_theme.{h,cpp}`: preferenza `view/applicationTheme` con valori `desktop`, `light`, `dark`; default desktop anche per valori sconosciuti. Le modalità manuali ignorano il colore desktop ma continuano a registrarne le notifiche: tornando a desktop usano lo schema più recente.
- `forgeCad2026_gui.cpp`: **Visualizza → Tema → Segui il desktop / Chiaro / Scuro** nello stesso componente menu dell'app. Scelta esclusiva, applicazione immediata a tutte le finestre aperte; ogni menu aggiorna la scelta alla riapertura.
- `forgeCad2026.cpp`: identità `ForgeCAD/ForgeCAD` impostata prima dell'inizializzazione del tema per leggere la preferenza salvata corretta. Anche il runner viewport inizializza dopo aver isolato le preferenze temporanee.
- README italiano/inglese, guida ribbon e descrizioni della mappa aggiornati.

## Verifiche eseguite

- Sonda PySide6/Fusion: riprodotti indicatori neri con fondo trasparente e scomparsa della spunta imponendo ingenuamente un fondo al solo indicatore; verificata la soluzione con palette nativa e fondo elenco opaco.
- `forgecad_application_theme_tests`: **PASS CTest**, preferenze temporanee, ripristino iniziale del tema salvato, default e valore invalido, transizioni, menu esclusivo e sincronizzazione tra finestre, override manuale e ritorno al desktop, sostituzione stile e stabilità degli eventi.
- Stesso runner su **Wayland PASS**, con `--simulate-theme`: backend reale e notifiche simulate, nessuna modifica alle impostazioni del desktop.
- Build definitiva di app, runner tema e runner viewport: **PASS**. Il collegamento desktop punta a `forgecad-release/forgecad`, aggiornato.
- CTest definitivo: **3/3 PASS** (`forgecad_application_theme_tests`, `forgecad_function_panels_tests`, `forgecad_surface_poles_multiple_tests`). Pannelli esercitati tramite azioni reali del menu; confronto dei pixel realmente renderizzati nei due temi e controlli delle dimensioni. Catture dei checkbox e del menu in `/tmp/forgecad-theme-qa/`, verificate visivamente.
- Wayland pannelli: **PASS** il blocco iniziale relativo a menu/checkbox, catture in `/tmp/forgecad-theme-qa-wayland/`; la suite completa fallisce poi l’asserzione esistente sul lungo messaggio diagnostico selezionabile/copiabile e sul relativo layout. Non attribuito a un difetto del tema né dichiarata la suite intera superata; causa di quel controllo non indagata in questo intervento.
- Mappa aggiornata: **676 file, 9755 relazioni, 331 unità C++, zero errori AST**; `--check` **PASS**. Verificate le chiamate GUI/entry point → gestore tema, collegamenti in entrambi i versi per tema/GUI/pannelli e tre viste HTML in Chrome locale. JSON/HTML conservati ignorati da Git, come richiesto dal progetto.

Il test del tema nel sandbox locale resta in attesa, come già registrato per la build precedente; eseguito dall'host passa. Le preferenze dei test sono isolate. Non chiudere l'app dell'utente: riaprirla per caricare il nuovo eseguibile già compilato. Nessuna verifica macOS locale.
