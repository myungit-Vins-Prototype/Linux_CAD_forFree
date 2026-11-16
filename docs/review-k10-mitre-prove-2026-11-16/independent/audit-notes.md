# Audit indipendente K10 traceBlendMitre

Letture prima del codice: AGENTS.md, CLAUDE.md, docs/tolerance-policy.md,
docs/review-k10-sheet-join-2026-11-16.md; graph.json focus mitre/surface:
2 nodi e 77 relazioni incidenti salvati in graph-before.json.

## Baseline verificata indipendentemente

* fk_blend_mitre.cpp: selezione affine quasi coincidente: 65 campioni posizione e
  normale; fitCurve della media; 129 campioni di verifica. Nessun limite globale.
* La mappa UV originale del ramo, (t0+f*(X0-t0),f*X1) / analoga su T,
  non sopravvive in BlendCut. Il caller aggiunge gamma come edge libero e relink
  assegna pcurve null: computePCurves riproietta il bordo.
* fk_blend_surface.cpp ~3899: ciclo successivo all'assemblaggio campiona 17
  proiezioni e può alzare edge.tolerance a 1.01*gap. Una nuova gamma certificata
  deve mantenere il cap originale ed essere esclusa da questa assegnazione.
* Il percorso diretto blendSurfaceChains->blendEdgesWith->blendEdges non chiama
  unifySameDomain dopo la costruzione. Il helper di cucitura in fk_blend.cpp~625
  è un altro ramo. Preparazione unifyTangentNurbsSupports avviene PRIMA del blend.
  La delega può ancora compiere ulteriori blend sul corpo già costruito: ciò non
  costituisce un certificato delle fin certificate precedentemente modificate.
* Newton implicito resta campionato: 3 campioni interni per cubica; per intervalli
  minimi fuori budget misura proiezioni e conserva approximationGap. Alla partenza
  singolare usa solo 0.5/0.9 e ignora i campioni Newton falliti. Nessun certificato
  generale esistenza/unicità/ramo può essere dedotto da questa procedura.
* OperationCancelled è distinto da std::exception e std::domain_error; non è
  dimostrato assorbimento di cancellazione dai catch. Nel file mitre baseline
  manca cancellationPoint, ma prova di reattività richiede test specifico.

## Campo pratico del provider esistente

