# CI del 6 novembre: raccordo sul coperchio spline

Richiesta: pubblicare il lavoro HoneyRepeat e controllare l'ultima CI fallita.

Run precedente: https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/42209135425
Commit: 318871415832b19cb68cdb917d055ca3ff0f1b27.

La compilazione è riuscita sia su Linux CPU sia su macOS CPU/Metal. Il solo
caso fallito della suite kernel è BlendPlanarCapOnTangentSplineSupports.
Riprodotto anche localmente: fixture Applicatore, facce invertite e rotazione
di 0,7 rad, raggio 0,15 mm. Il messaggio finale sul raggio nasconde il fallimento
del tentativo guidato: raccordi non coincidenti sulla cucitura del fianco.

Diagnosi: i box rifilati più stretti hanno esposto una dipendenza dalla scala
del box. Le sezioni risolte convergono (incertezza circa 1e-14 mm), ma il contatto
cerca il bordo usando la curva 3D già approssimata. Lo scarto tra i contatti è
9,35e-9 mm, superiore alla soglia esistente 8,79e-9 mm. Il bordo sorgente ha
tolleranza 4,64e-9 mm e una SP-curve ha errore 2,87e-9 mm. Ripristinare la vecchia
proiezione globale non risolve il problema.

Correzione integrata in fk_blend_surface.cpp: se i passaggi indipendenti non
coincidono, un raffinamento alternato minimizza congiuntamente lo scarto fra i
due contatti di ciascuna sezione. Gli spostamenti restano entro la risoluzione
sorgente e sul contorno entro il budget B-rep; una proposta che non riduce lo
scarto viene scartata. Il controllo finale conserva la soglia originale
1e-9 * scale. Anche convergenza, fit, regolarità degli offset e controlli
topologici conservano i budget precedenti. Il solo cambio della ricerca in UV
non risolveva il problema e non è stato integrato.

Nel riproduttore ruotato a R0,15 lo scarto massimo dei contatti scende da
9,35e-9 a 4,74e-9 mm. La regressione originale passa; aggiunta una seconda
rotazione con traslazione, con lo stesso controllo geometrico assoluto di
2e-8 mm. Tutti i 20 casi positivi e i quattro rifiuti a R0,2 passano.
Gli errori del test ora indicano modalità e raggio. Gli input restano invariati.

Prototipi e log esclusi dal repository in /tmp/forgecad-ci-20261106.
La verifica locale completa è terminata con successo; la nuova CI è ancora in corso.

Commit pubblicato su origin/main: e761f433604bf91372f3e6b37aa89eb823cdf07f.
Nuova CI: https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/42242095481

Build locale completa PASS. Mappa 624 file / 8.822 relazioni / 303 unità,
nessuna diagnostica; controllo di aggiornamento e due dettagli HTML PASS.
Verificati chiamanti del raccordo, dipendenze geometriche e regressioni,
distinguendo chiamate, include e riferimenti nei due versi.


Esiti locali conclusivi sul codice pubblicato:

- Applicazione e tutti i target della build locale ricompilati.
- Suite generale kernel: 467/467 PASS, incluso il test fallito sulla CI.
- CTest: 37/37 PASS (28 suite kernel autonome e nove UI/storia).
- Prova temporanea del nuovo calcolo dei coni con intermedi solo double:
  PASS. È un controllo numerico locale, non una compilazione su macOS.
- Diff senza errori di spaziatura; nessun documento CAD originale pubblicato.
- Il codice e761f43 è su origin/main. La CI 42242095481 ha completato
  configurazione e dipendenze ed è in compilazione su Linux e macOS.
  Nessun successo remoto ancora dichiarato.
- L'aggiornamento conclusivo di questa memoria e di CLAUDE è un commit solo
  documentale con skip CI, per conservare l'esecuzione del commit di codice.
