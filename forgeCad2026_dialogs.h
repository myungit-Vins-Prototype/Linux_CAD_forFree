#ifndef FORGECAD2026_DIALOGS_H
#define FORGECAD2026_DIALOGS_H

// Interfaccia interna dei dialoghi per finestra principale e test.
#include "forgeCad2026_dialogs_common.h"

// Finestra della misura di un raccordo o smusso con l'anteprima dal vivo
// (CadViewport::requestBlendPreview dopo Invio nel campo). `edgesLabel` descrive gli
// spigoli; `chamferBox` (facoltativa) permette di passare da raccordo a
// smusso; "Spigoli..." (se `reselect` non e' nullo) chiude la finestra e
// chiede di tornare alla scelta degli spigoli. `apply` fa il comando: la
// finestra resta aperta finche' non riesce o si annulla.
struct BlendDialogResult {
    bool applied = false, reselect = false;
    double size = 0.0;
    bool chamfer = false;
    ChamferSpec spec;
};

// Elica e spirale: base (cerchio o arco di uno schizzo, o lo spigolo / la
// faccia scelti nella vista), tipo, modo (passo e giri, altezza e giri,
// altezza e passo), conicita', angolo di partenza, verso, con l'anteprima.
// "Scegli nella vista..." (se `allowPick`) chiude la finestra con `reselect`:
// la base si sceglie con un clic e la finestra si riapre con i valori.
struct HelixDialogResult {
    bool applied = false, reselect = false;
    ExtrusionObject definition;
};

struct ThreadChoice { const char *name; double diameter; double pitch; };

struct ExtendDialogResult {
    QVector<EdgePoint> edges;
    bool applied = false, reselect = false;
    double distance = 0.0;
    bool linear = false;
};

const QStringList &planeNames();

bool runUntilAppliedModeless(QMainWindow *window, CadViewport *viewport, QDialog &dialog, QFormLayout *form, QDialogButtonBox *buttons,
                                    const std::function<QString()> &apply);

// Selezionare una riga identifica il riferimento senza cambiare gli operandi
// della funzione o ricalcolarne l'anteprima. Vale anche per liste con checkbox.
void connectReferenceList(QListWidget *list, QDialog &dialog, CadViewport *viewport,
                                 std::function<QVector<GeometryRef>(QListWidgetItem *)> references);

BlendDialogResult blendDialog(QWidget *parent, CadViewport *viewport, const QString &title, int base, int hidden,
                                     const QVector<EdgePoint> &edges, double size, bool chamfer, bool allowKindChange, const ChamferSpec &initialSpec,
                                     const std::function<QString(int, const QVector<EdgePoint> &, double, bool, const ChamferSpec &)> &apply,
                                     bool pickInView = false);

bool scaleDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &definition,
                        const std::function<QString(const ExtrusionObject &)> &apply);

HelixDialogResult helixDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                                     bool allowPick, const std::function<QString(const ExtrusionObject &)> &apply);

// Nome di un corpo risultante nelle finestre delle operazioni tra corpi: il
// corpo logico ("Corpo 2"), non la feature che ne e' l'ultimo stadio, con
// superficie/nascosto.
QString resultBodyLabel(CadViewport *viewport, int index, bool markHidden = true);

// Corpi da offrire come operandi a una funzione nel posto `replaced` (-1 se
// nuova): i corpi che esistono in quel punto della storia (resultBodiesBefore:
// l'ultimo stadio di ogni corpo, non le feature intermedie), piu' quelli gia'
// scelti in modifica; tenuti quelli che `accept` vuole.
QVector<int> operandBodies(CadViewport *viewport, int replaced, const QVector<int> &used,
                                  const std::function<bool(const ExtrusionObject &)> &accept);

// Elenco dei solidi con cui fondere un'estrusione, una rivoluzione o una sweep
// (spunte, indice in Qt::UserRole): i corpi, non le feature. Restituisce gli indici.
QVector<int> fillMergeBodyList(QListWidget *list, CadViewport *viewport, int replaced, const QVector<int> &used);

