# CI Linux e macOS — 10 novembre 2026

Richiesta: controllare le esecuzioni GitHub fallite negli ultimi commit.

## Diagnosi verificata

L’ultimo [run 42777286467](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/42777286467),
commit `7dda9c9`, compila applicazione e tutti i target su Linux e macOS.
Il fallimento è nella fase CTest:

- Linux: 81/82 suite riuscite o saltate per assenza di GPU; `forgekernel_open_offset_tests`
  interrotta dal limite esplicito CMake di 180 secondi, dopo i primi tre casi.
- macOS: stesso timeout e un errore in `BlendOpenChainsAtTangentSplineSeams`,
  modalità 2 / lato 1 / raggio 0,06 mm: chiusura terminale non risolta.
  Suite generale 474/475 casi riusciti.

Controllati tutti gli otto run falliti recenti: stesse due cause, build riuscite.
Il primo ha Linux verde; nei successivi ricorre il timeout anche su Linux.

| Commit | Run GitHub |
| --- | --- |
| `a388270` | `42479610221` |
| `ddf856a` | `42585263906` |
| `9f455a9` | `42591502293` |
| `30d4e72` | `42597313143` |
| `160e185` | `42607252330` |
| `d2bea6d` | `42625048869` |
| `a0c9915` | `42645055658` |
| `7dda9c9` | `42777286467` |

Il run multifaccia `42589299889` è stato annullato, non fallito.
I log completi sono in `/tmp/forgecad-ci-*.log`.
La suite offset ha cinque casi, compreso il nuovo spessore interno Shower Head;
il timeout CMake prevale sui 900 secondi passati da riga di comando a CTest.

## Correzione

Riprodotto il raccordo su Linux GCC e Clang con rotazione `0.7 + 1e-14` rad:
il caso prima fallisce con lo stesso errore di macOS. Su 40 rotazioni vicine,
la versione originale fallisce a due angoli. I due appoggi NURBS sono tangenti
entro l’errore del fit (circa 3,97e-10 mm). Newton tenta un’intersezione esatta:
il primo passo porta il residuo da 3,97e-10 a 0,108 mm e le iterazioni oscillano.
Il controllo finale può quindi riuscire o fallire secondo gli arrotondamenti.

`fk_blend_surface.cpp` conserva i tentativi precedenti e aggiunge una continuazione
stabile smorzata come ultimo recupero della chiusura:
accetta soltanto riduzioni del residuo geometrico e del vincolo parametrico,
conservando la soglia di accettazione originale. Il tentativo stabile è ora
accessibile anche se il bordo scelto è analitico; prima era riservato ai bordi
B-spline. I punti terminali già noti e le derivate dall’interno gestiscono la
tangenza. Il fit e il limite di scarto finale restano vincolanti; nessuna
tolleranza è aumentata. Il riproduttore isolato della chiusura scende entro
9,95e-10 mm, con budget del fit di 1e-9 mm.

Una prima estensione indiscriminata dello smorzamento non conservava tutte le
continuazioni valide: scartata. La versione finale mantiene prima il percorso
stabile originale e usa quello smorzato soltanto dopo un fallimento.

La regressione esistente aggiunge l’intera quarta rotazione, mantenendo i
controlli di raggio, tangenza, topologia, tassellazione, genealogia,
immutabilità dell’ingresso e roundtrip binario. Nessun caso precedente rimosso.

Il timeout della suite offset passa da 180 a 900 secondi, come il budget generale
CI: tutti i cinque casi originali restano attivi. Misurazione locale iniziale:
5/5 PASS, 101,49 secondi reali e 593,68 secondi CPU (più core). Con due core il
solo tempo CPU richiede già circa cinque minuti; il vecchio limite non basta.
Non è una variazione delle tolleranze geometriche.

## Verifiche locali

