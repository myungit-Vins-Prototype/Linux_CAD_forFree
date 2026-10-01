# Storyboard delle feature

Il modello distingue ora i corpi logici dalle feature che ne producono gli
stati successivi. `ExtrusionObject` resta l'unita' di calcolo e conserva il
B-rep intermedio, ma possiede un `featureId` persistente e un `modelBodyId`.
`ModelBody` contiene nome, visibilita' e feature finale (`tipFeatureId`).

## Regole della cronologia

- Soltanto il tip non soppresso di ogni corpo viene disegnato. I B-rep degli
  stadi precedenti restano disponibili per modifica, rollback e rigenerazione.
- Una nuova feature che modifica un corpo eredita il suo `modelBodyId`.
  Primitive, importazioni, rivoluzioni, loft e operazioni senza fusione iniziano
  invece un nuovo corpo. Datum ed eliche restano geometria di riferimento.
- Le booleane appartengono al corpo dell'operando A. I corpi strumento rimangono
  corpi logici distinti e vengono soltanto nascosti mentre sono consumati.
- La soppressione rende la feature un passaggio trasparente: il suo risultato e'
  quello dello stadio precedente e le feature successive vengono rigenerate.
- Eliminare una feature ricollega la base implicita allo stadio precedente. Le
  feature successive non vengono cancellate; un riferimento esplicito a una
  faccia o a un bordo scomparso produce un errore visibile nella storyboard.
- Il riordino dal menu o con trascinamento rimappa tutti gli indici, ricostruisce
  le basi implicite e viene accettato solo se ogni dipendenza resta precedente
  alla feature che la usa. La feature iniziale non puo' essere spostata.

## File e compatibilita'

Il formato `.prt` 19 salva gli identificatori, la soppressione e i corpi logici.
I documenti fino al formato 18 vengono migrati automaticamente seguendo gli
operandi gia' presenti: raccordi, smussi, scale, trasformazioni, booleane e
fusioni ereditano il corpo della loro base. La migrazione non modifica il file
finche' l'utente non lo salva.

## Limiti attuali

- I riferimenti geometrici a facce e bordi usano ancora il riconoscimento
  geometrico esistente. Dopo un riordino valido dal punto di vista del grafo,
  una faccia puo' non esistere piu': la feature rimane in errore e deve essere
  ridefinita.
- Lo spostamento tra due corpi diversi non e' implicito. Richiedera' un comando
  separato che scelga il nuovo corpo bersaglio e rimappi i riferimenti.
- La cronologia Undo/Redo continua a usare istantanee complete del documento;
  ogni soppressione, eliminazione o riordino e' un singolo passo.

## Lavoro successivo

Dopo la stabilizzazione della storyboard, il porting va gestito in un branch
separato. Le prime attivita' sono una toolchain CMake senza percorsi Linux,
backend grafico Qt/OpenGL verificato su Windows e macOS ARM64, sostituzione o
isolamento delle dipendenze specifiche di sistema, pacchetti Qt/OpenCASCADE per
x64 e arm64, CI multipiattaforma e mantenimento del backend CPU quando CUDA non
e' disponibile.
