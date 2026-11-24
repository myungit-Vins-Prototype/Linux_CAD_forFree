# Superfici_Riv_Tut.prt — Spessore e Cucitura

## Spessore interno di 2 mm con raccordo R2

La faccia 35 indicata nel primo errore appartiene a una fascia chiusa di 16 facce NURBS di raccordo (indici kernel 34–49). A 2 mm nel verso interno le due linee di tangenza di ogni pezza raggiungono la stessa curva. Le 16 facce originali restano sulla pelle esterna. Sul lato interno il kernel costruisce le facce superstiti e unisce esplicitamente le coppie di bordi che delimitavano le pezze collassate.

Il riconoscimento controlla i coefficienti Bézier sull'intera fascia, compreso il verso della normale. La famiglia gestita comprende fasce interne rettangolari con sezioni razionali quadratiche trasversali e un appoggio planare. Il recupero richiede un collasso dimostrabile e un solido valido.

Sul documento congelato con SHA-256 `83a11014cdcd1c8f883886584c9c2dbd12b665d800d4e596f1f71b9bb0989a58` e sulla versione salvata con Spessore 1 a 1,95 mm (`a56d9438de0f844a4333455f705dec84da355c5d473342f784ff01b268da3a34`), Raccordo 15 ha lo stesso B-rep. La build Linux aggiornata a 2 mm produce un solido di 108 facce in un guscio, zero bordi liberi e zero anomalie nei controlli B-rep e di intersezione dei contorni. Volume 64.092,912135 mm³. A 1,99999 mm restano entrambe le pelli del raccordo, con un volume di 64.092,605757 mm³.

**Controlli finali.** Le 16 giunzioni sono spigoli vivi interni. Ciascuna delle 16 facce del raccordo ha un discendente sulla pelle esterna e le altre 36 facce ne hanno due. Il B-rep ottenuto dallo snapshot coincide byte per byte con quello ottenuto dopo aver rigenerato Raccordo 15 su entrambe le versioni del file (SHA-256 `3c9e00fd524c560f494bba6137b54af7bf64c127eb33f7abee1989719e1bc02d`). La copia nativa `Superfici_Riv_Tut_interno_2mm.prt` conserva la funzione Spessore 1 esistente, con valore 2 mm e verso interno. La riapertura e il ricalcolo della funzione restituiscono la stessa geometria. Prima della rifilatura descritta sotto, 2,01 mm produceva un errore di curvatura sulla faccia 35.

Otto test del gruppo Spessore superati, compresi un raccordo toroidale NURBS con volume noto al collasso e il suo controesempio con un rigonfiamento interno e gli stessi bordi. Superati anche i quattro gruppi CTest su cucitura delle curve parametriche, spessore planare, scambio e offset aperto, inclusi entrambi i casi Shower Head.

## Spessore interno oltre il raggio R2

Il documento con SHA-256 `eb9f77ae6531e4a4010c07a8e3ece0ccc55e131c0f15bf9b3bc256eb4cfe96dc` contiene Raccordo 15 e Spessore 2. Il kernel ricava la distanza critica dalla geometria della fascia e verifica le pezze lungo tutto il loro dominio. Quando il lato interno supera il raggio, interseca gli appoggi superstiti con il piano traslato, conserva la regione con il bordo esterno originale e chiude il piano usando le curve della nuova sezione. A 3 e 4 mm il primo fit della pelle interna lasciava bordi scollegati dalle pareti. Dopo una cucitura fallita il kernel ritenta con un fit più preciso. Il budget esplicito dell’operatore resta vincolante.

| Spessore interno (mm) | Volume del solido (mm³) |
| ---: | ---: |
| 2,00001 | 64.093,241548 |
| 2,01 | 64.399,223093 |
| 2,1 | 67.149,542565 |
| 2,5 | 79.232,903941 |
| 3 | 94.016,401868 |
| 4 | 122.522,172694 |

A tutte le quote della tabella la costruzione produce un solido di 108 facce in un guscio, con zero bordi liberi e zero anomalie B-rep rilevate. Le 52 superfici esterne mantengono la loro parametrizzazione. Le 16 facce del raccordo hanno un discendente esterno, ciascuna delle restanti 36 ne ha due. La nuova giunzione interna ha otto bordi. A 2,5 mm la deviazione misurata dal piano è al massimo 1,42 × 10⁻¹⁴ mm e il residuo rispetto all’offset teorico del lato, campionato su cinque punti per bordo, è al massimo 2,05 × 10⁻⁵ mm. A ciascuna quota il risultato ottenuto dopo aver rigenerato Raccordo 15 coincide byte per byte con quello ottenuto dal B-rep salvato. La copia nativa a 2,5 mm conserva Spessore 2, con UUID distinto e SHA-256 `18038d679f607c95555010056ad2cbaf12a30a9c93d57da1fb68308b34a789a4`. La riapertura conserva la geometria e la rigenerazione riproduce il B-rep iniziale byte per byte. La serializzazione rinormalizza quattro direzioni di rette, con uno scarto di un’unità di arrotondamento su dodici coordinate.

Il recupero richiede una fascia interna chiusa e certificabile con un appoggio piano a contorno singolo. Il taglio deve preservare il contorno esterno e le facce estranee alla fascia. I test comprendono il tubo raccordato con volume di riferimento e il rifiuto di una sezione che oltrepassa il bordo aperto.

Anche il comando Offset con *Mantieni la cucitura* recupera la fascia quando si seleziona tutta la lamina. Su Raccordo 15 a −2, −2,01, −2,5, −3 e −4 mm, con precisione del documento di 10⁻⁷ mm, restituisce una lamina di 36 facce, un guscio, i 20 bordi liberi dell’apertura originale e zero anomalie B-rep. Il test di regressione copre anche la selezione parziale e la richiesta di facce separate.

