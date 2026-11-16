# Memoria e concorrenza — 15 novembre 2026

**Stato successivo:** M1 e M2 corretti e verificati; [patch, test e limiti](../cache-bspline-2026-11-15.md). Il seguito conserva la diagnosi e le prove prima della correzione.

Review del working tree, nessuna correzione implementata. Due difetti riprodotti; nessuna perdita permanente di memoria certificata. Una cache intenzionale, anche grande, non viene classificata come leak senza prova della sua durata e del rilascio.

## M1 — P1 — Riconoscimento cerchi: lettura di memoria liberata durante l'inizializzazione concorrente della cache

**Posizione:** `cad_drawing_curve_recognition.h:48–49`; produttore della cache `kernel/fk_bspline.cpp:220–226`.

Il range-for dereferenzia direttamente il `shared_ptr` temporaneo restituito da `cachedBezierSegments()`. Terminata l'espressione, il chiamante non conserva un proprietario della sequenza. Due thread possono trovare la cache vuota, costruire due sequenze e pubblicarle con due `atomic_store`: il secondo store libera la prima sequenza mentre il primo chiamante la sta ancora percorrendo. Il contenitore resta vivo solo finché nessuno sostituisce la cache.

**Prova:** otto thread riconoscono lo stesso cerchio NURBS raffinato in 500 tratti. AddressSanitizer segnala `heap-use-after-free` in `BSplineCurve<3>::weight`, chiamata dalla riga 49. Il log include il thread che legge e quello che libera. L'applicazione ha un percorso concorrente pertinente: `prepareDrawingSheet`, `cad_drawing_preparation.cpp:92–118`, prepara più viste della stessa geometria con fino a quattro worker automatici. Il crash dell'intera applicazione grafica non è stato riprodotto: la prova usa la funzione reale e una curva condivisa reale in un harness.

**Correzione proposta:** conservare `const auto segments = curve.cachedBezierSegments();` per tutta l'iterazione; valutare una pubblicazione della cache che non sostituisca un risultato già disponibile. La riga 81 ha la stessa forma sintattica ma opera su una spline locale: non è una seconda race dimostrata.

## M2 — P1 — La copia implicita di BSplineCurve legge una cache modificata atomicamente senza sincronizzazione

**Posizione:** `kernel/fk_bspline.h:16,66`; `kernel/fk_bspline.cpp:180,224`.

`BSplineCurve` ha un `shared_ptr` mutable per la cache, ma copia e assegnazione sono quelle implicite. `clamped()` restituisce `*this` per una curva già clamped; altre trasformazioni/conversioni copiano la stessa geometria. La copia ordinaria del membro legge la stessa variabile `shared_ptr` che un altro thread può modificare con `atomic_store`. L'atomicità del conteggio dei riferimenti non rende sicura una lettura non atomica della medesima variabile.

**Prova:** due thread invocano rispettivamente `cachedBezierSegments()` e `clamped()` su una stessa NURBS immutabile. ThreadSanitizer segnala immediatamente `data race`, write a `fk_bspline.cpp:224`, read nel costruttore di copia implicito `fk_bspline.h:16`, exit 66. L'harness esercita API pubbliche su una geometria condivisa, come consentito dall'architettura degli snapshot immutabili. Non è stata riprodotta una perdita dati di uno specifico documento utente.

**Correzione proposta:** copia/assegnazione esplicita che ometta le cache o le acquisisca atomicamente. `BSplineSurface` contiene cache analoghe: estendere l'audit della correzione anche alla superficie; qui la race della superficie resta un riscontro statico, non un secondo caso strumentato.

## Riproduzione e limiti

Dalla radice del repository, dopo avere copiato i due file `.cpp.txt` di `prove/` in `/tmp` con estensione `.cpp`:

```sh
g++ -std=c++17 -O1 -g -fsanitize=address -fno-omit-frame-pointer -I. -Ikernel /tmp/recognition_race.cpp forgecad-release/kernel/libforgekernel.a -pthread -o /tmp/recognition_race
ASAN_OPTIONS=detect_leaks=0 /tmp/recognition_race

g++ -std=c++17 -O1 -g -fsanitize=thread -fno-omit-frame-pointer -Ikernel /tmp/bspline_copy_race.cpp kernel/fk_bspline.cpp forgecad-release/kernel/libforgekernel.a -pthread -o /tmp/bspline_copy_race
TSAN_OPTIONS=halt_on_error=1 /tmp/bspline_copy_race
```

ASan strumenta harness/header, non l'intera libreria precompilata. TSan strumenta anche `fk_bspline.cpp`, non il resto dell'archivio. `detect_leaks=0`: nessuna affermazione LeakSanitizer. Controllati inoltre ownership B-rep, cache display, worker delle anteprime, cancellazione e rilascio del documento; il test dedicato `forgecad_display_cache_memory_tests` passa. Non è un profiling dell'intero ciclo di vita dell'applicazione, né una certificazione CUDA/Metal/macOS.
