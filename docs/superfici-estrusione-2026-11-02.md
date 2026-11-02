# Estrusione di Schizzo 3 fino a Corpo 1

Intervento del 2 novembre 2026, successivo alla review pubblicata con `d4f15fc` su `origin/main`.
L'utente ha confermato il problema sia con **Fino al prossimo corpo** sia con
**Fino a una faccia o a un piano**.

Il [successivo fix dell’unione e dei riferimenti](estrusione-unione-riferimenti-proiettati-2026-11-02.md) aggiunge **Unione** all’estrusione fino al corpo. I volumi e le verifiche seguenti riguardano il precedente risultato **Corpo nuovo**: non attestavano la fusione con la scocca.

## Documento e uso

Nella directory degli esempi esistono due documenti differenti: `superifici.prt`
(634.171 byte, precedente) e `Superfici.prt` (15.890.253 byte, aggiornato).
Il caso riprodotto nella scena corrente è nel secondo. Le prove usano copie congelate;
le sonde non scrivono i due originali.

Schizzo 3 è un cerchio di raggio 5 mm, centro circa
`(-27.74674219, 61.05967827, -10)`, normale locale verso −Z.
Nel pannello **Estrusione** scegliere Schizzo 3, **Corpo nuovo**, il verso
**Invertito** (+Z nel documento) e una delle due modalità:

- **Fino al prossimo corpo**: nessun riferimento di faccia necessario.
- **Fino a una faccia o a un piano**: selezionare la faccia interna di Corpo 1,
  il cui risultato corrente è Svuota 1; provate la faccia 18 di estrusione e
  la faccia 8 B-spline.

Il risultato verificato è un solido con cinque facce e volume
**2348,73301675 mm³**. Il limite segue il fondo curvo della scocca;
Corpo 1 resta visibile e geometricamente invariato.

## Salvataggio successivo rilevato durante il lavoro

Alle 16:39:58 UTC l’originale `Superfici.prt` risulta salvato con 18 feature,
16.074.185 byte e SHA256
`56779a6c668258e54d686a9f1e721de03acbea84a2c16d642e1083d364203adf`.
È distinto dalla copia QA iniziale e dai file dimostrativi delle sonde.
Non viene attribuito l’autore del salvataggio e non viene ripristinato il file.

Congelata anche questa versione ed eseguito un audit in sola lettura, collegato
agli oggetti della build definitiva: le 11 cache B-rep delle prime 17 feature
sono identiche byte per byte, mentre il profilo di Schizzo 3 è cambiato
(differenza massima fra poli razionali 4,70726758 mm). La nuova **Estrusione 18**
usa extent 4 / Invertito e viene rigenerata senza errori: cinque facce,
volume **2347,96902261 mm³**, identico alla cache salvata entro 1e-9 mm³.
Questa è una verifica aggiuntiva della feature già salvata, distinta dalle
nove prove GUI complete sulla versione iniziale. La serializzazione dell’intero
schizzo non è usata come confronto geometrico perché include metadati di storia.

## Cause e correzioni

1. `cad_datum.cpp` individuava già l'identità B-rep della faccia libera, ma
   restituiva errore se non era piana, cilindrica o conica. Il resolver ora
   accetta la faccia valida, senza inventare un piano tangente o un asse.
   I comandi che richiedono piano, asse o curva continuano a verificarne i flag.
2. Il certificatore delle prime superfici in `cad_extrude.cpp` controllava il
   supporto B-spline completo, comprese parti ripiegate esterne alla faccia.
   Il nuovo modulo puro `kernel/fk_face_normal_bounds` calcola bounds UV
   conservativi dalle SP-curve ritagliate esattamente ai range degli spigoli.
   Certifica l'orientamento dei contorni con area Bernstein e intervalli
   arrotondati verso l'esterno, poi controlla i poli delle derivate delle sole
   pezze che possono appartenere alla faccia. Non usa campioni della mesh.
3. La verifica finale ripeteva una booleana fra superfici di taglio coincidenti,
   fino all'errore di suddivisione dei semi. Il controllo degli ostacoli interni
   avviene ora prima del filtro delle shell: una shell scartata deve avere
   volume orientato strettamente positivo. Cavità negative o casi non validi
   producono errore, preservando il controllo senza quella booleana ridondante.
4. `cad_document_io.cpp` ripristina `solid` dalla B-rep decodificata. Il flag
   transitorio non veniva ricostruito dal decoder diretto, impedendo la ricerca
   dei candidati prima di una rigenerazione. È una causa separata dal rifiuto
   osservato nell'interfaccia.

Formato nativo 60, genealogia, tolleranze e selezione dei candidati restano
compatibili. La modalità su faccia usa il percorso geometrico già esistente,
ora raggiungibile anche dalle facce libere valide.

## Verifiche e limiti

