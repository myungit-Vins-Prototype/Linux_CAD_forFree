# K10 Plane: nuovi replay dopo l'interruzione

Prove eseguite il 16 novembre 2026 sul repository `/home/myungit/Linux_CAD_forFree`, con staging privato `/tmp/projection-replay-tests`. Questi sono nuovi replay ricostruiti dopo la perdita del precedente staging temporaneo: non recuperano i vecchi log e non ricreano una baseline globale. Nessun file di produzione, oggetto condiviso, CMake o eseguibile app è stato modificato da queste prove. Il coordinatore aggiorna separatamente il rapporto principale e CLAUDE.md.

## ASan/UBSan/LSan: 33/33 PASS

`sanitizer-run.log`: `RESULT passed=33 failed=0`, exit 0, 20,98 s. Nessun rapporto ASan, UBSan o leak. Esecuzione fuori sandbox, con `detect_leaks=1` e nessuna soppressione; opzioni esatte in `sanitizer-environment.json`. Si strumentano il test permanente `test_projection_fit_validation.cpp`, `fk_project.cpp` e `fk_curve_fit_bounds.cpp` con GCC 16.2.1, `-O1 -g -fno-fast-math -ffp-contract=off -fno-omit-frame-pointer -fsanitize=address,undefined,leak`. Gli oggetti privati precedono l'archivio corrente `forgecad-release/kernel/libforgekernel.a`; sezioni funzione/dato e `--gc-sections` eliminano le dipendenze inutilizzate. Comandi completi in `sanitizer-commands.json`, script riproducibile in `sanitizer_replay.py.txt`.

Limite: il resto del kernel proviene dall'archivio Release non strumentato. Questa prova mirata non certifica controlli UBSan/ASan su ogni istruzione del kernel né l'intera app. LSan osserva le allocazioni raggiunte dall'esecuzione e non ha rilevato perdite. L'archivio usato è quello corrente, SHA256 `fde7076d2cde820df08723c045bd7866aaadefbb890aca6c5ed280d8eba47eeb`; non è il vecchio archivio baseline.

## Probe app privato: 3/3 PASS

`app_projection_probe.cpp.txt` ricostruisce tre schizzi con frame personalizzato di origine z=1 e li proietta su `makePlaneSheet(Frame3(),100)` a z=0 tramite `forgeProjectedCurves`. Confronta con pipeline `forgeSketchSegments → buildProfile → embedCurve → projectCurves → joinProjectedPieces`. Per ogni scenario richiede due segmenti originali, una catena aperta, una curva finale, struttura spline e tutti i poli identici alla pipeline, errore campionato esattamente zero e riepilogo app identico al massimo `chain.deviation` formattato `g,3`, entro `ProjectionOptions().tolerance`.

| Caso | max piece deviation (mm) | max chain deviation (mm) | Riepilogo |
|---|---:|---:|---|
| Due linee con gap 5e-9 | 6.4437788920586767e-11 | 2.500008533536046e-9 | 2.5e-09 mm |
| Due NURBS grado 27, gap 0 | 8.813376176316891e-9 | 8.813376176316891e-9 | 8.81e-09 mm |
| Due NURBS grado 27, gap 5e-9 | 8.8133761763021755e-9 | 8.8133761763021755e-9 | 8.81e-09 mm |

Nel caso linee si richiede inoltre `chain > 10*piece`: rapporto effettivo 38,7972. Il riepilogo `2.5e-09` differisce da quello errato basato sui pezzi `6.44e-11`. Nei casi NURBS il limite interno dei pezzi domina la piccola modifica degli estremi: l'uguaglianza del limite finale con quello dei pezzi è attesa, senza indebolire la verifica del riepilogo o della geometria. I 28 valori z27 sono copiati dalla fixture permanente, con grado 27, 28 nodi 0 e 28 nodi 1; ogni coppia di curve usa x=`piece*(1+gap)+i/27`.

Il sorgente probe viene compilato con il comando di `cad_forge.cpp` estratto da `compile_commands.json`. Il link deriva da `CMakeFiles/forgecad.dir/link.txt`, sostituendo solo l'oggetto main `forgeCad2026.cpp.o` con l'oggetto privato; output privato e rimozione di `--dependency-file` evitano scritture nella build condivisa. Tutti gli altri oggetti app sono quelli correnti. Comandi in `app-commands.json`, hash degli input del link in `app-hashes.txt`. Esecuzione `QApplication` offscreen, tema piattaforma vuoto e stile Fusion: exit 0, 1,15 s. Nessuna nuova certificazione del renderer/OpenGL.

Primo tentativo fallito prima dei casi: `QT_QPA_PLATFORMTHEME=gtk3` ereditato richiedeva il display `:0` anche con QPA offscreen. Log ed exit 1 sono preservati nei file `app-*-first-gtk-failure.*`. I replay finali impostano esplicitamente i temi vuoti e Fusion; nessuna modifica alla sessione utente.

## Riproduzione e integrità

Copiare gli script `.py.txt` e il probe `.cpp.txt` nello staging indicato senza il suffisso `.txt`, quindi eseguire `python3 /tmp/projection-replay-tests/sanitizer_replay.py` fuori sandbox e `python3 /tmp/projection-replay-tests/app_replay.py`. Gli script usano intenzionalmente i sorgenti e gli oggetti correnti del repository; per ripetere lo stato verificato confrontare prima gli hash con le copie archiviate. Il test e i TU strumentati sono conservati come snapshot `.txt`; il link app originale e gli archi pertinenti consultati sono archiviati. `all-input-output-hashes.txt` include sorgenti, oggetti sanitizer, binari privati e archivio corrente. `archive-hashes.txt` copre i file di questa cartella, escluso il manifest stesso.