// Sweep: profilo (uno schizzo), percorso (un altro schizzo o una curva, come
// un'elica) e orientamento del profilo lungo il percorso, con l'anteprima.
bool sweepDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply);

bool loftDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply);

// Proprieta' di massa del corpo scelto (o di tutti i solidi visibili):
// volume, superficie, massa con la densita' del materiale, baricentro,
// tensore d'inerzia al baricentro e all'origine, momenti e assi principali.
// Blocco dell'interfaccia durante una finestra "leggera" (non modale, per
// lasciare la vista attiva alla scelta dei riferimenti): menu, barre, albero e
// tutte le azioni (anche le scorciatoie) spenti; la vista senza menu contestuali.
bool runModeless(QMainWindow *window, CadViewport *viewport, QDialog &dialog);

// Piano di costruzione (nuovo, o al posto del corpo `replaced`): tipo,
// riferimenti scelti nella vista (la finestra resta aperta e la vista attiva:
// un clic per riferimento, si passa da solo a quello che manca; il pulsante di
// un riferimento lo fa scegliere di nuovo), distanza o angolo, normale
// invertita, misura a video. Anteprima in arancio. La conferma chiama `apply`
// (l'errore resta nella finestra).
bool datumDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply);

// Ripetizione delle entita' selezionate nello schizzo attivo: lineare (una o
// due direzioni), circolare o specchio, con le copie in anteprima. Direzioni e
// retta dello specchio: gli assi del piano, un angolo o un segmento selezionato
// (che non si ripete); centro: origine, punto scelto con Ctrl+clic, centro di
// un cerchio o arco selezionato, o coordinate.
bool sketchPatternDialog(QWidget *parent, CadViewport *viewport, int kind);

// Offset delle entita' selezionate dello schizzo attivo: distanza, verso, dalle
// due parti, archi negli angoli, originali di costruzione. Anteprima
// tratteggiata in arancio; un passo di Undo.
bool sketchOffsetDialog(QWidget *parent, CadViewport *viewport);

// Sposta / ruota / copia le entita' selezionate nello schizzo attivo:
// traslazione, rotazione attorno a un centro (il punto scelto con Ctrl+clic,
// il centro della selezione o l'origine) e copia, con l'anteprima arancio.
bool sketchMoveDialog(QWidget *parent, CadViewport *viewport);

// Ripetizione di un corpo o di una funzione (nuova, o al posto del corpo
// `replaced`): corpo, tipo (lineare, circolare, specchio), cosa si ripete,
// riferimenti scelti nella vista come per i piani di costruzione (direzione,
// asse o piano; la seconda direzione della griglia), istanze, passo o angolo.
// Anteprima in ambra; la conferma chiama `apply` (l'errore resta nella finestra).
bool patternDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply);

// Estrusione (nuova dello schizzo initial.sketchIndex, o al posto del corpo
// `replaced`): fine a distanza o fino a un punto, uno spigolo, una faccia o
// un piano (scelti nella vista: la finestra non e' modale), e fusione con
// altri solidi (unione o sottrazione) nei corpi scelti o in quelli che
// l'estrusione tocca. Anteprima in ambra; la conferma chiama `apply`.
bool extrusionDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                            const std::function<QString(const ExtrusionObject &)> &apply);

// Spostamento e rotazione di un corpo (nuovo, o al posto del corpo
// `replaced`): corpo, traslazione X/Y/Z, angolo e asse della rotazione
// (assi del modello o una retta scelta nella vista), copia. Finestra non
// modale; anteprima in ambra; la conferma chiama `apply`.
bool transformDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                            const std::function<QString(const ExtrusionObject &)> &apply);

// Nome del solo corpo logico per i pannelli che operano sui corpi interi.
QString logicalBodyLabel(CadViewport *viewport, int index);

// Il candidato che corrisponde al corpo cliccato nella vista: lo stesso stadio
// o lo stadio del suo corpo logico tra i candidati; -1 se non c'e'.
int resultBodyForPick(CadViewport *viewport, const QVector<int> &candidates, int picked);