## Indicazione del verso nella viewport

Durante Offset e Spessore la freccia segue la normale orientata della faccia o il riferimento di direzione dello Spessore. Invertendo il lato si inverte la freccia. Con *Metà per parte* appaiono due frecce. Lungo la direzione di vista il cerchio con il punto indica un verso verso l’osservatore, quello con la croce indica il verso opposto.

Nella build Linux Release passano gli 11 test mirati del kernel su Spessore e Offset e i due test CAD dei comandi. Passano anche i sette casi di offset aperto, inclusi i due Shower Head, e le regressioni di cucitura delle curve parametriche, spessore planare e scambio. Verificata l’anteprima di Spessore a 2,5 mm nella viewport Wayland sul documento di prova.

## Prima verifica del modello

La prima verifica, precedente al recupero del raccordo a raggio zero, usa il documento con SHA-256 `63df85da4d5056051b2cb3ff33a3d09e01f611ddf7d7c838928532d37f1f0f05`. Precisione documento 1e-7 mm. La storia termina con Superficie tra curve 4. Le prove aggiungono Spessore e Cucitura a una copia del documento.

## Spessore di 2 mm su Raccordo 15 prima del recupero

| Lato rispetto alla normale | Risultato sull'intera superficie |
| --- | --- |
| Nel verso della direzione | Un solido, 124 facce. |
| Metà per parte | Un solido, 124 facce, con 1 mm per lato. |
| Nel verso opposto | Il raggio di curvatura locale raggiunge 2 mm sulla faccia 35. L'offset collassa. |

Nei due casi riusciti il controllo B-rep passa. Nel terzo caso un campione interno alla faccia rifilata conferma il raggio, entro l'arrotondamento numerico. È stato provato anche il verso opposto modificando Raccordo 14 da R2 a R3 sulla copia di lavoro. Dopo la rigenerazione di Raccordo 15, lo Spessore a 2 mm produce un guscio solido con 124 facce, zero bordi liberi e zero problemi al controllo geometrico e topologico.

La copia nativa `Superfici_Riv_Tut_2mm_simmetrico.prt` ha un nuovo UUID e conserva la costruzione fino a Raccordo 15, seguita da Spessore 2 mm. Il B-rep riaperto e quello rigenerato dalla definizione di Spessore coincidono con quello verificato. SHA-256 della copia `ef30fddb81ccc5ae0c54238d1b5a0ad684070ab3649102adeeb87d5eaaf255df`.

## Gli otto bordi

La cucitura di Raccordo 15, Offset 16 e Superficie tra curve 1–4 riproduce gli otto bordi alla tolleranza di 0,00001 mm. Tutti appartengono a Offset 16. L'offset salvato è di −1 mm e la sua selezione di 50 facce ne esclude due di Raccordo 15 (indici kernel 21 e 32). Restano due aperture quadrilatere speculari, centrate approssimativamente a X = −18 mm e Y = ±13,5 mm, con larghezza alle estremità di circa 0,011 e 0,028 mm.

La selezione **Tutte le facce** in Offset 16 produce 52 facce. Le quattro superfici di chiusura e la superficie d'origine completano così un guscio con zero bordi liberi. Nella versione iniziale la successiva richiesta **Crea un solido** falliva nella quadratura per l'orientamento del guscio su Superficie tra curve 3.

## Correzione della quadratura

L'integrale di Green sul bordo UV rettilineo attraversava un nodo interno della superficie NURBS senza suddivisione della campata di integrazione. Nella verifica fine appariva inoltre una campata fra 1 e il numero double successivo, senza punto medio rappresentabile. Il suo peso va valutato nel budget dell'intero intervallo dell'integrale interno. La correzione integra anche questa campata. Un errore superiore al budget disponibile continua a produrre una diagnosi.

La prima regressione riproduce una piega interna su un supporto con quattro bordi rettilinei. L'area di riferimento indipendente è 1,135306558113121, provata in entrambi i versi della faccia. La seconda copre un intervallo di un solo ULP sia come contributo trascurabile all'intero integrale sia come contributo determinante.

## Verifica nella build Linux Release

Rigenerati Offset 16 a −1 mm con tutte le facce, Schizzo 3D 1 e Superficie tra curve 1–4. La Cucitura con Raccordo 15 produce un solido di 108 facce in un guscio, zero bordi liberi e zero problemi nei controlli geometrici, topologici e di intersezione dei contorni. Volume 32.752,802436772 mm³ a tolleranza relativa 10⁻¹². Durante la rigenerazione Superficie tra curve 4 segnala il recupero per ID di un riferimento denominato. Il controllo finale passa anche con queste superfici rigenerate.

La copia simmetrica riaperta con la build finale presenta un unico corpo visibile, 124 facce in un guscio, zero bordi liberi e B-rep identico dopo il ricalcolo di Spessore. Verificato anche il verso opposto con Raccordo 14 a R3. Hash del documento sorgente riconfermato.

Build dell’applicazione completata. Sei CTest mirati superati (offset aperto, scambio, isolinee NURBS, spessore planare, cucitura e quadratura), oltre a 11 test del gruppo Mass nel runner con OCCT. I casi di quadratura includono 15 regressioni. La vista architetturale aggiornata comprende 812 file e 11.993 relazioni, con zero unità in diagnostica e controllo di coerenza superato. Le nuove suddivisioni ai nodi della superficie coprono rette UV e campate di spline lineari non razionali.
