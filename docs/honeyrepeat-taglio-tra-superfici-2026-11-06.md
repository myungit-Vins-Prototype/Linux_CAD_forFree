# HoneyRepeat — taglio di tutti i solidi tra due superfici

Input congelato: `forgecad-release/honeyrepeat-trim-qa/input-22a7d106.prt`,
76.493 byte, SHA256 `22a7d106e7e2bd09ff3a06ec276ad25549152370646e35aaad5a38bb719c4070`.
La prima copia non conteneva Offset4; dopo il salvataggio dell'utente sono
presenti entrambe le facce limite. Originale non modificato.

Richiesta: conservare le porzioni del Corpo2 (Estrusione3, 13 solidi) tra
B0:F1 (Rivoluzione1) e B3:F0 (Offset4). Il precedente Taglia superficie
converte gli operandi in lamine e sceglie una sola regione: non risponde
alla richiesta di mantenere tutti i volumi chiusi.

Implementazione: `clipSolidByFace` riusa intersezioni B-rep, suddivisione
facce e assemblaggio delle booleane. Conserva i sottoinsiemi sul lato
scelto e aggiunge le porzioni della faccia strumento interne al solido
come chiusure orientate. Non usa mesh o estrusioni numeriche approssimate.
`trimSolidBetweenFaces` compone i due tagli e la genealogia con tre operandi.
I lati automatici puntano verso l'altro limite, con inversioni indipendenti.
I limiti finiti devono attraversare completamente i volumi: nessuna
estensione implicita; un risultato non chiuso viene rifiutato.

Nuova funzione associativa SurfaceTools modo5, schema estensioni9:
corpo sorgente, due riferimenti a faccia e due inversioni. Preview e cache
comprendono i nuovi parametri; la storia conserva le dipendenze e nasconde
solo la precedente versione del corpo. Le superfici limite restano disponibili.
Il pannello permette selezione nella vista/storyboard, rimozione,
evidenziazione e anteprima. Il comando inizialmente separato è stato incorporato
in Taglia superficie come descritto nella sezione finale.

Prova kernel sul file: 104 facce, 13 shell solide, B-rep/loop validi, storia
con3 operandi, 139ms. Test sintetici: piani/cilindri, componenti disconnesse,
cavità, contatti senza volume, strumenti insufficienti, volumi esatti,
origine delle chiusure e determinismo1/4thread PASS.
App completa compilata. Prova Wayland del pannello PASS: rimozione del solo
limite scelto, Annulla senza modifiche, Conferma,13solidi/26chiusure curve,
contenimento su punti dei bordi esatti, visibilità di sorgente/strumenti,
Undo/Redo e salvataggio/rilettura schema9. Tempo GUI1332ms (inclusa attesa
automatica prima del clic). Le catture del pannello e del risultato sono
nella directory QA; sei CTest applicativi/kernel e38/38Boolean PASS.
Il test applicativo verifica anche rigenerazione senza cache, identità del
corpo, dipendenze dai tre operandi, invalidazione delle anteprime con le
inversioni e rifiuto atomico di un downgrade del codec.

Copia separata: `forgecad-release/honeyrepeat-trim-qa/HoneyRepeat-tagliato-tra-superfici.prt`.


Build finale di app e runner completata. Prova Wayland ripetuta sulla
build definitiva:1121ms inclusa l'attesa automatica; tredici solidi chiusi,
26facce curve e punti dei bordi nei due semispazi richiesti.
Vista architetturale:621file/8752relazioni/301unità, nessuna diagnostica;
check e rendering generale più7dettagli PASS. Controllate relazioni
entranti/uscenti e distinzione chiamate/include/riferimenti.

Risultato:583.083byte; SHA256 `21559039fd328a57d7d572cf2d1f264a27a7fcc39c43c96969c573dc1aca519a`.


## Ripetizione circolare dei tredici solidi — 6 novembre 2026

Input successivo congelato in `forgecad-release/honeyrepeat-pattern-qa/input-01f7151b.prt`,
533.274 byte, SHA256 `01f7151b37c5137b498cf46f83302a743d64dbf5be17066c36359eb46e238434`.
Contiene le cinque funzioni, compreso Taglio tra superfici 5, con tredici solidi.
L'originale resta estraneo alle prove e ai salvataggi.

Blocco riprodotto con due istanze a 180°: dopo 45 secondi non era terminata
la costruzione della ripetizione. Il debugger individuava la suddivisione
ricorsiva dei carrier nelle intersezioni delle booleane. `faceBox` usava
il periodo intero di U/V per ogni faccia di rivoluzione, anche quando il
ritaglio occupava pochi gradi. Le copie opposte apparivano sovrapposte.
La faccia esterna F92 attraversa inoltre la giunzione periodica V: alcune
SP-curve hanno parametri vicini a zero, altre vicini a 2π.

