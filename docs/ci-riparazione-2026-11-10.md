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

Confrontati anche run `42479610221` (`a388270`, 8 novembre) e `42645055658`
(`a0c9915`, 9 novembre): stesse cause. I log completi sono in `/tmp/forgecad-ci-*.log`.
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

## Verifiche in corso

Build pulita da `git archive HEAD`, con le sole tre modifiche a kernel/CMake e
alla regressione, in `/tmp/forgecad-ci-20261110/source`. Clang Release CPU,
stesse opzioni della CI; applicazione e tutti i target compilati. CTest completo
ancora in corso. Kernel GCC completo: **475/475 PASS**. Nessun nuovo successo
remoto ancora dichiarato.

- Applicazione locale `forgecad-release/forgecad` ricompilata completamente.
- Riproduttore su 40 rotazioni vicine: 40/40 B-rep validi (prima 2 fallimenti).
- Regressione completa `BlendOpenChainsAtTangentSplineSeams` GCC PASS,
  inclusa la quarta rotazione e tutti i controlli precedenti.
- Offset aperto Clang pulito: 5/5 casi PASS, 106,79 secondi reali.
- Mappa: 676 file / 9.751 relazioni / 331 unità, zero diagnostiche;
  `--check`, collegamenti entranti/uscenti e tre viste HTML PASS.
- Il test Qt del tema scade nel sandbox locale, anche sulla build iniziale;
  lo stesso eseguibile passa immediatamente nell’ambiente host con Qt 6.12.
  Non modificato: questo errore non compare nei log GitHub esaminati.
