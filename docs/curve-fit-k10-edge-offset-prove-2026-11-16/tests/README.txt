Prove private K10: bordi delle facce estruse
16 novembre 2026, Linux, GCC, kernel indipendente da Qt e OCCT.

Ambito

Il target è E(t) + d * normalize(G'(U(t)) × D). E è il bordo originale,
G è il generatore originale della superficie, U viene dalla SP-curve affine,
D è la direzione effettiva dell'estrusione. Invertire U cambia il percorso
sulla superficie e non deve invertire la normale. Il segno della faccia
viene applicato a d. Gli input sono definiti dai coefficienti IEEE originali.

Confronto funzionale con la baseline

baseline_fk_offset.cpp è la copia precedente alla correzione, compilata
privatamente e linkata prima dell'archivio kernel corrente. SHA256:
7c8a22480b028b1c460e41ef5e9c1c6e49bad1bdba99d6417b3668c3d6560185.
Il linker non estrae quindi anche fk_offset.o dall'archivio.

La baseline fallisce tutte le 8 regressioni pubbliche, con errori fra
5.416e-7 e 5.7425e-7 contro una tolleranza di 1e-7. La fonte e l'output
superano checkBody; la fonte resta identica nella serializzazione binaria.
Il riproduttore originale e il caso "public baseline witness" mostrano
un punto dell'offset esatto fuori dall'intero control hull dell'output
per almeno 5.742474088626416e-7: è un testimone geometrico, indipendente
dall'associazione dei parametri o da un algoritmo del punto più vicino.

Contro lo stage finale, tutte le 18 regressioni passano:
- 8 prove pubbliche offsetFaces: polinomi e razionali, U diretto e inverso,
  senso di faccia invertito, wrapper Trimmed e Transformed, bordo distinto
  dal generatore con V(t) obliquo ma SP-curve esatta;
- 10 prove del provider: E e G geometricamente indipendenti, distanza
  negativa, U inverso senza inversione della normale, wrapper, intervallo
  del bordo [-3, 5], nodi del generatore e della SP-curve, durata dei dati
  dopo la distruzione degli input, domini illegali, distanza NaN, normale
  degenere e cancellazione con deviation invariata.

Non ci si limita al confronto campionato: su ogni campata Bezier finale
viene richiamato il provider e richiesto un limite finito <= 1e-7.
Il massimo limite osservato è circa 9.40813403e-8; l'errore misurato massimo
è circa 3.92749e-8. I campioni sono testimoni aggiuntivi, non certificati
indipendenti dell'intero intervallo.

Riproduzione

reproduce.py.txt accetta il repository, una cartella privata di output,
lo stage dei sorgenti e le opzioni --baseline e --sanitize. Compila soltanto
in /tmp, senza modificare CMake o la build del repository. Integrazione del
test: definire HAVE_EDGE_PROVIDER oppure rimuovere i due guard relativi;
senza la definizione restano soltanto gli 8 casi pubblici della baseline.

Esempi dopo l'integrazione dei sorgenti di produzione:
python3 reproduce.py.txt /repo --output /tmp/edge-tests
python3 reproduce.py.txt /repo --output /tmp/edge-baseline --baseline
python3 reproduce.py.txt /repo --output /tmp/edge-asan --sanitize

Il primo stage sperimentale rifiutava 16 casi entro il budget invece di
accettare geometria errata. Il pullback Bernstein successivo ha risolto
il problema senza aumentare tolleranze o budget; il log iniziale resta
conservato, distinto dal risultato finale.

La mappa è stata consultata per fk_offset, fk_curve_fit_bounds, relativi
header e test_offset_validation, verificando gli archi cad_forge → offsetFaces,
offsetFaces → offsetCurve / assembleBody, offsetSurfaceImpl → provider
esistenti e fitCurveCertified. Nessun sorgente di produzione modificato
dall'agente test. I file archiviati sono solo prove autorizzate.

Sanitizer finale

ASan, UBSan e LeakSanitizer: 18/18 PASS, exit 0, nessuna diagnostica e
nessuna suppression. Strumentati fk_curve_fit_bounds.cpp, fk_offset.cpp,
fk_affine_surface_fit.cpp e il main del test. Il resto del kernel proviene
dall'archivio Release preesistente e non è strumentato. Esecuzione fuori
dal tracciamento ptrace della sandbox, richiesto da LeakSanitizer.
Non è una certificazione dell'applicazione grafica o dell'intero kernel.
