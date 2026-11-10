# Mesh visibile durante la scelta delle facce — 10 novembre 2026

Nel comando **Modifica poli NURBS**, “Scegli facce nella vista” nascondeva la superficie;
restavano i poli e i contorni della faccia sotto il mouse. La selezione geometrica funzionava,
ma mancava la mesh ombreggiata necessaria per individuare le facce.

## Causa e correzione

Una nuova funzione usa come proprietario la posizione successiva all’ultima funzione
(`owner == extrusions.size()`). `beginReferencePick` preparava la scena sorgente soltanto
per la modifica di una funzione esistente (`owner < size`). Il contesto rimaneva quindi vuoto:
`isBodyDrawn` nascondeva i corpi, mentre il picker continuava a riconoscere quello impostato
esplicitamente dal pannello. L’anteprima è correttamente sospesa durante la scelta delle facce.

`forgeCad2026_viewport_selection.cpp` prepara adesso il contesto anche per una nuova funzione.
Include i risultati precedenti e il corpo scelto esplicitamente nel pannello, compreso uno
stadio intermedio già consumato. Gli operandi della funzione esistente vengono letti soltanto
quando l’indice esiste. Nessuna modifica dei flag persistiti Mostra/Nascondi o del B-rep.

Terminare con il pulsante o Esc ripristina la normale scena dell’anteprima. Cambiare le facce,
non selezionarne alcuna o azzerare l’anteprima conserva la sorgente durante il picking.

## Verifiche

- Regressione aggiunta alla suite multifaccia esistente, senza nuovi target: apertura del pannello,
  clic reali nell’interno delle facce, selezione continua, Esc/Termina, anteprima già pronta,
  riapertura in modifica e stadio intermedio consumato scelto da una nuova funzione.
- Riproduttore **prima** della correzione su Wayland: 480 pixel ciano (soli poli), test della mesh fallito.
  **Dopo**: 212.061 pixel all’apertura, 165.343 tornando dalla preview e 140.126 in modifica;
  tutti i clic e i ripristini superati. Verificato anche dal runner ufficiale con `--gl`.
- **10/10 CTest pertinenti PASS**: cinque suite poli, strumenti superficie, selezione raccordi,
  riferimenti evidenziati, storyboard e piani/storia. Nessun test saltato; circa 6,3 secondi.
- Copia congelata di **Hook**, sorgenti Corpo 2 (184 facce) e Corpo 7 (una faccia): pannello reale,
  scelta con tutte le griglie, Nessuna, clic interno su una faccia e Termina PASS su Wayland.
  Pixel ombreggiati: 152.297 / 186.984. B-rep sorgenti byte-identici dopo Annulla.
  Screenshot controllati visivamente: mesh intera presente, anche con una sola griglia.
- Hook originale e copia restano SHA256
  `8e028d543346a8d422f7983a36d8ce43884d1a5d90fe9bf6f99ee86553d9219e`.
  Prove su sorgenti congelate; non rigenerata l’intera storia del documento.
- Applicazione completa `forgecad-release/forgecad` e runner ricompilati con GCC.
  App Clang ricompilata anche su sorgente pulita con la sola correzione, senza le modifiche delle altre chat.
  Log e immagini locali: `/tmp/forgecad-face-picking/`, riproduttore prima/dopo in
  `/tmp/forgecad-poles-soft/picking-{before,after}.log`.

Mappa aggiornata: 676 file, 9.756 relazioni, 331 unità di compilazione, zero diagnostiche.
Controllo di consistenza, collegamenti in entrambi i versi e tre viste HTML PASS;
chiamate, include e riferimenti verificati separatamente. HTML/JSON restano ignorati da Git.

La verifica grafica corrente riguarda Linux/Wayland. Riaprire ForgeCAD per caricare la nuova build.
Le modifiche preesistenti di altre chat restano separate dal commit della correzione.
