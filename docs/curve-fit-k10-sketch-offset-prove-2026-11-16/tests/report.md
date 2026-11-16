# K10 — test offset dello schizzo, 16 novembre 2026

Letti AGENTS.md e CLAUDE.md; consultate descrizioni del modulo e archi in entrambi i versi del grafo prima di creare il test. Operazioni soltanto in /tmp, nessun edit a produzione/CMake/docs/build condivisa da questo agente.

## Baseline e riproduzione

Snapshot prima delle build: 1100 file, 810 MiB, oggetti/librerie della build Qt/kernel e sorgenti pertinenti. Manifest `baseline-sha256.txt`; il link dei runner usa cad_sketch_offset e cad_curve_solver congelati, libforgekernel congelata. Il runner corretto ricompila il candidato in /tmp e usa la medesima libreria kernel congelata.

La fixture pubblica è CurveObject Converted con grado27, poli originali e nodi clamped. Origine coefficienti: fixture K10 thicken già versionata. Ordinata z27 scalata0.01 e offset0.001 evitano il controllo indipendente di collasso (soglia10e-7), mantenendo un errore oltre fitTolerance1e-7. La curva originale z27 non scalata viene rifiutata dalla baseline: non presentata come nuova finding.

Baseline: offset polinomiale accettato con quattro poli; massimo errore sullo stesso parametro5.74246e-7 e proiezione geometrica diagnostica5.74190e-7. Variante razionale w=1+0.5i/27, Xnumerator=t+0.5i(i-1)/(27*26), Y=0.01z27/w: baseline accetta, errore stesso parametro5.41620e-7 e proiezione geometrica diagnostica5.41409e-7. La prova rigorosa geometrica con hull/Fraction è affidata all'audit indipendente e non viene dedotta da sole proiezioni numeriche.

## Runner permanente proposto

`test_sketch_offset_validation.cpp`, main autonomo senza QApplication/moc. Link minimo Qt6::Widgets + forgekernel, due moduli applicativi cad_curve_solver/cad_sketch_offset. Nessun test OpenGL.

27 casi:16permutazioni polynomial/rational×poli inversi×reverse×bothSides;4catene (concava per esercitare extended e range finale accorciato, orientamento inverso, roundCorners);2NURBS razionali multispan con nodo C0 e angolo90° nei due versi;zero-speed con fallimento atomico e sentinella;refresh associativo con quota/sorgente modificate e distanza invalida atomica;loop parabola oltre cuspide1→1.1 con mapping dei domini postjoin;fallimento successivo a prima sorgente valida con sentinelle/flagconstruction/dimensioned;controlli analitici segmento/cerchio.

- Baseline congelata:6PASS e21FAIL attesi (16hidden+4chain+refresh).
- Candidato SHA62bc7824975f38656f0bbba2279e9bc1f47ca57380c6b5468d1170eba18ad69f:27/27PASS.
- Massimo scarto hidden dopo correzione3.92702e-8; nodoC0 a tangente unilaterale7.01905e-8.
- Parabola rifilata: scarto distanza alla sorgente1.36428e-8 per1mm e1.07631e-8 per1.1mm, senza anelli ripiegati.
- Persistenza/Undo demandati ai test viewport esistenti gestiti dal root: associativeOffset, inwardOffsetAndProjection, derivedSketch, panelUndo; non introdotte dipendenze documentali inutili nel runner minimo.

## Sanitizer e limiti

ASan+UBSan mirato: test, cad_sketch_offset, cad_curve_solver, fk_curve_fit_bounds, fk_curve_ops e fk_offset ricompilati con instrumentation; resto libreria kernel congelata non strumentato. Non è una certificazione completa di memoria del kernel/app. Primo run nella sandbox:27/27PASS e nessun errore ASan/UBSan; la scansione finale LeakSanitizer interrompe con errore di ambiente ptrace (exit1). Ripetizione fuori sandbox autorizzata:27/27PASS, LSan183byte/4allocazioni nel solo driver libnvidia-glcore.so.620.70.06 caricato dal link QtGui/Widgets, nessun backtrace prodotto. Gli stessi identici sei oggetti sono rilinkati con la sola QtCore (unica libreria Qt con simboli effettivamente richiesti), evitando il caricamento estraneo del driver GL; Terzo run con leakcheck attivo, senza soppressioni:27/27PASS, ASan/UBSan/LSan senza diagnostica, exit0 (sanitizer-core.log).

Test di regressione posizionale, geometria campionata e controlli topologici locali; non prova globale di topologia o distanza minima. Il certificato di produzione usa il provider originale, non questi campioni come criterio di accettazione.
