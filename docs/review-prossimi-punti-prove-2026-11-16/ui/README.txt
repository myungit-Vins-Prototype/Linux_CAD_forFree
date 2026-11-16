Audit statico UI successivo a UI8 — 16/11/2026

Repository reale: /home/myungit/Linux_CAD_forFree
Commit letto: 046685ef1d31fdf4b61571d9630ea0f245b4ffb7

Due casi confermati nel codice:

1. forgeCad2026_gui.cpp:1809 — Visualizza > Parametri della sezione...
   Il messaggio Scegli il piano nella vista. e la descrizione del piano usano
   sempre #9fc6e8. QLabel/QDialog locali, nessun ramo chiaro/scuro.

2. forgeCad2026_dialogs_analysis.cpp:565,618 (errore:569) — Analisi >
   Proprieta di massa... (Ctrl+M). Etichette della tabella #9fb3c8, nota
   Calcolo/Unita #8aa0b4, errore #ff7b72 inseriti come color HTML inline.
   L'aggiornamento della palette del pannello non cambia quei formati.

Candidato separato:

- forgeCad2026_dialogs_fillet.cpp:499 — Raccordo, separatore QFrame::HLine,
  color:#35485c locale. Il letterale e fisso; il colore della linea effettiva
  dipende anche da stile e ruoli del frame. Non dichiarato bug visivo provato.

Il fatto dimostrato per i primi due e il foreground UI indipendente dal tema.
Non sono state misurate leggibilita, contrasto o pixel; nessuna certificazione
su tutti i temi desktop. Nessuna attribuzione di nuovi difetti alla propagazione
UI8: ApplicationTheme aggiorna palette+QSS; questi sono override locali residui.

Distinzioni deliberate:
- esclusi carta bianca, bordo/cartiglio e contenuto del disegno: UI8 li preserva;
- esclusi sfondo 3D e albero scelti dall'utente; applyTreeBackground a
  forgeCad2026_gui.cpp:4661 deriva gia il testo dal colore personalizzato;
- esclusi letterali che fanno gia parte di rami chiaro/scuro: ApplicationTheme,
  FunctionDialogPanel::updatePalette e ruoli functionMessageRole;
- nessun inventario globale e nessuna conclusione che ogni colore fisso sia errato.

Percorso tecnico confrontato con la mappa:
forgeCad2026_gui.cpp -> ApplicationTheme e massPropertiesDialog ->
FunctionDialogPanel. Il pannello comune gestisce PaletteChange e StyleChange
(common.cpp:632) e offre ruoli semantici distinti per tema (common.cpp:842-851).
Questa disponibilita non applica automaticamente i ruoli ai QLabel locali o ai
formati HTML dei due siti. Nessuna patch proposta o applicata in questa fase.

findings.json contiene percorsi assoluti reali, linee e percorso di accesso;
provenance.json contiene hash dei sorgenti e sottoinsieme della mappa consultata.
Nessun test, build, processo app o modifica repository; sole scritture in questa
cartella. Memoria condivisa CLAUDE.md lasciata al coordinatore come richiesto.
