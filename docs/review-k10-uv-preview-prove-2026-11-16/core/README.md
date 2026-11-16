# K10 UV su Plane — freeze core V2

Questi tre snapshot corrispondono esattamente al candidato privato compilato,
affidato al root per l'integrazione. Il successivo chiarimento del commento
nell'header di produzione non modifica token C++: factory invalid/singular
throws; query inconclusiva restituisce +infinity e il fit la rifiuta.

## Contratto e scope

planePCurveErrorBound supporta Plane + edge Line/NURBS a pesi positivi,
Trimmed/Transformed. Conserva i coefficienti originali IEEE posseduti; non
trattiene geometrie del chiamante. Il lift O+X*Q.u+Y*Q.v e la proiezione di E,
con normale originale X cross Y, sono racchiusi outward. Due oracle separati:
projection limita lo scarto dal proiettato in mm per fit a 0.1*tolerance;
residual limita il completo S(Q(t))-E(t) in mm per il budget fin esistente,
inclusa la componente normale tollerante. I nodi sono solo indicazioni.

L'helper privato fitPlanePCurveCertified valida callback/poli finiti ma
accetta esclusivamente il bound in mm: nessun confronto tra distanza UV e
budget fisico. Le API pubbliche precedenti conservano il percorso Euclideo.
Entrambi i siti UV sono trattati: offsetFaces con trimSeams e
offsetJoinedFaces. Nel secondo il Plane offset definitivo precede il fit
e viene poi riutilizzato identico. La stima campionata rate non interviene
sui Plane riconosciuti. I budget fin non aumentano.

Dopo assembly ogni fin effettiva delle coppie raw-face/raw-edge, risolte solo
tramite AssemblyMap, è ricertificata da una nuova factory sui coefficienti
Plane/edge effettivamente emessi. Conteggi e multiset dei budget devono
corrispondere. Nessuna associazione per vicinanza. La guardia impone cubiche
UV Bezier non razionali, quattro nodi estremi, giunti tripli e intervalli
finiti crescenti. Lo scope comprende soltanto fk_curve_fit_bounds.h/.cpp e
fk_offset.cpp. Nessuna modifica ad AssemblyMap o fk_affine_surface_fit:
quest'ultimo riconosce il forward UV affine su estrusione, qui serve il
pullback Plane dell'edge effettivo generale.

## Prove e limiti

Suite privata 10/10: sei negativi joined, due positivi trimSeams, invalidità/
cancellazione e provider. Nel negativo joined checker accettava un residuo
S(PC(t))-E(t) circa 7.500005e-6 mm contro 1e-6 mm; corretto circa 2.05598e-11 mm.
È violazione di corrispondenza parametrica fisica, non volume mancante o
distanza dall'insieme geometrico dei punti dell'edge.

Probe 7/7: schema cubica con 7 poli/nodi lisci rifiutato prima dell'oracle; C0Bezier
corretto; budget NaN; metrica fisica; verifica di 24 fin reali con mappa; rifiuto
polo UV effettivo alterato; rifiuto budget effettivo raddoppiato.
Con X=(1e-12,0,0), E(t)=(t^4,0,0), callbackUV=(1e12*t^4,0,0), l'errore UV
misurato 3725.32 coesiste con bound fisico continuo 9.93415e-9 mm <= 1e-8 mm.
Le misure sono regressioni: ogni campata è accettata dall'oracle continuo.

V1 è preservata in /tmp/k10-uv-review/core/frozen-v1 con hash in freeze.json.
Confrontava anche errore UV campionato con tolerance in mm: la garanzia
continua dell'output accettato restava conservativa, ma unità e possibili
falsi rifiuti richiedevano la correzione V2. V1 non è dichiarata integrata.

Snapshot originali+proiettati limitati a due milioni d'intervalli scalari;
restano limiti di grado/poli/prodotti/refinement e cancellazione. Risorse
finite e bound conservativi possono rifiutare geometrie valide. Altri
supporti mantengono fallback; non si certificano tutte le superfici offset,
autointersezioni, volumi o genealogie. Queste prove private usano due TU e
la libreria Release esistente, non aggiornano l'app grafica. Build finale,
sanitizer e verificatore indipendente appartengono agli archivi root/test.
Nessun binario/CAD o archivio precedente è incluso o modificato qui.
