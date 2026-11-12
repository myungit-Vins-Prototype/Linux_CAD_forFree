# Cambio documento nella stessa finestra: memoria

Verifica e correzione dell’8 novembre 2026, Linux/glibc, Qt6/Wayland e OpenGL nativo.
Richiesta: controllare la RAM rimasta assegnata dopo aver aperto un file 3D e
averlo sostituito con un altro nella stessa finestra.

## Esito e cause

Sono stati riprodotti due fenomeni distinti:

1. Senza trascinamenti precedenti, i 30 B-rep e le 765 superfici/basi uniche di
   Bottle vengono distrutti, ma glibc conserva oltre 2 GiB di pagine libere.
   Il consumo residente resta alto anche passando a un documento vuoto.
2. Un normale clic/rilascio su un estremo di schizzo lascia `dragSnapshot_`
   valorizzato. Il caricamento azzera la cronologia, ma prima della correzione
   non azzerava questo snapshot: i 30 corpi e le 765 superfici rimangono vivi
   anche dopo il passaggio a Shower Head e poi a un nuovo documento vuoto.

Nel secondo caso c’è una vera ritenzione del vecchio documento; nel primo
la geometria è già distrutta e la memoria è libera all’interno dell’allocatore.
Ridurre i poli NURBS, la precisione o la risoluzione della mesh non serve a
correggere queste cause e non è stato fatto.

## Modifiche

- Nuovo `forgeCad2026_viewport_document_lifetime.cpp`: reset dei proprietari
  transitori soltanto per `loadDocument` e `loadPreviewDocument`. Libera snapshot
  di trascinamento/ridimensionamento, riferimenti di schizzo, anteprime,
  definizioni di maniglie, analisi e dati temporanei di selezione/taglio.
- Le definizioni conservate da anteprima e maniglia vengono rilasciate anche
  chiudendo il rispettivo pannello.
- Cache display CPU/GL svuotata con il contesto corretto al cambio documento,
  anche se il viewport è nascosto e non riceverà altri fotogrammi.
- Preparazione asincrona delle cache di selezione: riferimenti deboli ai corpi
  in coda e cancellazione per viewport. Un lavoro già in esecuzione abbandona
  al checkpoint; una coda sospesa non possiede più l’intero vecchio documento.
  Propagata anche la precisione del documento al worker.
- Alla fine del caricamento, un evento differito e accorpato chiama il rilascio
  delle pagine inutilizzate (`malloc_trim` su glibc), dopo la distruzione dei
  temporanei del caricatore. Nessuna modifica globale all’allocatore.
- Undo/Redo continua a usare `restoreDocument`, senza questo reset; un secondo
  viewport mantiene dati, cronologia e lavori propri.

Modificati coordinatori viewport/documento/anteprima/selezione e header;
registrati modulo e test in CMake e nel catalogo dell’architettura.
Le modifiche STL/OBJ già presenti appartengono all’altro intervento e sono preservate.

## Prova sui file reali

Copie congelate in `/tmp/document-switch-memory-2026-11-08/` degli originali
in `/home/myungit/Documenti/Cad_Example/`; nessuna scrittura ai documenti originali.

| File | SHA-256 | Contenuto caricato |
| --- | --- | --- |
| Bottle.prt | `e7abc620a25ec25306a0e6e3a506a4b3569d7381085ff163711184d0acb74017` | 33 funzioni, 30 B-rep, 765 superfici/basi uniche |
| Shower Head.prt | `45334127b9a2fa232d4e6c9a5466a2b14f787a267a0ef2eb560fb8c0929e91a7` | 12 funzioni, versione salvata corrente |

Un processo diagnostico separato usa la vera `PdfWindow::openDocumentPath`,
la stessa finestra Wayland, il contesto OpenGL e gli oggetti dell’eseguibile.
Apre Bottle → Shower Head → Nuovo documento, attende i lavori e gli eventi
GUI e misura `/proc/self/smaps_rollup`, `mallinfo2` e riferimenti deboli a tutti
i corpi e alle superfici del primo file. Impostazioni/cache del processo isolate.
Nelle misure con snapshot, la sonda memorizza lo stesso `DocumentState` lasciato
dal trascinamento; la riproduzione tramite clic/rilascio reale è nel test permanente.
Il processo dell’utente non è stato chiuso o riavviato.