Build pulita da `git archive 19b06de`, con le sole tre modifiche a kernel/CMake e
alla regressione, in `/tmp/forgecad-ci-20261110/source`. Clang Release CPU,
stesse opzioni della CI; applicazione e tutti i target compilati. Kernel completo:
**475/475 PASS con GCC e con Clang**, compreso il caso fallito sui runner.

CTest Clang: 82 suite, 80 PASS e due SKIP previsti (dispositivo GPU e contesto
OpenGL assenti). Il primo passaggio nel sandbox termina in 494,94 secondi con
il solo tema Qt in timeout; rieseguito lo stesso test dall’host con CTest,
sorgenti e timeout invariati: PASS in 0,10 secondi. La suite kernel completa
Clang impiega 386,35 secondi. Verificati per hash tutti i 657 sorgenti, header e
file CMake della build pulita: identici al commit pubblicato `5ad19c4`.
La build GCC locale usa il checkout condiviso; la build Clang pulita e la CI
usano i sorgenti pubblicati, con le modifiche delle altre chat escluse dai commit.
Log JUnit e risultati sotto `/tmp/forgecad-ci-20261110/`.

- Applicazione locale `forgecad-release/forgecad` ricompilata completamente.
- Riproduttore su 40 rotazioni vicine: 40/40 B-rep validi (prima 2 fallimenti).
- Regressione completa `BlendOpenChainsAtTangentSplineSeams` GCC PASS,
  inclusa la quarta rotazione e tutti i controlli precedenti.
- Offset aperto Clang pulito: 5/5 casi PASS, 106,79 secondi reali.
- Misura controllata sullo stesso eseguibile con affinità limitata a due core:
  5/5 PASS, 189,39 secondi reali / 355,39 secondi CPU, RSS massimo circa 1,01 GiB.
  Anche questa macchina supera quindi i 180 secondi precedenti. Nessun sorgente,
  parametro geometrico o controllo variato tra le due misure.
- Mappa: 676 file / 9.751 relazioni / 331 unità, zero diagnostiche;
  `--check`, collegamenti entranti/uscenti e tre viste HTML PASS.
- Il test Qt del tema scade nel sandbox locale, anche sulla build iniziale;
  lo stesso eseguibile passa immediatamente nell’ambiente host con Qt 6.12.
  Non modificato: questo errore non compare nei log GitHub esaminati.

## Pubblicazione

Correzione pubblicata su `origin/main`: `5ad19c4b969692a790060a3d36c0982aaed502e4`.
[Nuova CI 42786261591](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/42786261591),
avviata il 10 novembre alle 08:54 UTC. macOS ha compilato e avviato i test;
Linux si è interrotto sulle dipendenze, come descritto sotto. Il nuovo push del
workflow riavvia entrambi i job, secondo la concorrenza configurata.
Modifiche locali delle altre chat escluse dai commit; la build Clang usa soltanto
i sorgenti C++ pubblicati.

## Mirrorlist APT del runner Linux

Nel nuovo run la compilazione macOS riesce; Linux si interrompe prima di CMake,
alle 09:09:31 UTC, nel limite di 15 minuti delle dipendenze. Log del job
`129728115591`: APT legge `file:/etc/apt/apt-mirrors.txt` e scarica ancora da
`http://azure.archive.ubuntu.com/ubuntu`. Il pacchetto OpenJDK headless da
348 MB resta in download oltre dieci minuti. Non è un errore del compilatore.

Il workflow già correggeva i file `.list` e `.sources`, ma il nuovo formato
`mirror+file:` rinvia alle mirrorlist esterne, che erano rimaste escluse.
Aggiunte `apt-mirrors.txt` e `apt-security-mirrors.txt` alla stessa sostituzione
limitata al mirror Azure: archivio Ubuntu ufficiale HTTPS e firme APT invariati.
Gli altri mirror e le altre impostazioni restano conservati.

