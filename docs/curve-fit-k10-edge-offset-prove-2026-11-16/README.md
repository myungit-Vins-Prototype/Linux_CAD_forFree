# Prove K10 — offset dei bordi di estrusione

- `root/`: snapshot iniziale2788file dopo l’ellisse, sorgenti congelate, build completa,24CTest, output delle18regressioni raccolto dalla stessa esecuzione, mappa e conservazione.
- `core/`: originali e finali dei3file, delta, primo rifiuto conservativo privato, diagnosi, riproduttore e23regressioni precedenti.
- `tests/`: test permanente, baseline8fallimenti, normale18PASS e ASan/UBSan/LSan18PASS; manifest e istruzioni.
- `independent/`: prova Fraction della baseline,40dump originali/finali,301campate certificate e confronto byte della Release; [audit](independent/final-audit.md).

I sorgenti/script archiviati hanno suffisso `.txt` per restare fuori dal catalogo. Non sono conservati binari o oggetti. Prima di ripetere verificare gli hash e ricompilare sulla macchina corrente; gli script riportano i percorsi originali. I test si ripetono con `ctest --test-dir forgecad-release -R '^forgekernel_edge_offset_fit_validation_tests$' --output-on-failure`; il target definisce HAVE_EDGE_PROVIDER, necessario per tutti18casi.

Il sanitizer copre tre TU e main, il resto kernel è Release non strumentato. Il confronto baseline sostituisce solo fk_offset precedente; non è un checkout storico integrale. La verifica indipendente attesta i poli realmente esportati dalla Release finale, con40dump confrontati;32bordi pubblici più8fit diretti.

`root/final-audit.json` verifica conservazione, sorgenti/binari, link e flag FP; `root/change.diff` distingue queste modifiche da quelle precedenti. `SHA256SUMS` copre tutto l’archivio escluso se stesso. Le prove dell’ellisse e degli interventi K10 precedenti restano immutate.
