# Verifica GUI e visualizzazione 3D — 2 novembre 2026

**Esito conclusivo build4:** le sei prove grafiche sul runner finale completamente ricompilato passano su Wayland e XCB.
I dettagli e l'identificazione dell'eseguibile sono nell'ultima sezione; le sezioni precedenti conservano gli esiti
delle sonde intermedie e delle prove differenziali.

## Correzioni implementate

- **Picking prima del repaint (P2).** La cache degli identificativi GPU dipendeva soltanto dal seriale dell'ultimo
  frame disegnato. Fra un cambio di visibilita'/camera e il repaint differito di Qt era quindi possibile selezionare
  un corpo gia' nascosto o la sua vecchia proiezione. Una sonda reale conferma anche che `pickSceneObject(..., false)`
  propagava la selezione errata. La chiave include ora frame, matrici della camera, corpi effettivamente ammessi,
  identita' della mesh e trasformazioni delle istanze. Il callback usa le stesse matrici e lista di corpi della chiave.
  Il test copre nascondi/mostra, filtro dei corpi, pan e modifica della trasformazione di un'istanza senza nuovo frame.
- **Cursore pan durante auto-repeat (P3).** Premere a lungo Spazio durante il trascinamento cambiava il cursore da
  mano chiusa a mano aperta. Il gestore del tasto conserva ora la mano chiusa quando il pan e' gia' attivo. Il test
  copre anche ShortcutOverride, spostamento della camera, rilascio del tasto/mouse e perdita del focus.
- **T1, palette del runner.** Rimossa l'attesa del precedente accento arancione. Il controllo verifica ancora il
  contrasto tra testo e sfondo della selezione; la nuova regressione esercita campi e selezioni nei temi chiaro/scuro.
- **T2, directory storyboard.** Le due catture di `sketchStoryRegressions` usano la directory `QTemporaryDir`
  gia' posseduta dal test, anziche' una cartella fissa che doveva essere creata esternamente.

File funzionali: `forgeCad2026_viewport_interaction.cpp`, `forgeCad2026_viewport_selection.cpp`,
`forgeCad2026_gpu_picker.{h,cpp}`. Regressioni: `tests/test_gui_3d_review.h`, con aggancio minimo in
`tests/test_viewport.cpp`. Lo stesso runner contiene l'aggancio dell'agente storyboard (`--storyboard-review`).

## Verifiche effettive dell'agente GUI

- Sonda dedicata `/tmp/gui_review_probe`: nuova testata di regressione collegata agli oggetti della build Linux
  esistente; ricompilati i tre moduli GUI interessati. **Non e' una nuova build completa dell'applicazione.**
- Offscreen: palette e pan **PASS**. Sul gestore originale il controllo dell'auto-repeat fallisce; con la correzione
  passa. L'offscreen non attesta il rendering.
- Wayland e XCB reali: regressione completa **PASS** su entrambi. OpenGL Core Mesa 4.6, Qt 6.12, Intel UHD Graphics
  P630. Rendering e picking senza errori GL; verifica dei tre framebuffer e dello shader del vetro.
- Due reparent fra finestre temporanee distruggono e ricreano davvero il contesto. Il test osserva
  `aboutToBeDestroyed`, verifica l'azzeramento di shader/framebuffer e la loro ricreazione, conserva i pannelli e
  riprova il picking. Il precedente rilascio `GlassCompositor` (B6) passa questa verifica su entrambi i backend.
- Prova differenziale del nuovo test: collegando `selection` e `gpu_picker` originali, fallimento atteso su
  «corpo nascosto non selezionabile anche prima del repaint»; con i moduli corretti **PASS**.
- Tre immagini della sonda Wayland in `/tmp/forgecad-gui-review-qa/`: iniziale e due ricreazioni. Ispezionate
  visivamente la prima e l'ultima: corpo ombreggiato e vetro presenti; nessuna scena vuota dopo il cambio di contesto.
  Le due immagini successive sono identiche fra loro; il bordo iniziale evidenziato differisce coerentemente dal
  bordo dopo le prove di selezione. Questi file sono diagnostici temporanei, non asset del progetto.
- Controllo whitespace dei cinque file modificati rispetto al checkout originale: nessun errore segnalato.

## Ripetizione dopo la build completa

```sh
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= ./forgecad-release/forgecad_view_tests --gui-3d-review
QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME= ./forgecad-release/forgecad_view_tests --gui-3d-review --gl
QT_QPA_PLATFORM=xcb QT_QPA_PLATFORMTHEME= ./forgecad-release/forgecad_view_tests --gui-3d-review --gl
QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= ./forgecad-release/forgecad_view_tests --sketch-story
```

