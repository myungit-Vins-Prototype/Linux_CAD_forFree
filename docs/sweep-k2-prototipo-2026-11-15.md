# Sweep K2 — prove e prototipo, 15 novembre 2026

> Aggiornamento successivo: **K2 è stato completato e integrato in produzione**. [Implementazione, verifiche e limiti](sweep-k2-completamento-2026-11-15.md). Il testo seguente conserva lo stato di questo intervento precedente.

**K2 resta aperto; 18 dei 19 difetti iniziali sono corretti.** Questo intervento, con due agenti GPT-6.1 Sol, aggiunge prove dirette e un prototipo riproducibile. Non modifica il software di produzione: la verifica dell'inviluppo convesso respinge anche due sweep validi e non è ancora collegata da una prova al B-rep finale.

## Riscontri sul kernel attuale

Il riproduttore originale continua a restituire `checks=0`, volume **19,7417086214508** e box contenitore **12,6**. Sono stati aggiunti tre riscontri sulle superfici effettivamente memorizzate, non soltanto sul percorso ideale:

| Caso | Riscontro |
| --- | --- |
| Autoattraversamento di una sola faccia | Due punti interni della medesima superficie, con parametri lontani lungo l'elica, coincidono entro **3,04e-16**. |
| Intersezione fra testa e lato | Un punto della faccia laterale cade nel piano della testa entro **9,63e-17**, a distanza **0,400013** dal centro del disco di raggio **0,5**. |
| Intersezione fra regioni diverse | Punti interni di facce appartenenti a due regioni inizialmente disgiunte coincidono entro **2,22e-16**, dopo giri diversi del percorso. |

Newton/bisezione e verifica dei parametri producono testimoni numerici riproducibili. Non vengono presentati come una prova interval-Newton. Parametri, sorgenti e log sono nelle [prove](sweep-k2-prove-2026-11-15/README.md).

## Prototipo verificato, non promosso

Il core considera la mappa razionale affine nella sezione `F(x,y,t)=C(t)+U(t)x+V(t)y`. Usa inviluppi Bernstein, aritmetica intervallare verso l'esterno, separazione spaziale e un criterio sufficiente di iniettività locale. Controlla anche coppie nella stessa pezza, giunti e cuciture chiuse; non ignora una fascia di adiacenza scelta arbitrariamente. La suddivisione ha limiti di profondità/visite e propaga la cancellazione.

Il risultato positivo riguarda **solo la mappa e l'inviluppo forniti**. Il prototipo restituisce `Unresolved` quando non riesce a concludere; non dichiara provata una collisione soltanto perché due inviluppi si sovrappongono. Il limite di visite riguarda la ricerca, non è un tetto assoluto alla memoria allocata nella preparazione dei dati/BVH.

L'adattatore sperimentale raccoglie traslazioni e rotazioni razionali e aggiunge tre righe di base al fit comune quintico. Canonicalizza solo giunti genealogicamente consecutivi, entro budget fissati prima del controllo. Queste operazioni **non sono un certificato del rapporto con le superfici finali**, né dell'inclusione esatta del profilo in ogni inviluppo ottenuto con arrotondamenti. Per questo l'adattatore rimane nel pacchetto sperimentale.

## Due casi validi che impediscono l'integrazione

1. **Lamina piana da una parabola.** Il carrier esatto è `F(u,v)=(2u-1,0,v-0.4u(1-u))`, con `u` fra 0 e 1 e `v` fra 0 e 2. È iniettivo e ha area 4. Il suo profilo curvo occupa un dominio unidimensionale; sostituirlo con l'inviluppo pieno bidimensionale produce una mappa volumetrica degenere. Il prototipo rifiuta la lamina valida.
2. **Due tubi elicoidali intercalati.** Raggio percorso 4, passo 0,5, dischi di raggio 0,1 con centri di sezione a ±0,375. Per la geometria analitica nominale, la distanza fra le due curve dei centri è maggiore di 0,2495; i tubi restano separati di oltre 0,0495. L'inviluppo comune aggiunge materiale assente e si sovrappone. La dimostrazione nominale non viene trasferita automaticamente al fit B-rep; è comunque un controesempio all'uso dell'inviluppo come dominio reale.

Un segmento toroidale quasi chiuso, con teste vicine, è invece correttamente riconosciuto valido dal prototipo. La vicinanza delle teste da sola non prova un'intersezione.

## Audit numerico e versioni

Durante l'audit è emerso anche un errore **nel prototipo preliminare**: un triangolo con coordinate 1e-200 poteva essere ridotto a segmento perché il predicato di orientamento era usato fuori dal suo contratto rispetto all'underflow. La v2 elimina quella deduzione: riduce la dimensione solo per uno/due punti distinti; tre punti restano trattati in dimensione 2. Può essere più conservativa per input collineari non minimali, ma non inventa una dimensione inferiore dal risultato arrotondato.

La regressione include scale 1, 1e-200, 1e-300 e un insieme con ampia escursione di esponenti, distinguendo triangoli degeneri nella mappa da segmenti validi. Altre prove coprono pesi molto piccoli, NaN/overflow, budget esaurito, cancellazione e un piccolo anello percorso due volte. Questo audit mirato non è una verifica formale dell'intero modulo sperimentale.

## Verifiche effettivamente eseguite

- **19/19** test sweep sul runner di produzione corrente: **2.77s** nella riproduzione finale.
- **5/5** test diretti sul core v2, ricompilato dai sorgenti conservati.
- **23/25** test nell'integrazione sperimentale: **28.37s**. Passano tutti i 19 preesistenti e 4 nuovi gruppi; falliscono esattamente i due casi validi sopra. I tempi si riferiscono a batterie diverse, non sono un confronto di velocità dell'app.
- Riproduttore originale, tre testimoni numerici sui carrier e due controesempi validi riprodotti dalla procedura completa; compilazione degli esperimenti senza diagnostica.
- Mappa architetturale consultata prima degli esperimenti; controllo finale **724 file / 10429 relazioni PASS**. Nessun nuovo modulo di produzione richiede rigenerazione.
- Audit degli hash: 1037 file versionati confrontati con l’inizio dell’intervento, nessun sorgente di produzione cambiato; libreria kernel SHA256 invariata. Nessuna nuova build dell'app, prova UI/OpenGL o esecuzione dell'intera suite è stata fatta in questo intervento.

Il pacchetto conserva sorgenti come testo, patch da applicare solo nella directory temporanea, procedura eseguita da capo, log e hash. Non sostituisce l'app né modifica CAD originali. Le correzioni precedenti restano conservate.

## Lavoro necessario per chiudere K2

Il passo successivo deve trattare il **dominio reale**: componenti disgiunte, curve delle lamine, regioni e fori, evitando il riempimento artificiale dell'inviluppo. Occorre inoltre verificare i carrier finali direttamente oppure costruire un legame quantitativo con la mappa certificata, includendo derivate e connessioni tolleranti. Un solo scarto C0 non preserva l'iniettività: piccole oscillazioni possono creare pieghe.

L'assenza di collisioni trovate e un controllo topologico positivo continuano a non certificare la validità globale. Non è stato aggiunto un filtro euristico alla produzione né dichiarato chiuso K2.
