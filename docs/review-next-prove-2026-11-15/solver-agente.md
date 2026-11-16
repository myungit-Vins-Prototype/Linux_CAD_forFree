# S1/S2 — solver e codec numeri non finiti

Solo staging /tmp, nessuna scrittura nel repository. AGENTS/CLAUDE, review solver e grafo consultati prima: cad_constraints responsabilita/chiamanti GUI/sketch/history/document IO; collegamento codec -> migrateLegacyConstraints, dipendenze solver -> ConstraintLinear e curve solver.

Applicare constraints.patch, header.patch, codec.patch; regressions.inc contiene solverFiniteNumberTests da includere/invocare nel runner appropriato. Snapshot original_constraints.cpp/.h, original_io.cpp e original.sha256 preservano baseline preesistente (incluse tangenze transitive).

- wrapAngle: fmod in tempo bounded, senza ciclo. Stessa convenzione estremi [-pi,pi], conservando segno ±pi.
- Quote angolari e direzioni/step angolari pattern: modulo360 PRIMA della conversione radianti; nessun clamp/rifiuto dei grandi FINITI, anche DBL_MAX evita overflow prodotto.
- finiteSketchNumbers condivisa da API solver e codec reader/writer: definizione segmenti/curve/maniglie/pesi/nodi, frame, array legacy, quote/posizioni Fix/placement, dati numerici pattern e sezioni Pierce. Samples di sola visualizzazione esclusi. Formato invariato.
- Solver: rifiuto anticipato numeri nonfiniti, bersagli inclusi; controlli scala/incognite/residui, blocchi fissi senza incognite, Jacobiano denso/sparso, matrice densa e passi/trial. Ogni nonfinito osservato ritorna !ok, error non vuoto, residual=inf. Nessuna variazione soglie geometriche o convergenza.
- Commit geometria finale su copia con validazione prima dell'assegnazione: anche overflow nelle maniglie derivate fallisce atomicamente.
- constraintError ritorna inf con definizione/quota/residuo nonfinito; offset derivato validato.
- Codec: tre righe operative (readSketch prima migrate, encodeVersion preflight) riusano guardia; writer conserva bytes sul rifiuto, reader conserva DocumentState.

Verifica: compilazione staged cad_constraints.cpp/cad_document_io.cpp con flag reali compile_commands target forgecad_view_tests, link runner custom con gli altri object reali. Nessuna build app/globale né processo/CAD utente toccato.

regressions.inc: dense/sparse, quote Distance/Angle NaN/+inf/-inf, coordinate e target invalidi, Fix NaN, nodi NURBS NaN, pattern inf, overflow di coordinate finite, residuo NaN ottenuto da dot inf-inf con coordinate finite e blocchi sia liberi sia fissati; quote ±1e20, ±DBL_MAX, ±180, ±540, 3600000000090 e roundtrip validi. Reader provato con definizione valida decompressa, solo double quota sostituito con NaN/±inf, ricompressa con identico metodo/header valido. Writer e reader rifiuto atomico. Writer coordinateNaN rifiutato. Nuova matrice PASS.

Suite esistenti solverPartitionTests + tangentContactTests PASS: confronto denso/sparso, analisi, 96 casi tangenze, conflitti, codec, Undo/Redo. Log tests.log. Baseline stesso probe collegato object repo: exit1 al primo NaN (erroneamente soddisfatto), baseline.log. Il probe post termina normalmente, compresi grandi angoli; nessun timeout mascherato da successo.

Limiti: nessun esame globale di tutti gli altri double/document feature; focus definizione schizzo. analyzeSketch non cambia contratto diagnostico. Parent integra, aggiorna memoria/mappa e svolge build/full check.

## Revisione diagnostica e integrazione test

constraintError ora usa solo finiteSketchGeometry + finiteConstraintNumbers del constraint richiesto. Helper locali comuni con finiteSketchNumbers, che conserva la validazione completa per API/codec. Test aggiuntivi confermano Fix/pattern/Pierce numericamente invalidi passati come constraint esterni; un constraint valido rimane diagnosticabile anche se altro constraint nonfinito è presente nello schizzo. Matrice/partition/tangenze nuovamente PASS.

Per parent che ha già integrato la prima patch usare diagnostic-optimization.patch (delta); constraints.patch è stata aggiornata al risultato completo. tests.patch integra la matrice in tests/test_solver_partition.h, include a livello file e invocazione all'inizio solverPartitionTests. Verificata compilazione/running attraverso questa integrazione, senza includere librerie dentro una classe.

Ricerca mirata sentinel: nessun NaN intenzionale nei campi SketchObject validati. qQNaN passato da viewport a moveSketchPoint come fixed/nowhere viene usato solo nei confronti, non memorizzato. Sentinel NaN tavole e parser superfici non appartengono ai campi validati.

Costo: constraintError costruiva già System(sketch), quindi passare su tutti i constraint era già O(C*N). La guardia completa aggiungeva O(C*(N+C)), ora eliminato il termine scansione constraint estranei. Misure dedicate cost-before/after/repeat e cost-optimized-repeat mostrano il costo residuo della scansione geometria; non viene dichiarato trascurabile su migliaia di constraint. Nessuna cache aggiunta.
