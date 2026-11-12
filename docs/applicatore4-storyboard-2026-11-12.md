# Applicatore4: sottrazione scavalcata dalla storia — 12 novembre 2026

## Richiesta e prova congelata

Inseriti Schizzo 7 e Rivoluzione 19 prima di Rivoluzione 9, la sottrazione
appare alla conferma ma scompare avanzando al fondo della storyboard.
Originale `/home/myungit/Documenti/File_Esempio/Applicatore4.prt`, 12.293.747 byte,
modifica 12/11/2026 15:36:58 CET, SHA256
`70e401894d249ad87a834799deb51bb2a0e4487e570cf86ee3b59b7c2a69998b`.
Copia congelata e riproduttori in `/tmp/applicatore4-storyboard-2026-11-12`.
Originale non modificato.

## Causa verificata

Il documento contiene 19 lavorazioni e 7 schizzi. Rivoluzione 19 (indice 10,
featureId 31) sottrae correttamente da Raccordo 23 (indice 9). Rivoluzione 9
(indice 11, featureId 9) conserva invece mergeBodies={9}, scavalcando la
sottrazione. Entrambe appartengono al corpo logico 1. La geometria salvata a
valle contiene gia' la perdita: il movimento del cursore espone quella cache,
senza introdurre un nuovo errore nel kernel booleano.

L'inserimento diretto nel codice corrente ricollega il successore. Riprodotto
invece il difetto sul CAD congelato inserendo la rivoluzione indipendente e
modificandola poi in sottrazione: updateBody modifica il corpo ereditato ma
non le basi implicite delle fusioni successive; Rivoluzione 9 resta su indice 9.
Questo dimostra un percorso che genera lo stato osservato; non certifica la
sequenza di clic originale dell'utente.

## Implementazione

- `cad_history_chain.*`: ricongiunge la base primaria delle funzioni di profilo
  fuse/sottratte allo stadio precedente del medesimo corpo. Nessuna regola per
  file, nomi, ID o geometrie. Bersagli secondari, riferimenti espliciti e
  riferimenti non validi mantengono la propria identita'/diagnostica.
- Apertura: ripara le basi obsolete e invalida le cache dei consumatori anche
  attraverso gli schizzi, lasciando disponibili quelle dei rami indipendenti.
- Modifica: normalizza l'owner e ricongiunge le fusioni prima della rigenerazione
  dei discendenti. Conserva il riuso del risultato gia' calcolato della funzione.
- Regressione sintetica con volumi analitici: fallisce sul codice precedente
  con la diagnostica «la fusione successiva usa la sottrazione intermedia».

## Verifiche raggiunte

- Build completa app e runner riuscita. Nuova regressione sintetica PASS;
  19/19 CTest su storyboard, owner, cursore, schizzi, datum, soppressioni,
  rivoluzioni/estrusioni/booleane, pattern, cache e genealogia PASS (1,38 s).
- CAD reale: riparazione automatica all'apertura in 5,246 s; 19 lavorazioni,
  7 schizzi, nessun errore fino a Raccordo 17. Rivoluzione 9 ora usa indice 10.
  Il tip passa da 82 a 83 facce, conservando le feature e i raggi originali.
- Volume B-rep esatto prima 166,9038380308963 mm³, dopo 164,0844051260653 mm³:
  sottrazione conservata di 2,819432904831 mm³. Spostamenti cursore 10/11/19
  conservano lo stesso puntatore al tip; nessun nuovo calcolo del risultato.
- Copia `output/Applicatore4-storyboard-corretta.prt`: salvataggio/riapertura,
  B-rep finale byte-identico e ricalcolo completo (10,363 s, 0 errori) PASS;
  volume finale identico entro 1e-6 mm³. SHA256 dell'originale ancora invariato.
- Matrice addizionale con schizzo aperto/chiuso: 8 combinazioni PASS nel riproduttore.
  Bersagli primari verificati per estrusione, rivoluzione e sweep. GUI Wayland/OpenGL
  sul CAD reale PASS: due cicli cursore 11→19, stesso tip, framebuffer 1000×900
  ispezionato. Runner finale CTest 0,43 s e suite Wayland PASS, anche con
  schizzo aperto. Kernel geometrico invariato.
- Mappa: 687 file, 9.883 relazioni, 337 TU, nessuna diagnostica; `--check` PASS.
  Sei verifiche HTML in Chromium sui collegamenti viewport/modulo/test e
  modulo/dipendenze in entrambi i versi PASS; nessun errore JavaScript.
  Conteggio righe del test riallineato dopo la sua estensione durante l'analisi;
  le righe delle nuove chiamate AST corrispondono ai sorgenti finali.
- `git diff --check` PASS. Verifiche locali Linux; macOS non eseguito in questo intervento.

## Consegna

App Linux aggiornata: `forgecad-release/forgecad`; riavviare l'app e riaprire
Applicatore4 per applicare il controllo alle definizioni gia' caricate.
La riparazione all'apertura resta in memoria finche' non si salva.
Disponibile anche `output/Applicatore4-storyboard-corretta.prt` (14.639.661 byte), SHA256
`a182237e705e96a64518d484531a1b433da279bf25cc213f3491e7af55b272ea`.
L'originale e' invariato, con SHA256 verificato ancora al termine delle prove.

Modifiche di questo intervento: nuovi `cad_history_chain.*` e
`tests/test_storyboard_merge_chain.h`; integrazione in CMake, viewport documento,
runner/review storyboard; documentazione della storia, catalogo architetturale,
rapporto e memoria condivisa. Preservate le modifiche preesistenti delle altre chat.


## Ambito del commit richiesto

Il commit e push richiesti includono la correzione storyboard, le regressioni,
il catalogo architetturale e questa documentazione. I file condivisi sono
selezionati per singole modifiche; gli altri interventi vanno in un secondo commit,
come richiesto successivamente dall’utente.
Le prove native descritte sopra sono state svolte sul checkout completo,
comprensivo delle modifiche precedenti ai raccordi; non attribuiscono al solo
commit storyboard le correzioni del kernel ancora separate.

Controllo aggiuntivo prima del commit: sorgente documento e regressione compilati
con gli header dello snapshot index; prova runtime del sorgente selezionato
con gli header compatibili con gli oggetti del checkout PASS. Non e' una build
pulita dell'intero commit. I runner con layout diversi non sono una prova valida.
