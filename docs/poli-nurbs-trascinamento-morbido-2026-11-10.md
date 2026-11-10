# Trascinamento morbido dei poli —10novembre2026

Segnalazione su Hook, Corpo7, una faccia isolata con refit8×108: G0/G1 attivi ma un solo polo trascinato
produce una deformazione concentrata e nessun vicino segue. Il pannello indica correttamente zero giunti,
ma non chiariva che G1 agisce solo tra facce. La nuova opzione distribuisce il gesto dentro la griglia.

La copia congelata del file attuale è `/tmp/forgecad-poles-soft/Hook.prt`; contiene17funzioni,
Copiafacce16/indice15 con una faccia193×1483 e Modificapoli/indice16 con refit8×108.
La ricostruzione indipendente misura0,00913601mm, come lo screenshot. Grado3×3 e nodi interni semplici:
continuità parametrica C2, anche se muovere un solo polo può concentrare la curvatura fino a produrre una punta.
Questo non certifica la regolarità globale o l'assenza di pieghe dopo gesti grandi.

## Comportamento

“Trascina anche i poli vicini” è separato da “Mantieni la tangenza tra facce(G1)”. Raggi U/V1..32 in fasce
di poli, default3/6; peso compatto con decadimento C2. I poli selezionati seguono interamente; i vicini meno,
e sono verdi anche senza giunti. Le selezioni sovrapposte usano il massimo dell'influenza, senza sommare.
Il campo segue la topologia della griglia, senza cercare facce o poli per prossimità nello spazio.

Il gesto riparte dalla fotografia completa precedente a ogni movimento; Esc ripristina anche i vicini.
G0/G1 possono correggere i compensativi vicino ai giunti; comandi incompatibili restano rifiutati.
Oltre4096poli l'operazione viene rifiutata prima di modificare il modello. Cambiare opzione o raggi conserva
la forma già risolta. Le coordinate in tabella modificano direttamente il singolo polo.

Impostazione/raggi in schema15, lettura degli schemi precedenti conservata e opzione inizialmente spenta.
I delta risolti sono persistiti: la rigenerazione non applica nuovamente il campo di influenza.
L'influenza non mantiene la normale originale né impone tangenza G1/G2 fra punti qualsiasi della superficie.

## Verifiche completate

- App completa e runner ricompilati;11/11CTest pertinenti PASS (cinque suite poli, strumenti superficie,
  precisione, pattern, mapping, fill e suite kernel). Kernel18/18 dei poli, inclusi i due casi d'influenza.
- La nuova fixture ha rilevato un salto con movimenti identici ripetuti, dovuto alla riproiezione del gizmo
  aggiornato. Conversione pixel/mondo ora fissata all'inizio del gesto; regressione ripetuta PASS.
- Sonda Qt/OpenGL Wayland: griglia8×108, vicini verdi e influenza decrescente, Esc/ripetizione/rilascio,
  opzione/raggi senza alterare la forma corrente, anteprima, G1 sui giunti compatibili, funzionamento senza
  giunti, conferma/B-rep, Undo/Redo, codec15/negativo schema14, riapertura e rigenerazione senza cache finale PASS.
- Wayland sulla faccia reale di Hook, Corpo7/Copiafacce16:8×108, P(1,49), raggi3/6,47vicini verdi.
  Esc/ripetizione/rilascio, anteprima, B-rep0errori, conferma/Undo/Redo e rigenerazione ultima funzione PASS.
  Sorgente B-rep e file originali invariati; non rigenerata integralmente tutta la storia di Hook.
- SHA256 copia/originale `8e028d543346a8d422f7983a36d8ce43884d1a5d90fe9bf6f99ee86553d9219e`.
- Mappa676file/9.751relazioni/331unità,0diagnostiche, check e sei viste HTML PASS. Verificati i collegamenti
  in entrambi i versi e i confini di chiamata, distinguendo chiamate, include e riferimenti. HTML/JSON ignorati.

Il file Hook corrente contiene già176poli modificati nella funzione salvata, con delta massimo25,9323mm
rispetto al refit iniziale. Attivare l'influenza conserva quelle modifiche: il campo vale per i nuovi gesti.
Per una prova dalla forma iniziale si può usare Ricarica griglia, che azzera esplicitamente gli spostamenti.
I test nativi della nuova influenza partono dalla sorgente Copiafacce16 congelata, senza quei delta pregressi.

Sonde, log e screenshot in `/tmp/forgecad-poles-soft/`; impostazioni dei processi di prova isolate.
Le modifiche preesistenti di altre chat sono preservate ed escluse dal commit di questa funzione.
Riaprire ForgeCAD per usare la build aggiornata.

Implementazione pubblicata su `origin/main`: `7dda9c9`. Build app e runner complete; riaprire ForgeCAD.
