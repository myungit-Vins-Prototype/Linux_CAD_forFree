Audit indipendente K10 boundary — pacchetto curato

Leggere audit-notes.md e summary-final.json per esiti e limiti. Baseline e
candidato sono distinti. Le fixture finali sono Body sintetici che chiamano
helper di produzione, non prove pubbliche del comando raccordo.

Le cartelle exports-baseline ed exports-candidate conservano i JSON originali
prodotti dal tester, copiati senza trasformazioni. *.converted.json conserva
il singolo adattamento ai coefficienti IEEE esatti in esadecimale usato dal
verificatore. I verificatori non leggono né chiamano i provider di produzione.
I campioni sono menzionati soltanto come testimoni dell'alias del vecchio codice.

Ripetizione Fraction (Python standard library; nessuna dipendenza binaria):
  PYTHONDONTWRITEBYTECODE=1 python3 check_exports.py.txt exports-candidate rerun.json
  PYTHONDONTWRITEBYTECODE=1 python3 negative_controls.py.txt rerun.json.converted.json negatives-rerun.json

I suffissi .py.txt e .cpp.txt identificano sorgenti/script conservati come testo.
Il runner privato eseguito personalmente non è incluso: hash e exit sono
registrati; il log è v2-independent-tests.log (15/15, finale); candidate-independent-tests.log è V1 storico. Root conserva la build
riproducibile e le prove pubbliche nel pacchetto generale.

I risultati conservati sono attribuiti agli hash dei sorgenti recensiti V2; root
può rieseguire sui byte integrati e aggiungere un confronto esplicito. Nessun
ELF, oggetto, archivio, CAD o Body binario fa parte di questa consegna.