Correzione in `kernel/fk_classify.cpp`: per un singolo contorno locale si
riportano le SP-curve nello stesso periodo mediante traslazioni intere;
si calcolano gli inviluppi delle curve e l'orientamento tramite integrale
di area. Solo un contorno orientato verso il materiale e contenuto in meno
di mezzo periodo abilita il box locale, ricavato dai poli NURBS esatti.
Il box include i budget esistenti degli edge/SP-curve e l'arrotondamento
numerico. Non cambiano geometria, tolleranze delle operazioni o genealogia.
Fori su carrier completi, fasce avvolte, più loop o SP-curve mancanti/non
certificabili conservano il percorso precedente. Nessuna booleana è omessa
sulla base di un'approssimazione a mesh.

Verifiche:

- Riproduttore sul file: generazione due istanze 58 ms, separazione 141 ms,
  26 solidi validi con controllo B-rep e loop.
- Pannello nativo Wayland: due istanze 262 ms / 26 solidi; modifica a dodici
  istanze 2.767 ms / 156 solidi. Tempi della creazione/modifica, senza attese
  artificiali del clic. Tutti i risultati sono solidi chiusi, una shell
  ciascuno; il B-rep sorgente rimane identico byte per byte.
- Undo, Redo, salvataggio e riapertura PASS; cattura del risultato controllata.
- Regressione sintetica senza CAD dell'utente: tredici blocchi tagliati tra
  due rivoluzioni e unione con la copia ruotata, 26 shell valide. Test di
  ritagli su toro/rivoluzione, seam U/V, rotazione, complemento del contorno,
  fasce avvolte e SP-curve mancanti.
- 7/7 CTest: ripetizioni/variazioni, genealogia, taglio e limiti delle facce.
  Boolean 38/38, Marching 10/10, Mass 11/11 PASS. App e runner ricompilati.
- Mappa aggiornata: 621 file, 8.770 relazioni, 301 unità, zero diagnostica;
  controllo hash PASS. Verificate chiamate e include nei due versi, inclusi
  Boolean → Classify e Classify → Profile.

Copia separata con dodici istanze:
`forgecad-release/honeyrepeat-pattern-qa/HoneyRepeat-ripetizione-circolare.prt`.
Risultato: 2,877,658 byte; SHA256 `502bd3daa8ce85faa14e9250b2c9f7cfb45a7674e40d66be48e720a15c4be462`.

Rendering architetturale: panoramica e tre dettagli verificati su XCB con
rendering software; il visualizzatore Python/WebEngine su Wayland è
terminato con un crash, senza coinvolgere l'app CAD. La prova nativa CAD
su Wayland è riuscita. Hash dell'originale ricontrollato: invariato.


## Pannello unico Taglia superficie — 6 novembre 2026

Richiesta: incorporare il taglio tra due superfici nel comando Taglia superficie.
Il selettore **Tipo di taglio** ora offre **Con uno strumento** e
**Tra due superfici (solidi)**. Corpo sorgente, anteprima e conferma sono
comuni; ogni modalità mostra i propri controlli. La seconda mantiene le
selezioni delle due facce, la rimozione indipendente e l'inversione dei lati.
Il comando separato è rimosso: menu e ribbon aprono il pannello comune.

La funzione `trimDialog` è spostata da `forgeCad2026_dialogs_surfaces.cpp`
a `forgeCad2026_dialogs_trim.cpp`; quest'ultimo sostituisce il pannello
separato. `surfaceToolDialog` instrada qui i tagli modo5 già salvati.
La GUI crea e modifica il tipo corretto, anche passando da una modalità
all'altra. Il cambio interrompe il picking precedente, pulisce le evidenze
e rimuove dalla definizione gli operandi della modalità inattiva; i campi
rimangono disponibili tornando alla modalità precedente nello stesso dialogo.
Formati, kernel e genealogia del taglio non cambiano in questa integrazione.
README italiano e inglese aggiornati con il nuovo percorso.

Verifiche del pannello comune:

- Copia congelata HoneyRepeat `input-22a7d106.prt`, Wayland: 13 solidi chiusi,
  26 chiusure curve, controllo B-rep/loop e contenimento sulle facce esatte,
  rimozione del singolo limite, Annulla, Conferma, Undo/Redo e codec PASS.
  Tempo complessivo della conferma nel test: 1.004 ms, inclusi 900 ms di
  attesa automatica. Dati e catture in `forgecad-release/honeyrepeat-unified-trim-qa`.
- Comando reale e modifica dalla GUI, offscreen e Wayland: due modalità,
  input incompleto, cambio durante la selezione, conservazione delle facce,
  conversioni nei due sensi, identità della feature e Undo/Redo PASS.
- La prima versione del test di conversione premeva Conferma prima della
  visualizzazione: la preparazione delle regioni elabora già eventi Qt.
  Il test ora attende il pannello visibile e i gestori di conferma installati;
  non è stata introdotta una modifica del kernel per questo errore del test.
