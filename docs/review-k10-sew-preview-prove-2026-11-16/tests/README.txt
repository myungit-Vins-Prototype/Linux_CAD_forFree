K10 cucitura Plane — archivio tests curato,16novembre2026.

fontefinale.cpp.txt è il candidato standalone usato dai runner baseline e
prototipo. Il target permanente root sarà forgekernel_sew_pcurve_tests.
Macro HAVE_PLANE_SEW_PROVIDER abilita tre gruppi dei nuovi contratti.

Baseline:9 gruppi,6 fallimenti geometrici attesi e3PASS. Prototipo finale:
12/12PASS. Le regressioni verificano sorgenti coerenti, sourcebinary
invariati, esito realmente cucito con2 facce/7 edge/6 liberi/1 shell,
parametro e curva target originali conservati, provenienza esplicita e
residuo actualfin entro il MEDESIMO cap legacy. Coperti fit nonaffine,
riuso diretto campionato, rimappatura affine anticipata, ordine operandi,
range traslato/Trimmed, parametro inverso, scala e pesi nonuniformi.

Il negativo minimo usa q=t²+cB3,128, con c4.940214000122462e-7.
La lobo ha picco1.12e-7 a3/128, mentre il massimo campionato64 è9.7273e-8.
Il fit legacy restituisce4poli cubici. Input/output passano checkBody;
edgeTol e finTol sono0. trace-baseline.log attesta il ramo nonaffine.
Scala1000 e razionale erano già compatibili e non sono nuovi negativi.

Nuovi contratti: residuo normale fisico preservato e rifiuto atomico
se eccede il cap, controlli effettivi finali corrotti, budget NaN,
cancellazione e metrica in millimetri su Plane con X1e-12 e Y(.25,1,0),
Trimmed(Transformed). Quest'ultimo ha errore UV3714 contro fisico3.714e-9:
non si confrontano coordinate UV con una tolleranza fisica.

Sanitizer12/12PASS con ASan/UBSan/LSan, leak check attivo fuori sandbox e
nessuna diagnostica/soppressione. Quattro TU strumentati: exchange,offset,
sew e main. Tutto il resto del kernel è Release, provider bounds invariato
compreso. HeaderRawFin privato cambia layout: l'overlay include sew.cpp
ricompilato e non è stato usato per prove generali IGES/STEP. La build
completa e il replay Release di produzione sono responsabilità root.

Dump:8JSON baseline+8JSON finali,64 actualfin perversione,8 migrated.
Budget legacy, fin_tolerance e edge_tolerance sono campi distinti. Il
marker migrated viene dalle origini delle facce dichiarate e dall'edge
condiviso, non dal riconoscimento della forma della pcurve. Floathex
preserva gli input IEEE. I16JSON coincidono con i privati; gli8 finali
coincidono anche con la prima esportazione del prototipo.

Le misure del main sono regressioni indipendenti, non certificati globali.
Root esegue la propria verificaFraction su coefficienti originali/finali.
Il probe Helix è conservato separatamente e marcato SOLO callbackfitCurve:
non è ancora un riproduttore applicativo appendProjectedCurve Qt.

Nessun software oltre lo scope concordato modificato da questo agente.
Sorgenti/script sono tutti archiviati con suffisso.txt. Non sono inclusi
CAD, librerie, oggetti o eseguibili. metadata.txt e gli hash mantengono
attribuzione, scope e limiti; i percorsi/tmp negli script sono storici.
