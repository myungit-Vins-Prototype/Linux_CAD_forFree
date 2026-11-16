# Audit indipendente K10 — composizione superficie/SP-curve, 15 novembre 2026

Esito sul fix circoscritto `surface_poles`: nessun blocco individuato nei tre sorgenti finali verificati. Esito sulla revisione mapping: **K10 rimane aperto**, con un ulteriore riproduttore reale accettato da `wrapBodyOnSurface`. Nessun nuovo numero di difetto; nessuna certificazione globale dei 24 callback generici.

Produzione e build condivisa in sola lettura. AGENTS, CLAUDE e grafo architetturale consultati. Tutte le compilazioni qui richiamate producono soltanto oggetti/eseguibili privati usando i comandi reali di `compile_commands.json`; l'archivio kernel esistente fornisce le altre dipendenze. Il coordinatore gestisce build app, test generali, mappa e aggiornamento CLAUDE. Non sono stati toccati CAD o processi utente.

## Fonti effettivamente verificate

`manifest.json` contiene nomi relativi al repository e SHA-256 completi. Tre core finali:

- `kernel/fk_curve_fit_bounds.cpp`: `f1aeae2d7492cb39607f09e6f93dcc364adbf60ca467aedfb7153957bdab8281`.
- `kernel/fk_curve_fit_bounds.h`: `e1dd43b39101706d358e40fd99918bdaa5b0f907a23750cd3ed46fed84236d8a`.
- `kernel/fk_surface_poles.cpp`: `8ef57b1067c1c6268e439b8e5fe2f66d838c74ae33284165d306d004c18563ac`.

I moduli bounds/poles sono stati ricompilati privatamente da snapshot identici a questi byte. `mapping-manifest.json` e `mapping-sources/` congelano invece le quattro fonti del riproduttore mapping. Queste quattro fonti non sono cambiate durante il nuovo fix; i vecchi log mapping non sono presentati come test del nuovo adapter poles.

## Verifica del provider finale

`prepareIso`, bounds.cpp:580, ricava i coefficienti omogenei dell'isoparametrica dai poli/pesi ORIGINALI della superficie tramite de Boor outward sull'asse fisso e blossom sull'asse variabile. Non usa i poli arrotondati di `uIsoCurve`/`vIsoCurve` come sorgente certificata. Le similitudini di istanza si applicano in omogenee, con la traslazione moltiplicata per W. Normalizzare per il massimo peso positivo delle righe attive non cambia la geometria.

Su ogni campata originale la sorgente è H/W. Per un candidato cubico Q si limita il residuo Bernstein R=H-QW e si usa `||R||upper/Wlower`, con Wlower positivo; altrimenti il bound è inconclusivo. Le stesse formule restano valide dopo restrizione e cambiamento affine del parametro.

`compositionFactory`, bounds.cpp:701, riconosce strutturalmente Line2 isoparametriche a direzione unitaria e campate B-spline UV grado1 con pesi uguali. Conserva ancore, poli, nodi e parametro originali. Trimmed delega la valutazione allo stesso parametro della base. La mappa diretta e la sua inversa usano intervalli; gli estremi di query vengono allargati verso l'esterno. Le preimmagini nominali dei nodi sono soltanto suggerimenti per il raffinamento. La correttezza non dipende da un nodo nominale che cada esattamente sulla preimmagine.

L'iterazione sulle mappe include entrambi i vicini di query chiuse (`map.parameter.hi < query.lo` nel lower_bound). Query che toccano soltanto un estremo di pezzo ricevono anche il bound puntuale; il pullback del candidato mantiene il suo parametro sull'intera query originale. Reverse, restrizioni e cambi asse UV sono coperti.

La prova si riferisce ai valori double di ingresso come coefficienti esatti, con operazioni IEEE e arrotondamenti conservativi `nextafter`, senza fast-math/flush-to-zero. Richiede pesi razionali positivi. Overflow, denominatori non positivi dopo enclosure, dati fuori dominio o budget inconclusivi producono rifiuto, senza attribuire un certificato al fallback.

## Adapter e adiacenze

`fk_surface_poles.cpp:107` usa il provider sulla faccia scelta e il fit certificato allo stesso budget preesistente tol/4. Il controllo successivo, :122–162, ricostruisce un provider autonomo per OGNI faccia adiacente e verifica i coefficienti cubici effettivamente prodotti, con il budget originale di connessione. Questo avviene anche se la faccia scelta ha usato il fallback. I vincoli sui nodi del cubico vengono verificati prima di interpretarli come Bezier. Nessun aumento della tolleranza.

Non ho trovato alias che modifichino la sorgente né pubblicazione parziale del risultato. Le UV originali e la genealogia restano quelle della topologia copiata. Il controllo continuo riguarda bordo/portanti supportate; non dimostra regolarità o assenza globale di autointersezioni della superficie modificata.

## Prove indipendenti finali

