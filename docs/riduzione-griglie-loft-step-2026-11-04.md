# Riduzione delle griglie loft e dei file STEP — 4 novembre 2026

Richiesta: pubblicare prima il lavoro precedente, poi ridurre le griglie e lo STEP.
`3c644e2` pubblicato su `origin/main` e hash remoto verificato prima delle modifiche.

## Correzione integrata

La loft quintica raffinava ogni nodo delle guide alla molteplicita' 5, anche quando il fit
adattivo aveva gia' prodotto nodi C2 di molteplicita' 3. L'unione diventava una lista di
ascisse senza conservarne le molteplicita', poi tutte le righe della superficie venivano
espanse a quella griglia sovrabbondante.

Ora l'unione dei vettori espansi conserva la **massima molteplicita' richiesta in ogni nodo**.
Le righe vengono raffinate soltanto quanto serve. Le sezioni mantengono i loro ancoraggi e
le guide conservano i propri nodi reali e giunti. I nodi distinti restano distinti;
nessuna fusione per vicinanza, nessuna approssimazione o modifica delle tolleranze.
Il grado e la definizione geometrica delle sezioni rimangono quelli precedenti.

La cache semantica 11 scarta atomicamente le vecchie griglie dei documenti lavorativi con
loft attive e rigenera i derivati dalle definizioni. I check-in storici conservano lo snapshot.
Formato documento 62 e BodyIO 2 invariati; nessun aumento del limite cache standard di 1 GiB.

La riduzione dello STEP deriva dalla griglia piu' piccola esportata come superficie NURBS.
Gli specchi restano espliciti nello STEP per la compatibilita' gia' verificata con lettori esterni.
La condivisione delle basi nei documenti resta quella del [lavoro precedente](riduzione-complessita-specchi-2026-11-03.md).

## Prove e misure

Il corpo corrente e i due STEP ridotti sono stati rigenerati, salvati e riletti. Le sonde e i log
sono in `forgecad-release/complexity-reduction-qa/`, esclusi da Git. L'originale corrente e la
copia congelata conservano SHA256 `8aaae87956e4c19818a6a0447646b615c196721bcca441e82444e6880e53498b`.

| Dato | Prima (byte/poli) | Dopo | Riduzione |
| --- | ---: | ---: | ---: |
| PRT corrente, 26 feature incl. Svuota1 | 190.498.981 | 88.711.773 | 53,43% |
| PRT rispetto alla sola condivisione degli specchi | 116.868.166 | 88.711.773 | 24,09% |
| STEP corpo Raccordo25, 33 facce | 745.117.543 | 458.666.865 | 38,44% |
| STEP corpo svuotato, 64 facce | 928.478.475 | 642.734.063 | 30,78% |
| Poli NURBS logici Raccordo25 | 6.454.452 | 3.980.010 | 38,34% |
| Cache cumulativa B-rep | 1.082.774.364 | 686.375.092 | 36,61% |

Lo STEP baseline dello svuotamento e' stato effettivamente esportato dal risultato congelato,
non stimato dalla dimensione del B-rep. I tempi di esportazione misurati sono 21,5 s baseline
svuotato, 7,1 s corpo ridotto e 16,6 s svuotato ridotto; macchine/processi concorrenti,
nessuna promessa di guadagno universale. Rilettura piu' confronto del corpo ridotto 41,5 s,
dello svuotato 74,8 s. Entrambi hanno esattamente gli stessi poli, pesi, nodi e gradi rispetto
al B-rep esportato; un corpo e tutte le facce attese, checkBody con loopCrossings PASS.
Il lettore segnala bordi tolleranti fino a 0,005582 mm, coerenti con un B-rep sorgente che gia'
contiene bordi tolleranti: non viene dichiarata precisione globale 1e-9 del modello completo.

Le griglie principali passano da 13x671 a 13x407, da 2311x706 a 2311x434 e da 2177x726 a
2177x448. Gradi e nodi U invariati. L'audit comprende 79.992 campioni su tutte le campate: errore di posizione massimo delle sei
superfici loft 4,038e-13 mm e seno angolare 2,855e-12. Il conteggio include anche le superfici
dei raccordi, che richiedono un confronto
distinto perche' la rigenerazione puo' cambiarne la parametrizzazione.

