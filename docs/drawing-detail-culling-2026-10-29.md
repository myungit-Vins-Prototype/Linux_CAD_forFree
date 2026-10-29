# Dettagli 2D: verifica del ritaglio anticipato — 29 ottobre 2026

Verifica sperimentale completata fuori dal codice applicativo. Il rapporto accompagna la pubblicazione del parallelismo su `solver-mesh-gpu` e `main`; il codice sperimentale di questo studio resta isolato e non viene integrato nell’applicazione. Installazione desktop invariata.

## Guadagno misurato

Stessa copia congelata del CT0578 con **11 viste** del precedente studio. Il confronto parte dalla versione **gia parallela, massimo quattro worker**. Xeon W-10885M, 8 core fisici/16 thread logici, Linux/Qt offscreen. Tre coppie alternate di processi nuovi, cache filesystem calda, app chiusa dopo conferma dell’utente e nessuna compilazione concorrente.

| Preparazione foglio | Parallela precedente | Prototipo con esclusioni |
|---|---:|---:|
| Campione 1 | 11,022 s | 10,616 s |
| Campione 2 | 11,387 s | 10,558 s |
| Campione 3 | 11,071 s | 10,363 s |
| **Mediana** | **11,071 s** | **10,558 s** |
| CPU totale, mediana | 24,190 s | 22,522 s |
| Memoria residente massima, mediana | 1,553 GiB | 1,572 GiB |

**Guadagno aggiuntivo: 0,513 secondi, 4,63%, 1,049×.** È una riduzione modesta rispetto a quella ottenuta dal parallelismo. Una coppia separata che costruisce l’editor effettivo passa da **11,640 a 11,064 s**, circa 0,575 s in meno. Lettura/preparazione modello circa 0,27 s, escluse dai timer del foglio. Non è un confronto dell’avvio completo del programma; il valore non va sommato automaticamente ai guadagni di altri studi.

## Esclusioni provate e correttezza

La prima variante esclude le corde dei contorni apparenti che non possono intersecare il box del dettaglio, ampliato di 0,05 mm sul foglio, prima di eseguire proiezioni e occlusioni costose. Le corde cilindriche conservano il calcolo completo, necessario per mantenere l’ammissibilità degli assi.

La seconda variante aggiunge un filtro dei bordi non conici, basato sul box conservativo 3D della curva e sulla sua proiezione. Il filtro delle curve si disattiva quando gli assi cilindrici possono raggiungere il dettaglio. Tutte le coniche restano complete: gli indicatori di centro dipendono dalla loro visibilità anche quando l’arco è fuori dal ritaglio. Curve e riferimenti usati dalle quote non vengono eliminati dal modello. Il ritaglio finale originale rimane applicato.

Le esclusioni non interessano le mesh e gli indici degli occludenti, le immagini ombreggiate o il calcolo delle coniche. Questo è un **filtro conservativo parziale**, non una misura del massimo beneficio possibile da una visibilità interamente limitata al ritaglio.

Risultati identici dopo il ritaglio su tutti i sei processi CT: impronta completa di geometria campionata, contorni, tracciati visibili/nascosti, campiture, immagini, centri, assi e tutti i campi delle quote `24526793025eba0c26e0d3b1b1bb280844ae4f7f3c88782059a2fba965a2be0e`.

**72 confronti aggiuntivi superati:** dettagli circolari, rettangolari e poligonali concavi; due scale e orientamenti; sfere, tori e cilindri con altri corpi occludenti; campiture, centri, assi, ombreggiatura e sezione completa. Annullamento verificato sulla funzione sperimentale con risultato preesistente preservato atomicamente. La versione 1 nel solo pilota passa da 11,138 a 10,729 s; la versione 2 pilota a 10,509 s. Le mediane della tabella si riferiscono alla versione 2 confermata.

## Stato e limiti

Il prototipo resta isolato: il beneficio di circa mezzo secondo non viene presentato come un nuovo recupero di parecchi secondi. I sorgenti e l’eseguibile applicativo del parallelismo precedente sono stati ricontrollati con SHA e risultano invariati. Le 30 regressioni precedenti non sono state ripetute sul prototipo; i 72 confronti mirati e la sonda CT non sostituiscono una futura integrazione completa e la relativa regressione.

Una riduzione ulteriore del dominio degli occludenti e dei sottoarchi richiede di conservare l’ammissibilità di centri/assi e la deduplicazione globale. Il riuso delle sezioni geometriche identiche fra dettagli resta un candidato separato, non implementato o misurato qui. Nessuna nuova prova CUDA, macOS o Windows.

Sorgenti sperimentali, log, dati grezzi, hash e campioni congelati sono conservati localmente in `forgecad-linux-solver/benchmarks/drawing-detail-culling-2026-10-29` (ignorato da Git). Il rapporto JSON affiancato contiene i valori completi e le condizioni di misura.

Il porting su main riguarda soltanto il parallelismo precedente: build completa e 30/30 regressioni superate, CT congelato identico. Questi controlli non costituiscono una regressione completa del prototipo sperimentale.

Pubblicazione verificata sul remoto: parallelismo `solver-mesh-gpu` `8010b4b162d6fef9ab37b51e7791d47fb741f0f3`, `main` `f8caa11a45153a81c8671f908c5bae8c5e713129`. Il codice sperimentale del ritaglio dei dettagli resta escluso dall’applicazione.
