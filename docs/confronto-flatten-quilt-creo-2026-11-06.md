# Flatten Quilt Deformation: confronto con ForgeCAD

6 novembre 2026. Registro dell'indagine e dell'implementazione; gli esiti piu' recenti sono nelle sezioni finali.

## Richiesta e immagini

Il risultato richiesto e' un pattern con tutta la base aderente al manico e
celle che conservino proporzioni e aspetto del rilievo originale, sulle due
facce F5/F6 di KnifeHandle. La rototraslazione delle copie non basta.

La seconda schermata mostra nell'albero di Creo: Flatten Quilt 1, Pattern 1 of
Extrude 3, Flatten-Quilt Deformation 1, Round 1 e Round 2. Il dettaglio mostra
celle regolari; non dimostra planarita' delle facce esterne, invarianza di tutte
le quote o errore massimo di contatto. I raccordi successivi contribuiscono
all'aspetto finale.

## Fonti ufficiali PTC

- [About Flatten Quilt](https://support.ptc.com/help/creo/creo_pma/r12/usascii/surfacing/surface/About_Flatten_Quilt.html):
  parametrizzazione uniforme, anche mediante una superficie di riferimento;
  risultato piano unico e requisito di tangenza fra le superfici del quilt.
- [About Flatten-Quilt Deformation](https://support.ptc.com/help/creo/creo_pma/r12/usascii/surfacing/surface/Using_Flatten_Quilt_Deformation.html)
  e [procedura](https://support.ptc.com/help/creo/creo_pma/r12/usascii/surfacing/surface/To_Create_a_Flatten_Quilt_Deformation.html):
  si sceglie una precedente feature Flatten Quilt, poi i corpi da piegare o
  appiattire. I corpi devono essere vicini al quilt piano e non oltrepassarlo.
- [Interfaccia](https://support.ptc.com/help/creo/creo_pma/r12/usascii/surfacing/surface/About_the_Flatten_Quilt_Deformation_User_Interface.html):
  From Flat to Bent e From Bent to Flat usano la stessa feature di riferimento.
- [Nota tecnica PTC del 2003](https://support.ptc.com/carezone/archive/newsletters/011003_a.pdf),
  pp. 4-5 e 9-11: il precedente Bend Solid applica l'inverso della trasformazione
  di appiattimento. Per doppia curvatura il processo deforma cercando di
  conservare area e perimetro. E' una spiegazione storica del principio,
  non una certificazione dell'algoritmo interno delle versioni attuali.

Le fonti non pubblicano l'algoritmo completo. La regolarita' visiva non prova
stiramento nullo; non si puo' promettere un'isometria generale del piano sulle
superfici a doppia curvatura. Lo sviluppo esatto senza deformazione gia'
richiesto dall'utente resta distinto da questa operazione.

## Riscontri nei sorgenti attuali

- `kernel/fk_surface_mapping.cpp`, `unrollFace`: sviluppo analitico di piani,
  cilindri e coni; nessuna mappa globale multifaccia persistente e reversibile.
- `wrapBodyOnSurface`: scala UV usando le derivate al solo ancoraggio e aggiunge
  la quota lungo la normale. La metrica non e' uniformata sul quilt.
- `withFaceMapping` / `wrapBodyOnFaces`: proietta lungo la normale fissa del
  primo appoggio; richiede un grafico regolare e copertura nel trim. Non usa
  l'inverso di un precedente appiattimento.
- `mappingRigid`: conserva le copie con rototraslazioni. Appoggia i centri,
  allinea le normali, ma una base piana resta piana.
- `cad_surface_mapping.cpp`: le facce sviluppate sono isole separate;
  sviluppo e deformazione non condividono una corrispondenza persistente.

Nessuno dei metodi attuali equivale al comando Creo. Il modello di proiezione
puo' contribuire agli errori di mancata intersezione. Questo non dimostra che
ogni punto rifiutato del file abbia necessariamente una corrispondenza valida
in una futura parametrizzazione.

## Direzione proposta, da implementare

Serve una corrispondenza piano/quilt comune alle facce, con contorni rifilati
coerenti, transizioni tangenti e genealogia esplicita. La feature deve
conservarla, con origine e orientamento, per applicarne l'inverso ai corpi
costruiti sul piano, anziche' proiettare ciascuna copia indipendentemente.

Il trasporto dello spessore richiede una definizione e verifiche proprie;
le fonti PTC consultate non ne specificano tutti i dettagli. La geometria
risultante deve restare B-rep analitica/NURBS nei budget esistenti, senza
sostituirla con la mesh di visualizzazione.

Verifiche richieste: contatto dell'intera base, continuita' sulla giunzione,
variazioni delle dimensioni delle celle e dello spessore, assenza di pieghe,
dominio, dimensioni dei file e tempi sul caso reale. L'andata/ritorno da sola
non prova l'assenza di stiramento. La posa rigida resta distinta; nessun cambio
retroattivo dei documenti o allentamento delle tolleranze.

## Esito della prima indagine, prima dell’implementazione

Letti i sorgenti e le fonti ufficiali. Aggiornati questo rapporto e la memoria.
Nessun CAD modificato, nessun eseguibile ricompilato o nuovo test eseguito per
questa sola indagine/documentazione. Le precedenti 18 prove e la posa rigida
delle 155 copie non attestano la nuova funzionalita', che resta da implementare.


## Implementazione autorizzata — avanzamento

Avviato il nuovo metodo Piega da quilt appiattito, distinto dai due precedenti.
Il modulo `fk_quilt_mapping` costruisce coordinate trasversali e longitudinali
in lunghezza d'arco, una corrispondenza condivisa su una o due facce tangenti
con giunzione U isoparametrica, e applica l'inversa ai corpi. La quota segue
la normale locale. Il fit e' separato nel modulo interno `fk_mapping_fit`.
Aggiunti selettore GUI, chiave anteprima e flag persistente schema6 compatibile
con le letture precedenti; nessuna isometria generale viene dichiarata.

Prima verifica: cilindro analitico, andata/ritorno entro1e-9mm, piegatura solida
B-rep valida e regressione kernel preesistente PASS. Le successive prove di
giunto NURBS e applicazione sono ancora da eseguire a questo checkpoint.

Il primo prototipo sul file reale ha raggiunto il timeout nella costruzione
della mappa. Profilazione del riproduttore sotto gdb: quadratura delle derivate
miste alla profondita'29, dominata da cancellazione sulle campate NURBS fitte.
Il codice corrente integra soltanto le lunghezze delle isocurve esatte;
la corrispondenza piana viene rappresentata da una NURBS e invertita su quella.
La sua precisione di approssimazione metrica e' distinta dal budget di fit e
di connessione del B-rep finale. Nessun esito sul caso reale ancora attestato.


## Mappa implementata e stabilizzazione della giunzione

La nuova opzione **Metodo / Piega da quilt appiattito** genera internamente la
corrispondenza comune; non aggiunge un comando separato per creare una feature
piana Flatten Quilt visibile. Riferimenti associativi, ancoraggio, piano,
traslazione, rotazione e metodo sono persistiti; la mappa viene ricostruita
in rigenerazione, senza salvare una seconda copia dei carrier destinazione.
Sono supportati un carrier oppure due facce tangenti dello stesso corpo con
giunto U isoparametrico e parametrizzazione affine compatibile lungo il bordo.
Altre reti e giunzioni vengono rifiutate esplicitamente.

Y e' l'arco della meridiana comune. X e' ricavata dagli archi delle isocurve
trasversali e rappresentata da una NURBS piana. Il budget di approssimazione
di questa corrispondenza e'1ppm della diagonale della faccia: non e' il budget
di contatto del B-rep finale. Le differenze dei poli della NURBS polinomiale
certificano la monotonia trasversale. Origine e scala sulla giunzione usano
posizione e derivata del carrier esatto, eliminando il salto del fit piano.

Su punti distanti2e-8mm ai due lati della giunzione reale, le distanze sono
circa2e-8mm anche a quota0,1: il salto iniziale fino a2,9e-6mm e' eliminato.
La cella81 e' valida, scarto circa2,38e-8mm. Tutti i vertici delle155copie
rientrano nel quilt.

La prima prova completa ha raggiunto il limite NURBS alla copia104.
Il fit ora riceve la preimmagine della giunzione come nodo esplicito sulle
superfici sorgenti affini quando la linea e' isoparametrica. Le valutazioni
ripetute di coordinate longitudinali, posizione e normale sono riusate con
cache locali alla singola piegatura, senza stato condiviso mutabile. Solo
l'arrotondamento della trasformazione affine e' corretto sulla giunzione;
nessuna estensione dei trim.

Per il solo percorso quilt lo stencil delle derivate e'0,01della campata,
contro0,001nei metodi precedenti: riduce l'amplificazione del rumore nella
seconda derivata mista. Scarto, connessione1e-7mm e fit interno2e-8mm restano
invariati. La copia104 passa in139ms con scarto2,05557e-8mm. Build e verifica
completa della versione ottimizzata sono ancora in corso a questo checkpoint.

La regressione prima dell'ultima ottimizzazione ha superato18/18CTest in11,99s;
le prove mirate piu' recenti del kernel passano. Nessun esito del pattern
completo o della build finale viene dichiarato prima della relativa prova.


## Prima verifica completa riuscita — fit cubico

App e runner Linux compilati;18/18CTest PASS in11,79s. Wayland su input
congelato155copie:155regioni solide/1085facce,scarto riportato3,69235e-8mm.
Su77345punti delle facce alle quote sorgenti0/0,1mm, scarto massimo
2,15128e-8mm dalla mappa del carrier esatto. Conferma/modifica,Undo/Redo,
salvataggio/lettura e rigenerazione dell'ultima funzione senza cache PASS.
Log `/tmp/forgecad-quilt-native-optimized155.log`.

Parametri: F5+F6 di Solido cucito16,Piano4,U=0,V circa0,521818,
X circa0,723794mm,Y circa0,135235mm,angolo90gradi,quota normale0.
Copia QA `KnifeHandle-155-F5-F6-quilt.prt`,74973090byte; originale invariato.

**Distorsione misurata:** rapporti fra lunghezze dei bordi risultanti e
sorgenti0,794608–1,14322 (circa-20,5%/+14,3%);volume81,7705mm3.
La mappa assicura il contatto ma non conserva rigidamente le proporzioni delle
celle. Non e' dichiarata equivalente alla qualita' della parametrizzazione
proprietaria Creo. Sviluppo esatto e posa rigida restano operazioni distinte.

Il risultato contiene2391902poli NURBS,massimo23200per faccia. Per ridurre
questa densita' e' in verifica un fit Hermite quintico a derivate condivise,
con gli stessi budget geometrici, prima di sostituire la copia cubica.


## Consegna finale — fit quintico e limiti confermati

Il fit nuovo usa celle Hermite quintiche: valori e derivate ai bordi piu'
valori interni a1/3e2/3; derivate condivise per la continuita' fra celle
lisce. Le combinazioni sono calcolate rispetto a un'origine locale. Le
verifiche dello scarto usano9posizioni per asse, oltre ai bordi; i metodi
precedenti conservano il fit cubico. Budget geometrici e limiti di poli/celle
non sono stati aumentati. La copia104 passa in40ms e3382poli complessivi.

- Compilati app e runner Linux;18/18CTest PASS in11,61s. Aggiunta e superata
  anche la prova STEP del solido piegato con superfici quintiche.
- Wayland sul file155:155solidi/1085facce, scarto riportato3,46245e-8mm.
  Su77345punti lo scarto massimo e'2,68829e-8mm. Conferma/modifica,Undo/Redo,
  persistenza e rigenerazione dell'ultima funzione senza cache PASS.
- Rapporto lunghezze bordi0,794608–1,14322 e volume81,7705mm3 confermati:
  il fit piu' compatto non cambia la legge di deformazione o ne elimina le
  distorsioni. Le proporzioni del riferimento Creo non sono ancora replicate.
- Poli2391902→1385260 (-42,09%), massimo16426per faccia. Copia salvata
 74973090→52465312byte (-30,02%). Originale invariato, SHA256 completo
 `7f89e50938bb8639f47b01e450069b399874e0ac499490c69249f79fa396798b`.
- Controllo verso, su punti interni di F5/F6: normale orientata verso l'esterno
  del solido (p+0,05N esterno,p-0,05N interno). Rendering Wayland verificato
  anche senza piani/griglia e con spigoli posteriori nascosti.
- Mappa613file/8610relazioni/296TU/0diagnostiche;check PASS. Collegamenti
  call/include verificati nei due versi; overview e9dettagli HTML renderizzati
  e ispezionati. HTML/JSON generati restano ignorati da Git.

Il template del file congelato contiene prismi pentagonali (7facce ciascuno):
la funzione conserva questa topologia, senza convertirla negli esagoni della
foto di riferimento. La nuova mappa e' una prima implementazione basata sulle
isocurve, non l'algoritmo proprietario PTC ne' una garanzia di stiramento nullo.
Supporto attuale: una faccia oppure due facce tangenti con giunzione U; mappa
generata internamente, senza un comando separato che produca la feature
Flatten Quilt piana. Queste limitazioni restano esplicite anche nei README.

Copia di consegna, nella cartella QA ignorata:
`forgecad-release/knifehandle-wrap-twofaces-qa/KnifeHandle-155-F5-F6-quilt.prt`.
La versione precedente e' conservata come `KnifeHandle-155-F5-F6-quilt-cubico.prt`.
Evidenze finali: `/tmp/forgecad-quilt-native-quintic155.log`,
`/tmp/forgecad-quilt-quintic-final-tests.log`, `/tmp/forgecad-quilt-step-tests.log`,
`/tmp/forgecad-quilt-quintic-render.log` e i log architetturali omonimi.

Per utilizzare la nuova versione occorre riaprire `forgecad-release/forgecad`.
Nel pannello di adattamento scegliere Metodo / Piega da quilt appiattito,
selezionare Copie e le due destinazioni, quindi Centra e allinea dalla sorgente.
Le funzioni gia' salvate mantengono il loro metodo finche' modificato.
README inglese e italiano aggiornati. Nessun commit o push in questo intervento.


## Verifica di KnifeHandle su Piano 5

L'utente non riesce a distendere le superfici sul nuovo Piano 5 e conferma che
si tratta delle due facce F5/F6 gia' usate per la deformazione. Copia congelata
`forgecad-release/knifehandle-unroll-plane5-qa/input-920a5daf.prt`:
52.272.011 byte,175 feature/11 schizzi, SHA256
`920a5daf0e30c64a1a525775139c5b6c7d93719c56c60950722ab8aad7e1b7ba`.
La feature173 contiene la deformazione quilt delle155 sorgenti; Piano5 e'
la feature174. Non e' salvata una feature di distensione fallita.

Piano5 si rigenera e si risolve regolarmente: origine(0,0,-14,5)mm e normale
(0,0,1). Non e' un errore del riferimento al piano. La chiamata applicativa
`buildSurfaceMapping`, con riferimenti nominati alle facce originali e al
piano, riproduce il rifiuto di Solido cucito16:F5/F6. In punti interni al trim
le derivate esatte dei carrier danno rispettivamente K=0,002932 e
0,00293034mm^-2: entrambe hanno doppia curvatura, incompatibile con uno sviluppo
isometrico piano. Taglio12:F0/F1 conserva gli stessi carrier e lo stesso limite.
Non e' stato allentato il requisito dell'utente di sviluppo senza deformazione.

Controllo positivo sullo stesso Piano5: Taglio12:F2-F5 e Solido cucito16:F7-F10
(cilindri), oltre alla faccia piana F11, si sviluppano con B-rep valido e scarto
dal piano non superiore a1,78e-15mm. Dal pannello Wayland sono state selezionate
le quattro facce cilindriche di Taglio12 e Piano5: anteprima, Conferma, Undo/Redo,
salvataggio/lettura della copia QA e rigenerazione senza cache del risultato PASS.
Area907,803mm^2, differenza prima/dopo2,08811e-7mm^2. Picking iniettato mediante
il callback standard: non e' una prova del clic fisico del mouse.

Il metodo Piega da quilt appiattito costruisce la propria corrispondenza
internamente, ma non crea una superficie piana visibile con il comando
Distendi superficie. Appiattire F5/F6 richiederebbe un'opzione distinta,
non isometrica e con deformazione misurata; resta da implementare e non viene
sostituita automaticamente allo sviluppo esatto. Nessun algoritmo di produzione
modificato o eseguibile ricompilato in questo controllo: compilate solo le sonde
QA contro gli oggetti dell'app esistente. Aggiornati rapporto e memoria.

Log: `/tmp/forgecad-plane5-inspect.log`, `/tmp/forgecad-plane5-native.log`.
Sonde, screenshot e copia di sviluppo cilindrico nella directory QA indicata.
Hash del CAD originale identico dopo le prove; nessuna modifica al file utente.


## Appiattimento delle superfici autorizzato — implementazione

Richiesta: aggiungere l'opzione di appiattimento e tradurre quilt anche nella
deformazione. Distendi superficie ora propone Sviluppo esatto (senza deformazione)
e Appiattisci insieme di superfici; nella deformazione il metodo e' Piega da
superfici appiattite. Identificatori C++ e chiave JSON quilt restano compatibili.

La lamina usa la stessa corrispondenza piano/superfici della piegatura. Il bordo
topologico comune resta unico con due usi; contorni e fori sono trasferiti da
SP-curve a NURBS piane entro un budget fissato prima del fit. Genealogia esplicita
di facce/bordi/vertici. Nessuna riassociazione per vicinanza. Schema7 per il nuovo
appiattimento; schema6 della piegatura e vecchi metodi mantengono il comportamento.

Il pannello riporta variazioni lineari principali e di area, ricavate dai
Jacobiani in punti interni al trim, e scarto dei contorni. Sono misure campionate,
non estremi globali certificati. Origine suggerita sul centro del bordo comune
o in un punto interno, con coordinate U/V modificabili. Supportate una faccia
o due facce tangenti dello stesso corpo con giunzione U isoparametrica;
le superfici periodiche devono essere prima aperte.

Kernel delle mappature PASS: lamina NURBS multifaccia su piano inclinato,
ordine inverso, genealogia del bordo comune, metriche, piano senza deformazione
artificiale e STEP. App/GUI KnifeHandle/schema7/Undo/rigenerazione/mappa in verifica.


### Esito sul caso reale congelato 920a5daf

La prima prova ha rifiutato punti delle SP-curve canoniche per riclassificazione
vicino al trim. Corretto il percorso dei contorni: i punti derivano direttamente
dall'uso topologico del bordo, senza una nuova proiezione o classificazione;
la mappa di punti liberi conserva il controllo del trim. Nessun aumento di tolleranza.

F5/F6 su Piano5 PASS: lamina con2facce/7bordi/6vertici e un solo discendente di
E20. AncoraggioU=0,V=0,601843861038. Piano effettivo z=-14,499999999770772mm:
il confronto con il suo riferimento esatto da' scarto0; una prima asserzione
contro la quota nominale -14,5 arrotondata era troppo restrittiva ed e' stata
corretta nella sonda, senza alterare il modello o i budget del kernel.
707punti sui bordi: errore massimo del ritorno sul carrier3,02875e-8mm;
scarto del fit piano riportato1,99798e-8mm.346punti interni: variazioni principali
lineari -16,70%/+32,41%, area locale -7,82%/+18,20%. Non sono estremi globali.

Wayland: selettore dei due metodi, scelta F5/F6 e Piano5, ancoraggio automatico,
Esito con metriche, anteprima/Conferma/modifica, Undo/Redo, salvataggio/lettura
schema7 e rigenerazione senza cache del risultato PASS. Selezioni iniettate
nel callback standard; non e' una prova del clic fisico sulla geometria.
Copia pronta: forgecad-release/knifehandle-flatten-qa/KnifeHandle-F5-F6-Piano5-appiattite.prt,
52.316.007byte. Screenshot pannello e viewport nella stessa directory ignorata.

Prima suite:17/18PASS, errore nell'asserzione GUI sulla visibilita' effettiva
del widget; il test ora distingue metodo persistito e visibilita' esplicita
della riga. La verifica della genealogia della sonda richiedeva inoltre la
normale registrazione della feature, ora effettuata prima di consultarne la storia.
Risultati finali della suite e della mappa in verifica.


### Chiusura dell'appiattimento

App completa e runner aggiornati; suite finale18/18PASS in13,19s.
Mappa613file/8624relazioni/296TU/zero diagnostiche e controllo aggiornamento PASS;
collegamenti call/include/reference verificati nei due versi, nove dettagli HTML
renderizzati e modulo principale ispezionato. Nessuna CI remota, commit o push.

Durante le prove l'originale e' stato salvato con modifiche esterne: nuova copia
congelata input-latest.prt,60.124.276byte, SHA256
18dfd03f8f2c49a271ea5f941bf1539eed324e5681328f5e9187edc15dd875e0.
Contiene151feature e131copie, Piano5 all'indice149. Il B-rep B15 e' identico
alla prima copia (SHA256 binario a0d441b622847c7570cab0ee580c5333011a888b7f562a8392142423118ce95b).
Ripetuta tutta la prova GUI/contorni/Undo/codec/rigenerazione anche su questa
versione: PASS. La copia pronta e' stata aggiornata all'ultimo input:
KnifeHandle-F5-F6-Piano5-appiattite.prt,59.918.533byte; la prima con155copie resta
con suffisso -155.prt. Le prove non hanno scritto l'originale.
Log finali /tmp/forgecad-flatten-final-tests.log e /tmp/forgecad-flatten-latest-native.log.