Sui raccordi, proiezioni bidirezionali limitate alle facce rifilate: errore massimo misurato
1,449e-11 mm e seno angolare 7,638e-10. Un primo confronto agli stessi UV era inappropriato:
le superfici di passaggio cambiano dominio parametrico. I bordi rigenerati non sono identici:
scarto bidirezionale massimo 0,0022493 mm, entro il 9,123% del relativo budget locale originale.
E35 aveva gia' tolleranza 0,024656 mm; non e' corretto applicare a questi raccordi la promessa
di fit 1e-9 delle sole guide loft. Le tolleranze locali sono ricalcolate dall'algoritmo esistente
(massimo sul corpo 0,072151→0,094814 mm); costanti e controlli del kernel non sono stati allargati.
La QA conserva separati il confronto UV fallito, il confronto geometrico delle facce e la
verifica dei bordi entro i budget preesistenti.

Il documento corrente rigenera tutte le 26 feature senza errori in 207,4 s nel lancio misurato.
Conserva nomi/featureId; salvataggio e riapertura conservano esattamente poli/pesi/nodi/gradi
NURBS, topologia e vertici. I costruttori degli assi analitici seguono il contratto BodyIO;
scarto campionato sui bordi nel roundtrip nativo 0. La cache ora e' accettata con i limiti
standard, senza l'override QA di 4 GiB.

Svuota1: 64 facce, un guscio chiuso, 31 pareti interne, 662 campioni di spessore con errore
massimo 5,83439782e-8 mm; aperture F0/F14, genealogia, B-rep e mesh PASS.

Copie consegnabili: `SurfaceModellingExample219-griglie-ridotte.prt`,
`SurfaceModellingExample219-corpo-griglie-ridotte.step` e
`SurfaceModellingExample219-svuotato-griglie-ridotte.step` nella cartella QA.
App e runner completamente compilati. Loft42/42, Blend60/60 e Shell18/18 PASS; nove CTest
mirati PASS (sei kernel e lettura/genealogia/revisioni). Il test delle griglie comprende due
casi, inclusa la conservazione di un vero angolo della guida. Wayland/OpenGL sulla copia
ridotta ricaricata con budget standard PASS; immagine `grid-result-wayland.png` ispezionata.
Mappa: 574 file, 7.962 relazioni, 274 unita' AST, nessuna diagnostica; controllo e resa HTML
con collegamenti entranti/uscenti e filtri call/include/reference verificati.

La nuova regressione indipendente da OCCT usa una sezione quintica con velocita' diverse al giunto,
una conica razionale e due guide curve, ripetendo G1 e G2. Verifica B-rep, sezioni e intere guide
entro i budget esistenti e conservazione dei nodi C2.

## Esperimento sul grado escluso dalla produzione

E' stato provato separatamente il mantenimento del grado naturale dei denominatori razionali
nelle operazioni Bernstein. Le loft 13/15 passavano rispettivamente da 2311x706 e 2177x726
poli a 1681x434 e 1601x448, con grado U 24/25 anziche' 33/34.
Il confronto locale su ogni campata misurava scarto massimo di posizione 6,724e-13 mm e
seno angolare 4,312e-12; 42 regressioni loft e i test indipendenti passavano.

La rigenerazione completa del documento ha pero' fallito Raccordo25 con «i raccordi del
coperchio non coincidono sulla cucitura del fianco». Per questo l'esperimento e' stato rimosso
dai sorgenti di produzione. Non sono state aumentate le tolleranze del raccordo o indeboliti
i controlli. Il diff sperimentale e i B-rep prima del fallimento restano nella cartella QA.
Non e' dimostrata una causa generale del fallimento del raccordo; i confronti locali delle
loft non bastano a certificare l'intera storia.

La correzione consegnata riguarda i nodi superflui. Il prodotto dei denominatori e i gradi U
33/34 restano un limite distinto: una futura riduzione dovra' superare anche raccordi e
svuotamento del documento completo. Anche il numero delle pezze Bezier del proiettore puo'
restare invariato: i minori poli memorizzati non implicano un miglioramento universale dei tempi.
