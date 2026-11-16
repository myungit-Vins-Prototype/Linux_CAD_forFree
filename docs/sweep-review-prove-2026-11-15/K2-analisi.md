# K2 — analisi circoscritta, 15 novembre 2026


**Prosecuzione:** [prove dirette e prototipo del 15 novembre](../sweep-k2-prototipo-2026-11-15.md). K2 resta aperto; sono ora disponibili un esperimento riproducibile e due controesempi validi che impediscono l’integrazione del filtro su inviluppo.

**Esito storico di questa analisi: aperto.** Non esiste oggi un'API pubblica capace di certificare, per
ogni sweep supportato, l'assenza di autointersezioni globali. Nessuna modifica
al repository; nessun filtro parziale viene proposto come soluzione completa.

## Riproduzione corrente

Compilato `overlap.cpp` copiato dal riproduttore review, collegato con
`frame/fk_sweep.o` K1 e `forgecad-release/kernel/libforgekernel.a` corrente:

```
checks=0 volume=19.7417086214508 upper_bound=12.6 faces=4
```

Output conservato in `overlap.log`. Il volume supera il box contenitore
geometrico indicato dalla review. K1 non corregge K2. Il sampler locale di
fk_sweep.cpp continua a verificare solo il verso di avanzamento delle righe;
questo non controlla coppie di parametri lontane lungo il percorso.

## API e implementazioni esaminate

Consultati grafo architetturale (descrizioni sweep, checker, marching e
B-spline), fk_sweep.cpp, fk_body_check.{h,cpp}, fk_intersect.{h,cpp},
fk_marching.{h,cpp}, fk_bspline_surface.{h,cpp}, fk_surface_algo.h.

- `checkBody` verifica connessioni topologiche/geometriche; `loopCrossings`
  controlla loop nello stesso dominio UV, non attraversamenti della faccia
  nello spazio né intersezioni tra facce lontane.
- **Precisazione:** il commento in fk_marching.h che esclude B-spline è
  obsoleto. fk_marching.cpp:2314ss supporta coppie generiche attraverso
  `subdivisionSeeds` e `Marcher`; quindi non è corretto dire che nessun
  percorso di intersezione B-spline sia implementato.
- Tuttavia `subdivisionSeeds` (1728ss) termina su foglie grandi
  `leafSize = 0.02 * scale` oppure profondità >40, poi tenta Newton da un
  centro. Il commento ammette componenti più piccole perse. Un Newton
  fallito non produce un certificato negativo. `splineCoincidence` usa
  campioni. Un risultato vuoto di `intersectSurfaces` **non certifica**
  disgiunzione globale e non può diventare il criterio di accettazione.
- `disjointSplineCarriers` (2090ss) è invece un filtro conservativo negativo:
  gerarchie di box dei poli, separazione degli inviluppi e suddivisione.
  Un budget esaurito o una coppia incerta restituiscono false. È privato al
  modulo marching. Può servire come componente di broad phase, non come
  controllo self già pronto.
- `cachedBezierPatches` / `cachedPatchTree` sono riusabili; i pesi NURBS sono
  validati positivi, dunque l'inviluppo convesso dei poli è un bound valido.
  Ritaglio/suddivisione devono conservare omogenee, domini e provenienza.
- `projectPoint(surface,p)` ha branch-and-bound B-spline per un punto,
  ma né un insieme finito di punti né il minimo punto/superficie certificano
  tutte le coppie superficie/superficie o la propria diagonale parametrica.
- Anche iterare sulle sole coppie di facce distinte è insufficiente: una
  stessa faccia B-spline può attraversare se stessa. Suddividere soltanto il
  path in un numero fisso di parti sposta il problema senza risolverlo.

## Strategia generale concreta

Serve un sottosistema di validazione geometrica con esito ternario:
`CertifiedDisjoint`, `IntersectionWitness`, `Unresolved`, associato a domini
parametrici e identificativi topologici. Nessun `Unresolved` diventa successo;
se usato per accettare uno sweep, l'esaurimento deve produrre un errore
esplicito di verifica non conclusa, distinto da intersezione provata.