QVector<ThreadChoice> threadChoices(int standard);

bool threadDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                         const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply);

// Booleana (nuova, o al posto del corpo `replaced`): operazione, il primo
// oggetto (A) e gli strumenti, anche piu' d'uno (A op B op C...: l'unione di
// tutti, l'intersezione di tutti, A meno tutti gli strumenti). Si offrono solo
// i corpi che esistono in quel punto della storia (resultBodiesBefore), non gli
// stadi intermedi; si scelgono anche cliccandoli nella vista (finestra non
// modale). Anteprima in ambra; la conferma chiama `apply` (l'errore resta nella
// finestra).
bool booleanDialog(QMainWindow *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply);

// Offset di superficie (nuovo, o al posto del corpo `replaced`): corpo, facce
// scelte nella vista (ogni clic aggiunge o toglie una faccia; nessuna = tutte),
// distanza con il verso. Finestra non modale come lo spostamento: la vista
// resta attiva per scegliere le facce. Anteprima in ambra; la conferma chiama
// `apply` (l'errore resta nella finestra).
// Con initial.feature == DeleteFace e' la finestra di *Elimina facce*: le facce
// scelte (almeno una) si tolgono dal corpo, niente distanza. Il corpo e' quello
// della faccia cliccata e la finestra non forza la visibilita' di alcun corpo.
// Con initial.feature == Shell e' il *Guscio*: un solido svuotato con lo
// spessore verso l'interno, le facce scelte (anche nessuna) tolte per l'apertura.
// Sformo con selezione multipla e piano neutro associativo.
bool draftDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                        const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply);

// Curve proiettate (ProjectedCurve): corpo, schizzo e verso. Il taglio con
// la proiezione di uno schizzo e' un metodo di Taglia superficie (trimDialog).
bool projectionDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
                             const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply);

// Spessore (Thicken): superficie (o facce scelte di un corpo), spessore, lato
// e direzione (normale delle facce, un asse o un riferimento della vista).
bool thickenDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                          const std::function<QString(const ExtrusionObject &)> &apply);

bool offsetDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                         const std::function<QString(const ExtrusionObject &)> &apply);

// Stesso riferimento curva (per togliere un tratto gia' scelto con un secondo clic).
bool sameCurveRef(const GeometryRef &a, const GeometryRef &b);

// Curva 3D associativa e suo intervallo tagliato: la proiezione serve solo
// a individuare il taglio, mai a sostituire la geometria spaziale.
bool spatialSketchDialog(QMainWindow *window,CadViewport *viewport,const QString &title,int replaced,
    const ExtrusionObject &initial,const std::function<QString(const ExtrusionObject &)> &apply);

bool extractedCurveDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced,
    const ExtrusionObject &initial, const std::function<QString(const ExtrusionObject &)> &apply);

// Superficie rigata (nuova, o al posto del corpo `replaced`) tra due catene di
// curve scelte nella vista. Finestra non modale come l'offset; anteprima in ambra.
bool ruledDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                        const std::function<QString(const ExtrusionObject &)> &apply);

// Superficie tra curve (nuova, o al posto del corpo `replaced`): le curve di un
// contorno chiuso scelte nella vista (spigoli, entita' degli schizzi, eliche;
// con un clic tutto il bordo libero di una superficie). Non modale; anteprima in ambra.
bool boundaryDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                           const std::function<QString(const ExtrusionObject &)> &apply);

// Superficie di riempimento (nuova, o al posto del corpo `replaced`): un
// contorno chiuso di curve, curve guida interne e le facce adiacenti dei
// corpi da continuare in tangenza o curvatura (proposte da sole: le facce con
// un bordo libero sul contorno).
bool fillDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                       const std::function<QString(const ExtrusionObject &)> &apply);

// Superficie planare (nuova, o al posto del corpo `replaced`): i contorni
// chiusi di uno schizzo o contorni chiusi di bordi scelti nella vista.
bool planarDialog(QMainWindow *window, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                         const std::function<QString(const ExtrusionObject &)> &apply);

