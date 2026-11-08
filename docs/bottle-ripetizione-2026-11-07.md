# Bottle — ripetizione del Corpo 4 e simmetria del Corpo 1

7 novembre 2026. Richiesta: capire perché due copie del Corpo 4 non combaciano con il Corpo 1
per tagliarne le facce, distinguendo geometria e approssimazione.

## Esito

È una differenza geometrica già definita negli schizzi guida. Il Corpo 1 ha simmetria a
180° entro arrotondamento, ma non a 90°. La ripetizione circolare di quattro corpi usa
correttamente rotazioni rigide di 0°, 90°, 180° e 270° intorno a Z; le copie a 90° e 270°
non possono mantenere tutti i contatti del corpo originale con questa forma del loft.
Non rilevato un errore numerico nella trasformazione delle copie.

Il mancato contatto continuo non significa che le superfici non si intersechino mai:
nei punti misurati i bordi passano sia dentro sia fuori dal supporto del Corpo 1.
Per un rifilo completo occorre un utensile che attraversi la superficie lungo il contorno voluto.

## File e storia esaminata

Originale aggiornato: `/home/myungit/Documenti/Cad_Example/Bottle.prt`, 23.292.967 byte,
salvato alle 17:01 locali; copia congelata `/tmp/bottle-pattern/Bottle-original.prt`.
SHA-256 originale e copia prima/dopo identico:
`888f2381cbff4f0e0fc6f2af299c67f4359401a4ae635e02e77b67ac8812ba21`.
Sei schizzi e 17 feature. Corpo 1: `Loft 3`, poi `Taglio 12`;
Corpo 4: `Offset 7` interno −3 mm, rifilo, `Cucitura 13`, `Ripetizione 14`.
Quattro occorrenze totali, 360° distribuiti intorno all'asse Z, originale compreso.

| Corpo logico | Feature/indice interno | Rotazione | Scarto massimo dei bordi nei punti verificati |
| --- | --- | ---: | ---: |
| Corpo 4 | Ripetizione 14 / 13 | 0° | 3,50e-10 mm |
| Corpo 6 | Ripetizione 14 — corpo 2 / 14 | 90° | 0,331907 mm |
| Corpo 7 | Ripetizione 14 — corpo 3 / 15 | 180° | 3,50e-10 mm |
| Corpo 8 | Ripetizione 14 — corpo 4 / 16 | 270° | 0,331907 mm |

Misura: 8 bordi liberi per occorrenza, 9 parametri per bordo, totale 288 punti.
Distanza dalla più vicina delle otto superfici B-spline del loft sorgente, con proiezione
kernel sui domini esatti, senza mesh. I valori sono massimi misurati, non una stima
certificata del massimo continuo su tutti i bordi. A 90°/270° lo scarto orientato varia
circa da −0,331907 a +0,330457 mm. Questo basta a escludere la coincidenza dei contorni.

## Origine della differenza

Il loft usa quattro guide esplicite, tutte con influenza 1: due curve di Schizzo 4 sul
piano XZ e due di Schizzo 5 sul piano YZ. I punti di passaggio hanno le stesse quote e gli
stessi valori radiali nominali: 40 a Z0, 43 a Z15, 35 a Z70, 40 a Z120, 22 a Z180.
Le maniglie tangenti salvate sono però diverse. Per esempio, sul punto positivo (43,15):

- Schizzo 4: maniglia uscente `(41.870880145544312, 21.351822527485339)`.
- Schizzo 5: maniglia uscente `(42.780738245881921, 20.868733015366836)`.

Quindi le spline coincidono nei punti imposti ma descrivono forme differenti fra quei punti.
Intersezioni delle curve guida con rette alle stesse altezze, calcolate sulle curve native:

| Z (mm) | Raggio guida X (mm) | Raggio guida Y (mm) | X − Y (mm) |
| ---: | ---: | ---: | ---: |
| 15 | 43 | 43 | 0 |
| 30 | 40,115589 | 40,480405 | −0,364816 |
| 70 | 35 | 35 | 0 |
| 95 | 37,779027 | 38,117466 | −0,338439 |
| 120 | 40 | 40 | 0 |
| 150 | 33,014404 | 32,419681 | +0,594723 |
| 180 | 22 | 22 | circa 0 |

Su 1.801 quote fra 0 e 180 mm, massimo scarto fra i raggi delle guide circa 0,889249 mm
alla quota 6,4 mm. Questo valore riguarda le guide; è distinto dallo scarto dei bordi
ripetuti misurato nella tabella precedente.

## Controlli per distinguere la causa dall'approssimazione

- Rigenerato soltanto il loft sorgente dalla definizione: zero errori, 8 facce;
  nodi e dimensioni delle griglie identici alla cache, differenza massima fra i poli zero.
  Nessuna rigenerazione integrale delle successive feature di taglio è stata necessaria o eseguita.
- Rotazioni delle tre superfici B-spline per copia: confrontati tutti i poli con la
  trasformazione rigida attesa, errore zero. Sui bordi, cinque punti per curva:
  scarto massimo dalla rotazione attesa 7,95e-15 mm.
- Nel loft iniziale, confronto dei coefficienti con rotazione 180°: poli entro 4,30e-13 mm,
  pesi entro 1,67e-15. A 90° e 270° i poli corrispondenti differiscono fino a 0,900307 mm:
  dato parametrico di supporto, distinto dalla distanza geometrica dei bordi.
- Prova causale esclusivamente in memoria: sostituite soltanto le maniglie tangenti e
  i relativi flag di Schizzo 5 con quelli di Schizzo 4, conservando i punti e il piano YZ.
  Loft rigenerato senza errori: la simmetria a 90°/180°/270° torna con poli entro
  3,45e-13 mm e pesi entro 1,89e-15. Nessuna variante scritta nel documento utente.

Per quattro copie combacianti occorre uniformare anche le tangenti delle guide XZ/YZ.
Se si vuole conservare la forma attuale, gli utensili vanno costruiti/estesi e rifilati
localmente sul Corpo 1; il contatto originale non si conserva con una sola rotazione rigida.
Non aumentare le tolleranze per assorbire uno scarto dell'ordine dei decimi di millimetro.

Nessuna modifica al kernel, all'eseguibile o al file CAD. Analizzatori, corpi congelati e
log in `/tmp/bottle-pattern/`. Modificati solo questo rapporto e la memoria condivisa;
cronologia precedente preservata in `docs/history/claude-prima-bottle-ripetizione-2026-11-07.md`.
