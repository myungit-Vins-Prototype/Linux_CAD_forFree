# Correzione delle cache B-spline — 15 novembre 2026

**M1 e M2 corretti e verificati.** Su autorizzazione dell’utente, tre agenti GPT-6.1 Sol hanno preparato correzione lifetime, copie/pubblicazione delle cache e regressioni; il coordinatore ha revisionato, integrato e compilato. La patch M2 ha avuto anche una review indipendente. Gli altri 17 rilievi della [review](code-review-2026-11-15.md) restano aperti.

## Correzioni

- **M1:** `cad_drawing_curve_recognition.h` conserva il `shared_ptr` prima di entrambi i loop sulle sezioni Bezier. Il risultato resta posseduto durante l’iterazione, anche se il produttore cambia la cache. Nessuna copia aggiuntiva del vettore o modifica geometrica.
- **M2:** `BSplineCurve` e `BSplineSurface` acquisiscono atomicamente le cache nel costruttore di copia. L’assegnazione prepara uno snapshot completo prima di modificare la destinazione e poi usa il move `noexcept`. Curve, pesi, nodi, periodicità, base condivisa e trasformazione delle istanze sono conservati.
- **Pubblicazione:** CAS strong sulle cinque cache (segmenti delle curve; pezze, discontinuità, albero e isoparametriche delle superfici). Il primo risultato pubblicato resta stabile e i concorrenti usano quello già disponibile. Al primo accesso possono ancora essere eseguiti calcoli temporanei duplicati; non viene introdotto un lock nella valutazione geometrica.

Il contratto resta geometria immutabile condivisa: copia e letture della sorgente possono concorrere con il popolamento delle sue cache. Move, modifica/assegnazione della destinazione e distruzione richiedono accesso esclusivo all’oggetto interessato. Non si promette thread safety per mutazioni concorrenti della geometria. Le quattro cache della superficie sono snapshot indipendenti della stessa geometria, quindi non richiedono una transazione comune.

## Verifiche

| Verifica | Esito e copertura |
|---|---|
| M1 isolata | ASan PASS, 200 round × 8 thread su cerchio raffinato in 500 tratti, contro il produttore originale: la correzione del chiamante funziona anche senza CAS |
| M2 riproduttore storico | TSan PASS, stesso caso che prima segnalava la race nella copia implicita |
| Nuove regressioni | 5/5 PASS normale, ASan e TSan; curve 2D/3D, superfici e tutte le cache, destinazioni con dati precedenti, self-assignment/move, istanze, periodicità, durata dei risultati dopo distruzione della sorgente, letture e copie concorrenti |
| Build completa | PASS: kernel, applicazione `forgecad`, runner e target configurati ricompilati; opzioni della build esistente conservate |
| Integrazione | **47/47 CTest PASS**, nessuno skip: spline, istanze, geometrie proiettate, documenti, tavole, riconoscimento/visibilità, display cache ed export CPU. Il nuovo target contiene i cinque casi sopra |
| Mappa | **724 file / 10410 relazioni / 353 unità C++**, zero unità con diagnostica; `--check` PASS, HTML e archi del nuovo test verificati in entrambi i versi |

Runner kernel generale interrotto dal limite di 300 secondi: 325 casi conclusi senza fallimenti osservati. Questa esecuzione non certifica l’intera suite; il limite del processo non è attribuito a un difetto della patch.

I sanitizer strumentano harness/header e le due implementazioni spline; il resto dell’archivio collegato è precompilato. Non è una certificazione full-ASan/full-TSan dell’app. ASan è eseguito con `detect_leaks=0`: nessuna nuova affermazione sui memory leak. Le ultime aggiunte ai test sono commenti e static_assert `noexcept`, ricontrollati in compilazione e nella build finale.

La build conserva warning in sorgenti preesistenti non modificati (inizializzatori incompleti, parametri inutilizzati e indentazione); nessun errore di compilazione. Il confronto SHA256 dei file versionati conferma che sweep, solver e le altre modifiche locali già presenti sono invariati rispetto all’avvio di questo intervento.

## File e prove

Produzione: `cad_drawing_curve_recognition.h`, `kernel/fk_bspline.h/.cpp`, `kernel/fk_bspline_surface.h/.cpp`. Regressioni: `kernel/tests/test_bspline_cache.cpp` e relativo target in `kernel/CMakeLists.txt`. Catalogo aggiornato in `tools/generate_architecture.py`; HTML/JSON generati restano ignorati da Git. Documentazione e memoria condivisa aggiornate.

Le [prove conservate](cache-bspline-prove-2026-11-15/README.md) includono log e comandi. Riproduzione ordinaria dalla radice:

```sh
cmake --build forgecad-release --parallel 4
ctest --test-dir forgecad-release -R '^forgekernel_bspline_cache_tests$' --output-on-failure
```

L’eseguibile su disco è aggiornato; un’app già aperta deve essere riavviata dall’utente per usarlo. Nessuna app utente chiusa, nessun CAD originale modificato, nessun commit o push eseguito. Non effettuata una nuova prova visiva OpenGL/Wayland/macOS/Windows/CUDA. I difetti geometrici degli sweep e le incoerenze delle anteprime non sono oggetto di questa patch e restano nel piano di correzione.
