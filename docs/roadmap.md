# Roadmap generale ForgeCAD

Riesame del 30 ottobre 2026, dopo l'unificazione su `main`. Questo indice raccoglie lo stato corrente; i rapporti datati conservano misure, limiti e verifiche dei rispettivi checkpoint. Il registro condiviso [CLAUDE.md](../CLAUDE.md) contiene gli esiti effettivi e distingue implementazione, verifica ed esperimenti. Le priorità successive sono proposte modificabili durante lo sviluppo.

## Elica su una faccia di rivoluzione

Seleziona la faccia nella vista e apri **Elica e spirale**, oppure
sceglila dalla vista durante il comando. Per una fascia completa di
rivoluzione, il raggio dell’elica segue il meridiano alla quota
richiesta dal passo assiale. L’altezza si ricava dai due anelli
in tutti i modi di definizione. **Altezza e giri** conserva i giri
inseriti e ricava il passo. **Passo e giri** e **Altezza e passo**
conservano il passo inserito e ricavano i giri. Angolo iniziale,
senso e inversione dell’asse restano selezionabili. La curva
parte da un anello e l’asse invertito la fa partire dall’altro.
Il riferimento è associativo e usa i campi già previsti dal
formato .prt. La faccia richiede due anelli circolari completi e
un meridiano con quota strettamente monotona.
Il kernel approssima la traiettoria con una B-spline.

Verificata F1 di Senza nome1 con passo 2 mm, 7 giri e altezza
14 mm. Raggio da 1,4994 a 2,2927 mm e scarto massimo campionato
dal supporto di 2,86 × 10⁻⁹ mm su 297 punti. Tutti i modi
usano i 14 mm della faccia anche quando la definizione contiene
un’altezza diversa. Con 4 giri si ottiene un passo di 3,5 mm
anche in asse invertito. Con passo 4 mm si ottengono 3,5 giri.
Il pannello Qt mostra l’altezza automatica e rende modificabile
il solo parametro indipendente fra passo e giri. Il .prt di
esempio è stato salvato e riaperto con rigenerazione e
riferimento associativo a F1.
Build Release completa. Superate sei prove mirate su eliche e
sweep, inclusa la regressione su raggio variabile, passo e versi.

## Review del codice — stato del 20 novembre 2026

