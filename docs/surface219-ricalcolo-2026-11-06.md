# Surface219 — ricalcolo senza cache, 6 novembre 2026

## Causa riprodotta e correzione integrata

Richiesta: migliorare il calcolo effettivo della geometria oltre al riuso della cache.
Si conserva la copia congelata 2e09473fa5f7a6a4a438b5fcd75e188deee693f32ef36e4d827c3d1396ae4976
in `/tmp/surface219-drawing-20261106/input.prt`, 40 feature; originale mai scritto.
Le modifiche precedenti ad apertura/tavola sono preservate nel working tree.

La sonda ricalcola con `buildGeometry` la sola feature 35 (Loft 36), usando gli
operandi gia' valutati, senza riusare il risultato della loft. Il binario precedente
produce il B-rep identico alla cache in 263.903 ms. GDB avviato come genitore di una
seconda sonda temporanea individua `CurveLengthCache::get` e la quadratura scalare:
una spline cubica polinomiale di 4.747 poli viene integrata fino a profondita' 30,
con budget locali nell'ordine di 1e-25. Il campionamento non viene usato come benchmark.
L'aggancio a processi esistenti era vietato da ptrace; nessuna impostazione del sistema
modificata, nessun debugger agganciato alla sessione utente.

La somma delle derivate delle basi B-spline e' zero. Il vecchio valutatore sommava
coefficienti di segno opposto moltiplicati per coordinate assolute, perdendo cifre
significative quando il tratto era piccolo rispetto alla posizione del modello.
Il rumore sulla velocita' impediva alla quadratura di convergere.

`kernel/fk_bspline.cpp` ora sottrae un polo locale per gli ordini positivi delle
sole curve polinomiali. La posizione e il percorso razionale rimangono invariati.
Nessuna modifica a nodi, poli salvati, grado, risoluzioni o tolleranze della quadratura.
Non vengono saltati campioni o controlli geometrici.

La variante temporanea equivalente ricalcola Loft 36 in **7.849 ms**, due facce,
zero issue B-rep: circa 34 volte meno nella coppia di prove locali. Altri carichi
utente/build erano presenti; non e' una certificazione di un fattore universale.
Confronto con il risultato precedente: stessa topologia, griglie e nodi; scarto
massimo dei poli 1,1431e-13 mm. Su 20.402 punti: scarto 5,6983e-14 mm, normali
1,6605e-12; bordi identici nel campionamento. I byte non sono identici per gli
arrotondamenti delle lunghezze e delle costruzioni derivate.

## Verifiche e limiti

Nuovo target indipendente da OCCT: `forgekernel_bspline_derivative_tests`.
Tre prove passate: derivate Bernstein analitiche 2D/3D, gradi 1–9, traslazioni grandi,
limiti laterali ai nodi, e lunghezza di una retta sintetica in 800 pezze cubiche.
La fixture di lunghezza supera 100.000 valutazioni con il vecchio kernel; il nuovo
ne usa 12.000 con errore 6,99e-15, inferiore alla richiesta invariata di 1e-13.
Il conteggio limita il lavoro senza dipendere dalla velocita' della macchina.

Svuota 1 isolato: 60.972 ms prima e 69.901 ms dopo sotto carichi concorrenti;
**nessun miglioramento dimostrato** su questa funzione. Entrambi producono 62 facce
senza issue. Stesse griglie; su 632.462 punti di superficie scarto massimo 5,15e-14 mm,
scarto massimo sui bordi 5,06e-8 mm (entro la risoluzione geometrica esistente 1e-7).
Il confronto diagnostico con soglia arbitraria 1e-9 segnala quindi i bordi: non viene
presentato come uguaglianza binaria o come confronto entro 1e-9. Il controllo B-rep passa.

Build completa app/runner riuscita. **20 CTest mirati superati** in 8,50 s, comprese
le tre nuove prove indipendenti. Suite generale: 307 test passati senza fallimenti
prima del limite diagnostico di 300 s, durante i test STEP; non viene dichiarata
completa. Gruppi espliciti conclusi: Shell 18/18, Offset 26/26, Loft 42/42,
Mass 11/11, Project 25/25. Questi gruppi possono sovrapporsi alla suite generale:
i conteggi non vanno sommati come prove tutte distinte.

Ricalcolo completo sulla build integrata: lettura della sola definizione 37 ms,
**zero B-rep caricati**; `CadViewport::loadDocument` completa tutte le 40 feature
in **136.788 ms**, zero errori. La mesh finale e' inclusa (1.328 ms, 12 corpi richiesti).
I tempi per feature principali sono:

| Funzione | Tempo di costruzione |
| --- | ---: |
| Svuota 1 | 67.708 ms |
| Solido cucito 40 | 24.368 ms |
| Loft 36 | 8.506 ms |
| Offset 33 | 6.871 ms |
| Cucitura 39 | 6.207 ms |
| Taglio 32 | 6.199 ms |

La somma non e' il tempo totale perche' alcune funzioni indipendenti sono parallele.
Il precedente timeout a 180 s non misurava il ricalcolo totale prima della correzione;
non si attribuisce quindi un fattore preciso di accelerazione all'intera storia.
Il dato certo e' che adesso l'intera storia termina in meno del tempo precedentemente
richiesto dalla sola Loft 36. La sonda applicativa completa e' offscreen: certifica
costruzione, errori e tassellazione, non il rendering OpenGL nativo.

Mappa 625 file/8.841 relazioni/304 unita', zero diagnostiche; `--check` positivo.
Tre dettagli HTML renderizzati in XCB, collegamenti in entrata/uscita verificati;
le chiamate template al valutatore sono attribuite alla dichiarazione in `.h`.
Include, chiamate e riferimenti rimangono distinti. `git diff --check` pulito.
Impronta della copia congelata invariata; nessun CAD originale scritto.

La lentezza residua dello svuotamento e della cucitura finale resta aperta: non viene
presentata come risolta dalla correzione delle derivate. Nessun benchmark su macOS
eseguito in questa sessione. Le precedenti modifiche a cache/tavola sono conservate;
nessun commit o push richiesto per questo intervento.

Sonde, binari prima/dopo, confronti e log: `/tmp/surface219-recompute-20261106/`.

Log principali: `loft-before.log`, `loft-after.log`, `loft-compare.log`,
`shell-before.log`, `shell-after.log`, `shell-compare.log`, `full-after.log`,
`build-full.log`, `ctest.log`, `kernel-tests.log`, `tests-{Shell,Offset,Loft,Mass,Project}.log`,
`architecture.log`, `architecture-edges.json`, `architecture-render/`.
