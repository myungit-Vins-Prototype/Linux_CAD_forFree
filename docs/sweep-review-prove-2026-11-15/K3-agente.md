# K3 — cucitura sweep NURBS chiuso

Consegna staged, nessuna scrittura nel repository. Letti AGENTS/CLAUDE/review/tolerance-policy e grafo (descrizione sweep, chiamanti cad_forge/sweep API; mass, inversione, test).

Applicare sweep.patch (due hunks stretti) e mass.patch; aggiungere regressions.inc a kernel/tests/test_sweep.cpp. Non copiare test_sweep.cpp: contiene snapshot sorgente concorrente di test K1. original.cpp/original_mass.cpp sono snapshot baseline dei due file; original.sha256 conserva il percorso/hash sweep iniziale.

Causa: superficie tensoriale non periodica ma cucitura topologica con edge condiviso, proiezione sceglie stesso v=1 per due fin. SPcurve esplicite scelgono v=lo/hi per trasversali e u=lo/hi per longitudinali. Il parametro u(t) trasversale viene ricavato dalla SPcurve ordinaria e mantenuto, perché una conica convertita NURBS non conserva il parametro angolare affine. Il fit usa budget già nominato pCurveTolerance/edge.tolerance, verifica scarto dopo scelta ramo e conserva budget misurato.

Seconda causa: massa ignora SPcurve con budget >1e-8 e reinverte 3D perdendo il ramo. Patch conserva tali SPcurve come seed dei campioni (periodi srotolati verso previous), poi usa la normale inversione. Soglie massa e inversione invariate. Una suddivisione forzata eviterebbe la seam ma cambierebbe topologia e numero facce; scelta non necessaria.

Probe post: un tratto due facce V=.822445219580420 A=16.4489043843124; due tratti quattro facce V=.822445219580414 stessa area. Atteso V=.822445218850813. Prima: un tratto V=A=0. Entrambi post checkBody=0, failedFaces=0; UV loop chiusi e seam uses v=0/v=1 distinte.

Runner staged collegato a libforgekernel.a con fk_sweep/fk_mass e test ricompilati in tmp. Nuova regressione:1/1 PASS; filtro Mass11/11 PASS, PCurve9/9 PASS. Complessivo36/37: unico fallimento è nuovo test K1 SweepMinimalTwistHelixAvoidsUniformGridAliasing, acquisito da test_sweep.cpp durante lavori concorrenti mentre sweep staged resta snapshot pre-K1. Test K3 forza anche fallback mass impostando budget fin>=2e-8 su copia body; prova cerchio e quadrato chiusi in uno/due tratti, volume/area attesi cerchio, seam v distinti, estremi UV e scarto, più spline aperta. Vecchio test_sweep solo +K3 prima acquisizione K1:9/9 PASS.

Log: post.log, tests.log, seam-tests.log, mass-tests.log, pcurve-tests.log. Non eseguita build app/full; parent integra, documenta memoria e verifica mappa.
