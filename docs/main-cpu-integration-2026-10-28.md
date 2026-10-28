# Ottimizzazioni CPU su main — 28 ottobre 2026

Trasferimento dal branch di sviluppo `solver-mesh-gpu` (`815d760`) sul remoto main basato su `49d69b0`, conservando come antenati i due fix main locali `d8fd740` e `149b769`. Il ramo locale main divergente è preservato: la pubblicazione usa il checkpoint verificato sul ramo remoto.

Portati filtri conservativi delle proiezioni degli spigoli e degli apici, ricerca inclusiva dei tratti Bézier nel range, certificati di distanza con fallback originale, riuso limitato delle proiezioni, valutazione del solo punto B-spline, cache delle lunghezze del loft, riuso dei campioni UV nelle cuciture e quadrature per faccia parallele con riduzione nell'ordine originale. Inclusi i relativi test. Conservata la broad phase CPU di main; nessun solver sperimentale, pipeline mesh differita o nuovo backend GPU trasferito.

La prima sonda su main remoto si fermava con 20 errori conseguenti alla ripetizione della sweep solida non supportata. È una differenza di funzionalità preesistente, non una regressione del filtro: i due fix locali già committati abilitano quel caso e completano il checkpoint di soppressione. Quella misura è esclusa.

Verifica finale: applicazione completa e runner compilati su Linux, **191/191 casi kernel distinti PASS** nelle suite pertinenti e test applicativi nel log allegato PASS. CT, mouse2 e assieme mantengono tutte le firme B-rep, errori e visibilità rispetto alla versione di sviluppo verificata; input SHA invariati. Le mesh dei due rami non sono dichiarate identiche: sviluppo include già un filtro dei triangoli collassati non trasferito con questo intervento CPU. Il confronto finale forza la tassellazione CPU nella sonda, separatamente dall’app completa che conserva il proprio backend GPU. Errori 0/0/4, gli ultimi quattro importati già noti. Main prepara anche mesh nascoste che sviluppo differisce: i soli conteggi triangoli delle viste nascoste non sono confrontati fra le due pipeline.

Queste sono verifiche funzionali della combinazione main, non nuovi confronti prestazionali. Il guadagno CT 26,645→17,214s del rapporto su sviluppo non viene attribuito automaticamente a main. Nessuna nuova verifica macOS o installazione desktop.

Mappa rigenerata: 472 file, 6619 relazioni, 231 unità senza diagnostiche, controllo di aggiornamento PASS e blocco interessato verificato in entrata/uscita e visivamente. HTML/JSON della mappa ignorati da Git.

[Dati](main-cpu-integration-2026-10-28.json). Log, sonde e risultati locali in `forgecad-linux-solver/benchmarks/main-cpu-integration-2026-10-28`. Documenti reali non salvati o modificati. Commit/push richiesti dall'utente; esito registrato in CLAUDE.md.
