# Wrapper E/Q: candidata staging congelata

Delta solo fk_surface_curve_bounds.cpp/.h (una TU), API invariata; nessuna modifica repo/curve/ctor/transformBody, commit/push o UI. AGENTS/CLAUDE/policy e descrizioni+archi call/include/reference nei due versi consultati; evidenze e SHA allegati. Root cura integrazione/catalogo/mappa/memoria, tests e audit le loro verifiche indipendenti.

Produzione source7dfb4c8f4b2d06ac1be0ef469d053232aa879fe2b762cf176567fabfd4059fba, headerdd92c62ce5707241743f70a34602af9b0b0e6eca048c765a23fc7a8e634a1aca. SOURCE-FREEZE.json SHA8378f203f549278c024fafc2a9a8b154f1672d8bcbdba0df14dc0efe0c1504a2 resta invariato. Unica libreria readonly root/libforgekernel-baseline.a SHA6f0a76dfdde164289e0b646843120c1efdcaabcede8eb38446f5aa3459987356, non copiata.

## Contratto e implementazione

E ammette catene combinate TrimmedCurve3/TransformedCurve su base cubica NURBS positiva già supportata. Q ammette TrimmedCurve2 su Line2/B-spline1/3 positiva già supportata. S, gradi e famiglie base restano invariati. CurveSource<N> cammina al massimo32 link TOTALI per ciascuna E/Q (trim e trasformazioni sommati), con cancellazione a ogni nodo. Decodifica storedTrim.domain (locale), basis e12scalari finiti di ogni transform PRIMA di qualunque domain/isPeriodic/breakpoints ricorsivo del wrapper. Non usa similarityguard/inversa per E: matrici affini finite incluse shear e singolari sono legittime.

La query deve essere EXACT contenuta in OGNI trim visitato: nessun epsilon1e-12 ereditato dai costruttori. Solo dopo il walk si riconosce la famiglia base e si verifica il dominio naturale supportato. Così un Circle periodico validamente trimmato oltre2π resta Unsupported, senza falsoFailed dal suo dominio fondamentale. Invalidità degli stessi wrapper/range/matrice o oltre32link fallisce prima, anche se la base non sarebbe supportata. Il callback di cancellazione propaga distinto.

E trasformata: snapshot dei poli NATIVI originali in I, applicazione di T_inner→T_outer separatamente (mai composizione double, mai poli world arrotondati), centratura dell'ultima traslazione PRIMA delle somme M*P+(t-anchor), poi moltiplicazione per i pesi originali se variabili. L'anchor double arrotondato è soltanto un centro COMUNE finito a E/S, non prova o geometria sostitutiva. Gli intermedi non conclusivi/nonfiniti danno Failed conservativo. Nella famiglia nativa/soloTrim con zero matrici, snapshot e aritmetica geometriche restano le precedenti.

Candidate Q controlla gli ORIGINALI trim e native-domain PRIMA di unwrap/clamp/conversioneLine. Se un wrapper originale è invalido, restituisce il medesimo puntatore: l'unica prova provider lo decodifica e restituisce Failed, impedendo bypass da surrogato. Qunchanged mantiene il wrapper originale. QLine diventa Bernstein1 sullo stesso queryrange senza normalizzare velocità; un Q B-spline modificato conserva nodi/grado/pesi e, se proveniva da wrapper, riceve un solo Trimmed2 sul medesimo queryrange già validato. Input e puntatori condivisi non vengono mutati. Unsupported conserva il percorso legacy dichiarato; Failed mai recuperato con campioni.

Il walk ha storage fisso32pointer/curva, nessuna copia/allocazione prima delle guardie di setup esistenti; maxScalars2M,maxVisits16384,maxDepth24,maxCells256 e budget fisico esterno restano invariati. Le nuove matrici originali sono riferite, non copiate in array geometrici dinamici; il cap33 è terminale. Non è prova globale Newton/trim/topologia né dell'intento del carrierfit.

## Prove svolte da questo agente

Compilazione overlay Release con Wall/Wextra/Wpedantic exit0/no output.10check diagnostici PASS: E32/Q32Certified e E33/Q33Failed; query fuori trim interno di2^-54 rifiutata con candidato puntatore originale, mentre stessa geometria senza trim e con domini E/S sufficienti è Certified; perdita da flattenRounded negativa (δ2^-36,cap2^-40,traslazione2^18) rifiutata, pairedpoli world arrotondati invece Certified; shear affine singolare+trim Certified. Tutto sintetico e in memoria; source/log inerti allegati.

Compatibilità su input NATIVI:48fin pubbliche cilindro/NURBS e6probe Erazionali+7probe Qrazionali, baseline6f0a vs overlay7dfb. Tutti JSON IEEE/stdout byteidentici (4cmp exit0); SHA in compatibility-runtime.json. basis/homogeneousBasis/residual/rationalUVResidual/snapshot/onRange sono byteidentici alla baseline (confronto statico allegato). Queste prove non certificano tutte le famiglie possibili e non aggiornano l'app. Test permanenti/sanitizer e audit sono gestiti dagli altri due agenti, non attribuiti a questo pacchetto.