Per salvare le catture del test GL, impostare `FORGECAD_GUI_REVIEW_QA_DIR` su una directory diagnostica.
Build completa, runner generali dopo T1/T2, CMake e mappa architetturale sono verifiche centralizzate dal coordinatore.
Nessuna prova macOS eseguita. Nessun documento originale o processo dell'utente modificato; nessun commit/push.

## Nuova estrusione «Fino al prossimo corpo» — parte GUI

Su ulteriore richiesta dell'utente, il pannello `extrusionDialog` espone la fine `extent=4` in coda alle quattro
opzioni precedenti, conservandone gli indici. La nuova modalita' mostra il verso Positivo/Invertito, senza distanza
o selettore di riferimento. Richiede un verso e un solido: simmetrica, due versi e sola superficie sono disabilitati.
La vecchia illustrazione della distanza e' nascosta soltanto in questa modalita', rendendo la direzione subito visibile
anche nel pannello compatto. Il core geometrico, la selezione dei corpi limite e le dipendenze sono dell'agente storyboard.

Regressione dedicata in `tests/test_extrusion_next_body_gui.h`, CLI `--extrusion-next-body-gui [--gl]`:

- Pannello reale: opzione appesa, campi/alternative corretti, nessun picker; prova grafica della direzione dentro
  l'area effettivamente visibile dello scroll.
- Profilo quadrato 2×2 a Z=0 fra due corpi limite: anteprima positiva Z=0..4 e invertita Z=-5..0, controllate sui
  vertici B-rep. Conferma reale dell'estrusione invertita, un solo Undo e Redo con B-rep identico byte per byte.
- Scena senza corpi limite: errore esplicito sul corpo assente sia nell'anteprima sia in conferma, nessuna richiesta
  di scegliere un riferimento e nessuna modifica dopo Annulla.
- **PASS offscreen, Wayland e XCB**, con la sonda `/tmp/extrusion_gui_review_probe`: oggetti mirati del dialogo e
  cinque oggetti core appena compilati dall'agente storyboard, collegati agli altri oggetti del runner esistente.
  Nessun errore GL nelle prove grafiche. Ispezionati pannello e geometria in `/tmp/forgecad-next-body-gui-qa/`:
  direzione visibile, anteprima ambra arrestata sul corpo inferiore e corpi limite conservati nella scena.

Modifiche GUI: `forgeCad2026_dialogs_solids.cpp`; aggancio minimo nel runner. Sintassi del dialogo, della nuova
testata e dell'intero `tests/test_viewport.cpp` verificata prima dell'ultima integrazione dei test del coordinatore.
La build completa finale e le prove geometriche piu' ampie restano centralizzate; questi esiti non le sostituiscono.

## R4 — verifica dei frontend modello e tavola

`tests/test_revision_access_gui.h` (`--revision-access-gui`) esercita le guardie del coordinatore nei veri frontend:
editor e lettore di un `.prt`, editor e lettore di un `.drw`, tutti in un workspace temporaneo.
Un catalogo della generazione precedente sotto `catalog.lock` simula la pubblicazione interrotta fra catalogo e head.

Con i nuovi oggetti del runner, sonda `/tmp/revision_access_gui_probe` **PASS offscreen**: entrambi gli editor
conservano le modifiche consentite e l'esclusiva effettiva; i lettori restano protetti. Le acquisizioni concorrenti dei
lock falliscono durante lo stato transitorio. Dopo il ripristino del catalogo gli editor restano operativi. Una
corruzione persistente del catalogo porta invece modello e tavola in sola lettura, impedisce le modifiche e rilascia i
due lock, verificati tramite acquisizione indipendente. Nessun sorgente di produzione R4 modificato dall'agente GUI.
Prova differenziale: due copie in `/tmp` rimuovono una sola guardia `temporarilyUnavailable` alla volta, mantenendo il
nuovo store/client. Senza la guardia modello il test fallisce sulla conservazione del lock e della modifica; senza
quella tavola fallisce la modifica locale con «Archivio temporaneamente occupato». Con entrambe le guardie **PASS**.

## Precisazione utente: spigoli dei raccordi selezionati «da soli» e corpi nascosti

La nuova indagine interpreta «raggi» come raccordi. La selezione deliberata di una faccia che aggiunge tutti i suoi
bordi e' dichiarata nell'interfaccia ed e' conservata; il test la verifica esplicitamente. Sono stati invece riprodotti
e corretti due difetti distinti.

