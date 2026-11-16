UI8 — tema nelle schede, prove del 16 novembre 2026

Il sigillo SHA256SUMS inventaria i file effettivamente archiviati. Verifica:
  cd docs/review-theme-tabs-prove-2026-11-16
  sha256sum -c SHA256SUMS

root/: baseline di 4076 hash, sorgenti precedenti e finali, differenza della fase,
compilazione completa, CTest selezionati, esecuzioni Wayland del runner integrato,
avvio app, confronto QSS, mappa e audit di preservazione. I sei PNG misti scelti
e i corrispondenti solo2D sono del runner Release integrato da 1569/898 controlli.
Il primo CTest prematuro sul vecchio binario è conservato separatamente: non è
una verifica del candidato e non entra nei risultati finali.

core/: quattro sorgenti del candidato V2 e patch; inventario SHA baseline/candidato.
independent/: riproduzione dell'evento/QSS, audit icone/menu, reingresso V2,
confronto carta/cartiglio. Il confronto V1 è storico e distinto dal V2 integrato.
tests/: header permanente, sorgenti del driver, comandi di compilazione/link,
log baseline/candidato/sanitizer e manifesti di attribuzione. I sei PNG selezionati
qui provengono dal driver precedente V2 da 1469 controlli, stessi sorgenti di
produzione; il manifesto distingue questa attribuzione dai test finali 1569.

I manifesti di attribuzione degli agenti conservano i riferimenti originali in
/tmp, inclusi hash di eseguibili e oggetti: NON sono l'inventario del pacchetto.
ELF, oggetti, librerie e documenti CAD non sono inclusi. I sorgenti delle prove
sono testo inerte *.cpp.txt, *.h.txt, *.py.txt; CMakeLists.txt.txt è intenzionale.

Ripetizione su build propria:
  cmake --build forgecad-release --parallel 2
  ctest --test-dir forgecad-release --output-on-failure -R 'forgecad_document_theme|forgecad_application_theme'
  QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ./forgecad-release/forgecad_view_tests --theme-tabs
  QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ./forgecad-release/forgecad_view_tests --theme-tabs-only-2d
Il runner permanente isola impostazioni, recupero e CAD temporanei. Per
riprodurre la baseline, usare un checkout sacrificabile con i quattro sorgenti
root/before precedenti e mantenere le nuove regressioni: non sostituire i file
nel checkout di lavoro. La baseline fallisce 173/48 controlli.

Limiti: Linux/Qt6.12 locale, offscreen/Wayland. Desktop tramite segnali Qt,
non un cambio nativo OS certificato. ASan+UBSan solo controller tema e driver,
resto Release; detect_leaks=0. Nessuna certificazione globale di memory leak,
nessuna uniformazione implicita dei colori letterali ancora aperti.
