# Variazioni della ripetizione: esempio girante.prt — 13 novembre 2026

Richiesta dell'utente: spiegare l'uso delle variazioni usando il suo modello. Indagine e istruzioni, nessuna modifica al software o al documento originale.

## Stato effettivo del file

Sorgente letta: `/home/myungit/Documenti/Cad_Example/girante.prt`. Copia congelata e prove in `/tmp/girante-variazioni-2026-11-13/`.

- `Rivoluzione 1`: base, generata da `Schizzo 1`.
- `Estrusione 4`: da `Schizzo 3`, risultato **Corpo nuovo**, fine **Fino a una faccia o a un piano**, riferimento a una faccia di `Rivoluzione 1`.
- `Ripetizione 3`: **Lineare**, **Asse Y**, **3 istanze**, **Il corpo intero**, nessuna variazione. Gli altri due eventi sono i suoi corpi derivati.
- `Schizzo 2`, con NURBS, non è il profilo diretto di questa estrusione.

Le variazioni sono disponibili soltanto in **Una funzione**. Il criterio attuale ammette funzioni additive/sottrattive con destinatari oppure booleane unione/differenza. L'estrusione autonoma del file non è una sorgente ammessa in questa modalità: cambiare soltanto il selettore della ripetizione non basta.

## Percorso di esempio verificato sulla geometria

Per provare nell'interfaccia, salvare prima una copia del documento.

1. Modificare `Estrusione 4`: **Risultato → Unisci ai solidi**, selezionando il corpo prodotto da `Rivoluzione 1`. Confermare. L'estrusione diventa un'aggiunta alla base.
2. Modificare l'evento principale `Ripetizione 3`: **Ripeti → Una funzione**, sorgente `Estrusione 4`.
3. Per un esempio di girante radiale scegliere **Circolare**, **Asse Z**, **3 istanze**, **Angolo totale 360°**. La configurazione salvata originale è invece lineare; la variazione numerica funziona anche nelle ripetizioni lineari.
4. Passare a **3. Parametri**. Le quote dello schizzo diretto vengono mostrate nella vista senza entrare in modifica dello schizzo.
5. Cliccare la quota **15 mm** fra `Segmento 1` e `Segmento 2` di `Schizzo 3`. Compare una riga nella tabella, la quota selezionata diventa verde. Impostare **Incremento = 1 mm**.
6. La tabella mostra **Iniziale = 15**, **Incremento = 1**, **Ultima = 17**. Confermare dopo aver controllato l'anteprima.

| Istanza | Angolo nell'esempio circolare | Quota del profilo |
| --- | --- | --- |
| Originale | 0° | 15 mm |
| Seconda | 120° | 16 mm |
| Terza | 240° | 17 mm |

La legge è `valore iniziale + indice * incremento`, con indice zero per l'originale. L'incremento è per ogni passaggio: con -1 si ottiene 15/14/13; con zero 15/15/15. Il numero delle istanze include l'originale. Il valore iniziale deriva dalla sorgente. Le quote del modello originale non vengono modificate dalla generazione delle varianti. La distribuzione angolare o il passo lineare sono indipendenti dall'incremento della quota.

Si possono selezionare più quote, ciascuna con il proprio incremento. Le esclusioni mantengono il numero nominale e quindi non rinumerano i valori delle altre copie. Ogni combinazione deve consentire la soluzione dei vincoli e generare geometria valida.

## Quote disponibili nel caso specifico

Il catalogo, dopo aver reso l'estrusione una funzione additiva, contiene quattro quote di `Schizzo 3`: distanza verticale 75, distanza fra segmenti 15, raggio dell'Arco 5 pari a 9, distanza verticale 5,875 mm. Le quote si aggiungono cliccandole nella vista; il menu **Aggiungi** è riservato ai parametri numerici attivi della funzione.

La distanza dell'estrusione non è esposta qui perché la fine è determinata da una faccia. Le quote di `Schizzo 1` e `Schizzo 2` non appartengono al catalogo di questa sorgente. Non occorre cambiare questa impostazione per variare la larghezza del profilo.

## Evidenze e limiti della verifica

Consultate descrizioni e connessioni nel grafico architetturale e i sorgenti `cad_pattern`, `cad_pattern_variation`, `cad_pattern_variation_editor`, `forgeCad2026_dialogs_solids`; coerenti con la documentazione del 4 novembre.

Un programma temporaneo collegato agli oggetti del runner corrente ha letto il PRT nativo e ricostruito in memoria l'estrusione unita e la ripetizione circolare variabile 15/16/17. Entrambi i risultati hanno B-rep presente, messaggio errore vuoto e zero diagnostiche di `checkBody`. Nessun nuovo test di interazione grafica; i passaggi UI sono ricavati dal pannello corrente e dalle precedenti prove documentate, non dichiarati ricertificati su questo file.

SHA256 del file originale e della copia congelata, verificati anche dopo la prova: `ce0f8839e276fa129197b65a6970f9d99aaf3d63de757002d7c29d0c59d85030`. Nessun salvataggio sul CAD originale, nessuna modifica del codice applicativo, nessuna nuova build dell'app, nessun commit/push. La rigenerazione della mappa non è necessaria per questa indagine documentale.
