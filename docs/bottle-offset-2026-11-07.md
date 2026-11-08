# Bottle — offset di 3 mm sul Corpo 1

Data: 7 novembre 2026. Richiesta: correggere l'offset del Corpo 1 di `Bottle.prt`, distanza 3 mm.
Il verso non era specificato: verificate e preparate entrambe le direzioni.

## Risultato

Offset con cucitura riuscito a +3 e −3 mm: 8 facce, una shell, zero problemi con
`checkBody({true,true})`, nessuna faccia fallita nella tessellazione. Il corpo sorgente è una
superficie aperta; il risultato conserva questa natura, senza aggiungere chiusure.

Copie native con identità documento autonome, originale invariato:

- `output/Bottle-offset-3mm.prt`: esterno +3 mm.
- `output/Bottle-offset-interno-3mm.prt`: interno −3 mm.

Eseguibile completo ricompilato: `forgecad-release/forgecad`. Riavviare l'app già aperta per
caricare la correzione. Nessun processo utente chiuso, nessun commit o push eseguito.

## Riproduttore e cause

Input congelato da `/home/myungit/Documenti/Cad_Example/Bottle.prt`, 1.739.623 byte;
SHA-256 prima/dopo identico:
`be0cdce025111df5b95346eb813f81d6efa6eba52b6e0cfad700ea088fcbd417`.
La copia di prova è `/tmp/bottle-offset/Bottle-original.prt`.
Corpo 1 corrisponde a `Loft3`: originariamente 12 facce B-spline, 36 bordi, 24 vertici,
con sezioni e guide definite nei sei schizzi del documento.

1. Il confronto dei jet delle pezze Bezier identificava falsamente 152 linee come pieghe:
   molti nodi erano separati da un solo ULP, ma con molteplicità 3 e grado 5 garantivano C2.
   L'offset falliva con «piega interna senza nodo C0 separabile».
2. L'unione esatta dei nodi di guide adattate indipendentemente conservava eventi equivalenti
   a distanza di pochi ULP. Dopo aver escluso le false pieghe C2 rimanevano due nodi C0
   `0.61230909488562113` e `0.61230909488562135`, generando una microfaccia e un errore
   sui fin opposti durante la cucitura.
3. Risolti i nodi, il calcolo delle intersezioni offset cambiava rango lungo bordi che
   attraversavano sezioni quasi tangenti. Il fit della stessa curva vedeva due solutori
   numericamente diversi e raggiungeva il limite di raffinamento.

## Implementazione

- `kernel/fk_bspline_surface.cpp`: la molteplicità certifica prima la continuità C1/C2;
  il confronto dei jet resta per i nodi che possono rappresentare una vera piega C0.
- Nuovo `kernel/fk_loft_knots.{h,cpp}`, collegato da `fk_loft.cpp` e dal CMake del kernel:
  armonizzazione dei soli nodi fra guide entro 64 epsilon per la scala parametrica.
  Le sezioni originali hanno priorità; nodi distinti della stessa guida e ancore distinte
  non vengono fusi. Poli/pesi conservati; la variazione è controllata sui coefficienti
  omogenei raffinati nella base comune, con budget di solo arrotondamento pari a
  256 epsilon per la scala delle coordinate. Dettagli significativi causano rifiuto atomico.
- `kernel/fk_offset.cpp`: scelta uniforme del solutore per tutto il bordo quando due appoggi
  attraversano la soglia angolare già presente di 1e-3. Riutilizzati regolarizzazione e
  controlli di residuo esistenti; tolleranze e budget di fit non aumentati.
- `cad_document_io.{cpp,h}`: cache B-rep 15. Le copie di lavoro con loft non soppresso e
  cache precedente rigenerano atomicamente geometria e derivati. Check-in immutabili;
  cache di offset senza loft conservate. Formato delle definizioni nativo 66 invariato.
- Test di lettura/revisioni/storia aggiornati alla cache 15. Nuovo target indipendente da
  OCCT `forgekernel_loft_knots_tests`, cinque casi. Catalogo architetturale aggiornato.

Dopo la rigenerazione il loft ha 8 facce, 24 bordi e 16 vertici: le pezze adiacenti G1
compatibili si uniscono normalmente. Le definizioni sorgenti degli schizzi/guide sono preservate.

## Verifiche eseguite

- Build completa dell'app e del runner grafico riuscita, inclusa la condizione finale di migrazione.
- 21 target CTest pertinenti complessivamente verdi: loft, offset, lettura, revisioni,
  storia/cache, istanze e raccordi. Il controllo legacy della storia aveva individuato
  un'invalidazione eccessiva delle vecchie cache di offset: condizione ristretta ai loft,
  quindi rieseguiti e superati tutti e tre i target lettura/cache/revisioni coinvolti.
- Nuovi cinque casi: continuità razionale/non razionale nei due assi, ancore e nodi ULP,
  conservazione dei dettagli/rifiuto atomico, invarianza dell'offset ±3 mm, cucitura quasi
  tangente ±3 mm con misura su entrambi gli appoggi estesi.
- Suite kernel: 26/26 Offset, 18/18 Shell, 42/42 Loft, totale 86 casi superati.
- Sul Bottle reale, 35.424 punti per direzione: massimo scarto da `S + d N` rispettivamente
  5,30028e-8 mm (+3) e 5,45142e-8 mm (−3). Otto facce tessellate e zero fallimenti per verso.
  Tempi misurati della sola operazione: circa 11,67 s esterno e 22,74 s interno.
- Sessione nativa Wayland/OpenGL: pannello reale dell'offset esterno, anteprima e conferma;
  entrambi i versi verificati con cucitura, Undo/Redo, codec con/senza cache, rigenerazione,
  salvataggio/riapertura e controlli B-rep. Immagini esterno/interno ispezionate.
- Regressione in sola lettura `Onion2-raccordato.prt`: migrazione cache 14, tutti i 18
  raccordi a R0,092797, 73 facce e zero errori; file non risalvato.
- Mappa: 638 file, 9.052 relazioni, zero unità con diagnostica; `--check` PASS.
  Overview e sei schede HTML aperte e controllate, inclusi entrata/uscita e chiamata
  risolta `fk_loft.cpp → fk_loft_knots.cpp`; chiamate/include/riferimenti distinti.
- `git diff --check` PASS. Hash originale riconfermato dopo tutte le prove.

Log e riproduttori della sessione in `/tmp/bottle-offset/`; rapporto e memoria restano nel repository.
Le prove certificano i casi descritti e i residui misurati, non l'assenza globale di
intersezioni di qualunque superficie offset. Restano i limiti geometrici generali già registrati.