- Test delle regioni standard/reciproche `--trim-pick` PASS.
- App e runner completi ricompilati. 5/5 CTest PASS: pannelli, cache della
  storia, evidenze dei riferimenti, taglio tra superfici e proiezione.
  `--trim-pick` ripetuto sul runner finale: PASS.
- Mappa: 621 file / 8.774 relazioni / 301 unità, zero diagnostica e check PASS.
  Verificati chiamanti GUI/SurfaceTools, chiamate verso picking/preview/regioni,
  include e riferimenti distinti nei due versi; panoramica e tre dettagli
  HTML controllati su XCB. Controlli dei campi e scorrimento su Wayland PASS.

Le prove non scrivono il documento originale. Il formato SurfaceTools modo5
rimane compatibile con i tagli creati prima dell'unificazione del pannello.

## Unione di tutti i corpi tranne Corpo 3

Copia congelata del 6 novembre, SHA256
`fb13d236fab2939765a0bc65c029de4e6328686973467d4d11b63efd18f824ad`:
308 feature, 131 risultati disponibili. Corpo 3 è la superficie Offset 4
(indice feature 3) ed è escluso. Le unioni già salvate arrivano a Unione 308,
Corpo 1 (indice 307); restano 129 copie indipendenti, indici 137–265.

L'errore riprodotto anche con la sola Rivoluzione 1 e la copia 138 era
`risultato non chiuso (Body::build: l'edge 0 non ha due fin opposte)`.
Entrambi gli operandi superavano il controllo B-rep. Le chiusure F0/F2 della
rivoluzione sono coni quasi piani: l'intersezione dei raggi di classificazione
perdeva le radici delle due falde vicine, prima del controllo del contorno.
Alcune facce esterne venivano quindi classificate interne ed eliminate.

`kernel/fk_intersect.cpp` risolve ora la quadratica con sin/cos e un'origine
ricentrata per i coni quasi piani, conservando entrambe le radici fino al
controllo della faccia. I coni quasi cilindrici mantengono l'origine del raggio,
per evitare cancellazioni dovute a un vertice lontano. Intermedi aritmetici
estesi; coordinate, geometria e risultati restano double. Nessuna modifica
alle superfici del documento, né aumento delle tolleranze geometriche.

`cad_feature_build.cpp` aggrega gli strumenti dell'Unione con l'albero
bilanciato già usato da `cad_pattern`, quindi unisce l'aggregato ad A una volta.
La genealogia compone tutti gli operandi originali. In caso di insuccesso
rimane il percorso sequenziale, con il nome dello strumento nell'errore.
Differenza e Intersezione conservano l'ordine precedente.

Verifica completa sulla copia, finestra reale PdfWindow/Wayland/OpenGL:

- Unione di Corpo 1 con le 129 copie: 46.522 ms sulla build finale (51.568 ms nella prima prova),
  2.096 facce, una shell solida,
  controlli B-rep e loop PASS. Tempi locali con altre verifiche in corso.
- Corpo 3 resta un risultato indipendente; la genealogia contiene 130 operandi,
  tutte le facce hanno origini dichiarate e tutti i corpi contribuiscono.
- Undo/Redo, riuso dei B-rep precedenti, salvataggio e rilettura PASS.
- Framebuffer controllato: tutti i rilievi sulla circonferenza; originale
  invariato, verificato con SHA256. Copia del risultato e log locali in
  `/tmp/honeyrepeat-union-20261106/`.
- Il percorso sequenziale, dopo la correzione dei raggi, aveva completato
  37 strumenti senza errori; interrotto volontariamente per passare alla
  prova completa bilanciata. Nessun tempo totale precedente inventato.

Regressioni aggiunte: `test_cone_rays.cpp` riproduce il difetto numerico su
geometria sintetica (falliva con la libreria precedente); controlla raggi,
coni di entrambi i segni, tangenza, generatrici, classificazione e unioni
con ordine e thread differenti. `test_boolean_history.h` controlla volume,
genealogia completa e Undo/Redo dell'unione bilanciata di dieci strumenti.

Verifiche sulla build finale: app e runner ricompilati; 9/9 CTest UI/storia e
5/5 CTest kernel PASS. Selezione a rettangolo senza A preselezionato ripetuta
su Wayland PASS. L'unione completa e il controllo del framebuffer sono stati
ripetuti con la libreria finale, conservando gli stessi 130 operandi e 2.096
facce. Il risultato di prova è `HoneyRepeat-unito-escluso-Corpo3.prt`.

Suite supplementari sul kernel finale: Boolean 38/38 e Intersect 5/5 PASS.
Mappa aggiornata: 624 file, 8.822 relazioni, 303 unità senza diagnostica;
`--check` PASS. Controllati i due versi delle relazioni tra classificazione e
intersezioni, feature build e aggregatore pattern, dialogo e selezione.
Include e riferimenti a funzione distinti dalle chiamate. Quattro dettagli
HTML renderizzati, stato del pannello verificato e i dettagli kernel/build
ispezionati visivamente. `git diff --check` PASS. Nessun commit o push.