1. **P2 — Clic su un corpo opaco anteriore seleziona una faccia del corpo posteriore.** Con uno spigolo gia' scelto
   sul corpo posteriore, `edgePickFaceBody` restituiva sempre quel corpo, anche se il clic cadeva al centro della
   faccia di un altro corpo davanti. La sonda trova `pickSceneObject=1`, nessuno spigolo vicino al clic e
   `edgePickFaceBody=0`: un clic porta gli spigoli scelti da uno a quattro, tutti sul corpo dietro. Ora la scelta della
   faccia rispetta l'occlusione, come quella del singolo spigolo. L'opzione X-ray esplicita continua a permettere la
   selezione attraverso i corpi.
2. **P3 — Evidenziazione residua di uno spigolo da una lista non piu' visibile.** Nel vero pannello Raccordo,
   selezionare una voce Bordi e passare alla scheda Tra due facce nascondeva la lista, ma lasciava il suo riferimento
   evidenziato nella scena. La sonda conferma lista invisibile e highlight ancora attivo con un riferimento.
   `GeometryReferenceList::hideEvent` rimuove ora quella sola evidenziazione; i bordi della definizione restano invariati.

Per non impedire la modifica di un raccordo la cui base e' stata consumata, rendering e picking condividono ora
`isBodyVisibleInScene`: considera risultato nascosto dall'anteprima, base forzatamente mostrata, dipendenti, operandi
ripristinati e filtro dei corpi. Il predicato e' estratto dalle condizioni gia' usate dal rendering e non cambia i flag
di visibilita' persistenti. Anche la sagoma degli oggetti selezionati rispetta la scena dell'anteprima, evitando di
disegnare la selezione di un risultato momentaneamente nascosto sopra la base.

File di questa precisazione: `forgeCad2026_viewport.h`, `forgeCad2026_viewport_scene.cpp`,
`forgeCad2026_viewport_selection.cpp`, `forgeCad2026_viewport_render.cpp`, `forgeCad2026_dialogs_common.{h,cpp}`,
nuovo `tests/test_fillet_selection_review.h` e aggancio in `tests/test_viewport.cpp`.

**Verifiche effettuate:** sonda `/tmp/fillet_selection_review_probe`, collegata agli oggetti del runner esistente
ricompilando i quattro moduli interessati. `--fillet-selection-review [--gl]` **PASS offscreen, Wayland e XCB**;
assenza di errori GL nella scena modificata. La regressione esercita faccia preselezionata, occlusione, X-ray, base
nascosta/risultato visibile in modifica, cambio schede, Esc del riferimento, Esc del comando, Annulla con richiesta di
anteprima pendente, chiusura finestra e conferma reale con Undo/Redo. Dopo ogni chiusura verifica bordi, timer/callback,
evidenziazioni e scena finale ripristinati, con visibilita' persistente invariata. Ispezionata anche la cattura
`/tmp/forgecad-fillet-selection-qa/fillet-edit-base.png`.

La prova differenziale fallisce sulla selezione dietro il corpo opaco collegando il vecchio picker; mantenendo il
picker corretto e togliendo solo la pulizia Hide fallisce sull'evidenziazione della lista nascosta. Con entrambi i fix
passa. Sul runner con i quattro moduli aggiornati passano inoltre le suite esistenti `--fillet-refinements` e
`--advanced-fillets` su XCB, compresi percorsi variabili, marker, persistenza e cancellazione.

In questi percorsi non resta un corpo finale nascosto dopo Annulla/Esc/chiusura; le anomalie di consumo della storia
sono verificate separatamente dall'agente storyboard. Non si attribuisce a questi due casi la spiegazione universale
di ogni comportamento riferito dall'utente. Nessuna modifica kernel, geometria persistente o protocollo revisioni.

## Checkpoint precedente sul runner completo — mtime 16:38:05 CET

Eseguite sequenzialmente le sei prove richieste usando esclusivamente
`/home/myungit/Linux_CAD_forFree/forgecad-release/forgecad_view_tests`, dopo la build completa finale comunicata dal
coordinatore. Non sono le sonde con collegamento parziale descritte sopra.

Identificazione del runner verificato: 98.008.120 byte; mtime 2 novembre 2026, 16:38:05 CET;
SHA-256 `b726e1f4e4801fa92dddc93149c54707857442279bda4f97a301699c985100e0`.

| Prova del runner finale | Wayland | XCB |
| --- | --- | --- |
| `--gui-3d-review --gl` | PASS | PASS |
| `--fillet-selection-review --gl` | PASS | PASS |
| `--extrusion-next-body-gui --gl` | PASS | PASS |

