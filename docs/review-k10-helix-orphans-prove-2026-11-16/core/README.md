# K10: proiezione certificata di Helix con wrapper — freeze privato

16 novembre 2026. Nessuna modifica di produzione effettuata dall'autore di questa consegna. Le quattro fonti in questa directory sono congelate; `stage-hashes.json` identifica i byte. `base-hashes.json` identifica gli originali dei due file esistenti; null indica un file nuovo. `prototype.patch` ha percorsi di destinazione reali. Controllare gli hash base prima di applicarla.

## Contratto e portata

`detail::helixProjectionErrorBound` possiede snapshot di HelixSpec, coefficienti IEEE memorizzati rise/slope, matrici originali di ogni wrapper e assi originali dello sketch. Certifica frame.toLocal(source(t)).XY, in millimetri e nello stesso parametro originale. Supporta Helix con catene Trimmed/Transformed, massimo 256 wrapper, intervallo finito crescente contenuto in tutti i domini. Callback e certificato sopravvivono alla distruzione della sorgente. Non usa sorgenti catturate vive o campioni per certificare.

`projectHelixCertified` produce cubiche non razionali C0. Il bound combina Hermite M4*h^4/384 con errore di tutti i quattro poli effettivi rispetto ai poli Hermite ideali, calcolati mediante intervalli outward, riduzione dell'argomento con bracket di pi/2 e Taylor sin/cos. Include matrici e proiezione originali. Tolleranza esistente invariata. Preflight 16384 campate uniformi sufficienti: rifiuto conservativo di risorse, non prova di impossibilità geometrica. Nessuna allocazione proporzionale ai milioni di giri prima del rifiuto.

`requireHelixProjection` ricostruisce il certificato dai dati originali e controlla schema cubico C0, domini, nodi e tutti i poli 2D effettivi. Nessun output/deviation viene pubblicato in caso di errore. Unsupported ritorna vuoto/null; input riconosciuti invalidi o phase non risolvibile generano eccezione; query invalide restituiscono infinito. Getter aggiunti a HelixCurve espongono i coefficienti IEEE già memorizzati, senza mutare layout o ricostruire coefficienti ideali.

La UI usa questo percorso soltanto dove entrava il fitter generico: Helix contenente Transformed, eventualmente con Trimmed annidati. Dopo conversione a QPointF e recalculateCurve ricostruisce curveGeometry e ricertifica i dati realmente emessi prima dell'append; salta la scorciatoia collineare campionata. Helix nuda e Trimmed senza Transformed conservano il percorso quintico C2 helixBSpline preesistente: questo blocco non lo certifica. Altri Other mantengono il fallback esistente. È quindi una migrazione K10 parziale, non una garanzia su tutte le curve Other.

Il traversal UI usa un helper iterativo locale con cap256 prima della verifica implicita e del fitter, dentro il try esistente; preserva OperationCancelled separata. Il provider non chiama domain() ricorsiva sui Transformed. È una guardia di robustezza; l'audit non ha riprodotto crash nella build Release su 20000 wrapper.

## Prove attribuibili

- Negativo Qt pubblico preesistente: Transformed(Helix, identity), radius1e-4, pitch1e-6, turns1e6. Su YZ il vecchio codice emette Converted grado3/4 poli, errore1e-4 contro budget1e-7. Su XZ collassa erroneamente in segmento, errore2e-4. Fonti/log: `/tmp/k10-sketch-review/tests/probe_projected_helix.cpp` e `.log`. Entrambi ora rifiutati atomicamente per risorse.
- Suite Qt dell'agente indipendente: 13/13 PASS prima dell'ultima guardia traversal; replay finale e sanitizer di refs/provider/driver assegnati al medesimo agente. Percorso `/tmp/k10-sketch-review/tests/test_sketch_projection_fit_validation.cpp`; leggere il log finale per attribuzione all'ultimo hash.
- Verifica Fraction indipendente del parent: 8 casi migrati, 624 campate complete, massimo7.61259490419804e-8 mm; caso tight5.2614e-9<=1e-8; polo alterato rifiutato. Baseline due alias con lower bound oltre1000x/2000x il budget. La guardia finale non cambia la matematica o i risultati geometrici.
- Audit statico matematico indipendente e replay finale sul provider ffa39b64: 37 PASS/0 FAIL, inclusi query non valide, schema, NaN, cancellazione, recupero, cap256/257 e catena20000 con stack256KiB. Consegna `/tmp/helix-independent-audit/delivery`; il report matematico iniziale cita il provider pre-format16349e7. `formatting.json` documenta equivalenza dei token dopo sola formattazione; ultima guardia limitata ai traversal e rilette anche le modifiche UI.
- Probe diretto finale incluso: fit769 poli, bound1.5120907025127785e-8<=1e-7; source distrutta dopo factory; caso enorme rifiutato con deviation123 invariata. La misura densa riportata è solo diagnostica, non il certificato.

## Integrazione a cura del parent

1. Copiare/applicare le quattro fonti congelate ai percorsi presenti in `stage-hashes.json`. Non occorre cambiare fk_helix.cpp.
2. In `kernel/CMakeLists.txt` aggiungere fk_helix_projection.h e fk_helix_projection.cpp accanto a fk_helix.h/.cpp nella lista di forgekernel. Aggiungere fk_helix_projection.cpp alla lista set_source_files_properties dei validatori outward con COMPILE_OPTIONS `$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-fno-fast-math;-ffp-contract=off>`. La correttezza richiede questi flag anche se il progetto esterno abilita fast-math.
3. Aggiungere il test Qt permanente preparato dall'agente test alle liste app/view-test previste dal progetto; nessuna dipendenza Qt o OCCT nel nuovo modulo kernel.
4. Catalogo `tools/generate_architecture.py`: nuova descrizione per il modulo `fk_helix_projection` (kernel): proiezione Helix originale in coordinate sketch, snapshot coefficienti/wrapper/frame, certificato trigonometrico continuo, fit cubico e ricertificazione dei poli effettivi, budget finito/cancellazione. Integrare la descrizione `cad_sketch_refs` con il nuovo percorso limitato alle Helix con Transformed e recertificazione dopo recalculateCurve; dichiarare Helix nuda quintica legacy. Conservare le altre descrizioni.
5. Rigenerare graph e HTML, eseguire --check e verificare i collegamenti bidirezionali: cad_sketch_refs -> nuovo modulo project/require; nuovo modulo -> fitCurveCertified in fk_offset e Helix/getter. Eseguire build completa, test pertinenti e registrare hash eseguibile aggiornato separatamente dalle prove private.

## Limiti

Il cap risorse può rifiutare curve valide. Argomenti troppo grandi per una riduzione rigorosa finita vengono rifiutati. Non garantisce continuità superiore a C0 nel contratto certificato; non modifica il ramo Helix nuda C2. Non certifica le altre curve Other, né il vecchio helixBSpline. Il certificato posizionale non attesta assenza di autointersezioni o altre proprietà topologiche.