surfaceCompositionErrorBound riconosce B-spline con UV affine isoparametrica;
coordinate entrambe variabili su B-spline restituiscono bound vuoto. Il provider
fk_affine_surface_fit preesiste e supporta affine generale SOLO Extrusion.
Il campo non è stato introdotto dall'attuale intervento (hash baseline salvati).
Per mitre entrambe B-spline, una correzione che lo riusa senza ampliarlo certifica
solo u costante con v variabile (o l'asse alternativo). Non autorizza claim sulle
mitre diagonali, né sul Newton. Occorre conservare i parametri/poli originali
in UV BSpline grado1 e verificare le fin/carrier effettivamente emesse.

## Prova indipendente eseguita

Il riproduttore di tests esporta coefficienti autentici dell'output detail,
non un Body pubblico. verify_baseline.py ricostruisce separatamente con Fraction
le basi Cox/de Boor omogenee dei supporti ORIGINALI e la UV originale del ramo.
Nessun coefficiente del provider C++ viene riusato. S upper 1.5431413444937805e-11;
T rifiutato da un testimone esatto. A t=19/4096 lo scarto Z esatto è
37228125/268435456 mm = 0.13868557289242744 mm; cap 1e-7 e gap pubblicato
6.0243662598582149e-12 mm. Questo è un testimone alla mappa originale; la
proiezione fisica sul supporto è una misura distinta riferita dal tester.
Selftest tensor indipendenti PASS (bilineare, nodi multipli, razionale positivo,
polo output alterato rifiutato). La griglia del tester individua un testimone,
non costituisce il certificato continuo.

## Revisione statica candidato core V1 (privato /tmp)

Non è una verifica dei byte integrati. Il candidato conserva UV originali e cap
nel BlendCut; requireBlendMitreCertificate ricostruisce il provider dai dati
originali ed esamina i poli cubici effettivi, richiedendo copertura integrale
senza estrazione arrotondata. Il caller disfa l'orientamento V sulle UV originali,
ricertifica sui due veri carrier, assegna budget prefissato a edge/vertici,
installa le UV nei BuildFin per incidenza esatta prima di build e computePCurves.
La validazione successiva conta esattamente una fin per piano/carrier, conserva
puntatori/range/budget e ricostruisce ancora il bound sui dati emessi. Le curve
certificate vengono escluse dal ciclo legacy di ampliamento tolleranze.
Ultimo controllo prima pubblicazione storia dopo checkBody e computePCurves.
Questa struttura appare coerente con il contratto continuo limitato alle ISO.

Segnalato a core: gX>0 deve essere validato PRIMA del nuovo ramo (altrimenti
Xv=0 su entrambi e u variabile evita la guardia); controlli finite e cancellazione
anche dentro selezione/Newton/raffinamento. Core riferisce di aver recepito:
nessun esito di test finché non eseguito indipendentemente o esportato.

## Verifica candidato eseguita dall'audit

L'audit ha compilato un proprio exportatore C++ contro fk_blend_mitre.cpp.o
candidato e la libreria baseline. Ha esportato con hexfloat i dati originali
dei carrier, i poli/knots/weights dell'edge restituito e le due pcurve effettive
nel cut. Non sono fin di Body pubblico. Fraction: 8/8 mappe, 4 varianti
(polinomiale, razionale positivo, posa rigida, scala2.5+posa), 734 campate;
upper massimo 9.422179106173454e-10 mm contro cap1e-7.
Separatamente convertiti senza perdita i 17-digit export del tester:
10/10 mappe, 5 varianti, 736 campate; upper massimo 5.000003783880121e-8 mm
nel caso con T separato di mezzo cap. Sono in larga parte gli stessi casi:
NON sommare i due gruppi come 18 casi o nuovi difetti.
Tre controlli negativi indipendenti (polo edge, polo supporto, ancora UV
alterati) sono tutti rifiutati dal verificatore Fraction con testimone esatto.
Nessun exportatore chiama surfaceCompositionErrorBound per ottenere i coefficienti.

Verifica hash indipendente: gli otto file repo registrati all'inizio sono
invariati al termine di questa fase audit. Nessuna modifica repo/docs.

Limiti: questi PASS non certificano fin di Body prodotto dall'API pubblica,
né orientamento commonB sul percorso pubblico, ulteriore delega blend,
unificazione arbitraria, correttezza topologica globale, G1/G2, membership
nei rifili o assenza autointersezioni. Richiedono conferma sui byte finali
integrati. Il ramo diagonale e Newton restano fuori certificazione.

## V2 finale congelata

V2 del core: mitre.cpp a36cf93ed20b..., header9b7c65b9d030...,
surface.cpp0fd41f225758..., libdb372de78785... (hash interi nel confronto).
Il ripiego campionato della media è ripristinato SOLO dopo il blocco ISO per
famiglie unsupported. Un fallimento nel blocco ISO non è catturato e non può
recuperare quel ripiego. Questo evita la regressione V1 dimostrata dal tester
su S=T diagonale (baseline successo, V1 Newton singolare). IL TESTIMONE DIAGONALE
BASELINE RESTA PERTANTO UN LIMITE APERTO IN V2: non è parte corretta di K10.
La prova negativa di correzione è invece ISO hidden-knot, fallita in baseline
e PASS per rifiuto conservativo in V2. Non contare le due varianti come nuovi
rilievi.

I tre helper originalBlendMitreUV/installBlendMitreCertificates/
requireEmittedBlendMitres sono ora nel modulo mitre e usati dallo stesso caller.
originalBlendMitreUV riflette V con la somma del dominio originale; il caller
ricertifica ancora sui due carrier non orientati. Revisione statica congelata
senza bypass identificati per il campo dichiarato. Nessun unify postbuild.

L'audit ha rilinkato il proprio exportatore sulla libV2 congelata: export8mappe
identico BYTE PER BYTE a V1 e nuovo Fraction PASS (734campate). I5export tester
V2 (10mappe/736campate) sono pure identici BYTE PER BYTE ai V1.

Nuova verifica richiesta, senza ampliare famiglie o verificatore:
2/2 fin REALI (ID0 e4) di Body sintetico costruito da install produzione +
Body::buildSheet, sensi opposti. Fraction PASS,2campate, upper massimo
6.725652854973881e-11 mm contro cap1e-7. Questo è un vero output Body sintetico,
NON il risultato di blendEdges pubblico. Reflection originalUV è provata dal
tester separatamente, non da un Body con carrier riflesso. Non dichiarare tale
campo implicitamente coperto dalle due fin in sensi opposti.

L'audit ha eseguito personalmente il runnerV2 del tester:13/13PASS exit0,
inclusi rifiuti alterazioni coefficienti/provenienza/budget, compatibilità
diagonale senza certificato, installazione Body e sensi. Log salvato; input
binario/hash esterno registrato senza includerlo nel pacchetto curato.
Il diagnostico pubblico V1 riferisce16/16PASS e121traceNewton,ZEROISO: non è
una verifica del ramo certificato nel pubblico. PubbliciV2 restano al tester/root.
Tester riferisce infine pubbliciV2 16/16PASS; l'audit non li ha rieseguiti.
Il campo pubblico del ramo ISO resta non dimostrato, poiché la diagnosi V1
mostrava esclusivamente Newton sulle stesse fixture.
