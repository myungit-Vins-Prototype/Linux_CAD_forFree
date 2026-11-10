# Poli NURBS esposti e selezione senza attraversare il corpo

Richiesta: mostrare soltanto i nodi visibili, nascondendo quelli coperti dalle superfici,
per rendere utilizzabile la modifica su corpi complessi con milioni di poli.

## Comportamento implementato

- La vista normale nasconde i poli coperti dal corpo e le parti della gabbia dietro alle superfici.
  Lo stesso filtro vale per clic e selezione rettangolare: due nodi sovrapposti sullo schermo
  permettono di scegliere quello davanti. Ruotare, zoomare o cambiare anteprima aggiorna il filtro.
- **Mostra poli nascosti**, inizialmente disattivato, abilita esplicitamente disegno e selezione
  anche attraverso il corpo. L’opzione riguarda la vista e non cambia il documento o il codec.
- La selezione già fatta rimane tale quando si ruota. Gruppi, influenza morbida e vincoli G0/G1
  continuano a muovere i poli previsti, compresi quelli che diventano nascosti.
- Un polo di controllo può essere interno al corpo anche su una faccia rivolta verso l’osservatore:
  in questo caso viene nascosto; l’opzione consente di recuperarlo senza modificare la griglia.

## Implementazione e prestazioni

`cad_surface_poles_visibility` conserva una profondità GPU delle mesh correnti, distinta dal framebuffer
MSAA della scena. Include le superfici dell’anteprima, istanze e calotte delle sezioni. La cache cambia
con camera, geometria, sezione e dimensioni della vista; hover e selezione non richiedono nuove letture.
Anche proiezioni, maschera dei nodi e segmenti visibili sono riutilizzati quando restano invariati.

`forgeCad2026_viewport_pole_visibility` raccoglie gli stessi corpi usati dal rendering, incluso il
contesto storico durante la scelta delle facce. Ripristina contesto/FBO/stato OpenGL. Il renderer
prepara il filtro prima di QPainter; `SurfacePolesInteraction` lo usa per clic e riquadro.
I segmenti sono interrotti dietro un ostacolo anche quando entrambi i poli terminali sono visibili.

Il controllo opera sulla **gabbia campionata già esistente**, non su tutte le righe della tabella.
Il modello virtuale e i milioni di poli originali rimangono integri. Il margine di un pixel e il bias
del depth buffer gestiscono soltanto silhouette e arrotondamenti grafici: nessuna tolleranza geometrica
è stata cambiata. Risorse e snapshot della cache vengono rilasciati quando si chiude la gabbia e alla
distruzione/ricreazione del contesto GL.

## Prove completate

- App completa e runner GCC ricompilati; app Clang pulita con la sola modifica corrente PASS.
  **11/11 CTest pertinenti PASS** (poli, strumenti superficie, selezione raccordi/riferimenti, piani,
  storyboard e pannelli). Regressione OpenGL dal runner ufficiale a scala **1× e 2× PASS**:
  retro/interno nascosti, nodi sopra la faccia e fuori dalla silhouette accessibili; confronto reale
  dei pixel con/senza overlay; clic sui nodi sovrapposti, riquadro visibile/X-ray, rotazione prima del
  repaint, anteprima reale, annullamento, sezione/calotta, segmenti interrotti e riuso della cache.
- Fixture multifaccia completa PASS nelle stesse due prove: selezione delle facce, griglie,
  trascinamento e rilascio, Esc, anteprima, conferma, Undo/Redo, codec e rigenerazione senza cache.
- Hook congelato, **Corpo 2**: **2.323.734 righe** virtuali, gabbia di **3.644 poli**;
  **1.789 esposti / 1.855 coperti**. Dopo una rotazione di 20°: 1.809 esposti, filtro aggiornato in
  **17,23 ms**. Query ripetute a vista ferma: media **0,004 ms**, nessuna nuova lettura GPU.
- Hook, **Corpo 7**: 286.219 righe, gabbia 3.818; 3.812 esposti / 6 coperti.
  Rotazione: 7,44 ms; query ripetute 0,0036 ms. È una lamina aperta, quindi quasi tutti i poli
  della vista di prova sono esposti. Queste misure locali non certificano ogni modello/GPU.
- Entrambi i corpi: opzione Mostra nascosti, rotazione, Nessuna, clic interno su una faccia,
  ritorno all’editing e Annulla PASS. B-rep sorgenti byte-identici; screenshot confrontati.
  Prove sulle copie delle sorgenti, senza rigenerare integralmente tutta la storia di Hook.

Sonda aggiuntiva delle istanze PASS: una mesh traslata nasconde il polo, modificare la trasformazione
prima del repaint lo rende nuovamente accessibile; senza trasformazioni la chiave segue i vertici locali.
La CI del precedente `577734d` è verde su GitHub (run42812903944); non certifica le modifiche successive.

Mappa:679 file/9.779 relazioni/333 unità, zero diagnostiche e controllo di consistenza PASS.
Verificati blocchi, collegamenti in entrambi i versi e quattro viste HTML; chiamate, include e
riferimenti distinti. HTML/JSON restano generati localmente e ignorati da Git.

Sonde/log/immagini in `/tmp/forgecad-face-picking/visibility-*` e `Hook-corpo{2,7}-{visibili,xray}.png`.
La verifica grafica corrente riguarda Linux/Wayland. Offscreen, senza GL, il filtro mantiene il
comportamento accessibile dei test ma non certifica l’occlusione. Riaprire l’app aggiornata.
Le modifiche preesistenti di altre chat restano separate dal commit di questo intervento.