Verifica locale: lo snippet precedente lascia entrambe le mirrorlist su Azure;
quello nuovo corregge anche quei file, preserva `Signed-By` e le sorgenti
`mirror+file:`, mantiene il mirror security e non cambia nulla alla seconda
esecuzione. Parsing YAML e matrice/fasi CI PASS. L’archivio HTTPS risponde e
la sua Release `resolute` include `amd64` e `amd64v3`, richieste dal runner.
La nuova pubblicazione modifica soltanto workflow e documentazione;
sorgenti C++, controlli geometrici e suite invariati rispetto a `5ad19c4`.

## Seconda pubblicazione

Fix delle mirrorlist pubblicato su `origin/main`: `994c771e56e712cc8bc4a0953220739e50a9043e`.
[CI 42789007301](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/42789007301),
avviata alle 09:14 UTC. La nuova esecuzione sostituisce la precedente ancora
attiva, secondo la concorrenza del workflow. Nessuna ulteriore modifica C++:
applicazione e test sono quelli già verificati su `5ad19c4`.
Dipendenze Linux completate in circa tre minuti. Configurazione e compilazione
complete sono riuscite su Linux e macOS. macOS ha concluso positivamente:
85 suite, 84 PASS / uno SKIP grafico previsto / zero errori, 604,32 secondi.
Artefatto scaricato e verificato: kernel475/475 e
`BlendOpenChainsAtTangentSplineSeams` PASS; offset aperto PASS in 194,29 secondi.
Anche Linux ha concluso positivamente: 82 suite, 80 PASS / due SKIP grafici
previsti / zero errori, 565,90 secondi. Verificati il kernel475/475, il raccordo
precedentemente fallito e l’offset aperto (181,56 secondi).

Il log conclusivo del precedente job macOS `129728115109` conferma il PASS della
suite offset in 205,05 secondi prima dell’annullamento dei test restanti.
La cancellazione è dovuta al nuovo push, non a un errore rilevato nel raccordo;
la suite generale era ancora in corso. Non la si considera certificata.

Mappa aggiornata anche per il workflow: 676 file / 9.751 relazioni, 331 unità
da cache e zero diagnostiche, `--check` PASS. Quarta vista HTML verificata;
il workflow ha solo il catalogo, senza chiamate C++ attribuite al file YAML.

## Esito finale GitHub

[CI 42789007301](https://github.com/myungit-Vins-Prototype/Linux_CAD_forFree/actions/runs/42789007301):
**SUCCESS** sul commit `994c771e56e712cc8bc4a0953220739e50a9043e`.
Configurazione, applicazione completa e tutti i test configurati riusciti su entrambi i sistemi.

| Runner | Suite PASS | SKIP previsti | Fallimenti | Kernel | Tempo CTest |
| --- | ---: | ---: | ---: | --- | ---: |
| Linux CPU | 80 | 2 | 0 | 475/475 | 565,90 s |
| macOS CPU e Metal | 84 | 1 | 0 | 475/475 | 604,32 s |

Gli SKIP riguardano il broad phase GPU su Linux e l’overlay OpenGL su entrambi.
`BlendOpenChainsAtTangentSplineSeams` e tutte le cinque regressioni offset passano
su entrambi i runner. Anche i test Metal configurati sono superati su macOS.
Nessuna tolleranza geometrica aumentata e nessun controllo disattivato.

Artefatti JUnit, `LastTest.log` e log delle dipendenze scaricati e verificati in
`/tmp/forgecad-ci-20261110/remote-linux` e `remote-macos`. Il log Linux conferma
l’uso di `https://archive.ubuntu.com/ubuntu`, senza URL Azure: il blocco dei
pacchetti è risolto. Metadati del run salvati in `remote-final.json`;
entrambi i job risultano `success`, conclusi alle 09:37:15 / 09:39:40 UTC.

L’aggiornamento finale della memoria e della descrizione del workflow è soltanto
documentale, con `skip ci`, per conservare questo esito sul codice verificato.
