K10 joinSplineExtensions — regressioni indipendenti

Test permanente finale: kernel/tests/test_sheet_join_validation.cpp
SHA256 6f107960db76d10bb652cba60e591758b2789533056b5a5c38340754d859f5d6
Freeze core V3: 8e9bd9d6c1d61bf9e8a189e783d6803510c3c61f07ed28c8e5b82f2efa457e2f
Libreria V3: a739561386cea5c0509a8f4113046824a3e9670b1b9c8d61024f7ec6cc69fea3

Esiti:
- V3 Release: 29/29 gruppi PASS. 19 corpi positivi, 8 gruppi negativi / 9 corpi, cancellazione e quattro lunghezze invalide.
- 19/19 JSON V3 originali/output/genealogia byte-identici V2: v2-v3-coefficients-comparison.json.
- Baseline completa: 11/29. Dieci gruppi positivi falliscono solo le nuove asserzioni provider affine e fin cap esplicito; non sono dieci difetti geometrici precedenti.
- Baseline compatibilità privata: 21/29. Rimuove soltanto due asserzioni nuove; conserva tutte le prove fisiche, cap edge, range, codec, storia, immutabilità e atomicità. Otto gruppi negativi falliscono perché la baseline accetta i nove corpi.
- ASan/UBSan/LSan V3: 29/29, exit 0, nessuna diagnostica/soppressione. Strumentati driver, fk_test_main, fk_sheet-v3 e fk_curve_fit_bounds; resto della libreria Release. run-v3-untraced.log.
- Prima esecuzione V2 nel sandbox: test29/29, LSan fatal ptrace. Esclusa dal PASS leak; poi V2 e V3 stessi binari/opzioni fuori ptrace PASS.

Regressioni geometriche distinte:
1. Alias nodi stretti: superfici NURBS adiacenti u2/v3, dominio supporto v[0,2] e facce rifilate v[0,1]. A ha z=0, B ha un bump oltre il trim largo1e-4/alto.001 intorno v1.12345; il seam originale è esatto. Estensione .25: baseline emette seam quasi rettilineo z0 ed è accettata da checkBody pur con residuo .001 sul supporto B. Le fasi1.111/1.137, posa, scale .01/1000 e selezione inversa sono varianti della stessa famiglia. Fraction audit indipendente ha confermato un testimone oltre budget per fin11/edge2/face1 baseline.
2. Fallback tangente: B z=1e-4*(v-1)^2 oltre v1, zero sul trim originale. Baseline accetta gonfiando edge tol a1.1897679952409259e-5; cap iniziale1e-7. V3 rifiuta senza pubblicare output/storia. Posa trasformata inclusa.

Positivi: continuazione implicita non tangente, affine tangente, razionali, pose e scale .01/1000, selezione inversa, senso facce, direzione canonica inversa, estremità iniziale, supporti con trim interno. Nuova via tangente verificata dopo unify su pcurve realmente emessa grado1 e cap esatto già fissato; scarto fisico sul supporto, conservazione UV originali, sorgente byte-identica, genealogia non vuota, codec/range/tolleranze controllati. Nessuna macro per disattivare garanzie nel sorgente permanente.

Export: impostare FK_SHEET_JOIN_EXPORT_DIR a una directory esistente. JSON contiene actual e source; ogni fin ha curve/pcurve/superficie in coefficienti double esadecimali esatti, range, edgeTolerance, finTolerance, resolution e faceOrigins. Budget export generico = edgeTolerance+finTolerance+resolution; l'audit separato usa anche il cap proprio più stretto delle nuove fin.

Limiti: nessuna build applicativa/UI da questo agente. Conservazione UV nel test è campionata; verifica Fraction continua è dell'agente audit. Continuazione implicita non affine resta campionata e non riceve certificazione generale da questi test. ASan mirato non è una certificazione dell'intero kernel. Nessun CAD originale usato o modificato. Nessuna modifica al repository da questo agente; CLAUDE/docs/CMake/grafico gestiti da root.