// Cucitura di superfici (nuova, o al posto del corpo `replaced`): le superfici
// spuntate (di partenza quelle scelte nella vista, o tutte le superfici
// visibili), la tolleranza e il solido se chiudono un volume.
bool sewDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                      const std::function<QString(const ExtrusionObject &)> &apply);

// Misura nella vista (Analisi -> Misura): si cliccano una o due entita' con gli
// agganci della scelta dei riferimenti (vertici, punti medi e centri degli
// spigoli, punti degli schizzi, spigoli e curve, facce, piani e assi). Una
// entita': coordinate, lunghezza, raggio, area...; due: distanza minima con i
// punti piu' vicini (disegnata nella vista), angolo, interasse. Il clic su
// un'entita' gia' scelta la toglie, un terzo clic ricomincia; Esc nella vista
// azzera. Solo lettura: il documento non cambia.
void measureDialog(QMainWindow *window, CadViewport *viewport);

// Analisi -> Micro-geometrie: facce sottili, spigoli corti e quasi tangenti
// del corpo selezionato (o del primo solido visibile), segnati nella vista
// finche' la finestra resta aperta. Sono le geometrie di rumore che fanno
// fallire raccordi e offset: si correggono nella feature che le crea.
// Segni nella vista e righe del rapporto per le micro-geometrie del corpo `index`.
QVector<GeometryRef> microFeatureMarks(const ExtrusionObject &body, int index, QStringList &lines);

void shapeAnalysisDialog(QMainWindow *window, CadViewport *viewport);

void microFeaturesDialog(QWidget *parent, CadViewport *viewport);

void massPropertiesDialog(QWidget *parent, CadViewport *viewport);

// Finestra del taglio: corpo e strumento (corpo, superficie, piano o schizzo),
// parti da tenere scelte nell'elenco o direttamente con un clic nella vista.
// Le parti si ricalcolano a ogni cambio di superficie o di strumento; ognuna
// e' ricordata da un suo punto (trimKeep). `definition` porta i valori
// iniziali; `replaced` e' il corpo modificato (-1 nuovo): si scelgono solo i
// corpi che vengono prima. `apply` fa il comando.
bool trimDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, ExtrusionObject definition,
                       const std::function<QString(const ExtrusionObject &)> &apply);

// Finestra dell'estensione di una superficie: distanza e tipo (stessa
// superficie o lineare), con l'anteprima; "Bordi..." (se `allowReselect`)
// torna alla scelta dei bordi.
QVector<EdgePoint> completeExtensionContours(const ForgeCad::ForgeBody &body, const QVector<EdgePoint> &edges);

ExtendDialogResult extendDialog(QWidget *parent, CadViewport *viewport, const QString &title, int base, int hidden, const QVector<EdgePoint> &edges,
                                       double distance, bool linear, bool allowReselect, const std::function<QString(const QVector<EdgePoint> &, double, bool)> &apply);

// Finestra della rivoluzione (anche per modificarne una: lo schizzo resta
// quello, `fixedSketch`): schizzo, asse (dall'elenco o scelto nella vista: un
// segmento o una linea di costruzione dello schizzo, gli assi del modello, uno
// spigolo rettilineo o l'asse di una faccia cilindrica, purche' stia nel piano
// dello schizzo), angolo con il verso, risultato (corpo nuovo, unione o
// sottrazione con i solidi, come l'estrusione) e l'anteprima. La conferma
// chiama `apply`; la finestra resta aperta se fallisce.
bool revolutionDialog(QWidget *parent, CadViewport *viewport, const QString &title, int replaced, const ExtrusionObject &initial,
                             bool fixedSketch, const std::function<QString(const ExtrusionObject &)> &apply);

QString primitiveTitle(PrimitiveKind kind);

// Finestra dei parametri di una primitiva (il tipo non cambia): valori
// iniziali e risultato in `parameters`.
bool primitiveDialog(QWidget *parent, PrimitiveParameters &parameters,
                            const std::function<QString(const PrimitiveParameters &)> &apply = {},
                            const PreviewSpec<const PrimitiveParameters &> &preview = {});

#endif