- `provider.log`: **30/30 PASS**. Comprende 24 fit effettivi su superficie razionale tensoriale non clamped, gradi3×2, fixedU e fixedV interni, nodi interni, pesi moltiplicati per 1e−100/1/1e100, UV reverse e nonunitarie, trim annidati, percorso UV a L con cambio asse e due placement composti validi. Per ogni fit 2.001 confronti indipendenti soddisfano errore misurato <= deviation certificata <=1e−7. Gli altri sei controlli verificano il contratto partial/mixed/unsupported e il dominio periodico.
- `provider_queries.log`: **24/24 PASS**. Candidati cubici perturbati controllati contro superficie originale, con query intera, dimezzata, terminante/iniziata nel nodo, intorno al nodo con ±1ULP e campate di 1e−9. Line2 a origine non nulla, linea inversa, UV a L diretta/inversa. Tutti i bound sono finiti e maggiori o uguali al massimo misurato su 2.001 punti. Questi controlli sono controprove del codice; la certificazione continua discende dall'algebra outward, non dalla densità di verifica.
- `body.log`: **actual body PASS**. Modifica reale `editSheetPoles` con sostituzione NURBS grado6×1, bump alto0,001 confinato in [0,1;0,101]. Risultato accettato, errore bordo massimo su20.001 punti per fin `9.6498960637564541e−8` contro tol1e−6; `checkBody=0`, sorgente con Z identicamente0 invariata. È una fixture indipendente più liscia del repro grado1 del kernel agent.

Ripetizione: `python3 build_provider.py`, `python3 build_queries.py`, `python3 build_body.py`, da questa cartella. Gli script usano percorsi assoluti e i dati qui congelati; per ripetere in futuro gli stessi byte, conservare i moduli finali riportati nel manifest. Non sono state eseguite da questo agente nuove prove sanitizer; il coordinatore e il test agent le gestiscono separatamente.

## K10 residuo reale nel mapping: prova e impatto

`composition.cpp`, `composition.log`, `mapping-input.json` e `mapping-sources/` contengono il riproduttore. Sorgente: lamina rettangolare piana unitaria, quattro spigoli Line3 nativi, piano XY, UV calcolate normalmente. Target: estrusione in Y di una B-spline polinomiale grado6. Nodi: 0×7, 0,1×6, 0,101×6, 1×7; ascisse dei poli Greville; unico poloZ non nullo all'indice9 con Z=0,0032. Il rilievo ha Zmax=0,001 a u=0,1005. `SurfaceWrapOptions`: anchor(0,0), Frame3 standard, normalOffset0, rigid=false, tolerance1e−6.

Risultato effettivo:

```
narrow_degree6 edge_composition_deviation=1.9374946091943457e-11
narrow_degree6 dense_edge_error=0.001 edge_budget=2e-8
narrow_degree6 wrap_accepted=1 reported=2.8207991498163665e-11
narrow_degree6 carrier_error=0.001 surface_pole_absZ=0 issues=0
```

TUTTI i poli della superficie restituita hanno Z=0. La superficie restituita è dunque contenuta nel piano Z=0, mentre il punto target a u=0,1005 ha Z=0,001: la distanza geometrica dal risultato è almeno0,001, ossia1000tol. Questo limite inferiore non dipende dall'accoppiamento dei parametri. `checkBody` non rileva problemi perché bordo e portante approssimati condividono lo stesso dettaglio perso.

La geometria target è regolare: `regularity.py` usa aritmetica Fraction esatta sui DOUBLE effettivi dei poli. Ogni coefficiente Bernstein di x'(u) nelle tre campate è positivo; il minimo continuo conservativo è `0.9999999999999721`. Per S(u,v)=(x(u),v,z(u)), `||Su×Sv||=sqrt(x'^2+z'^2)>=x'>0`. Nessuna cuspide o piega; Z e le sue prime due derivate si annullano ai bordi del rilievo. `regularity.json` conserva anche le frazioni esatte. Non si deduce la regolarità da soli campioni.

Causa: `fk_surface_mapping.cpp:152` fitta map(edge(t)) usando i soli nodi della curva SORGENTE; :165 fitta map(sourceSurface(u,v)) con griglia della superficie SORGENTE. La sorgente piana non contiene i nodi del target. I controlli fissi di `fk_mapping_fit.cpp:132` e della SP-curve a surface_mapping.cpp:187 possono mancare lo stesso rilievo stretto. Il nuovo provider poles non è invocato da questo percorso; il residuo rimane aperto per un blocco successivo K10.

Controprova distinta grado27: il fit del bordo perde circa5,74e−5, ma `wrapBodyOnSurface` rifiuta al successivo gate SP-curve. NON è presentata come altra accettazione errata.

## Perimetro e prossimi passi proposti

Resta esplicitamente campionato il mapping generale, il pullback UV tramite inversione (`surface_mapping.cpp:182`) e l'appiattimento quilt (`fk_quilt_mapping.cpp:411`, con integrazione lunghezze/atlante e controllo campionato della giunzione). Non è stato provato un secondo body errato per quilt; il rischio più generale è un limite del contratto, non un nuovo finding dimostrato.

Riuso sicuro: trasformazioni geometriche affini mediante poli/pesi originali; composizioni UV affini isoparametriche mediante il provider appena verificato. Una UV affine diagonale su patch tensoriale razionale può avere composizione omogenea di grado p+q dopo suddivisione per attraversamenti dei nodi, ma richiede estrazione dedicata outward. Curve UV razionali generali richiedono isolamento rigoroso delle preimmagini dei nodi e composizione con denominatori; inversione/arc-length richiedono limiti propri. Aumentare i campioni o aggiungere soltanto preimmagini nominali non risolve quel contratto.

Limiti del fix attuale: altre portanti, coniche UV, diagonali, UV nonlineari, mappe costanti o B-spline UV grado1 a pesi disuguali restituiscono provider vuoto e mantengono il fallback campionato dichiarato. Mappe riconosciute fuori dominio naturale, comprese traslazioni/attraversamenti periodici, vengono rifiutate. Valori difficili possono esaurire il budget pur essendo validi. Nessuna migrazione di coniche con nuova parametrizzazione angolare è stata proposta o eseguita.
