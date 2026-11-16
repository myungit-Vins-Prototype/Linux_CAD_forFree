Audit indipendente CI sheet regression — V4 finale

Esito: NO BLOCKER sul sorgente fk_sheet.cpp SHA
b99a70318f2cf7dcd6b24e646d8976bd287f6622ed9cf4accab968403380afbc.
Leggere audit-report.md e summary.json per attribuzioni e limiti.

Il pacchetto contiene soltanto UTF-8, JSON, log e copie inerti di sorgenti/script.
Nessun ELF, .o, .a, .prt o Body binario è incluso. I runner eseguiti sono
identificati per hash; root conserva la build e le prove generali.

Riproduzione con sola Python standard library, dalla cartella del pacchetto:
  PYTHONDONTWRITEBYTECODE=1 python3 verify_failed_join.py.txt failing-certificate.json failed-rerun.json
  PYTHONDONTWRITEBYTECODE=1 python3 check_source_point_witnesses.py.txt source-shared-edge1.json source-points-rerun.json
  PYTHONDONTWRITEBYTECODE=1 python3 verify_actual_newton_fins.py.txt actual-newton-fins.json actual-rerun.json
  python3 check_fma_underflow_guard.py.txt

I verificatori originali Fraction sono riusati dai precedenti audit, con loader
locale dei file .py.txt. Non leggono né chiamano il provider o l'estrattore di
produzione. I valori IEEE effettivi sono ricostruiti come razionali esatti.
Il witness negativo è geometria precommit realmente emessa dal vecchio algoritmo.
La coppia finale di fin proviene da extendSheet pubblico e viene selezionata con
BodyHistory (facce0/1, vertice sorgente, esclusione dell'edge originale), mai con
una ricerca per prossimità. Il suo successo non certifica in generale Newton.

SHA256SUMS sigilla tutti i file salvo se stesso. manifest.json elenca il contenuto
curato; i file baseline e le versioni precedenti non vengono confusi col finale.
