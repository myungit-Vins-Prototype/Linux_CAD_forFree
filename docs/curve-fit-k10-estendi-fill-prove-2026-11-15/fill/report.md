# K10 successivo — fill e proiezione affine

Implementato nei soli file assegnati: fk_fill.cpp, nuovo fk_curve_projection.h/cpp, nuovo test_curve_projection.cpp. AGENTS/CLAUDE e grafo consultati; parent possiede CMake/generator/docs/build integrata. Snapshot fill pre-edit in fk_fill.before.cpp; patchsolo delta fill.patch, manifest SHAfinali.

API projectCurveAffine(source,range,frame): coordinate frame.toLocal(C(t)).xy; non exactPCurve e nessuna premessa che la curva giaccia sul piano. BSpline2 conserva grado/knots/weights e proietta poli; Transform3 composte correttamente dall'esterno all'interno, incluse affinità nonsimilarity/shear; TrimmedCurve2 conserva dominio e evaluateLeft nativo. In Trimmed si estrae base sull'intero dominio del trim, evitando il bug identificato dall'audit per LineTrim[0,10] con requestedrange[2,4]. Line emette BSpline1 con endpoint nello stesso parametro e nodi range; collasso sotto proiezione ammesso. Nessuna normalizzazione della direzione proiettata.

Coniche angulari Circle/Ellipse e Other restituiscono null: fill mantiene il fit legacy esplicito per questi tipi. Non introdotti nuovi CurveTypeOther o riparametrizzazioni razionali coniche; compatibilità BodyIO preservata. Invalidsource/range lancia, cancellationpropagata.

Build privato: python3 build.py (comandi reali compile_commands e archivio Release esistente), tests.log5/5PASS; build.log pulito. Copia finale sorgenti nel repo dopo prova privata, incluso fixTrimmedLine già testato.

5 gruppi permanenti:
- NURBSrazionale multi-span: grado/knots/weights bitidentici, posizione/derivate0..2 e evaluateLeft su nodo C0; offplaneplane arbitrary; sameparameterdense checks.
- NestedTrim/Transformed con matrice shear3x3 nonsimilarity, rotazione/traslazione successiva, dominipreservati, extrapolazione oltre range richiesto.
- Line3obliqua e collassata, lineTrimwide/nested+rangeparziale, conicheunsupported esplicite anche transformed.
- Invalidrange/source e tokeniniziale OperationCancelled.
- fill endtoend con bordo NURBSrazionale, grado/knots/weights della PCurve preservati, punti edge→surface stessi parametri; scrittura/lettura BodyBinary e ripetizionecontrolli.

Controprova regressione: build-checks.py baseline compila fk_fill.before.cpp e collega nuovohelper/test. baseline.log:4/5; gruppo fillfallisce2asserzioni (prima/dopo BodyIO) sulla conservazione degree/knots/weights. Non affermiamo che la vecchiageometria razionale di questo caso fossefuoritolleranza: il controllo dimostra che fillusa il percorsoesatto senza fit e che lapersistenza conserva tale rappresentazione. Geometria stessoparametro è testataseparatamente nelhelper e nell'outputfill.

Sanitizer: build-checks.py asan strumenta fk_curve_projection.cpp, fk_fill.cpp e test_curve_projection.cpp con ASan/UBSan; frameworkmain e resto libforgekernel.a nonstrumentati. Esito runtime da aggiornare separatamente dopo run; nessuna pretesa di sanitizer completo kernel/app.

Esito sanitizer finale: **5/5PASS**, ASan/UBSan/LSan con detect_leaks=1:halt_on_error=1 e UBSAN halt_on_error=1; exit0 nessuna diagnostic. Esecuzioneescalated per evitare interferenza LSan del sandbox. Log asan.log, build asan-build.log. Solo moduli indicati strumentati, restoarchivio nonstrumentato.