L’[elenco consolidato dei punti aperti](review-punti-aperti-2026-11-17.md) distingue K10, uniformazione di pannelli e anteprime e verifiche mancanti. UI11 del menu cache e overflow e PRE9 del pannello Proiezione sono corretti. I rilievi funzionali corretti sono 35. In K10 sono verificati il recupero dei prodotti subnormali, il rifiuto all’esaurimento della profondità di quadratura e il controllo continuo del wrap per target B-spline anche periodici, a gradi misti e a pesi variabili, nativi o istanziati, entro i limiti di calcolo, UV separabili od oblique e sorgente piana a quota affine anche attraverso zero. Il wrap rifiuta il solido inclinato e la lamina con il rilievo perso, anche su un’estrusione con generatrice B-spline nativa, ritagliata o trasformata, aperta o chiusa. [Prove sulla riflessione e sulle trasformazioni annidate](review-generale-2026-11-20.md#k10--estrusione-con-generatrice-trasformata). Il caso chiuso verifica il passaggio della giunzione. Corrette anche le deviazioni sottostimate su estrusioni circolari ed ellittiche, comprese generatrici riflesse o soggette a scala non uniforme. [Misure sulle coniche trasformate](review-generale-2026-11-20.md#k10--riflessione-e-scala-non-uniforme-delle-coniche-estruse). [Misure e prove sulle facce reali](review-generale-2026-11-20.md#k10--estrusioni-di-cerchi-ed-ellissi). [Prove sulla giunzione](review-generale-2026-11-20.md#k10--giunzione-dei-profili-estrusi-chiusi). [Prove sull’estrusione](review-generale-2026-11-20.md#k10--wrap-su-estrusione-aperta). Corretto il ramo interno del toro fusiforme. K10.3 comprende ora le rivoluzioni a meridiano analitico o B-spline. Nel wrap multifaccia sono corretti la distorsione su UV oblique e il passaggio di un bordo sopra una fessura proiettata. Il kernel rileva anche materiale sopra aperture piane parzialmente coperte e bordi curvi che entrano e rientrano nella fessura. Il residuo è verificato nel continuo su più grafici B-spline cartesiani riconosciuti, anche con proiezione affine obliqua. [Prova obliqua con nodi disuguali](review-generale-2026-11-20.md#k103--proiezione-obliqua-fra-appoggi-curvi-21-novembre). Il wrap rileva anche il foro delimitato da una B-spline quartica polinomiale o razionale su un appoggio piano. La ricerca dei fori interni comprende anche gli appoggi curvi, con una regressione sul cilindro. Il residuo continuo copre più grafici quadratici nativi con coordinate separabili non lineari e quota affine attraverso lo zero. [Prove](review-generale-2026-11-20.md#k103--fori-su-cilindro-tre-appoggi-curvi-e-quota-affine). La scansione rileva anche una striscia scoperta fra quattro contorni esterni cilindrici. Il controllo del residuo include tre grafici accoppiati con trasversale comune e grafici Bézier razionali con proiezione affine a quota zero. Verificata la copertura continua per trim piani ortogonali con rientri e fori. Il residuo include trasversali distinti con impronte disgiunte e Bézier razionali con X(U) monotona, Y=V e quota variabile. Il controllo copre anche trim circolari e convessi obliqui, appoggi Bézier con pesi bidimensionali e Y=V e sorgenti Bézier non piane con proiezione cartesiana. Il certificatore comprende anche l’inclusione dei trim spline sorgente, le unioni piane con giunzioni parziali, le carte razionali accoppiate con impronta rettangolare e le sorgenti Bézier non piane con proiezione inclinata. Inclusi anche regioni inscritte in trim spline di appoggio, giunzioni curve intere, carte razionali a impronta non rettangolare e sorgenti non piane su appoggi a proiezione non affine quando l’altezza entra solo nella quota. Verificata anche la copertura dei fori spline piani dentro e fuori dal materiale sorgente e il residuo sulle carte razionali con nodi interni in entrambi gli assi. K10.3 comprende anche il winding certificato dei fori spline, giunzioni parziali lungo campate Bézier già presenti, sovrapposizioni certificate e proiezione inclinata con una coordinata invariata dal rilievo. Verificati anche tagli interni esatti e perimetri rientranti, grafici curvi coincidenti e sorgenti inclinate con rilievo accoppiato quadratico. Restano aperte le famiglie generiche. [Prove e perimetro](review-generale-2026-11-20.md#k103--tagli-interni-rientri-e-proiezione-accoppiata). [Dettagli](review-generale-2026-11-20.md#k103--trim-circolari-e-obliqui-pesi-bidimensionali-e-sorgenti-non-piane). [Prove e perimetro](review-generale-2026-11-20.md#k103--copertura-continua-ortogonale-trasversali-distinti-e-proiezione-razionale). [Dettagli](review-generale-2026-11-20.md#k103--lacune-fra-appoggi-curvi-e-grafici-accoppiati-o-razionali). [Prove e limiti](review-generale-2026-11-20.md#k103--estremi-delle-spline-nei-trim-e-proiezione-non-lineare-sui-due-assi). [Residuo fra due appoggi curvi](review-generale-2026-11-20.md#k103--residuo-fra-appoggi-curvi). [Prove su fori e bordi rientranti](review-generale-2026-11-20.md#k103--fori-interni-e-bordi-rientranti-nel-wrap-multifaccia). [Prove su rivoluzioni e multifaccia](review-generale-2026-11-20.md#k103--rivoluzioni-e-wrap-su-più-facce). [Prove del wrap razionale](review-generale-2026-11-20.md#k10--rilievo-perso-su-superficie-razionale), [perimetro precedente](review-k10-quota-zero-toro-2026-11-18.md) e [quadratura](review-k10-underflow-2026-11-17.md).

Il Wrap multifaccia K10.3 certifica anche giunzioni spline con pesi proporzionali, rientri fuori dal contorno spline della sorgente, quote razionali fino al numeratore cubico e denominatore quadratico, residuo diretto fino alla quota comune quintica e appoggi quadratici distinti con giunzione tangente. [Prove e limiti](review-generale-2026-11-20.md#k103--fattore-dei-pesi-quote-razionali-cubiche-e-residuo-quintico).

## Studio Lattice e trama di filamenti — 22 novembre 2026

OPT Industries descrive un modello procedurale a fili con nodi,
diametri e profili. Le figure 29–30 illustrano la ripetizione di
una cella in volumi a otto vertici. Le figure 31–32 applicano una
cella di tessitura a una superficie con spessore. La descrizione
include geometrie guida di taglio e slicing diretto dei fili per
processi DLP. [US 11,567,474 B2](https://patents.google.com/patent/US11567474B2/en)

Per ForgeCAD si propone una feature parametrica che prende il
solido selezionato come volume di ritaglio. La definizione salva
il riferimento, il tipo di cella, l’origine del reticolo, il
passo e il diametro. Un reticolo portante unisce le aste ai nodi.
La trama usa famiglie di curve con alternanza sopra e sotto
agli incroci, con una distanza controllata fra i fili. Il
riempimento automatico esistente ripete una feature nelle posizioni
di posa e non genera una rete di fili né rifila le loro sezioni.

Prove preliminari sul kernel Release. Due cilindri ritagliati
da una sfera e fusi al nodo formano un unico B-rep valido di sette
facce. Due sweep a sezione circolare su curve cubiche che si
incrociano in proiezione producono due solidi validi con
intersezione vuota. La dimostrazione visuale degli incroci alterna
la quota di due famiglie di curve in base alla parità degli indici.
Il comando parametrico, il ritaglio di reti grandi e il controllo
delle distanze lungo tutta una trama rimangono da implementare e
verificare. La prima verifica d’uso dovrà includere un solido con
foro o cavità, modifica del passo e del diametro e salvataggio
con rigenerazione.

### Prototipo a celle su Applicatore4

`quantizeSolid` campiona tutto il solido o box selezionati, mantenendo il
B-rep fuori zona. `latticeSolid` usa lo stesso percorso e mantiene tre
famiglie di aste ortogonali a sezione quadrata. Passo della cella, lato
dell’asta e passo di campionamento sono indipendenti. Il clipping isola
alcuni frammenti sulla testina scanalata. Il costruttore elimina i contatti
solo per spigolo o vertice. Se trova attacchi al volume conservato scarta
le componenti staccate, altrimenti tiene la rete principale. L’unione
finale deve superare `checkBody` e formare un solo solido. I test includono
fascia cilindrica, foro passante, ponte scomparso per passo troppo grande
e connessione dei pori alla base piena.

Sul B-rep di Raccordo 20 di Applicatore4.prt (copia del Desktop) sono
stati generati due solidi. Il reticolo in punta da Y=10,2 mm
ha passo 1,20 mm, aste 0,60 mm e campionamento 0,30 mm. Il reticolo
laterale da X=0,1 mm, fra Y=8 e 13 mm, conserva l’anima piena. Ha
passo 1,32 mm, aste 0,66 mm e campionamento 0,33 mm. I calcoli sono
durati circa 35 e 27 s. Le copie STEP AP242 sono state rilette e
convalidate con una shell chiusa ciascuna, rispettivamente 367 e 328
facce. Volumi calcolati sul B-rep: 146,798 mm³ l’originale, 132,608 mm³
la punta e 135,363 mm³ il laterale. La rilettura STEP conferma i volumi
entro 0,01 mm³. Il file sorgente è rimasto identico.
Il prototipo è nel kernel. Il passo di campionamento limita i dettagli
della zona ricostruita. Flessibilità, carico di rottura e fatica restano
da misurare.

### Reticolo con trama intrecciata

`wovenLatticeSolid` aggiunge ordito e trama al reticolo cubico
nelle zone selezionate. Gli incroci sono sfalsati rispetto alle
colonne. I fili si alternano sopra e sotto, fra due piani del
telaio, e si raccordano alle aste tramite sovrapposizione di
materiale. L’API controlla la separazione nominale agli incroci,
fra strati e rispetto alle colonne. Sul campione da 3,2 mm,
due incroci adiacenti conservano un varco e lo stesso filo cambia
quota da sotto a sopra. La base rimane collegata alla rete.

Sulla testina di Applicatore4 la prova laterale da X=0,1 mm,
fra Y=8 e 13 mm, usa cella da 1,60 mm, aste da 0,52 mm, fili
da 0,40 mm, ampiezza di 0,46 mm e campionamento da 0,20 mm.
L’origine X/Y è 0,26/8,10 mm; Z è il limite inferiore del
solido più 0,10 mm. Il kernel ha impiegato circa 52 s e ha
prodotto un solido di 511 facce con una sola shell, riletto
come STEP AP242 con volume confermato entro 0,01 mm³. Il volume
è 134,728 mm³, circa l’8,2% in meno dell’originale. Sulle due selezioni complete in punta il
kernel ha rifiutato rispettivamente loop a contatto e contatto
puntuale. Il passo discretizza la sezione dei fili. Le prestazioni
meccaniche dipendono dal materiale e restano da misurare.
Cinque test mirati e ASan/UBSan sul modulo passati.

### Testina intera di Applicatore4

Il kernel corregge due cause distinte dei contatti puntuali.
Nella griglia, due ottanti vuoti opposti possono incontrarsi
in un solo vertice anche se il materiale è connesso. Il
costruttore apre un passaggio a facce comuni rimuovendo due
celle locali. Durante la fusione di facce piane il kernel
conserva le partizioni quando il contorno unificato
ripeterebbe un vertice. Le due regressioni sono nel runner
`forgekernel_quantize_tests`. Per la trama, il campionamento
riusa le intersezioni B-rep di una riga e ripiega sul
classificatore puntuale nei casi ambigui.

La zona di lavoro parte dalla spalla a Y=0,06 mm e include
l’intera testina fino alla punta. Passo della cella 1,60 mm,
aste 0,52 mm, fili 0,40 mm, ampiezza 0,46 mm, griglia 0,20 mm.
La Release ha rigenerato un B-rep di 2673 facce, una sola
shell, genere 33, in circa 21 s. Alla rilettura dello STEP
AP242 tutti i 4232 vertici hanno un solo ventaglio di facce.
Il file conserva il volume di 82,898 mm³, il 43,5%
in meno dell’originale. Il volume del fusto ricontrollato
sotto la spalla è 40,417116 mm³ prima e dopo. La punta
campionata avanza di circa 0,06 mm lungo Y rispetto
all’originale. I tre target mirati di quantizzazione e
booleane sono passati. Sette test del runner passano anche
con ASan, UBSan e LeakSanitizer sui due moduli modificati.

### Quota originale e prova in ForgeCAD

La zona di Applicatore4 termina sul piano originale a Y=14,20 mm.
La cella terminale usa quel limite con il passo generale da 0,20 mm.
Alla rilettura dello STEP, gli estremi Y coincidono con l’originale
da −8,50 a 14,20 mm (lunghezza 22,70 mm). Il volume del fusto sotto
la spalla resta 40,417116 mm³. Il risultato ha 2673 facce e una sola
shell e supera il controllo del ventaglio dei 4232 vertici.

**Funzioni → Struttura volumetrica…** offre reticolo intrecciato,
reticolo cubico, voxelizzazione e giroide a parete sperimentale.
La zona può coprire l’intero solido
o un intervallo lungo X, Y o Z in coordinate del modello. Il viewport
mostra l’inviluppo e distingue la zona elaborata dalla parte conservata.
**Calcola** verifica un B-rep connesso. **Conferma** riusa quel risultato
e aggiunge uno stadio al corpo logico. La modifica dalla storyboard
riapre la zona e i parametri con la forma già calcolata nel viewport.
Un cambio di parametri invalida il risultato di **Calcola** e lascia
visibile l’ultima forma mentre l’anteprima si aggiorna. Per la TPMS
curva il pannello mostra prima un’anteprima indicativa e poi la forma
calcolata, distinguendo in entrambi i casi l’intervallo dalla parte
piena. La conferma senza modifiche chiude il pannello senza rigenerare.
**Zoom tutto** include le facce del giroide quando gli spigoli a
video appartengono alla parte piena.

Il collaudo Wayland/OpenGL su una copia di Applicatore4 ha mostrato
la TPMS salvata nel primo fotogramma a 140 ms, con 36 px sopra la
testina dopo Zoom tutto. Modificando lo spessore, il risultato
precedente è rimasto visibile e l’anteprima indicativa è comparsa a
circa 0,55 s. Due modifiche successive, la seconda durante il calcolo,
hanno confermato il risultato del valore più recente. Verificati
anche annullamento, maniglia ferma, prima anteprima in creazione e
riapertura del reticolo intrecciato. Build Release completata.

I campi numerici usano la calcolatrice condivisa dei pannelli.
Accettano punto, virgola ed espressioni come `0,5/2`. Il risultato
appare nella locale Qt e nell’unità del documento. Invio o l’uscita
dal campo applicano il valore e aggiornano l’anteprima.
Nella prova Wayland, digitazione e conferma hanno salvato il
campionamento di 0,25 mm. Verificati anche la quota negativa, la
locale inglese, la conversione da centimetri e Annulla. Il test
della calcolatrice condivisa è passato dopo la build Release.

**Parametri automatici** dimensiona la griglia sulla zona, entro
80.000 celle. Per i reticoli propone anche passo e spessori e adatta
il campionamento alle quote finché il valore resta automatico.
La voxelizzazione usa il lato della cella. L’anteprima grafica
ha un budget di 120.000 celle e segnala la semplificazione.
Per PreForm è disponibile **File → Esporta → STEP AP242…**.

Il primo collaudo ha verificato i tre modi iniziali dal pannello,
il salvataggio, la rigenerazione senza cache e la modifica
con la maniglia ferma. Su Applicatore4 una prova Wayland/OpenGL
ha riaperto il `.prt` e mostrato il cambio dei fili da 0,40 a 0,44 mm
prima di annullarlo. Lo STEP AP242 riletto ha un guscio chiuso,
2673 facce, 4232 vertici con ventaglio unico e lunghezza 22,70 mm.
Il campione STL diretto ha restituito 179 bordi aperti nella
triangolazione; l’artefatto per PreForm è lo STEP. La verifica di
compatibilità si basa sui formati documentati da Formlabs e sulla
rilettura geometrica in ForgeCAD.

Verifica della prova precedente in Wayland/OpenGL su Applicatore4 con
intervallo Y da 0,06 a 14,20 mm. Il framebuffer cambia al cambio
di passo e fili. La modifica ha aggiornato l’anteprima in circa
222 ms nella prova. La rilettura STEP ha confermato il solido da
2673 facce e la chiusura del pannello ha ripristinato il disegno
della scena. Sul campione compatto l’adattamento live alle quote
ha portato il campionamento da 0,15 a 0,09 mm. Verificato anche
il runner del kernel.

### Giroide a parete sperimentale

**Funzioni → Struttura volumetrica… → Giroide a parete (sperimentale)**
imposta la fascia del campo trigonometrico mediante periodo, spessore
nominale, passo di campionamento e zona. Il kernel normalizza il campo
con il gradiente per stimare la distanza dalla superficie centrale e
costruisce un B-rep per celle con facce piane. Lo spessore è nominale
e le pareti risultano discretizzate. La fase è centrata nella zona e
rimane stabile al cambio di risoluzione. Il codec 73 salva la nuova
modalità e lo spessore. I file 72 ricostruiscono i modi precedenti.

Sul Raccordo 20 di Applicatore4, fascia Y da 0,06 a 14,20 mm, con
periodo 2,50 mm, spessore nominale 0,42 mm e passo 0,18 mm, il kernel
ha prodotto un solido chiuso di 6306 facce in circa 33 s.
L’estensione Y resta da −8,50 a 14,20 mm. L’intersezione con la stessa
maschera sotto Y=0,06 mm restituisce 40,417829465 mm³ prima e dopo.
Le dieci prove del kernel includono anche i due vuoti su lati opposti
della parete, la connessione al fusto e il rifiuto del campionamento
troppo grosso.

La Release su Wayland/OpenGL ha creato la lavorazione dalla finestra,
salvato e riaperto il .prt di Applicatore4 e verificato il cambio
d’anteprima da spessore 0,42 a 0,50 mm con la maniglia ferma.
Il lettore apre il precedente .prt 72 del reticolo intrecciato e
ricostruisce il nuovo modo da un .prt 73 senza cache su un campione.
Nel B-rep della testina, le 6241 facce sopra la spalla hanno normali
parallele ad assi cartesiani.

Il giroide usa un campo implicito e le operazioni volumetriche del CAD.
La [verifica FTO](fto-struttura-volumetrica-2026-11-22.md) rimane
il riferimento per geometria reale, produzione e mercati.

### Prova TPMS a parete curva

Per riprodurre il tipo di parete continua mostrato nella seconda foto,
prova geometrica esterna al CAD con lo stesso campo del giroide.
Passo periodico 2,50 mm, spessore nominale 0,42 mm e campionamento
di 0,051 mm. L’isosuperficie interpolata produce la parete curva.
Il campione cubico da 2 × 2 × 2 celle forma una componente chiusa
di 391608 triangoli.

Applicatore4 usa l’inviluppo tassellato dal B-rep originale di
Raccordo 20. Sotto Y=0,06 mm il fusto è pieno nella prova campionata.
La componente collegata al fusto conserva il 99,75% del volume
orientato dell’estrazione, misura 22,70 mm in Y e ha 537356
triangoli. La rilettura binaria STL conserva la stessa topologia.
Confermati due triangoli di verso opposto per spigolo e un solo
ventaglio per vertice. Questo primo campione esterno è una mesh di studio.

**Funzioni → Struttura volumetrica… → TPMS a parete curva** è la
variante parametrica. Con un intervallo X/Y/Z, il **Punto di partenza**
è la quota di inizio del giroide. Il modo è salvato nel codec 74 con
passo di campionamento, periodo, spessore e quote. I modi 72/73
conservano valori, geometria e possibilità di modifica.

Il kernel ricava l’inviluppo con attraversamenti del B-rep in double
lungo i tre assi. Sui lati della griglia intercetta separatamente
la parete del giroide e il contorno esterno, usando la prima uscita
dal materiale. Dove è disponibile un unico attraversamento del
B-rep con orientamento coerente, quella coordinata è riusata sul
contorno esterno. Il pannello conserva gli attraversamenti insieme
all’inviluppo a parità di corpo e campionamento. Geometria calcolata,
anteprima e conferma condividono il risultato. Parete TPMS e contorno
generico sono approssimati da facce triangolari, con normali
interpolate a video.

La prima versione a parete curva campionava anche le superfici fuori
intervallo, pur lasciando pieno il materiale. Ora il kernel rifila il
solido originale sulle quote di inizio e fine. Il raccordo della
parete usa una fascia di tre celle dal lato TPMS e le sezioni piane
verificano il contenimento dei contorni prima della cucitura.
Fuori intervallo restano i supporti B-rep originali, con la loro
tassellazione di visualizzazione. La cache 17 rigenera le vecchie
TPMS parziali e i derivati nelle copie modificabili. I check-in
restano snapshot storici.

Il cilindro di regressione conserva il raggio e le facce esatte su
entrambi i lati. Su Applicatore4 il kernel ha prodotto un unico
solido chiuso di 94443 facce, con estensione Y da −8,50 a 14,20 mm,
e ha superato il controllo topologico e geometrico esteso.
Wayland/OpenGL ha verificato la migrazione del .prt di Applicatore4,
la rigenerazione e la riapertura della cache 17. Sotto Y=0,06 mm
sono state ritrovate 50 facce del sorgente, di cui 40 curve,
riconducibili alle parametrizzazioni originali. La verifica STL
della variante analitica ha ancora rilevato bordi aperti nell’export.

Il .prt salvato più tardi usa lo stesso B-rep di Raccordo 20
con inizio Y=0, periodo 4 mm, parete nominale 1,3 mm e passo 0,25 mm.
La copia con queste quote si apre come unico solido chiuso di
18522 facce e mantiene 49 facce del fusto sui supporti originali,
di cui 39 curvi. Nello STL verificato alla deflessione di 0,012 mm
sono stati trovati 66 bordi a incidenza unitaria.

Sul parallelepipedo allineato agli assi, la modalità TPMS applicata
all’intero corpo allinea la griglia ai sei piani originali. Le
porzioni occupate riusano i loro supporti e lasciano aperture sulla
superficie esterna. Il cubo da 5 mm è un solido chiuso e il test
verifica sia un’apertura sia del materiale sul lato esterno.

Sul contorno curvo della testina di Applicatore4 il rifilo esatto
resta aperto. Due sottrazioni, una per canale, hanno chiuso il B-rep
su cubo e cilindro nel prototipo. Su Applicatore4 la prova per
intersezione (200 s) e quella per sottrazione (180 s) hanno
esaurito il tempo di prova; anche la sottrazione su una fascia
di 2,50 mm si è fermata nel limite di 90 s. Con le quote salvate
(passo 0,25 mm), la prima sottrazione ha superato 210 s senza
risultato. Con i filtri conservativi sperimentali sulla prima
sottrazione, le coppie candidate scendono da 99966 a 28185 e il
tracciamento richiede 26–29 s. L’assemblaggio successivo rileva
60 spigoli aperti o con verso incompatibile su 40 facce sorgenti.
Il primo collega (−1,88726; 11,00000; −1,02992) a
(−1,89087; 10,9788; −1,02328) mm e ha un solo uso.
La sottrazione combinata
dei due canali completa il tracciamento e la raccolta dei tagli,
poi esaurisce 210 s durante la classificazione.

La cache 18 aggiorna la TPMS curva e i suoi derivati nei documenti
modificabili. Il confronto a parità di quote su Applicatore4 usa
Y=0, periodo 4 mm, parete nominale 1,3 mm e campionamento 0,25 mm.
Su Wayland/OpenGL la copia si rigenera in 19,4 s e si riapre come
unico solido di 18522 facce, lungo 22,70 mm, con 49 facce del
fusto sui supporti originali (39 curve). La regressione sul
cilindro verifica che i vertici esterni del contorno campionato
riusino gli attraversamenti del supporto. Superate 15 prove
del kernel. Il rifilo analitico dei supporti curvi nella zona
TPMS resta aperto.

**TPMS con parete spline, sperimentale nel formato .prt 75.**
Il kernel sostituisce le facce planari riconosciute come parete
interna con supporti B-spline bicubici. I bordi fra queste facce
sono curve cubiche condivise, con rifili affini nelle carte UV.
I poli sono determinati dal gradiente analitico del campo e
limitati per conservare l’iniettività della proiezione locale
del triangolo. La procedura conserva vertici e incidenza, le
facce di transizione e i supporti della zona piena. Il passaggio
da un campo TPMS a superfici CAD è discusso in
[TPMS2STEP](https://arxiv.org/abs/2405.07946).

La prova kernel su Applicatore4 con le quote salvate costruisce
4607 facce spline su 18522 e mantiene 49 facce del fusto
sui supporti di Raccordo 20, di cui 39 curvi. Il B-rep e lo STEP
riletto formano un unico solido e superano il controllo esteso.
Su 69105 punti interni alle facce adattate il residuo RMS al
campo del giroide scende da 0,01363 a 0,00645. La distanza
lungo la normale fra le due pareti spline va da 0,958 a
1,182 mm nei 50 riscontri con quota nominale 1,30 mm.
Sono campioni locali. Restano aperti il rifilo analitico
dei pori sulle facce curve della testina, il controllo globale
dello spessore e la continuità tangenziale fra tutte le pezze.

Superati 16 casi della suite di quantizzazione e il provino da
5 mm con tutti i sei supporti planari originali e le aperture.
ForgeCAD è stato ricompilato. Il pannello ha rigenerato e confermato
Applicatore4 con Y=0, periodo 4 mm, parete nominale 1,3 mm e griglia
0,25 mm. Il .prt 75 riaperto conserva la modalità e le quote, e il
pannello in modifica mostra l’anteprima salvata. L’export AP242
dall’effettivo .prt 75 si rilegge come un unico solido controllato.

**Rifilo dei pori su Applicatore4.** Una prova fuori dal pannello
usa due canali B-rep campionati e li sottrae insieme a Raccordo 20.
Il taglio simultaneo traccia i due gruppi di contorni sulle
superfici sorgenti con una sola classificazione dell’operando.
Con Y=0, periodo 4 mm, spessore nominale 1,30 mm e griglia
0,55 mm, la sottrazione ha prodotto 2823 facce in circa 169 s.
Il modello ha 135 facce della testina sui supporti originali,
134 curve, e conserva 49 supporti di facce del fusto. Sono
state ricostruite 887 facce di parete a spline. Il modello e
lo STEP riletto sono un unico solido e passano il controllo
geometrico e topologico esteso. Sul confine fra supporti sorgenti
e pareti dei canali ci sono 58 contorni di apertura, 1977 spigoli
di contorno e nessun vertice di grado diverso da due. L’estensione
dei vertici lungo Y resta da −8,50 a 14,20 mm.

Lo spessore è misurato per fasce da 1 mm lungo tutta la testina,
con raggi normali alla faccia di partenza e controllo del primo
impatto sul B-rep completo. Su 126345 punti valutati il minimo
misurato è 0,9504 mm, da (−0,41554; 5,78975; 0,19788) a
(0,13521; 6,35526; −0,33140) mm. Il valore è campionato.
Il risultato è una prova STEP e l’integrazione nella funzione
parametrica resta aperta.

A 0,42 mm il primo assemblaggio ha lasciato tre spigoli liberi
all’altezza Y=11,52–11,94 mm. I tre lati appartengono a una
faccia piana del tagliente. L’intersezione isolata col sorgente
non trova archi. Il primo raggio di classificazione conta due
radici sulla stessa faccia spline a circa 1,15×10⁻⁶ mm di
distanza e rende pari un numero di attraversamenti che gli
altri raggi trovano dispari. Il classificatore ora scarta il
raggio con stime di attraversamento sovrapposte e prova la
direzione successiva. La faccia isolata viene mantenuta dopo
la correzione. Dopo la modifica il rifilo a 0,42 mm ha
chiuso tutti gli spigoli in circa 232 s. Due frammenti
esterni al guscio del fusto, di 0,000328 e 0,000190 mm³,
sono stati esclusi dalla copia. Il solido rimasto ha 4226
facce, di cui 1951 spline di parete adattate, 132 facce della
testina sui supporti originari (131 curve) e 49 facce del
fusto sui supporti originari. Ha 50 contorni di apertura,
2458 spigoli di contorno e nessuno spigolo libero. L’estensione
dei vertici lungo Y è −8,50–14,20 mm. Il B-rep e lo STEP
riletto superano il controllo esteso con un unico guscio.

Su 75230 punti valutati nelle fasce della testina, il minimo
osservato è 0,9518 mm. Il segmento va da
(−0,37793; 9,86239; 0,31615) a (0,14942; 10,42002; −0,24669) mm.
Il controllo del primo impatto usa il B-rep completo.
Il valore è campionato. Alcuni tratti di parete interna
restano planari. ForgeCAD è stato ricompilato con la correzione
della classificazione e sono passati cinque CTest mirati.

L’ispezione del B-rep a 0,42 mm individua 2091 facce
planari di asportazione nella testina. Di queste, 2063 hanno
almeno uno spigolo non rettilineo e restano fuori
dall’interpolazione, che richiede tre spigoli rettilinei.
Le altre 28 sono triangoli rettilinei. Una strada da provare
è costruire le due pareti a spline prima del rifilo sui
supporti originali, controllando lo scarto dalla legge del
giroide e le normali fra pezze. Da verificare il rifilo dei
pori, la chiusura del solido e lo spessore dopo l’adattamento.

**Provino di rifilo spline su cubo da 5 mm.** Con fase
(2,38; 2,61; 2,47) mm, periodo 4 mm e spessore nominale
1,30 mm, la prova del kernel sottrae contemporaneamente
i due canali dopo la costruzione delle spline sui taglienti.
Le radici sui lati della griglia sono raffinate per bisezione
sulla legge del giroide. I centri interni delle celle sono
proiettati sul livello con spostamento limitato.

A parità di passo 0,30 mm, adattare le spline dopo il rifilo
lascia 642 facce interne piane e un salto mediano delle
normali di 19,35° (95º percentile 47,54°). Costruirle prima
del rifilo con radici raffinate dà 7037 facce interne spline,
zero facce interne piane e mediana 1,05° (95º percentile
31,32°). A parità di passo, la variante con spline prima
del rifilo e senza raffinamento delle radici dà 19,13°
di mediana. Le giunzioni più critiche presentano ancora
salti di tangenza.

Il nuovo provino mantiene i sei piani del cubo, 20 contorni
di apertura, zero bordi liberi e un unico guscio. Anche lo
STEP riletto supera il controllo geometrico e topologico
esteso. Su 109866 punti valutati nelle cinque fasce del cubo
il minimo campionato lungo la normale è 0,9559 mm. Il rifilo
ha richiesto circa 9,3 s. Il confronto visivo usa lo stesso
passo e lo stesso punto di vista. Si tratta di una prova
fuori dall’applicazione su supporti piani.

**Taglienti spline su Applicatore4.** La geometria usata per
la prova coincide byte per byte con Raccordo 20 nel file
Applicatore4 del Desktop. Con inizio Y=0, periodo 4 mm,
parete nominale 1,30 mm e passo 0,42 mm, i due taglienti
hanno 24963 facce spline di parete prima della sottrazione.
Il rifilo impiega circa 313 s. Un frammento esterno al
guscio del fusto, di 0,0000691 mm³, è escluso dalla copia.

Due pezze si estendevano fino a circa 0,005 mm sotto Y=0.
La testina rifilata viene quindi tagliata a Y=0 e unita alla
parte inferiore di Raccordo 20, tagliata sullo stesso piano.
Su 661 raggi validi di confronto sotto Y=0, inclusi cinque
livelli nella fascia di giunzione, gli attraversamenti sono
identici al sorgente. I 23 raggi ambigui sono esclusi dalla
comparazione. Sulle pareti campionate non ci sono punti
oltre i limiti assiali.

Il solido e lo STEP riletto hanno un unico guscio, zero
spigoli liberi e 51 contorni di apertura. Le 4097 facce delle
pareti sono spline. Il modello riusa 132 facce della testina
sui supporti originari e 49 del fusto. L’estensione dei
vertici è Y=−8,50–14,20 mm. Il salto mediano delle normali
scende da 19,62° a 3,60° rispetto al rifilo precedente con
lo stesso passo. Il 95º percentile resta a 44,71°.

Su 91272 punti nelle 15 fasce della testina, il minimo
campionato è 0,95596 mm tra i punti
(0,28239; 12,25925; 0,28655) e (−0,26665; 11,70466; −0,26556) mm.
La misura usa il primo attraversamento verificato del B-rep.
Questa misura si riferisce alla prova STEP prima dell’integrazione.

**Modalità parametrica del 24 novembre.** In **Funzioni → Struttura
volumetrica → TPMS con rifilo spline (sperimentale)**, l’anteprima
nel viewport si aggiorna con zona, passo e spessore. **Calcola**
avvia in background la costruzione dei due taglienti spline, il
rifilo sul B-rep originale e il controllo del solido. Le parti
fuori intervallo sono conservate dai tagli sull’originale.
**Conferma** registra la feature nella storyboard. Il formato
FCAD76 salva la nuova modalità insieme ai parametri, riapribili
in modifica. Il lettore conserva il supporto delle versioni
precedenti.

La prova Wayland sul cubo da 5 mm ha aperto il comando dal
menu, aggiornato l’anteprima al cambio dei parametri, avviato
manualmente il rifilo e verificato il solido dopo salvataggio
e riapertura della feature. Superati i 17 casi del runner di
quantizzazione. Sul B-rep originale di Applicatore4, intervallo
Y da 0 a 14,20 mm, periodo 4,00 mm, parete nominale 1,30 mm e
campionamento 0,42 mm, il kernel ha prodotto un unico guscio
chiuso di 4281 facce, di cui 4097 spline sulle pareti. Il
controllo geometrico e topologico esteso non riporta problemi.

**Diagnosi rifilo spline su Senza nome1 (26 novembre).** Con
intero solido, campionamento 0,20 mm, periodo 3,00 mm e parete
nominale 0,80 mm, la copia di `Senza nome1.prt` riproduce
l’errore F47391, operando 1. Il solido di rivoluzione ha 3 facce
e supera il controllo B-rep esteso. I due taglienti spline
totalizzano 112670 facce. Durante la differenza booleana, un
bordo della faccia F47391 e la curva di intersezione entrano
nello stesso nodo, circa (0,123381; −1,842142; 13,280502) mm.
L’ordinamento angolare assegna loro lo stesso tratto uscente.
Il grafo ha due ingressi e due uscite, ma il percorso incontra
un tratto già visitato e interrompe la ricostruzione dei
contorni. I parametri passano la validazione e il taglio
arriva al nodo con estremi separati di circa 0,00000032 mm,
entro la tolleranza di 0,000001 mm. La prova ha richiesto 48 s.
La booleana ricava la successione dagli usi topologici quando
un unico taglio interno arriva nel mezzo di uno spigolo e
l’ordinamento angolare assegna due volte lo stesso tratto.
Il nuovo test imprime una spline quasi tangente su una lamina.
Prima della correzione fallisce la divisione della faccia.
Dopo la correzione si ottengono due facce valide.

Nella build Release il calcolo della feature su una copia di
Senza nome1 con i valori originali ha prodotto in 74 s un unico
solido chiuso di 30488 facce. Preserva tutti e tre i supporti
originali e supera il controllo B-rep esteso. Superati i 38
test Booleane, i 39 test di genealogia, i 17 di quantizzazione
e le suite su bordi condivisi e cuciture booleane. A 0,25 mm
la build finale conferma l’errore distinto di chiusura sullo
spigolo 259.
A 0,25 mm, mantenendo periodo e parete, la divisione supera
quel passaggio ma il risultato della booleana resta aperto
(spigolo 259 senza due fin opposte, 42 s).

Prova di risoluzione del 23 novembre sulla copia con periodo
4 mm e parete 1,3 mm. Il passo di campionamento dimensiona la
griglia che genera le facce piane della parete. A 0,25 mm, il
lato massimo dei triangoli della testina è 0,442 mm su 18472
triangoli. A 0,16 mm scende a 0,307 mm su 46476 triangoli.
Entrambi i B-rep superano il controllo esteso con un solo guscio.
Il modello a 0,16 mm misura 22,729 mm in Y, rispetto ai 22,700 mm
della prima copia. A 0,125 mm il calcolo della prova supera 130 s
senza completarsi entro il limite impostato.

A 0,16 mm il campionamento dell’inviluppo richiede 74,7 s, il
contorno e la costruzione 4,0 s, i controlli e le misure 1,7 s.
Il campionamento percorre un asse alla volta con fino a quattro
thread CPU, mentre i campi sono elaborati per strati su due
thread. In questa prova la GPU interviene solo nella visualizzazione.
Il raffinamento uniforme riduce le dimensioni dei triangoli e
conserva il limite del contorno campionato.

#### Calcolo ibrido CPU/CUDA della TPMS

La build Release usa fino a otto thread per i raggi esatti sul
B-rep e precomputa le fasi del giroide per asse. Durante la
triangolazione, la CPU emette la prima parte delle celle mentre
CUDA calcola i collegamenti delle celle successive. Fino a
quattro thread CPU preparano poi le facce e le curve sul piano
di ciascuna faccia. L’unione degli spigoli condivisi conserva
l’ordine originale, con gli stessi indici e gli stessi supporti.

CUDA si attiva automaticamente quando il lotto assegnato
contiene almeno 262144 celle. In assenza di CUDA o se il lotto
fallisce, il kernel usa le stesse regole sulla CPU.
`FORGECAD_TPMS_CUDA=1` forza la prova con lotti più piccoli e
`FORGECAD_TPMS_CUDA=0` seleziona la CPU. Con `FORGECAD_PROFILE`
il log riporta i lotti effettivamente elaborati.

Sulla Quadro RTX 3000, il calcolo isolato dei collegamenti su
circa 2,1 milioni di celle è passato da circa 23 a 13 ms.
Su Applicatore4 a 0,16 mm, il campionamento sulla stessa macchina
è passato da 48,2 s con quattro thread a 24,6 s con otto. Con
l’inviluppo pronto, la costruzione richiede circa 2,5 s in
entrambe le modalità. CUDA forzata ha elaborato 109296 celle.
I due percorsi e il riferimento a quattro thread producono un
B-rep identico byte per byte, con 46526 facce e un guscio chiuso.
Il provino inclinato conferma l’attivazione automatica su
435200 celle e l’identità del B-rep con la CPU. Superati
cinque test mirati, compresa la parità CPU/CUDA delle
connessioni e del solido.

Nella prima versione, su Applicatore4 con Y da 0,06 a 14,20 mm, periodo 2,50 mm, parete
nominale 0,42 mm e campionamento 0,12 mm, il kernel ha prodotto un
solido di 115774 facce in 78 s. Il controllo B-rep esteso è riuscito
e la lunghezza Y resta 22,70 mm. La prova Wayland/OpenGL ha misurato
75 s per la prima anteprima e 1,1 s spostando il punto di partenza da
0,06 a 0,46 mm nello stesso pannello. Sotto Y=0,06 mm il volume
campionato è 40,300 mm³, contro 40,418 mm³ nel B-rep originale.

Verificate conferma nella storyboard, riapertura del .prt 74 di
Applicatore4, rigenerazione senza cache su campione semplice e
lettura dei modi dei .prt 72 e 73. Le 12 prove del kernel sono
superate. La tassellazione conserva i lati rettilinei sotto la
deflessione e usa direttamente le facce piane già triangolari.
Lo STL esportato da ForgeCAD con le impostazioni suggerite ha
115774 triangoli, con due facce di verso opposto per spigolo e una
sola componente chiusa alla rilettura indipendente.

La [verifica FTO](fto-struttura-volumetrica-2026-11-22.md)
resta applicabile al processo di produzione considerato.

### Reticolo per applicatori e costo della rappresentazione

Un reticolo con nodi uniti può servire da anima porosa di una
testina. La trama alterna invece i fili sopra e sotto. Per
confrontare due applicatori, vanno misurati il carico e il rilascio
di prodotto e la flessione delle setole a parità di materiale e
ingombro. L'anima e le setole possono avere parametri distinti.

Per reticoli densi, conservare la definizione parametrica con
cella, passo, diametri e volume di ritaglio B-rep. Valutare la
geometria localmente, riusare la cella con istanze per l'anteprima
ed elaborare uno strato alla volta per gli impianti che accettano
sezioni. Se il fornitore richiede una mesh, generarla all'export
con tolleranza legata ai dettagli stampabili. Il B-rep completo
delle aste resta una prova per reti piccole. Tempi, picco RAM e
dimensione dell'export sono da misurare su una testina reale.
[Foucault e altri, 2024](https://doi.org/10.1051/meca/2024019),
[Liu e altri, 2021](https://arxiv.org/abs/2101.05031)

Una variante da provare usa il B-rep come inviluppo e come
riferimento per le quote. La funzione implicita del reticolo
si valuta all'interno dell'inviluppo. Si possono riservare
fori, sedi e superfici di accoppiamento nel modello parametrico.
Il passo della cella e la risoluzione di campionamento hanno
ruoli distinti. La seconda si sceglie rispetto al dettaglio
minimo e alla tolleranza, verificando fori e regioni sottili.
Campionare per zone o per strati evita di materializzare la
griglia 3D densa. Questa variante va provata su un inviluppo
chiuso con un foro e una sede, con misura di RAM e scostamento.
Il confronto con i brevetti di metodo dipende dalle operazioni
dell'intera filiera. Il confronto con la rivendicazione di
prodotto riguarda la testina fabbricata e i suoi elementi.
[Pasko e altri, 2011](https://eprints.bournemouth.ac.uk/18563/),
[Liu e altri, 2021](https://arxiv.org/abs/2101.05031)

### Confronto FTO con il report su Applicatore4

La [verifica delle rivendicazioni](fto-struttura-volumetrica-2026-11-22.md)
confronta codice, STEP e anteprime TPMS con i titoli citati.
Il procedimento Chanel EP2120631 B1 e US8172473 B2 merita priorità
per la stampa commerciale. Il titolo francese FR3060956 B1 usa
un rapporto di aree distinto dal requisito dello smusso EP3558055 B1.

### Lettura preliminare delle rivendicazioni OPT

La modellazione procedurale dei reticoli regolari e delle
microstrutture volumetriche era già pubblicata nel 2011.
Le figure del brevetto descrivono applicazioni della tecnologia.
La portata va esaminata sulle singole rivendicazioni, tenendo
conto della giurisdizione e degli equivalenti.
[Pasko e altri, 2011](https://staffprofiles.bournemouth.ac.uk/display/journal-article/16979),
[articolo 69 e protocollo CBE](https://www.epo.org/en/legal/epc/2020/protinta69.html)

- US 11,567,474 B2, rivendicazione 1. Calcola la forma da
  rappresentare nella sezione dove interseca la linea del filo,
  sulla base almeno del diametro e della rotazione della sezione.
  Le rivendicazioni 13 e 21 riguardano l’assegnazione in parallelo
  di blocchi del modello a fili e dei dati dei fili che li
  intersecano. Questi passaggi riguardano l’elaborazione digitale.
  [Rivendicazioni concesse](https://patents.google.com/patent/US11567474B2/en#claims)
- US 11,681,269 B2, rivendicazione 1. Programma che interpreta
  una struttura di dati con nodi indicizzati, diametri e
  superficie di attacco per controllare la stampa 3D.
  [Rivendicazioni](https://patents.google.com/patent/US11681269B2/en#claims)
- US 11,275,354 B2 e US 12,578,700 B2 comprendono rivendicazioni
  sul prodotto fisico. Per gli applicatori va confrontata anche la
  configurazione di reticolo, setole, rinforzi e impugnatura.
  [US 11,275,354](https://patents.google.com/patent/US11275354B2/en#claims),
  [US 12,578,700, USPTO](https://patentsgazette.uspto.gov/week11/OG/html/1544-3/US12578700-20260317.html)

Una strada da valutare per ForgeCAD è generare e rifilare
solidi B-rep con le operazioni geometriche del kernel e
consegnarli in STEP o STL a un flusso di fabbricazione esterno.
La distinzione tecnica sta nelle operazioni effettive sui
solidi. Il nome del file, il passo o il diametro dei fili da soli
non determinano la portata delle rivendicazioni. La pubblicazione
europea EP 4 111 351 A1 appartiene alla famiglia OPT.
La libertà di attuazione resta da verificare sul prodotto o
software definitivo nei paesi pertinenti.
[Pubblicazione internazionale e famiglia](https://patents.google.com/patent/WO2021195267A1/en),
[registro EPO](https://register.epo.org/application?number=EP21776386)


Per la vendita di applicatori il confronto con le rivendicazioni
di prodotto resta necessario anche con un generatore B-rep
indipendente o un produttore esterno. In Italia l’art. 66 CPI
riguarda anche vendita e importazione di prodotti direttamente
ottenuti con un procedimento brevettato. Negli Stati Uniti il
§ 271(g) disciplina vendita e importazione di prodotti ottenuti
con un procedimento brevettato. Per il procedimento digitale
va verificato il nesso con la fabbricazione dell’applicatore.
Il fascicolo EPO di EP 4 111 351 A1 contiene la notifica
del 15 ottobre 2026 (modulo 2524, regola 112(1) CBE).
La domanda è considerata ritirata per mancato versamento
della tassa del sesto anno e della soprattassa nei termini.
Eventuali atti successivi e ripristino vanno verificati.
[Codice della proprietà industriale, artt. 54, 66, 124 e 125](https://www.wipo.int/wipolex/en/legislation/details/22344),
[35 USC § 271](https://uscode.house.gov/us/usc/119-134/t35/s271/)

### Anteriorità sugli applicatori e fascicoli USA ed EPO

Chanel documenta nel 2018 un applicatore stampabile con anima
forata a reticolo per trattenere il cosmetico, WO 2018/115770 A1.
Erpro documenta la produzione commerciale del brush Chanel
nel 2018–2019. Il rapporto di ricerca, il PTO-892 e gli elenchi
IDS esaminati per la domanda OPT 17/665,467 non riportano quel
prodotto o la corrispondente famiglia Castex. Il PTO-892 del
20 marzo 2025 cita Thayer, US 6,295,994 B1 (2001).
[Chanel](https://patents.google.com/patent/WO2018115770A1/en),
[Erpro](https://erpro-group.com/en/success-story-2/),
[fascicolo USPTO](https://patentcenter.uspto.gov/applications/17665467/ifw/docs)

Nell'atto del 20 marzo 2025 e nel Notice of Allowance del
10 novembre 2025, l'esaminatore identifica in Thayer un
rinforzo fra tratti intermedi delle setole. Indica la struttura
a reticolo e la pluralità di fili di rinforzo come differenze
della rivendicazione 1. Erano presenti nella domanda iniziale.
Il rigetto riguardava la chiarezza ai sensi del § 112(b).
La risposta OPT del 19 settembre 2025 chiarisce il testo, tra
cui le diverse lunghezze e i diversi diametri delle setole.
GEKA descriveva già un rinforzo a doppia elica attorno a
un'anima convenzionale. Resta aperta la verifica della
combinazione completa e della sua ovvietà ai sensi del § 103.
[Thayer](https://patents.google.com/patent/US6295994B1/en),
[GEKA](https://patents.google.com/patent/US7025068B2/en),
[concessione USA](https://patentsgazette.uspto.gov/week11/OG/html/1544-3/US12578700-20260317.html)

Il parere EPO del 16 febbraio 2024 (modulo 1703, punti 4–5)
contesta novità e attività inventiva del formato dati della
domanda EP, con D1–D4 per la novità e D5, Messner, per l'attività
inventiva. Il 5 settembre 2024 OPT ha riformulato la
rivendicazione 1 come programma per controllare la stampa,
con un diametro associato a ogni nodo dell'asse del filo e
informazioni sulla superficie di attacco. La risposta sostiene
che questa rappresentazione riduce la dimensione del file.
Il fascicolo allegato si chiude con la notifica di ritiro e
non mostra un esito sulle rivendicazioni modificate. La
rivendicazione statunitense sulla geometria dell'applicatore
è distinta. [Fascicolo EPO](https://register.epo.org/application?number=EP21776386)

## Funzioni già presenti

| Area | Stato da sorgenti e registro | Riferimento |
| --- | --- | --- |
| Sviluppo e portabilità | Un solo `main`, backend CPU con CUDA/Metal opzionali, CI Linux/macOS. L'unificazione è stata verificata sui due sistemi. | [CI](ci.md), [rapporto unificazione](unificazione-main-2026-10-30.json) |
| Storia e documenti | Storyboard, rigenerazione, soppressione, ripetizioni, riferimenti topologici, recupero e compressione Zstandard. | [Storia](storyboard-history.md), [README](../README.it.md) |
| Superfici 3D | Estensione fino a riferimento, reti U/V a celle Coons, continuità per contatto, modifica poli/pesi NURBS, con i limiti documentati. | [Superfici](../README.it.md#modellazione-di-superfici) |
| Viste 2D | Proiezioni, sezioni, ausiliarie, dettagli con contorno circolare/rettangolare/libero, collegamenti alla madre e viste interrotte. | [Studio 2D](drawing-revisions-study.md), [interruzioni](drawing-breaks.md) |
| Sezioni composte | Gradini multipli e allineata a due piani, creazione con mouse e snap oltre all'opzione numerica. Restano i limiti delle sezioni rimosse. | [Registro](../CLAUDE.md), [studio 2D](drawing-revisions-study.md) |
| Quotatura e tavole | Catene/ordinate, tolleranze dimensionali, datum e sei caratteristiche GD&T, badge persistenti, fogli multipli, scala foglio/vista, richiami obliqui e PDF. | [Studio 2D](drawing-revisions-study.md) |
| Revisioni | Consultazione, check-out/check-in, ripristino nella copia di lavoro, rilascio coordinato locale, verifica/recupero catalogo. | [Studio, sezioni 24–26](drawing-revisions-study.md#24-ripristino-di-una-revisione-nella-copia-di-lavoro) |
| Prestazioni | Filtri CPU e riuso di query già integrati; GPU disponibile per carichi specifici. Ogni beneficio riguarda il caso misurato, senza fattore globale 10× dimostrato. | [Prestazioni](viewport-performance.md), [storico solver/GPU](solver-cuda-roadmap-2026-10-26.md) |

## Incremento attuale

**Allineamento grafico delle quote**, già durante il primo posizionamento e nel successivo trascinamento: guide lampeggianti sul centro del testo più vicino nel verso del movimento, orizzontale e verticale; snap prioritario fra punte coincidenti, anche quota angolare/lineare con lato comune orizzontale, verticale o inclinato. Indicatore sul punto comune, Alt/Esc e un solo Undo al rilascio. Completato e verificato su Linux: build completa, 37/37 regressioni, Qt XCB nativo, PDF e mappa PASS; dettagli nel [registro](../CLAUDE.md). [Uso](drawing-revisions-study.md#allineamento-grafico-delle-quote--30-ottobre-2026).

Incremento precedente completato: **rugosità strutturate Ra/Rz**, micrometri, asportazione, processo, agganci e PDF vettoriale. [Uso e limiti](drawing-revisions-study.md#rugosità-superficiale-strutturata--30-ottobre-2026).

## Lavoro ancora aperto

| Priorità proposta | Incremento concreto | Criterio di completamento |
| --- | --- | --- |
| 1 | Annotazioni di saldatura strutturate | Campi distinti dal testo libero, simboli vettoriali, agganci, proprietà, Undo e codec; definire prima il sottoinsieme supportato. |
| 2 | Pacchetto autonomo di un rilascio | Modello e tavole nei check-in esatti, tutti i fogli/PDF e manifesto con impronte; consegna verificabile fuori dal workspace originale. Il manifesto locale esiste già. |
| 3 | Ulteriori riferimenti ai contorni apparenti e casi HLR | Partire da casi riproducibili di superfici generiche, senza promuovere mesh o pixel a geometria di quota e senza riassociare per vicinanza. |
| 4 | Profilazione della rigenerazione dopo i filtri già integrati | Baseline attuale, copie congelate, confronto numerico e ripetizioni; scegliere il costo dominante prima di nuovi lotti o parallelismo. |
| 5 | Budget complessivi GPU e carichi prolungati | Coordinare limiti fra backend/superfici, saturazione e fallback CPU; verificare su hardware reale distinto. |

Estensioni successive: altre caratteristiche/modificatori GD&T, più parametri di rugosità e indicazioni complete; ulteriori combinazioni delle sezioni composte. Approvazioni, autenticazione/permessi, filesystem di rete e porting Windows richiedono requisiti e prove propri; non sono coperti dai lock cooperativi o dalla CI Linux/macOS.

## Regole per la prosecuzione

- Verificare prima nel codice e nel registro se un vecchio punto è già completato.
- Integrare incrementi circoscritti su `main`, preservando il fallback CPU e la compatibilità dichiarata.
- Per modifiche strutturali aggiornare e verificare la [mappa locale](architecture/README.md).
- Registrare build, test realmente eseguiti e limiti senza attribuire alla nuova sessione le misure storiche.
