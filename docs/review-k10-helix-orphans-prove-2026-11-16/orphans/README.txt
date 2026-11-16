Cinque funzioni private/locali orfane —16novembre2026
PROPOSTA PRIVATA. Nessuna rimozione ancora integrata dall'agente.

Scope: eliminare SOLTANTO5funzioni,2dichiarazioni e commento specifico associato,
7file,34righe eliminate,0aggiunte. Nessuna API pubblica, dato/classe/enum/formato,
CMake, dipendenza o metodo chiamato modificato. Fonti finali in stage/.
Patch applicabile al checkout base: git apply --check riuscito, base7/7invariata.
Prima di integrare confrontare base-hashes.json per non sovrascrivere altre chat;
preferire change.patch anziche' sostituire interi file se il checkout cambia.

CANDIDATE RICONTROLLATE E PROPOSTE
CadViewport::setSelectedPointPosition:privata; dichiarazione viewport.h1256,
selection.cpp746. ViewportInteractionTest e' friend; nessun suo test la usa.
StoryboardTree::firstFeature:privata statica; .h94/.cpp549, nessun usoQt.
Corner::otherFace:locale namespace anonimo fk_blend.cpp369, nonvirtuale.
ChainEdge::inPlane:locale namespace anonimo fk_blend_loop.cpp107, nonvirtuale.
BandMatrix::band:locale namespace anonimo fk_fill.cpp240, nonvirtuale.
Nessuna delle5ha mostrato un rischio concreto che richieda conservarla.
Non vengono conteggiate come5difetti funzionali o correzioni di leak.

EVIDENZA CORRENTE
CLAUDE.md corrente e rapporto review-orphans-refresh riletti.
767file attivi codice/configurazione non ignorati scansionati con rg; elenco e
hash in scanned-*. Sono inclusi275file sotto roots test indipendentemente da
CMake:163tests,111kernel/tests e1CMakeLists in kernel/occt_tests.
Non esistono ulteriori .inc/.ipp/.inl/.tpp/.cuh/.hh/.hxx/.m nel tree attivo.
Scansionati backend opzionali e rami ifdef in sorgenti, senza filtrarli per build.
Nessun include diretto dei3.cpp kernel; nessuna presa di indirizzo/chiamata
osservata. Nessun Q_OBJECT/Q_INVOKABLE/Q_PROPERTY/slot/signal rende i metodi
invocabili da Qt; viewport dichiara soltanto il friend considerato sopra.
Ricerca non limitata alla compile_commands o ai soli test elencati in CMake.
Tre controlli positivi verificano regex include diretto, uso nei test e puntatore
membro; non sono test del prodotto o nuovi test di getter inutilizzati.

Mappa corrente:769nodi/11127archi,7/7hash fonti combaciano. Descrizioni e
collegamenti entranti/uscenti consultati. Tutte5:0archi entranti call/reference.
ChainEdge::inPlane ha4site uscenti:derivative,cross,normalized,operator*;
sono rimossi col corpo, le funzioni destinazione rimangono usate altrove.
Graph-audit conserva descrizioni,hashsnapshot,site e contesto per5simboli.

OMONIMI ESCLUSI DALLE RIMOZIONI (falsi positivi lessicali)
FaceIntegrator::band in fk_mass.cpp e' chiamato realmente; preservato.
Lambda band in cad_drawing_derived e' chiamata; preservata.
Lambda inPlane cad_sketch_refs.cpp336/340 e' chiamata; preservata.
Structured binding inPlane in fk_blend_loop e variabili otherFace in fk_boolean
non sono i metodi candidati. Variabili/campi/parametri band in offset,vincoli,
tavole e test non sono BandMatrix::band. I risultati puntatore &band sono
riferimenti C++ a variabili o parametri, non indirizzi del metodo candidato.
selectedPointPosition (getter) e gli altri metodi di tutte le classi restano.
Non sono emerse nuove chiamate che rendano una delle5un falso positivo reale.

17API PUBBLICHE CONSERVATE, NESSUNA RIMOZIONE PROPOSTA
CadViewport::setEdgePickLinear
CadViewport::setExtendPickCallback
CadViewport::resumeExtendPick
CadViewport::constraintsVisible
CadViewport::sectionEnabled
CadViewport::geometrySnap
CadViewport::gridSnap
CadViewport::basisMatrix
CadViewport::normalizedViewAngle
CadViewport::extrusionNames
ForgeCad::DrawingSheetEditor::setModelProvider
ForgeCad::forgeIntersectRay
ForgeCad::modelBodyFeatures
ForgeCad::usesSketch
ForgeCad::commandIconNames
ForgeCad::nearestBodyEdge
ForgeCad::appendFixedReferenceLine
Public-seventeen.json conserva definizioni/dichiarazioni/visibilita' dal rapporto,
nuove occorrenze lessicali e grafo corrente:0call/reference per i17scope esatti.
Gli omonimi ViewportCamera::basisMatrix/normalizedViewAngle sono chiamati e
restano. Stringhe QSettings geometrySnap/gridSnap non sono usi dei getter.
API callback/getter pubbliche non vengono rimosse sulla sola assenza di usi.

VERIFICHE E LIMITI
Effettuati audit statico, confronto delta, git apply --check e basehash7/7.
Nessuna build, esecuzione app, suite regressioni o prova linker dichiarata.
Root integrera', fara' fullbuild, test pertinenti gia' esistenti e mappa.
La rimozione cambia simboli e archi uscenti, quindi mappa deve rigenerarsi.
Non sono scritti test che replichino getter inutilizzati.
Archivi ignorati/build generate, consumatori esterni e chiamanti futuri non
sono certificati; tre funzioni locali anonime non costituiscono API esterne.
Nessuna scrittura repository, nessuna modifica CLAUDE o vecchi archivi in
questo blocco di rimozione. Solo /tmp/orphan-five-review.
