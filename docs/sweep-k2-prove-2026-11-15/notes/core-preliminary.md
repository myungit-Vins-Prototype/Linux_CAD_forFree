# Certificatore sperimentale map-only K2 — congelamento

Sorgenti/object/hash in questa directory. Nessuna modifica del repository,
nessuna build globale. API e durata: `duration` è metadato validato positivo;
la prova riparametrizza ogni pezza con una costante positiva derivata dalle
sue derivate (chart continua con velocità eventualmente discontinua).

## Contratto matematico

F(x,y,t)=C(t)+U(t)x+V(t)y, campi Bezier razionali di stesso grado e pesi
positivi comuni. Il dominio della sezione è l'inviluppo convesso dichiarato.
Su chart con intervalli intermedi, estremi identici e seam ciclica esplicita,
la parte simmetrica uniformemente positiva di L*J prova monotonia forte:
per ogni z≠w, (z−w)·L(F(z)−F(w))>0 mediante integrazione lungo il segmento.
L è un precondizionatore costante arbitrario; LDL intervallare certifica SPD,
con scaling positivo delle righe trasversali. La riparametrizzazione positiva
per pezza conserva la prova tramite integrazione piecewise. Hull di dimensione
0/1 usano Jacobiano ridotto. Le coppie non certificate localmente richiedono
separazione conservativa dei hull Bernstein, anche attraverso suddivisione.
Nessuna fascia della diagonale/adiacenza viene ignorata. BVH contigua e chart
con slab intermedi evitano il prodotto cartesiano indiscriminato.

Operazioni intervallari outward, derivata H'W−HW'/W², coefficienti binomiali
intervallari, propagazione entire di overflow/NaN; cache delle tre serie di
numeratori per pezza. Esiti CertifiedInjective / Unresolved. Non emette
IntersectionWitness dal solo hull (potrebbe intersecare una zona esterna al
profilo reale, per es. foro o concavità).

## Prove locali

- Compilazione O2, ff-contract=off PASS.
- Straight2slab Certified, visits2; sheet hull1D straight Certified visits2.
- Backtracking2slab Unresolved, visits150, depth24.
- Ring razionale closed4quarter Certified, visits180, depth3.
- Mappa ricavata da fit comune C,C+N,C+B di frame Bishop generico:
  K1 R4/pitch1/16turn/r.2,512slab,hull16vertici: Certified,2144visits,
  depth0,0.537s nella prova locale.
  K2 R1/pitch.1/4turn/r.5,128slab: Unresolved,527visits,depth24,0.196s.
  Quasiplanare R10/pitch.1/.25turn/r8,16slab: Certified,1187visits,
  depth2,0.182s.
- Il fit è quello del probe isolato, non del BRep integrato; i confini
  sono canonicalizzati esplicitamente. I tempi sono misure singole.
- Agente indipendente riferisce4/4test diretti PASS su una versione precedente
  alla formattazione finale; fare riferimento ai suoi hash e log distinti.

## Limiti importanti: non promuovere a validatore di produzione

1. Il certificato riguarda SOLO la mappa fornita. Il fit indipendente delle
   righe fisiche non conserva necessariamente identità affine in double;
   errore C0 o residuo misurato non trasferiscono l'iniettività al BRep.
   Servono costruzione/estensione coerente e bound C1 sulle perturbazioni.
2. Parent ha riprodotto validi rifiutati: sheet curva planare in XZ estrusa Z
   (il suo hull bidimensionale × path collassa, pur essendo iniettiva la
   superficie curva), e due sezioni disconnesse intrecciate ma disgiunte
   sull'elica (il singolo hull convesso introduce volume inesistente).
   Servono domini parametrizzati per curve e unioni di domini, non un
   permesso a ignorare Unresolved.
3. **Falso Certified trovato fuori scala ingegneristica:** hull triangolare
   {(0,0),(1e-200,0),(0,1e-200)}, C=tZ,U=X,V=Z. orient2d del kernel restituisce0
   per underflow (il suo contratto esclude overflow/underflow), riducendo
   erroneamente il rango a1. `../probe_extreme.cpp` stampa orient=0,status=0.
   Nessuna modifica al congelamento per istruzione parent. Prima di ogni
   uso generale occorre un guard di dominio numerico per riduzione di rango
   o un predicato scalato esatto, con Unresolved quando non certificabile.
4. maxVisits limita solo le visite ricorsive della validazione, NON memoria
   o costo totale di preparazione input/BVH/coefficienti. maxVisits0 ritorna
   presto senza costruire Validator. Checkpoint cancellazione presenti per
   slab/build/visita; non in ogni singola operazione di hull o prodotto
   Bernstein. Non dichiarare un hard resource limit complessivo. Per una API
   pubblica aggiungere budget totale coefficienti/hull e preparazione.
5. K2 rifiutato come non concluso, non come intersezione provata. Tangenti,
   coincidenze, gradi estremi o margini stretti possono restare non conclusi.

K2 resta aperto; artefatto sperimentale utile per una famiglia sufficiente
ma non una correzione completa o un nuovo certificato BRep.
