# Audit indipendente finale K10 Mapping UV / Quilt

Esito: nessun rilievo bloccante nel candidato V3 esaminato. Audit limitato al trasferimento SP-curve su carrier Plane conservato e alla costruzione dell'offset analitico Plane in Quilt. Nessun file del repository, CAD originale o processo utente modificato dall'auditor; tutte le prove sono sotto /tmp/mapping-uv-review/audit. Le modifiche della memoria condivisa e l'integrazione appartengono al root.

## Attribuzione e contratto

Freeze iniziale: 15 file e relativi SHA256 in baseline/sha256.json. Letti AGENTS.md, CLAUDE.md, politica delle tolleranze e rapporti pertinenti mapping/diagonal/UV/sew. Consultati descrizioni e archi in entrambi i versi: baseline/architecture-selected.json contiene 133 relazioni incidenti, distinte per tipo.

Freeze finale V3: fk_surface_mapping.cpp SHA256 c830c1c802657b50dd1cafa223af91263dba185cdf109524a16c602b6986f707; fk_quilt_mapping.cpp SHA256 21a41fb7513ba6ca48987956632aa2d2bbdf57c49c4f9681907a4c512668df05. Sorgenti del repository confrontati byte per byte col freeze. Entrambe le TU appartengono all'elenco CMake -fno-fast-math/-ffp-contract=off.

Il primo sito usa la curva 3D effettivamente costruita e il Plane effettivo, mantenendo il medesimo parametro t. Il fit è governato dalla proiezione fisica in mm: min(1e-10,0.05*fitBudget), fitBudget=0.2*min(tolleranza,risoluzione). Il residuo completo fin ha cap precedente 0.8*min(tolleranza,risoluzione). Nessuna distanza UV è confrontata con questi budget in mm nel ramo riconosciuto.

Dopo Body::build/Body::buildSheet si ricrea il certificato sugli edge/Plane/SP-curve finali. Genealogia dichiarata, supporto identico, range originale e multinsieme esatto dei pointer SP-curve e budget sono verificati; ogni contratto viene consumato una volta. Nessun riaggancio per prossimità. Body::build conserva oggi esplicitamente l'ordine degli input; mappedHistory ne dichiara la corrispondenza. Non viene eseguito unify in questo percorso, quindi non si attribuisce una verifica a un'operazione assente.

La storia viene preparata soltanto se richiesta o necessaria al ramo Plane. Storia e deviazione sono pubblicate dopo i controlli finali e dopo cancellationPoint. Provider riconosciuti invalidi/inconclusivi rifiutano il risultato; i limiti finiti di coefficienti/raffinamento e la cancellazione sono mantenuti. Nel Quilt Plane il vecchio fit UV per l'ingombro dell'offset è eliminato: offsetSurface(Plane) non dipende da quelle finestre e conserva gli assi del carrier.

## Prova continua indipendente

Export finali V3 congelati in exports-final: 20 JSON, 80 fin sorgenti e 80 fin effettive finali. I casi comprendono polynomial/rational, Trimmed, Transformed, range traslato, inversione, pose, scale 0.01/1/1000, quota e offset. Nove casi attraversano il dettaglio mapBodyByFunction e undici il chiamante pubblico SurfaceQuiltMap::bend; i due negativi reali UV=10000 passano dal chiamante pubblico.

Il verificatore non chiama il provider C++ e non riusa i suoi controlli estratti. Legge nodi/pesi/poli/trasformazioni/assi Plane IEEE esportati in floathex come Fraction esatte. Inserisce i nodi in aritmetica razionale, restringe tutte le campate con de Casteljau, solleva il Plane originale O+uX+vY e confronta i residui omogenei in Bernstein. Con E=H_e/W_e, PC=(H_u,H_v)/W_q, si confronta R_k=(O_k*W_q+X_k*H_u+Y_k*H_v)*W_e-H_e,k*W_q. Il convesso dei coefficienti di R e il minimo positivo dei pesi danno un limite continuo, con radice superiore razionale; le sintesi floating sono arrotondate verso l'esterno. La proiezione viene verificata separatamente usando la normale razionale X cross Y, senza assumere un frame perfettamente ortonormale.

Risultati finali:
- 80 fin sorgenti / 240 campate entro la risoluzione esplicita 1e-7 mm; massimo limite 4.163336342344337e-14 mm. Non si rivendica esattezza algebrica a budget sorgente zero.
- 80 fin finali / 27089 campate complete entro i budget realmente pubblicati; massimo limite superiore 1.678182363074691e-8 mm.
- Le stesse 80 fin soddisfano separatamente il budget di fit proiettato 1e-10 mm; massimo limite superiore 7.5803980688009e-11 mm.
- Negativo del verificatore: alterare pole[1].x della prima SP-curve finale di 1e-4 produce un testimone razionale fuori budget (residuo inferiore sicuro circa 3.75e-5 mm).

Dati: fraction-final.json (sorgenti/residui completi), fraction-final-physical-fit.json (cap e fit fisico strict), final-summary.json e fraction-final-negative-altered-pole.json. Le ricerche preliminari V1/V2 sono archiviate separatamente e non descrivono il contratto V3 finale. Il verificatore ottimizza soltanto ricerca delle campate/molticiplità; non modifica il metodo razionale della prova.

## Cause e limiti

Il lobo cubico razionale iniziale non è un negativo: baseline e candidato passano le prime 18 varianti. La baseline campiona anche le campate e i suoi 72 residui finali risultano entro i budget; non si conta un finding distinto per variante. I due riproduttori pubblici pertinenti sono le traslazioni del solo frame UV di 10000, con posa identità e posa 3D e geometria vicina all'origine: il vecchio fit UV per costruire un offset Plane esaurisce il raffinamento, mentre il ramo analitico elimina quel lavoro e la V3 riesce. Il confronto baseline è eseguito dal test agent; qui sono stati verificati sorgente e output effettivi dei casi finali. Le traslazioni UV di 100000/1000000 rimangono rifiuti conservativi atomici nella V3 strict, non successi rivendicati.

Una prova reale Circle->toBSpline sul quarto d'arco r=1 evidenzia 0.015739897675019612 mm a uguale parametro pi/8: la forma conica è conservata, il parametro angolare non lo è. La V3 non introduce tale conversione. Questo è un controllo di contratto, non un nuovo difetto pubblico conteggiato.

La corrispondenza edge/fin finale certificata non dimostra la fedeltà continua dell'edge 3D al mapping sorgente: quel fit rimane campionato. Restano campionati anche i Jacobiani locali e il fit generale delle superfici. Altri carrier, superfici istanziate non materializzate in Plane e il ramo UV non Plane mantengono il percorso precedente. Il cap/fit fin non certifica regolarità globale, iniettività, trim membership continuo, topologia globale, volume o assenza di autointersezioni. Nessuna prova GUI/OpenGL, build completa o sanitizer è stata svolta dall'auditor in questo blocco; quelle verifiche restano attribuite agli agenti che le hanno eseguite.
