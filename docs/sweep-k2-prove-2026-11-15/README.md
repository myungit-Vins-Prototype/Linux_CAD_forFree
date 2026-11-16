# Prove sweep K2 — 15 novembre 2026

**Esperimento isolato, non integrato nel kernel di produzione.** Il prototipo conserva due rifiuti errati e non certifica il B-rep finale. Non applicare `integration.patch` direttamente al repository come correzione.

- `prototype/`: core v2, adattatore e patch sperimentale. I sorgenti sono conservati con suffisso `.txt` per distinguerli dai moduli di produzione e dalla mappa architetturale.
- `sources/`: riproduttori, sei regressioni sweep e cinque test diretti del core.
- `logs/`: risultati dell'esecuzione completa del riproduttore, hash, audit del repository e della libreria.
- `notes/`: audit geometrico, prove analitiche dei casi validi e moduli architetturali consultati.

## Riproduzione

Dalla radice del progetto, con una build Release corrente che includa i test OCCT:

```sh
python3 docs/sweep-k2-prove-2026-11-15/reproduce.py.txt
```

Lo script usa una nuova directory temporanea; compila e collega lì, senza cambiare sorgenti, CMake, librerie o app del repository. Si può specificare `--root`, `--build` e `--out`. Non richiede rete. Per i confronti conserva l'hash della libreria effettivamente usata; gli hash di questa prova non certificano build successive.

Esiti attesi: **19/19** sweep di produzione; **5/5** test diretti del core sperimentale; **23/25** test del runner sperimentale. I due fallimenti attesi sono `SweepGlobalCheckPreservesSeparatedInterleavedRegions` e `SweepGlobalCheckPreservesRankTwoSheet`: documentano perché l'integrazione non può essere promossa. Lo script verifica i nomi dei fallimenti, non trasforma un errore arbitrario in successo. Il suo completamento indica che ha riprodotto gli esiti descritti, non che K2 sia corretto.

Il caso originale è ancora accettato dalla produzione, con volume19.7417086214508 maggiore del box12.6. `witness_probe` valuta i carrier effettivi e stampa tre ulteriori collisioni numeriche. Questi residui non sono certificati interval-Newton.
