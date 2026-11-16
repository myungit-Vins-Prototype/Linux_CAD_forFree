Audit indipendente V2 layout Sezione — 16/11/2026

Nessun blocco. Ultimo source12afcc0d8088eb99ea2475e1d9eb6ec0c6bf8a21de60c92e549a997fb1afba37,
patch0633e1502f8ed6992a05506ef714756dc4bcb17a8322c325e13ac069e53ec4b9.
Primo candidato d67 storico non e la fonte attribuita alle prove finali.

Driver proprio:14controlli/9stati, candidato14/14PASS exit0. Identico driver
sulla baseline gia corretta nei colori (a4ca) fallisce3controlli/exit1:
istruzione default, descrizione lunga, descrizione hidden su riapertura.
run-status.json contiene exitcode osservato da subprocess; l'ultima riga
stdout del piccolo driver mostra solo rc eccezione, non l'esito dei check.

Episodi reali:
- Dialogo gia visibile, combo index3: width257 resta, height271→288;
  QLabel34px=heightForWidth34. La vecchia label24px richiedeva34.
- Resize utente470x620 e posizione73,91: nuova istruzione lascia dimensioni
  e posizione intatte; niente ritorno al sizeHint.
- Corpo box con nome lungo e spazi, GeometryRef valido della sua faccia:
  consegna alla callback reale del picker, apply/geometryRefText producono
  il testo. Width320 e posizione81,99 restano, height290→339;
  QLabel85=heightForWidth85. Nessun setText diretto del driver.
- Tornando al piano standard/testo vuoto la finestra non si restringe.
- Finestra ridimensionata320x290, nascosta, descrizione lunga aggiornata;
  nessun resize da nascosta. Riapertura via QAction porta320x339 e label85,
  conservando posizione utente83,101. Questo esercita WA_Resized nativo.

La patch usa solo layout totalHeightForWidth alla larghezza attuale, con
fallback totalSizeHint se non HFW; cresce solo se necessario. Nessun conteggio
di righe/pixel fisso e nessun reset di larghezza, posizione o altezza maggiore.
Dimensioni del driver sono scenari, non costanti dell'implementazione.
Le4presentazioni del messaggio e i2show passano dal medesimo calcolo.

Posizione: il primo reopen automaticamente posizionato da Qt ricentra9px
anche sulla baseline; non e nuovo comportamento del V2. Le posizioni esplicite
utente sono verificate immutate nelle espansioni visibili e nel reopen.

Limiti: QtWidgets Linux/offscreen, font Sans Serif9; dimensioni cambiano con
font/piattaforma. Il test non rivendica un hit-test mouse del picker, ma usa
callback di produzione e riferimento topologico valido del box sintetico.
Nessuna nuova prova GL, piattaforme native o stringhe arbitrariamente immense.
La contrazione manuale volontaria senza un successivo messaggio non e una
nuova politica di resize automatico. Massa/colore restano fuori dalla patch.

Nessuna modifica produzione, commit/push o documentazione condivisa da audit.
Sigilli final-proof e layout-supplement precedenti lasciati invariati.
Solo testi/JSON,ricette,sorgenti/patch inerti .txt,manifest e SHA256SUMS.
Nessun ELF,oggetto,libreria,PNG o CAD copiato in questo pacchetto.
