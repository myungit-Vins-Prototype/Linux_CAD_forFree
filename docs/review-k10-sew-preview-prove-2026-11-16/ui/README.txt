PRE8 — Smusso non aggiorna preview alla fine editing tramite Tab
Prototipo privato, nessuna modifica repository.

Difetto visivo/stato P2: nella distanza primaria, seconda distanza o angolo,
Ctrl+A/testo/Tab aggiorna il parametro UI ma conserva vecchia preview valida,
chiave e generazione e messaggio pronta. Con job pendente il vecchio worker
rimane non annullato e puo' pubblicare il vecchio valore come pronto.
Conferma e' sempre protetta dalla chiave: nessuna promozione B-rep sbagliato;
sulla baseline viene ricostruita geometria con il parametro UI aggiornato.
Il primo probe reale chamfer.cpp/log mostra1.25UI/0.5preview e conferma corretta.

Causa: forgeCad2026_dialogs_solids.cpp:198-201 imposta solo onReturn.
ExpressionSpinBox::keyPressEvent e stepBy (cad_expression.cpp:103-117)
lo invocano per Invio e frecce; focusout aggiorna valore con editingFinished.
Mancano i collegamenti editingFinished alla refresh locale.
Chiamanti reali gui.cpp:410(edit storico) e2129(nuovo smusso).
Per raccordo !chamfer la stessa funzione inoltra al wizard diverso:fuori scope.

Correzione proposta: solo due QObject::connect dei due campi editingFinished
alla refresh esistente. Restano Invio, frecce e conversioni esistenti.
Nessuna API nuova, nessuna modifica kernel o CMake effettuata dall'agente.
Pista angoli scartata: setSuffix nonvuoto disabilita gia' conversione lunghezze,
come documentato in review-2026-11-15/interfaccia.md:79; nessun nuovo UI7.

Regressione permanente: test_chamfer_preview_editing.h dentro ViewportInteractionTest,
static void chamferPreviewEditingReview(); CLI --chamfer-preview-editing.
Target integrazione scelto: forgecad_chamfer_preview_state_tests.
Sei casi con watchdog15s ciascuno; timeout CTest proposto120s.
3campi (primaria,seconda distanza,angolo) x2stati (pronta/jobpendente).
Tasti reali Ctrl+A/testo/Tab; verifica editingFinished e valore UI, generazione,
invalidazione geometria/stato, token inizialmente falso e poi annullato.
Invio e Up/Down verificati in ogni caso; conferma reale createBlend, parametro,
volume B-rep mostrato e confermato identici, una sola apply e cleanup.
Check accumulati; throw soltanto dopo loop e cleanup. Header non cambia QSettings;
solo wrapper privato isola settings con QTemporaryDir.

Header finale compilato ed eseguito davvero:
private-header.log:90PASS/0FAIL offscreen,exit0.
private-header-wayland.log:90PASS/0FAIL Wayland,exit0.
baseline-header.log:63PASS/27FAIL sui soli oggetti app,exit1.
baseline-six-original.log e' la stessa prova prima dei18check Invio/frecce:
45PASS/27FAIL, senza difetto del fixture o modifica del prodotto fra i due.
I27fallimenti sono asserzioni sul medesimo PRE8, non27bug.
Non si tratta di90difetti distinti o di una certificazione dei pixel OpenGL.

Comandi da workspace:
python3 /tmp/preview-next-review/next/build-regression.py --private --header
python3 /tmp/preview-next-review/next/build-regression.py --header
QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion /tmp/preview-next-review/next/regression-private-header
Il replay registrato privato finale aggiungeva --reuse-overlay per conservare
l'oggetto privato solids gia' compilato; fonte immutata. I due link JSON
registrano esattamente quali oggetti erano sostituiti. Wayland fuori sandbox
tramite escalation consentita auto-review. Nessun CAD o processo utente toccato.

Integrazione e verifica Release/mappa rimangono al coordinatore. Non e'
dichiarata aggiornata l'app. Nessuna campagna sanitizer nuova qui.

ARCHIVIO CURATO
Fonti.cpp/.h/.py sono conservate come.txt per non entrare in build.
Per ripetere, copiarle in/tmp/preview-next-review/next rimuovendo il solo suffisso.txt;
il build script richiede la build forgecad-release e non modifica sorgenti produzione.
SHA256SUMS sigilla i nomi effettivi archiviati; nessun oggetto/binario/CAD incluso.
Le prove private precedono l'integrazione e non anticipano i nuovi test Release root.
