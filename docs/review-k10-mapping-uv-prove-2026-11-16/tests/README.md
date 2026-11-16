# Regressioni indipendenti Mapping/Quilt UV — 16 novembre 2026

Tutto il lavoro di questo agente è privato in questa directory. Nessun sorgente del repository, build condivisa, documento, CAD originale o processo utente è stato modificato da questo agente. Il coordinatore può integrare separatamente il sorgente permanente.

## Congelamento e attribuzione

- Baseline: `baseline/fk_surface_mapping.cpp`, `baseline/fk_quilt_mapping.cpp`, archivio privato `baseline/libforgekernel.a`; hash sorgenti/header in `baseline/sources.sha256`, archivio e oggetti originali in `baseline/objects.sha256`.
- Candidato definitivo V3 strict: overlay e oggetti del core, hash in `final-freeze.sha256`; superficie `c830c1c802657b50dd1cafa223af91263dba185cdf109524a16c602b6986f707`, Quilt `21a41fb7513ba6ca48987956632aa2d2bbdf57c49c4f9681907a4c512668df05`.
- Test permanente: `test_mapping_pcurve_validation.cpp`, SHA256 `df459b846a422ebe6d2bd56ea5eb6c136d573a0700413c2fbc8397143119224a`. Quattro spazi. Nessuna macro per omettere garanzie nel file permanente.
- GCC `16.2.1 20260810`, Linux; shell `/bin/bash`, cwd reale `/home/myungit/Linux_CAD_forFree`. Contrazione FP disabilitata e fast math vietata nei comandi correnti.

## Prove correnti definitive

`candidate-v3.log`: **21 gruppi PASS**. Venti corpi validi: nove varianti Mapping e nove Quilt (polinomiale, razionale, trim/intervallo -2..5, verso invertito, posa/wrapper Transformed, scala 1000, scala 0,01, quota non nulla, offset normale), più due Quilt con UV intorno a 10.000 e geometria mondo nel campo nominale. Il gruppo atomico controlla due cancellazioni e quattro rifiuti conservativi (UV 100.000/1.000.000, posa assente/presente).

Tutte le fin finali vengono verificate da `requirePCurveOnPlane` al proprio budget realmente pubblicato, oltre ai testimoni geometrici indipendenti, budget operativo fisso, range originali, genealogia esplicita, sorgente serializzata immutata e roundtrip B-rep. La misura del mapping Quilt richiama `fold` del carrier originale allo stesso parametro del bordo sorgente: la posa ideale ricostruita separatamente introdurrebbe un errore diverso nei frame traslati. I testimoni densi sul mapping non certificano globalmente il fitter 3D campionato a monte.

`baseline-final.log`: **19 PASS / 2 FAIL attesi**, entrambi falsi rifiuti pubblici Quilt UV 10.000 (`fitCurve: impossibile rispettare la tolleranza entro il limite di raffinamento`). Il sorgente privato `baseline_compat.cpp`, compilato con `FK_MAPPING_BASELINE_COMPAT`, omette soltanto la nuova asserzione del provider continuo. Questo è dichiarato nel log; testimoni geometrici e soglie sono gli stessi del permanente. La nuova garanzia non è usata per attribuire una falsa accettazione alla baseline. I 18 corpi storici sono positivi anche in baseline; la prova Fraction separata li conferma entro i budget pubblicati.

La correzione mantiene `min(1e-10,.05*fitBudget)` come limite fisico del fit PCurve. Il tentativo V2 a 1e-9 e i suoi quattro successi UV 10.000/100.000 non sono finali. V3 rifiuta UV 100.000 e 1.000.000 conservando gli output: non si promette il supporto di queste configurazioni.

## Export per audit indipendente

`exports-v3` contiene **20 JSON, 80 fin finali e 80 fin sorgenti**, oltre alle serializzazioni B-rep. Hash JSON in `exports-v3.sha256`. I double sono in formato esadecimale; ogni fin contiene indici, verso, range originale, budget, Plane con assi originali, edge 3D e PCurve realmente emessi, incluse basi di wrapper Trimmed/Transformed. La genealogia delle facce è esportata esplicitamente. La verifica Fraction/Bernstein appartiene all'agente audit e non è sostituita dalle griglie di questo runner.

`exports-baseline`/`exports-candidate`, `candidate.log`, `candidate-v2.sha256`, `probe*`, `faruv*` documentano esperimenti precedenti: non attribuirli alla consegna V3. `faruv` iniziale ha cercato UV 10.000, 100.000 e 1.000.000; non dimostra falsa accettazione Plane.

## Riproduzione e limiti

Comandi completi in `run-release.sh` e `run-sanitizer.sh`, con oggetti privati prima dell'archivio privato. La build dell'app e il CTest del repository sono compiti del coordinatore; questo agente non ha costruito app o root e non certifica l'eseguibile aperto.

Sanitizer mirato: driver permanente più `fk_surface_mapping.cpp`, `fk_quilt_mapping.cpp`, `fk_curve_fit_bounds.cpp` strumentati ASan/UBSan; resto archivio Release non strumentato. Provider sorgente SHA in `sanitizer/provider.sha256`. Il primo run con LSan dentro sandbox è fermato dal limite ptrace (`sanitizer/result.log`), non contato PASS. Il rilancio fuori ptrace usa gli stessi binari e le stesse opzioni: **21/21 PASS, exit 0**, nessuna diagnostica ASan/UBSan/LSan né soppressione, nel log `sanitizer/result-unrestricted.log`.

Il percorso continuo qui coperto riguarda fin su Plane. Non certifica il fitter 3D generico, le famiglie non Plane, l'iniettività globale, i volumi, l'assenza di autointersezioni, il renderer, altre piattaforme o leak dell'app intera.

Per l'archivio prove conservare soltanto sorgenti, script, manifest, log e export JSON/B-rep necessari; **non archiviare binari, oggetti o librerie statiche**. Ingombro temporaneo circa 668 MiB incluse copie private e sanitizer; pulizia affidata al coordinatore dopo aver salvato le prove necessarie.