- Build completa definitiva applicazione e runner Release: PASS.
- Suite permanente estrusione e GUI dell’estrusione: PASS; controlli aggiunti
  per resolver di faccia libera, flag solido del decoder e risultato su B-rep
  appena decodificata. Le asserzioni dei casi precedenti restano attive,
  inclusi ostacoli interni, profili disgiunti, rollback e Undo/Redo.
- Nuovo certificatore kernel: **8 gruppi PASS**; supporto ripiegato fuori/dentro
  il trim, range spigoli rispetto al dominio globale, verso faccia, fori e
  complementi, pcurve mancanti/non supportate/fuori dominio, pesi, estrusioni
  razionali/ellittiche parallele, periodicità e cancellazione.
- Prove differenziali: il vecchio resolver fallisce sulla faccia 18 libera;
  quello corretto risolve sia la faccia 18 sia la 8. I comandi che richiedono
  davvero piani/assi continuano a rifiutare riferimenti non compatibili.

La QA sulla build comune finale passa **9/9**: tre modalità (prossimo corpo,
faccia 18, faccia 8), ciascuna su offscreen, Wayland e XCB. Per ogni caso:
vero pannello, anteprima, conferma, Undo/Redo, codec e riapertura; corpo limite
visibile e B-rep identica. Cap controllato con raggi B-rep esatti al centro,
su 16 punti del bordo e su 16 punti appena interni, ripetuti sui tre stati.
Tutti i risultati hanno volume 2348,73301675 mm³; errore cap misurato nullo.
Questi raggi sono una verifica indipendente campionata, aggiuntiva alla prova
analitica del certificatore.

GL reale valido, nessun errore rilevato: Wayland/Intel P630 Mesa e
XCB/Quadro RTX 3000 NVIDIA. Catture ispezionate; nessuna prova macOS.
Copia dimostrativa: `/tmp/forgecad-superfici-fix/Superfici-estrusione-fino-corpo.prt`.
La riapertura dell’app è necessaria per caricare la build aggiornata.

Suite comune completa in una sola esecuzione: **154 PASS, 1 SKIP, 1 FAIL su 156**,
421,76 s. Unico fallimento già presente prima dell’intervento:
`DraftCurvedAndConcaveSolids`, `kernel/tests/test_draft.cpp:134`, volume
253,5270716475676 contro 253,5270734420086, differenza 1,794441e-6 > 1e-6,
identica al checkpoint precedente. Kernel generale **457/458**. Nessuna tolleranza
modificata. Skip `forgecad_overlay_uniform_tests` per il contesto offscreen;
le prove GL reali sono quelle separate sopra. Nessun nuovo fallimento.

Tre manifest CTest copiati conservano comandi, timeout e proprietà; cambia solo
`FORGECAD_EXAMPLE_DIR`, rivolto alle copie congelate. Hash delle cinque copie e
di quattro originali confermati invariati dopo le prove. Il file originale
`Superfici.prt` presenta invece un salvataggio successivo allo snapshot QA:
la distinzione e il controllo della nuova versione sono nel rapporto estrusione.
Le sonde non scrivono alcun originale; autore del salvataggio non attribuito.
I 29 sorgenti verificati sono registrati nel manifest della build finale.

Mappa rigenerata: **541 file, 7437 relazioni, 255 unità AST, zero errori**.
`--check` PASS; sei confini di chiamata verificati, incluse API nuove e guardie
STEP/IGES. Controllati entrambi i versi dei collegamenti, distinguendo chiamate,
include e riferimenti a funzione. HTML reso in Firefox e ispezionato sul modulo
`cad_projected_curve.cpp`, con entrate e uscite leggibili. HTML/JSON generati
restano ignorati da Git.

[Archivio delle evidenze](history/superfici-curva-proiettata-2026-11-02-verifica.zip):
log/JUnit della singola suite completa, hash build e fonti, verifiche fixture,
sonde e rapporti, sanitizer mirati, collegamenti production, catture native e
controlli della mappa. Le sonde intermedie sono distinte dalla build comune finale.


Il certificatore è sufficiente, non universale: pcurve sconosciute, periodiche,
fuori dominio o con orientamento non dimostrabile conservano il controllo del
supporto completo. Le superfici razionali trasversali a pesi variabili possono
restare non certificabili. Il verso parallelo alla generatrice conserva il
supporto per basi ellittiche e razionali. Copertura incompleta, intersezione al
piano iniziale, sottosquadri e ostacoli interni continuano a dare errore.
Non sono introdotti fit, mesh geometriche o tolleranze più larghe.

I manifest delle copie congelate, i log e le sonde riproducibili sono
nell'archivio delle evidenze indicato in fondo. Il file originale recente ha
SHA256 `c5666c219d013c190fd5d3da34eb92ebc438d04b33f9e2eeef997dc7128b9dca`.
