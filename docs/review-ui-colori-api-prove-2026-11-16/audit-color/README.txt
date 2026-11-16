Audit indipendente UI residua + quattro API — 16/11/2026

Esito: nessun blocco sui due gruppi UI candidati e sulle quattro rimozioni API.
Nessuna modifica produzione, commit o push eseguita da questo auditor.

UI congelata:
gui.cpp a4ca3185da83aa638c038433de886c713a5d3e8ec4fbb35ace968d37e812395e
analysis.cpp 3e74eba725601726a5c2ec2c9fddb002bbb65c4733fb6ea3a0ac5bcec63468b4
patch 2a10264f9a0e5195159bb9f1059058f1234fee17e1b664e3e912869d6de69321

Verifica propria:104 controlli,14 misure,exit0. Driver indipendente con
PdfWindow/CadViewport/dialoghi reali e i due oggetti candidati congelati.
Sezione aperta via QAction, istruzione reale di selezione piano. Massa di un
box sintetico valido e di una copia con superficie nulla: errore kernel reale
catturato da massReport, presentato dal dialogo. Ritorno al valido Error→Normal.

Foreground realmente rasterizzato:
- Sezione Link chiaro/scuro: contrasto5.39/9.73.
- Massa normale Text ereditato: contrasto16.29/10.88 sul Base effettivo.
- Massa errore semantico: contrasto7.35/6.17.
- Nessun QTextFragment conserva ForegroundBrush esplicito nei due stati.
Pixel foreground presenti in tutte14misure. Ricalcolo indipendente dei rapporti
dai colori JSON PASS, minimo5.3949. Letti inoltre13PNG del tester: pixel del
Base dichiarato coprono almeno70% di ogni widget. Hash/conteggi conservati;
immagini non duplicate, restano con tester.

Pannello aperto: Light/Dark/Light e invio diretto PaletteChange/StyleChange.
Documento, revisione, HTML, plain text, direzione anchor/position e scroll h/v
invariati. Nessun handler/richiamo setHtml/show/reportFor al solo cambio tema:
aggiornamento naturale palette/QSS. Copia del valido verificata. Il tester
copre separatamente copia identica a ogni transizione e nessun nuovo start
massReport. Etichette e nota adottano deliberatamente Text normale al posto
della tinta attenuata; struttura, heading, tabelle, valori, metodo/unita restano.

Baseline stesso driver:exit1 dopo7controlli sull'asserzione ruolo Link. Lookup
con fallback sul QLabel storico: non fallisce per il nuovo objectName. Questa
non e una suite baseline completa: il tester separato misura64controlli e13FAIL
sulla baseline,64/64 sul candidato.

API: patch in5file rimuove soltanto CadViewport::basisMatrix/normalizedViewAngle,
extrusionNames e usesSketch. Ricerca nomi e archi AST: nessun consumer dei quattro
wrapper. Gli omonimi di ViewportCamera sono chiamati e restano; sketchesOf,
enum/dati persistiti restano. Audit closed-world del repository, nessuna promessa
ABI per binari/plugin esterni; build finale al coordinatore. Nessun test che
replichi getter rimossi.

Limiti: Linux Qt6.12/offscreen, non certificazione macOS/Windows o di tutti i
plugin/compositori. Nessuna nuova prova OpenGL. Nessun CAD/preferenza/carta utente
modificato; impostazioni isolate in QTemporaryDir. HLine fillet499 escluso, resta
candidato senza resa provata. Carta/contenuto e sfondi utente fuori perimetro.
Letti UI8, CLAUDE e mappa778/11369 prima del codice. Integrazione e aggiornamento
memoria condivisa affidati al coordinatore.

Solo UTF-8 testi/JSON/patch e sorgenti inerti .txt. Niente ELF,oggetti,librerie,
documenti CAD. execution-binding.json lega hash/comandi del driver realmente
eseguito, senza copiare i binari. SHA256SUMS copre gli altri file del pacchetto.