1. **Dominio e topologia.** Lavorare sulla geometria B-rep finale in double,
   comprese facce di testa e fori. Estrarre pezze razionali esatte con domini
   UV, loop di trim e provenienza face/edge/fin. Costruire coppie anche nella
   medesima faccia. Il dominio delle coppie è quadridimensionale.
2. **Scarto conservativo.** BVH sui box dei poli, inflazionati per errore di
   arrotondamento e budget geometrico dichiarato. Inviluppi separati provano
   disgiunzione; box sovrapposti significano soltanto candidato. Riutilizzare
   concetti di `disjointSplineCarriers`, rendendo chiari i contratti pubblici.
3. **Diagonale e adiacenze.** Non scartare tutte le coppie adiacenti né una
   fascia parametrica scelta arbitrariamente. Certificare localmente la
   regolarità/iniettività delle pezze attraverso bound Bernstein/intervalli
   sulle derivate e un criterio di univalenza di una proiezione. Per i bordi
   condivisi, provare che la sola intersezione ammessa è la curva genealogica
   comune e che i due interni stanno dai lati coerenti; trattare seam e
   vertici con le identità topologiche esplicite. Subdivisione quando il
   certificato non vale. Questa è la parte assente più sostanziale.
4. **Coppie lontane.** Suddivisione adattiva delle coppie candidate e bound
   intervallari razionali sul residuo `A(u,v)-B(s,t)`, con arrotondamento
   verso l'esterno. Zero escluso da una componente è certificato negativo.
   Per una coppia trasversa Newton può trovare un candidato, ma il testimone
   va validato con interval Newton/Krawczyk su una sezione trasversa 3×3
   (quattro parametri e tre equazioni), verificando trim e separazione dai
   bordi ammessi. Tangenze/coincidenze richiedono classificazione separata;
   se non certificate restano `Unresolved`.
5. **Accettazione.** La sola regolarità del bordo non copre automaticamente
   duplicazione completa o inclusioni: esplicitare i prerequisiti del
   teorema usato (località/iniettività della mappa volumetrica, orientamenti,
   componenti/shell e grado topologico) oppure aggiungere un certificato di
   molteplicità del volume. Per la prima versione è preferibile rifiutare
   esplicitamente casi non certificati rispetto a promettere un controllo
   completo delle sole superfici.
6. **Fit.** Definire se il certificato riguarda il B-rep razionale generato o
   la mappa sweep ideale. Il primo è già un obiettivo utile e preciso;
   estenderlo al secondo richiede un bound di fit conservativo e margini
   adeguati, non il solo massimo misurato ai campioni. Non aumentare
   tolleranze per ottenere separazione.

Implementazione ragionevole in un modulo mirato (p.es.
`fk_self_intersection` con helper Bezier/interval), non un'aggiunta locale al
sampler sweep. Integrazione iniziale solo come validatore dello sweep con
budget/cancellazione e diagnostica; eventuale risoluzione topologica delle
sovrapposizioni è un progetto ulteriore di booleane/splitting, non necessaria
se si sceglie il rifiuto esplicito.

## Regressioni necessarie prima di dichiararlo risolto

- Probe K2 rifiutato con testimone geometrico non locale; R4/passo1/r.2
  e casi validi K1 accettati, senza eccezioni per eliche.
- Attraversamento interno a una sola faccia; fra facce distinte; tra testa
  e lato; tra regioni multiple; con fori; duplicazione/inclusione completa.
- Semplice cucitura periodica e giunzioni tangenti valide accettate;
  intersezione oltre il bordo condiviso rifiutata.
- Tangenza isolata, contatto lungo curva, coincidenza d'area; piccola ansa
  inferiore alla vecchia leafSize; curve con tratti retti/flessi.
- Scale/pose/nodi suddivisi/riparametrizzazioni equivalenti; budget esaurito
  restituisce sempre non concluso. Misurare performance su patch dense.

Nessun fullbuild né test UI eseguito per questa analisi. K2 richiede lavoro
ulteriore; un controllo solo euristico può eventualmente fornire testimoni
positivi di difetto ma non deve presentare il risultato vuoto come validità.
