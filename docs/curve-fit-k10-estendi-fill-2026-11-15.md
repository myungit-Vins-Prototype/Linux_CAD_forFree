# K10 — Estendi superficie e Riempimento, 15 novembre 2026

## Esito e perimetro

Prosecuzione con tre agenti **GPT-6.1 Sol**, dopo il [primo blocco K10](curve-fit-k10-2026-11-15.md). Migrati altri tre siti del fit: i due offset planari di Estendi per sorgenti NURBS e la proiezione dei bordi di Riempimento per NURBS e rette. **K10 resta aperto** per le altre famiglie e i chiamanti ancora campionati. I 24 rilievi precedenti corretti e le 22 orfane candidate non cambiano conteggio.

Implementazione integrata; build completa, regressioni congiunte e mappa verificate. [Prove e manifest](curve-fit-k10-estendi-fill-prove-2026-11-15/README.md).

## Estendi superficie

Entrambi i rami planari, faccia singola e modello multifaccia, scelgono `fitCurveCertified` quando il provider riconosce una sorgente NURBS, anche sotto wrapper supportati. La distanza firmata è `length * traversal * orientation`: il certificatore usa la stessa normale e lo stesso verso della curva richiesta. Un controllo non conclusivo su una sorgente supportata produce errore; non torna al fit campionato. Il budget preesistente resta invariato. Le famiglie non supportate dal provider conservano il percorso precedente.

Due gruppi permanenti esercitano 12 configurazioni: otto combinazioni dei versi faccia/bordo e dei due rami in posa obliqua; quattro casi NURBS razionali di grado 27 nei due rami e alle scale 1/1000. Le verifiche `du/dv` provano che il caso multifaccia attraversa davvero il ramo `generalPlanar`. Sulla versione precedente falliscono i quattro casi avversi; sulla nuova passano tutti.

Nel caso avverso a scala 1, errore misurato da **5,416313e-5 mm a 7,149060e-7 mm**, con budget invariato circa 1,665e-6 mm: **32,5 volte il budget → 0,429 volte**. Il rapporto resta coerente a scala 1000. La griglia misura il caso di regressione; la garanzia del fit deriva dal provider continuo già verificato nel primo blocco, non dalla griglia.

## Riempimento

Il nuovo modulo `fk_curve_projection` proietta i poli NURBS nel piano dei parametri e conserva grado, nodi, pesi e parametro. Gestisce wrapper di ritaglio e trasformazioni affini, incluse trasformazioni non uniformi. Le rette diventano spline di grado 1 sullo stesso intervallo, evitando la normalizzazione che altererebbe il parametro. Sono ammesse proiezioni collassate.

L'audit indipendente ha rilevato e fatto correggere, prima della consegna, il caso `Trimmed(Line,[0,10])` richiesto sul solo intervallo `[2,4]`: la base va costruita sull'intero dominio del ritaglio, altrimenti il wrapper più ampio viene rifiutato. La regressione copre anche ritagli annidati.

Cinque gruppi verificano parametri, derivate destra/sinistra ai nodi, pesi, trasformazioni, input invalidi e cancellazione, integrazione Fill e salvataggio/lettura B-rep. Il confronto con il vecchio Fill dimostra la perdita della rappresentazione originale, non un errore geometrico fuori tolleranza su quella specifica fixture. Non viene introdotto un nuovo tipo di curva persistito. Cerchi, ellissi e curve procedurali mantengono il fit campionato esplicito: una conversione razionale delle coniche non conserverebbe automaticamente il parametro angolare.

## Residuo K10 confermato in Superficie tra curve

**Aggiornamento successivo:** il residuo descritto sotto è stato corretto per lati di rette/NURBS nel [blocco BoundarySheet](curve-fit-k10-boundary-2026-11-15.md), con controllo continuo dei bordi finali. I dati seguenti documentano lo stato precedente.

`boundarySheet` rimappa i tratti di contorno ma passa al fitter solo i confini fra i tratti, omettendo i nodi interni delle sorgenti. Due spline di grado 6 con variazione concentrata fra 0,1 e 0,101 producono una superficie esattamente planare (tutti i poli Z=0), mentre il bordo ha altezza 0,001 mm oppure 0,000952154249 mm. Con tolleranza richiesta 1e-7 mm, lo scarto geometrico è almeno **10000× / 9521×**. `checkBody` non segnala problemi. Anche la precedente fixture di grado 27 riproduce il difetto.

Un controllo razionale sui coefficienti IEEE dimostra una derivata X positiva su tutto il dominio delle due spline di grado 6: le sorgenti sono regolari. Il difetto non è dedotto solo da campioni né dalla corrispondenza dei parametri della superficie. È un'ulteriore manifestazione di K10, non un nuovo rilievo contato due volte. **Non corretto in questo blocco.** Il prossimo intervento deve conservare i nodi e verificare continuamente la composizione dei tratti; aggiungere soltanto campioni non chiude K10.

## Verifiche finali

- **Build completa Release app e runner: PASS**, incluso rebuild finale dopo la correzione del ritaglio. Gli avvisi preesistenti nei test Qt non riguardano i nuovi moduli.
- **129 casi kernel distinti / 145 esecuzioni: PASS**, filtri Offset, Shell, Sheet, BoundarySheet, Fill, Projected, Sew, Thicken e sette Blend. Fill ripetuto dopo l'ultimo rebuild; gli altri gruppi non sono interessati dal solo ritocco del helper di proiezione.
- **28/28 gruppi CTest PASS**, compresi i due nuovi runner (5+2 gruppi permanenti) e gli 11 casi del precedente fitter. I 14 gruppi Qt sono stati ripetuti dopo il link finale. Impostazioni isolate e offscreen; nessuna certificazione OpenGL.
- **Audit indipendente: 4/4 proiezioni e 8/8 offset dei fori PASS**; i tre riproduttori Boundary restano intenzionalmente classificati come difetti aperti. Due prove sui coefficienti dimostrano la regolarità delle sorgenti di grado 6.
- **ASan/UBSan/LSan mirati: 5/5 Fill e 2/2 Sheet PASS**, nessuna segnalazione. Fill strumenta helper, fill e TU dei test; Sheet strumenta sheet, TU e gli oggetti offset/bounds/framework del blocco precedente. Il resto dell'archivio non è strumentato; non è una prova completa del kernel/app.
- **Mappa 744 file / 10679 relazioni / 367 unità: PASS**, zero diagnostica; `--check`, HTML/JSON e archi in ingresso/uscita verificati, separando call/include/reference.
- **Conservazione**: confronto degli hash con i 1521 file dello snapshot iniziale e `git diff --check` PASS. I soli file esistenti modificati appartengono al perimetro dichiarato; nuovi moduli/test, rapporti e prove sono enumerati nel manifest.

L'eseguibile su disco è aggiornato; l'app eventualmente aperta deve essere riavviata per caricarlo.

## Limiti e conservazione

Dei 24 siti originari, cinque hanno ora un percorso esatto/certificato per famiglie esplicitamente supportate (due nel primo blocco, tre qui); **19 siti restano interamente da migrare**, oltre ai fallback nei cinque già trattati. La proiezione esatta non certifica globalmente la superficie Fill, né il controllo del fit prova assenza di autointersezioni o validità di tutto il B-rep. Il fit implicito restante in `fk_sheet.cpp` non cambia.

Le prove sanitizer sono mirate, non una certificazione dell'intero kernel o dell'app. Nessuna nuova verifica OpenGL/desktop. Le modifiche precedenti e i CAD originali vengono preservati; nessun commit o push richiesto.