Tutti i processi terminano con codice 0; nessun timeout, skip o regressione rilevato in queste sei esecuzioni.
I controlli includono picking prima del repaint, due ricreazioni reali del contesto GL per backend, occlusione e
pulizia dei raccordi, tutti i modi di chiusura del pannello, anteprime esatte nei due versi e Undo/Redo della nuova
estrusione. I controlli GL del runner non rilevano errori.

Il runner finale riporta due percorsi grafici distinti:

- Wayland: Intel UHD Graphics P630, OpenGL 4.6 Core, Mesa 26.2.5-arch3.1.
- XCB: Quadro RTX 3000, OpenGL 3.3.0, NVIDIA 615.78.08.

Ambiente di configurazione isolato in `/tmp/forgecad-final-gui-review/settings`, temi di piattaforma disabilitati
solo nei processi di test, fixture e recovery nelle directory temporanee proprie del runner. Log completi nei due
rami `/tmp/forgecad-final-gui-review/wayland/` e `/tmp/forgecad-final-gui-review/xcb/`:
`gui-3d.log`, `fillet-selection.log` ed `extrusion-next-body.log`. Dodici PNG diagnostici sono conservati nei
sottopercorsi `gui-3d/`, `fillet/` e `next-body/` dei rispettivi backend.

Ispezionate sul runner finale le scene dopo la seconda ricreazione GL di entrambi i backend, il pannello compatto
Wayland e l'anteprima dell'estrusione XCB: corpo e vetro presenti, Direzione immediatamente visibile, anteprima ambra
arrestata sul corpo inferiore e corpi limite ancora visibili. Nessun documento originale, processo dell'utente o
sorgente modificato durante questa verifica. Il solo rapporto e' stato aggiornato; CTest offscreen e vista
architetturale finale restano verifiche del coordinatore, non attribuite a queste esecuzioni grafiche.

## Verifica conclusiva build4 — mtime 16:46:48 CET

Dopo l'ultima ricompilazione completa sono state ripetute, una alla volta, tutte e sei le prove reali sullo stesso
percorso `/home/myungit/Linux_CAD_forFree/forgecad-release/forgecad_view_tests`. Le variazioni comunicate dal
coordinatore rispetto al checkpoint precedente riguardano la conservazione degli ID dei componenti `DeleteFace>0`
e la fixture della storia dei corpi con Nascondi esplicito; nessun header o modulo GUI e' cambiato.

Runner build4 verificato: 98.008.120 byte; mtime 2 novembre 2026, 16:46:48 CET;
SHA-256 `6c7a2fc67660ca46ee58408e210dd9f164fc25da04f56638bc9ce6ed94e0dad9`.

| Prova sulla build4 finale | Wayland | XCB |
| --- | --- | --- |
| `--gui-3d-review --gl` | PASS | PASS |
| `--fillet-selection-review --gl` | PASS | PASS |
| `--extrusion-next-body-gui --gl` | PASS | PASS |

**6/6 PASS**, tutti con codice 0, senza timeout, skip, errori GL o regressioni rilevate. Confermati picking prima del
repaint e due ricreazioni del contesto per backend, selezioni/chiusure del raccordo e Undo/Redo, pannello e anteprime
dell'estrusione nei due versi, conferma e gestione del corpo limite assente.

Percorsi grafici nuovamente riportati dal runner: Wayland Intel UHD P630/OpenGL 4.6 Core/Mesa 26.2.5-arch3.1;
XCB Quadro RTX 3000/OpenGL 3.3.0/NVIDIA 615.78.08.

Questa ripetizione ha configurazione isolata in `/tmp/forgecad-final-gui-review/build4/settings` e diagnostica nei
rami `build4/wayland/` e `build4/xcb/`: sei log (`gui-3d.log`, `fillet-selection.log`, `extrusion-next-body.log` per
backend) e dodici PNG nei sottopercorsi `gui-3d/`, `fillet/` e `next-body/`. Ispezionati anche la scena Wayland dopo la
seconda ricreazione e il pannello compatto XCB: corpo/vetro presenti e Direzione, Conferma e Annulla visibili.

Il precedente 6/6 resta un checkpoint riferito al suo hash; l'esito conclusivo e' questo della build4. Nessuna nuova
ricerca o modifica di sorgenti, documenti originali o processi dell'utente durante la ripetizione: aggiornato soltanto
questo rapporto. Le 152 prove CTest finali sono eseguite dal coordinatore e non sono conteggiate qui.
