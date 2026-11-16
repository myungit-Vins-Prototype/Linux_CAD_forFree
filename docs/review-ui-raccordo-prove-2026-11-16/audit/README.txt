UI11 — separatore Raccordo, audit componente Qt isolato

Bug cromatico confermato: color:#35485c di fillet.cpp499 viene realmente
dipinto dal QFrame, non e una proprieta ininfluente. QFrame HLine, shadowPlain
default, lineWidth1, foregroundRoleWindowText. Palette WindowText e Text sono
forzate al letterale; i ruoli Light/Dark del tema variano ma non la linea.

Baseline9/9 verifiche raster. In Chiaro→Scuro→Chiaro la linea ha360pixel
esatti#35485c, su720pixel di sfondo, frame360x3. Sfondo effettivo del pannello
#e6eef6→#1c2735: contrasto descrittivo8.03→1.61. Controllo identico senza
colorelocale cambia#202020→#d8e4ea. Il colore fisso e quindi visibile e
indipendente dal tema; nessuna diagnosi basata sulla sola palette.

Candidato9/9 verifiche: unica rimozione dell'override. Stesso componente,
layout/stile, linea che segue WindowText nei due temi; vecchio#35485c assente.
Il primo e il terzo stato chiaro coincidono. Confronto comprende vero parent
FunctionDialogPanel di produzione, non una replica del suo stylesheet.
Sorgente candidato filletSHA5de6912b585ade4b03b2e51092c92d782367ed376fb96eb6469f2b53afaeb670.

La prova e volutamente MINIMA: QFrame reale con le istruzioni del sito,
FunctionDialogPanel reale, ApplicationTheme reale; non apre il dialogo fillet
completo o un viewport, non costruisce CAD, non prova algoritmi Raccordo.
Qt6.12/Fusion/QStyleSheetStyle Linux offscreen. PanelOpacity100 rende lo
sfondo deterministico, non certifica il vetro traslucido su ogni scena.
Nessuna finestra Wayland o interazione con la sessione bloccata. Nessun test
su altri desktop/plugin/stili nativi. Rapporti contrasto descrittivi, nessuna
soglia accessibilita o difetto funzionale attribuiti a questo separatore.

Nessuna scrittura repository da audit. La patch minima inerte e congelata;
il coordinatore gestisce integrazione/app build/mappa/CTest/commit/CI.
Mappa consultata prima del codice (ora778/11373), collegamento filletDialog→
FunctionDialogPanel confermato. Nessun altro colore esaminato o modificato.

Pacchetto: sorgenti inerti .txt, ricette JSON, log exit0 di baseline/candidato,
risultati palette+raster, PNG selezionati dei pannelli chiaro/scuro e delle
linee scure, patch one-line, manifest e sigillo. Nessun ELF/.o/.a/CAD.
La chiave JSON literal identifica il widget del sito in entrambe le varianti;
nel candidato styleSheet e vuoto e la linea usa il foreground nativo.
