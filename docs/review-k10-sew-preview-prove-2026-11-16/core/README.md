# Cucitura Plane — contratto e freeze core

I quattro sorgenti congelati in questo archivio sono il candidato privato
trasmesso al root. Hash, probe e limiti sono in freeze.json. Il sottocompito
core non modifica produzione, CLAUDE, catalogo o altri archivi.

## Responsabilità e garanzia

Il sito storico remapPCurve si chiama ora sewnPCurve. Per Plane con target
Line/NURBS a pesi positivi, anche Trimmed/Transformed, il nuovo percorso usa
l'edge target e gli assi del Plane originali. Non dipende dal solve implicito
del parametro sulla curva sorgente. Riconosce la famiglia prima dei vecchi
riusi campionati dell'identica pcurve o della sua rimappatura affine.

Le due API detail sono semantiche: fitPCurveOnPlane costruisce cubiche UV C0;
requirePCurveOnPlane ricrea l'oracle dai dati effettivi e verifica lo schema
Bezier e tutti i poli effettivi. Non è stata esposta un'API pubblica generica
di fit con metrica arbitraria. Il fitter mirato usa solo il bound fisico.

La callback inverte il Gram del Plane in double per generare candidati.
La sua approssimazione non certifica alcunché: la factory preesistente
planePCurveErrorBound solleva i poli UV con O/X/Y originali outward e
confronta con i coefficienti omogenei originali del target. projection guida
il fit a 1e-8 mm; residual misura S(PC(t))-E(t), includendo lo scarto normale.
Callback/poli non finiti, inversioni singolari, dati riconosciuti invalidi e
query inconclusive causano un fallimento, senza fallback campionato nella
famiglia riconosciuta. Solo target/supporti non supportati mantengono il
percorso precedente.

Il cap è esattamente quello legacy: originalSample+max(1e-7,10*tolerance).
Non viene sostituito da un upper più largo. La nuova pcurveTolerance pubblica
è l'upper continuo del residuo effettivo, verificato <=cap prima di produrre
il risultato. L'eventuale puntatore deviation resta invariato su fallimento.
La PC sorgente viene usata solo per calcolare originalSample nello stesso
modo precedente: non se ne certificano globalmente validità o precisione.
La prova dell'output non assume la corrispondenza sorgente/supporto.

## Provenienza dopo assemblaggio

RawFin trasporta solo metadati non persistenti: obbligo di certificazione e
cap fissato. AssemblyMap identifica faccia ed edge emessi. L'identità dello
shared object PC identifica le fin migrate; il riferimento Raw ne conserva
la durata ed evita riuso dell'indirizzo. Non si usa vicinanza geometrica.
Il controllo legge nuovamente ogni PC effettiva e ricrea l'oracle sul Plane
ed edge effettivi, verifica range, ID contenuti nel Body, numero >=1 e
multiset esatto dei budget per identità. Un oggetto PC sostituito, anche con
copia identica, fallisce per provenienza persa. Le fin rappresentanti non
migrate che condividono la coppia faccia/edge con una PC diversa non
acquisiscono il certificato del vicino.

Il probe conservato passa 5/5: Body::buildSheet costruisce una faccia Plane
con contorno esterno e foro collegati da un bridge usato due volte; nella
stessa coppia faccia/edge una fin ha PC cubica certificata e l'altra PC Line
legacy. Checker valido e controllo misto positivo. Sono rifiutati: clone
PC con identità diversa, budget effettivo raddoppiato, polo effettivo mutato
nello stesso oggetto, range dell'edge effettivo cambiato.
Il probe esercita il controllo sul Body realmente costruito, non afferma
che assembleBody supporti già tutte le cuciture non periodiche con bridge.
La prima fixture privata aveva erroneamente range 1 per Line che normalizza
la direzione; corretta con la lunghezza effettiva. Era un errore della prova,
non un fallimento della produzione corretto modificando tolleranze.

## Difetto e limiti

Il negativo pubblico dell'agente test usa due lamine adiacenti: target
q(t)=t^2+c*B(3,128)(t), c=4.940214000122462e-7, e sorgente Line. PC originali
esatte; remapCost mantiene il target. La parte t^2 impedisce i vecchi fast
path, mentre il piccolo dettaglio Bernstein sfugge al fit e ai controlli
campionati. Il residuo a t=3/128 supera 1.12e-7 mm con cap 1e-7 mm, sebbene
checker/input/output risultino validi e la sorgente rimanga invariata.
È una violazione del medesimo parametro della fin, non geometria mancante
nell'insieme dei punti del bordo. Il ramo fitter precedente è tracciato
separatamente nell'archivio test. La suite privata finale passa 12/12 gruppi;
le baseline e le attribuzioni finali sono conservate dall'agente test/root.

Nessun nuovo provider matematico: bounds.cpp resta invariato. La famiglia
non certifica source PC, il matching geometrico generale della cucitura,
tutti i supporti, assenza d'autointersezioni o topologia/volumi globali.
Risorse finite e limiti conservativi possono rifiutare geometrie valide.
appendProjectedCurve Other resta aperto: le NURBS wrapper erano già trattate,
Helix nuda usa un ramo separato e non esiste un certificato continuo generico
per Other opache. Non è dichiarata una migrazione nominale di quel sito.