RSS residente in GiB (1 GiB = 2³⁰ byte):

| Caso | Shower Head, dopo assestamento | Nuovo documento vuoto | Corpi/superfici Bottle ancora vivi |
| --- | ---: | ---: | ---: |
| Prima, senza snapshot, ciclo 1 | 2,670 | 0,399 dopo rilascio diagnostico | 0 / 0 |
| Prima, senza snapshot, ciclo 2 | 3,255 | 2,707 | 0 / 0 |
| Prima, snapshot residuo | 3,474 | 2,968 | 30 / 765 |
| Corretto, ciclo 1 con snapshot | 0,895 | 0,454 | 0 / 0 |
| Corretto, ciclo 2 con snapshot | 0,878 | 0,419 | 0 / 0 |
| Corretto, ciclo 3 con snapshot | 0,901 | 0,421 | 0 / 0 |
| Controllo corretto, solo Shower da processo nuovo | 0,785 | 0,330 | nessun Bottle caricato |

Nel primo caso non corretto, un `malloc_trim` diagnostico porta Shower da
2,670 a 0,749 GiB senza cambiare l’heap usato: dimostra le pagine libere trattenute.
Con lo snapshot non corretto, lo stesso rilascio lascia vivi tutti i corpi.
Nel primo ciclo corretto la chiamata diagnostica aggiuntiva recupera solo
circa 5 MiB; i cicli 2 e 3 usano esclusivamente il rilascio automatico nuovo.

Dopo la correzione, heap usato a documento vuoto: 57,9 / 58,1 / 58,4 MiB;
controllo con solo Shower: 57,5 MiB. Shower dopo Bottle resta circa 0,09–0,12 GiB
sopra il controllo a freddo, senza crescita progressiva nei tre cicli.
Non è una promessa di RSS identico all’avvio: allocatore, Qt, driver GL e cache
possono conservare memoria. Il conteggio dei proprietari esclude invece la
ritenzione dei vecchi B-rep/superfici nel percorso misurato. Le ampie arene
virtuali riservate non equivalgono a RAM residente.

## Regressioni e limiti

`tests/test_document_lifetime.h`, opzione `--document-lifetime`: clic/rilascio
reale su schizzo; rilascio corpo/superficie al cambio; chiusura anteprima/maniglia;
Undo/Redo; sostituzione con anteprima; coda selezione sospesa e cancellata;
isolamento di un secondo viewport e confronto binario del suo B-rep.
Con `--gl` controlla anche il rilascio della cache display in un viewport nascosto.

Build completa `forgecad` e runner PASS. Tutti i 16 CTest pertinenti PASS:
lettura/unità/consultazione documenti, precisione, durata, snapshot Mouse/chunk,
anteprime e durata asincrona, maniglie, storia dichiarata/genealogia,
mesh di anteprima storica, utilizzo/workflow riferimenti schizzo, cache display.
Prove native Wayland `--document-lifetime --gl` e cache display `--gl` PASS,
compreso il viewport nascosto con cache realmente popolata prima della sostituzione.
La prova sui tre cicli nativi usa gli oggetti della stessa app compilata.
Mappa architetturale aggiornata: 654 file, 9378 relazioni, 320 unità C++ senza
diagnostica; controllo `--check` PASS. Quattro dettagli HTML verificati nel browser
e nelle immagini, con chiamate in entrata/uscita e collegamenti reciproci.

```sh
ctest --test-dir forgecad-release --output-on-failure -R 'document_lifetime|async_lifetime|mouse_snapshot'
QT_QPA_PLATFORM=wayland ./forgecad-release/forgecad_view_tests --document-lifetime --gl
QT_QPA_PLATFORM=wayland ./forgecad-release/forgecad_display_cache_memory_tests --gl
```

L’app aperta continua a eseguire il binario caricato in precedenza: riaprirla
per usare la nuova build; non è stata riavviata automaticamente.
Il rilascio delle pagine è specifico di glibc: sugli altri sistemi l’helper
esistente non forza il rilascio; il reset dei proprietari è multipiattaforma,
ma macOS e altri allocatori non sono stati verificati qui.
Tre cicli e i proprietari controllati non costituiscono una verifica di ogni
possibile comando/importatore o una profilazione generale del driver GPU.
