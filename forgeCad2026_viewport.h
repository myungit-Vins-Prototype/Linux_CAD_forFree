#ifndef FORGECAD2026_VIEWPORT_H
#define FORGECAD2026_VIEWPORT_H

#include "forgeCad2026_gui_common.h"
#include "forgeCad2026_camera.h"
#include "forgeCad2026_glass_compositor.h"
#include "cad_constraints.h"
#include "cad_curve_solver.h"
#include "cad_display_cache.h"
#include "cad_document_io.h"
#include "cad_export.h"
#include "cad_history.h"
#include "cad_import.h"
#include "cad_mass.h"
#include "cad_overlay_renderer.h"
#include "cad_shape_analysis.h"
#include "cad_sketch_edit.h"
#include "cad_sketch_offset.h"
#include "cad_snap.h"
#include "cad_spatial_sketch.h"
#include <QImage>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLWidget>
#include <QPainterPath>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVector2D>
#include <array>
#include <atomic>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>

class QPainter;
namespace ForgeCad { class ExpressionSpinBox; }

struct SelectedPoint {
    int kind = 0;
    int element = -1;
    int point = -1;
};

enum class EditablePointKind { Control, TangentIn, TangentOut };

enum class SceneObjectKind { None, Plane, Sketch, Extrusion };

struct SceneSelection {
    SceneObjectKind kind = SceneObjectKind::None;
    int index = -1;
    int subIndex = -1;

    bool operator==(const SceneSelection &other) const {
        return kind == other.kind && index == other.index;
    }
    bool operator!=(const SceneSelection &other) const { return !(*this == other); }
};

struct SketchElementSelection {
    int kind = -1;
    int index = -1;

    bool operator==(const SketchElementSelection &other) const {
        return kind == other.kind && index == other.index;
    }
    bool operator!=(const SketchElementSelection &other) const { return !(*this == other); }
};

// Widget interno della vista 3D. Le implementazioni sono nel .cpp dedicato.
class CadViewport final : public QOpenGLWidget, protected QOpenGLFunctions {
    friend class ViewportInteractionTest;
    template<class Callback>
    static void deliverResult(QPointer<CadViewport> guard, Callback callback);
public:
    int shapeAnalysisBody_ = -1;
    int shapeAnalysisMode_ = -1;
    int shapeAnalysisFace_ = -1;
    BodyDisplay shapeAnalysisFaceDisplay_;
    bool shapeAnalysisThrough_ = true;
    float zebraFrequency_ = 12, zebraAngle_ = 0;
    ForgeCad::ShapeAnalysis shapeAnalysis_;
    void calculateShapeAnalysis(const std::function<void()> &work);
    void clearShapeAnalysis();
    explicit CadViewport(QWidget *parent = nullptr);
    ~CadViewport() override;

    void setDisplayMode(int mode);
    void setHiddenEdgesVisible(bool visible);
    void setTopologyIdsVisible(bool visible);
    void setLightingPreset(int preset);
    // Aggancio dello schizzo: alla geometria (punti notevoli, segmenti, curve)
    // e alla griglia, indipendenti.
    void setGeometrySnap(bool enabled);
    bool geometrySnap() const;
    void setGridSnap(bool enabled);
    bool gridSnap() const;
    void setConstraintMode(int mode);
    void setLineLength(double length);
    void setLineAngle(double angle);
    void setPolygonSides(int sides);
    void setTessellationQuality(int quality);
    void setWheelZoomEnabled(bool enabled);
    quint64 renderedFrameSerial() const;
    void setGlassPanel(quintptr id, const QRect &rect, int blur, int radius, bool visible);
    void removeGlassPanel(quintptr id);
    void setExportMeshPreview(const BodyDisplay &display, bool quadrangular, bool limited);
    void clearExportMeshPreview();
    bool gpuGlassAvailable() const;
    QImage panelBackdropFrame();
    // Zoom: zoom_ e' l'altezza visibile in unita' del modello. I limiti
    // dipendono dalla geometria della scena (zoomLimits); "zoom tutto"
    // (resetZoom) inquadra tutta la geometria visibile.
    void zoomIn();
    void zoomOut();
    void resetZoom();
    // Inquadra tutta la scena; finche' l'utente non muove la vista si ripete
    // quando il widget cambia dimensione (il compositor la fissa dopo il primo disegno).
    void fitAll();
    // Pan: tenendo premuto questo tasto (o il tasto centrale del mouse) il
    // trascinamento sposta la vista invece di ruotarla (anche in modalita' schizzo).
    void setPanKey(int key);
    int panKey() const;
    void setReferencePlanesVisible(bool visible);
    void setPlaneVisible(int plane, bool visible);
    const BackgroundSettings &background() const;
    void setBackground(const BackgroundSettings &background);
    void setSplineShapeOptions(const ForgeCad::SplineShapeOptions &options);
    void setDrawingTool(DrawingTool tool);
    // Raggio del raccordo o distanza dello smusso tra segmenti dello schizzo.
    void setSketchBlendSize(bool chamfer, double size);
    double sketchBlendSize(bool chamfer) const;
    void setStatusCallback(std::function<void(const QString &)> callback);
    // Campioni dell'antialiasing (0 = spento); limitati al massimo della scheda.
    void setAntialiasing(int samples);
    int antialiasing() const;
    int maxAntialiasing() const;
    void setGridVisible(bool visible);
    // Default: meta' del lato dei piani di riferimento (che vanno da -4 a 4).
    static constexpr double kDefaultAxisLength = 2.0;
    void setAxisLength(double length);
    // Scala scelta dall'utente; la lunghezza a video segue lo zoom.
    double axisLength() const;
    double axisDisplayLength() const;
    void setAxesVisible(bool visible);
    bool axesVisible() const;
    void setAxesOnTop(bool onTop);
    bool axesOnTop() const;
    // Opacita' delle facce dei corpi in modalita' schizzo (0.05-1; 1 = opache).
    QColor featureHighlightColor() const;
    void setFeatureHighlightColor(const QColor &color);
    void setSketchBodyOpacity(double opacity);
    double sketchBodyOpacity() const;
    // Estrusione o rivoluzione dallo schizzo: la vista si puo' ruotare per vedere
    // da che parte va la funzione; alla fine torna la vista dello schizzo.
    void setSketchViewUnlocked(bool unlocked);
    // Vista di nuovo normale al piano dello schizzo (X a destra, Y in alto),
    // con lo stesso zoom e lo stesso punto dello schizzo al centro.
    void alignViewToSketch();
    void setOriginSnap(bool enabled);
    bool originSnap() const;

    void setSelectionCallback(std::function<void(SceneSelection)> callback);
    // Osservatore temporaneo usato dalle finestre non modali (per esempio
    // loft) senza sostituire la selezione permanente della finestra principale.
    void setSketchPickCallback(std::function<void(int)> callback);
    void setSketchEntityPickCallback(std::function<void(int, int, int)> callback);
    void setDocumentChangedCallback(std::function<void()> callback);
    void setPlaneContextCallback(std::function<void(int)> callback);
    // Lo strumento dello schizzo e' cambiato dal viewport (apertura dello
    // schizzo, Esc, linea chiusa su un punto): la finestra aggiorna i menu.
    void setToolChangedCallback(std::function<void(DrawingTool)> callback);
    void setSketchModeCallback(std::function<void(bool)> callback);
    void setRendererCallback(std::function<void(const QString &)> callback);
    void setWorkCallback(std::function<void(bool, const QString &, bool)> callback);
    int activeSketchIndex() const;
    bool sketchModeActive() const;
    // Documento corrente (per il salvataggio) e apertura di un documento: la
    // cronologia riparte da zero; i corpi usano lo snapshot salvato quando e'
    // presente, altrimenti si rigenerano dalla definizione.
    DocumentState currentDocument() const;
    DocumentState recoveryDocument() const;
    bool recoverySnapshotReady() const;
    bool hasPendingSketchChanges() const;
    void adoptDocumentIdentity(const ForgeCad::DocumentVersion &identity, const QUuid &base);
    // Rigenerazione manuale (Modifica -> Rigenera tutto): ogni feature si
    // ricalcola con il kernel attuale, anche quelle che vengono dallo snapshot
    // salvato nel documento. Un passo di Undo. Restituisce quante falliscono.
    int rebuildAllFeatures();

    LengthUnit lengthUnit() const;
    void setLengthUnit(LengthUnit unit);
    void loadDocument(DocumentState state,
                      const std::function<void(int, int, const QString &)> &progress = {});
    // Anteprima nella finestra Apri: usa soltanto le geometrie gia' presenti
    // nella cache del .prt. Non rigenera la cronologia parametrica e non
    // modifica il documento aperto nella finestra principale.
    void loadPreviewDocument(DocumentState state);
    // Corpi da esportare: quelli visibili con una geometria valida (gli
    // operandi delle booleane e le basi dei raccordi sono nascosti), anche
    // le curve (eliche).
    QVector<ForgeCad::ExportBody> exportableBodies() const;
    ForgeCad::StlExportOptions suggestedStlOptions() const;

    // Impostazioni dell'interfaccia che la finestra salva.
    int displayMode() const;
    bool hiddenEdgesVisible() const;
    int lightingPreset() const;
    int tessellationQuality() const;
    bool wheelZoomEnabled() const;
    bool referencePlanesVisible() const;
    bool gridVisible() const;
    const QVector<SketchObject> &sketches() const;
    const QVector<ExtrusionObject> &extrusions() const;
    const QVector<ModelBody> &modelBodies() const;
    int historyPosition() const;
    void applyHistoryPosition();
    void setHistoryPosition(int position);

    // Corpo della storyboard in cui confluisce ogni corpo logico: un corpo
    // consumato da una booleana, da una fusione o da una cucitura di un altro
    // corpo non esiste piu' da solo, le sue feature sono un ramo della storia
    // di quello (id -> id del corpo che resta; i corpi indipendenti mappano
    // su se stessi).
    // Corpi che esistono prima della posizione `position` della storia (tutta
    // con -1): per ogni corpo logico l'ultimo stadio calcolato (non soppresso,
    // con geometria) se nessuna feature precedente lo ha gia' consumato
    // (booleana, fusione, cucitura). Indici delle feature, in ordine. Le
    // feature intermedie e i corpi spariti non ci sono: e' l'elenco da offrire
    // come operandi.
    QVector<int> resultBodiesBefore(int position) const;

    QHash<quint64, quint64> storyboardRoots() const;
    QVector<int> featureSketches(int index) const;

    void setModelBodyVisible(int index, bool visible);

    QColor modelBodyMeshColor(int index) const;

    void setModelBodyMeshColor(int index, const QColor &color);

    int modelBodyIndexForFeature(int feature) const;

    QColor meshColorForFeature(int feature) const;

    QString renameModelBody(int index, const QString &name);

    int modelBodyTip(int index) const;

    void deleteModelBody(int index);

    QString setFeatureSuppressed(int index, bool suppressed);

    // Soppressione (o riattivazione) di piu' feature in un passo di Undo.
    QString setFeaturesSuppressed(const QVector<int> &indices, bool suppressed);

    void deleteFeature(int index);

    // Elimina le feature della storia in un passo di Undo: la base implicita
    // di quelle che seguono prosegue dallo stadio precedente.
    void deleteFeatures(const QVector<int> &indices);

    QString moveFeature(int index, int direction);

    QString moveFeatureTo(int index, int target);

    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    bool isObjectVisible(SceneObjectKind kind, int index) const;
    void setObjectVisible(SceneObjectKind kind, int index, bool visible);
    // Visibilita' di piu' schizzi/corpi in un passo di Undo.
    void setObjectsVisible(const QVector<SceneSelection> &objects, bool visible);
    void showAllObjects();

    SceneSelection selection() const;
    QVector<SceneSelection> selectedObjects() const;
    // Selezione di piu' oggetti dall'albero (Ctrl/Maiusc+clic): le funzioni
    // scelte si evidenziano sul modello (facce che hanno creato).
    void setSelectedObjects(const QVector<SceneSelection> &objects);
    void setMultiSelectionCallback(std::function<void(const QVector<SceneSelection> &)> callback);
    // `wholeBody`: si evidenzia tutto il corpo (scelta di un corpo), non le
    // sole facce create dall'ultima feature.
    void selectObject(SceneObjectKind kind, int index, bool wholeBody = false);

    QStringList extrusionNames() const;

    // Booleana esatta (B-rep) tra due corpi solidi, anche su piani diversi o
    // risultati di booleane precedenti. Il risultato e' un nuovo corpo che
    // conserva il riferimento agli operandi (rigenerato se cambiano); gli
    // operandi vengono nascosti ma restano nell'albero. Restituisce l'errore.
    QString createBoolean(BooleanOperation operation, int firstIndex, const QVector<int> &tools, const QString &name);

    // Candidati della fusione automatica di un'estrusione, rivoluzione o sweep
    // nel posto `index`: i solidi (corpi visibili) che esistono in quel punto
    // della storia, cioe' l'ultimo stadio di ciascuno, non le singole feature.
    QVector<int> mergeCandidates(int index) const;
    // Definizione pronta per la costruzione: con la fusione automatica i corpi
    // da fondere sono ancora da scegliere tra i candidati.
    ExtrusionObject withMergeCandidates(ExtrusionObject definition, int index) const;
    // La finestra di modifica con i valori di prima: la definizione coincide
    // con quella del corpo `index` (a parte la prova della fusione automatica,
    // che ritroverebbe i corpi gia' fusi) e il corpo ha la sua geometria.
    // Allora anteprima e conferma riusano il body esistente senza ricalcolare.
    bool unchangedFeature(const ExtrusionObject &definition, int index) const;
    // Gli spigoli `edges` (presi sulla base attuale) sono quelli del raccordo `blend`.
    bool sameBlendEdges(const QVector<EdgePoint> &edges, const ExtrusionObject &blend) const;

    // Una superficie (lamina) si puo' intersecare con un solido o tagliare con
    // un solido (la parte fuori); l'unione richiede due solidi.
    static QString booleanOperandsError(BooleanOperation operation, const ExtrusionObject &first, const ExtrusionObject &second);

    // Parametri nuovi per il corpo `index` (stessa funzione, stessi
    // riferimenti a schizzi e corpi): si ricostruisce e,
    // se riesce, prende il posto del vecchio e si rigenerano i corpi che ne
    // dipendono (un passo di Undo). Restituisce l'errore.
    QString updateBody(int index, const ExtrusionObject &definition);

    // --- Quote dei segmenti -----------------------------------------------------

    // Quota del segmento selezionato nello schizzo attivo (o di `index`).
    QString editSegmentDimension(int index = -1);

    // --- Strumento Quota (D) ---------------------------------------------------
    // Prima si scelgono le entita' da quotare (clic su punti ed entita': un
    // segmento, un cerchio o arco, due punti, punto e retta, due rette...),
    // poi la quota segue il puntatore e un clic nel vuoto la mette e ne chiede
    // il valore. Le quote tra due punti (e la lunghezza di un segmento) sono
    // orizzontali, verticali o allineate (oblique) secondo dove sta il
    // puntatore; H, V, O (obliqua) e A (automatico) fissano l'orientamento.
    QString beginDimensionTool();
    // Orientamento delle quote lineari: 0 automatico (dal puntatore), 1 allineata, 2 orizzontale, 3 verticale.
    void setDimensionOrientation(int orientation);

    // Punto o entita' dello schizzo attivo per la quota sotto il puntatore:
    // prima i punti (estremi, centri, punti delle curve, origine), poi le entita'.
    bool dimensionRefAt(const QPointF &point, ConstraintRef &ref) const;

    // Quota per i riferimenti scelti con il puntatore in `cursor` (falso se non ce n'e' una).
    bool buildDimension(const QVector<ConstraintRef> &input, const QPointF &cursor, SketchConstraint &result);
    void refreshDimensionPreview(const QPointF &cursor);

    // Clic con lo strumento Quota: su un punto o un'entita' la sceglie (o la
    // toglie); nel vuoto mette la quota in anteprima e ne chiede il valore.
    void dimensionToolClick(const QPointF &point);

    // Valore scritto per una quota d'angolo: senza segno vale nel verso attuale
    // della quota (come appare nel disegno), con il segno meno lo inverte.
    static double signedAngleInput(double input, double current);

    // Finestra del valore della quota nuova (orientamento per le quote tra due
    // punti, raggio o diametro per i cerchi), poi il vincolo con il risolutore.
    QString placeDimension(SketchConstraint constraint);

    // --- Eliminazione -----------------------------------------------------------

    // Corpi che dipendono (anche a cascata) dai corpi `removed`, compresi.
    QSet<int> withDependentBodies(QSet<int> removed) const;

    // Elimina lo schizzo o il corpo; se altri corpi ne dipendono lo dice e
    // chiede se eliminare anche quelli (tutto in un passo di Undo). In
    // modalita' schizzo elimina le entita' selezionate dello schizzo.
    void deleteSelection();

    // Solo feature della storia: si tolgono senza cancellare a cascata le
    // successive (come una per volta); altrimenti eliminazione con i dipendenti.
    void deleteObjectsOrFeatures(const QVector<SceneSelection> &objects);

    void deleteObject(SceneObjectKind kind, int index);

    // Elimina schizzi e corpi; i corpi che ne dipendono (e non sono tra quelli
    // scelti) si elencano e si eliminano solo con la conferma. Un passo di Undo.
    void deleteObjects(const QVector<SceneSelection> &objects);
    // --- Selezione a riquadro ------------------------------------------------------
    void armBoxSelection(const QPoint &position, bool additive);
    QRectF boxRect() const;
    // Da sinistra a destra: solo gli oggetti tutti dentro; da destra a sinistra anche quelli toccati.
    bool boxWindowMode() const;
    static bool segmentTouchesRect(const QPointF &a, const QPointF &b, const QRectF &r);
    // La polilinea (schermo) e' scelta dal riquadro.
    bool polylineInBox(const QVector<QPointF> &points, const QRectF &r, bool window) const;
    // Tutte le polilinee dell'oggetto devono esserci (riquadro da sinistra) o almeno una (da destra).
    bool polylinesInBox(const QVector<QVector<QPointF>> &polylines, const QRectF &r, bool window) const;
    QVector<QPointF> projectSketchPoints(const QVector<QPointF> &points, const SketchObject &sketch) const;
    QVector<QVector<QPointF>> sketchScreenPolylines(const SketchObject &sketch) const;
    void finishBoxSelection();
    // Maiusc+clic senza trascinare (fuori dallo schizzo): aggiunge o toglie l'oggetto sotto il puntatore.
    void toggleObjectAt(const QPoint &position);
    // Il riquadro mentre lo si trascina: continuo e azzurro da sinistra (oggetti dentro), tratteggiato e verde da destra (toccati).
    void drawSelectionBox(QPainter &painter) const;

    // Toglie lo schizzo e rinumera i riferimenti dei corpi.
    void removeSketchAt(int index);

    // Menu contestuale della vista: "Modifica parametri..." di un corpo.
    void setEditBodyCallback(std::function<void(int)> callback);

    // Estrusione esatta dei profili dello schizzo attivo. Restituisce l'errore.
    QString createExtrusion(double distance, const QString &name);
    // Estrusione con condizione di fine e fusione (definition.sketchIndex; -1: lo schizzo attivo).
    QString createExtrusion(const ExtrusionObject &definition, const QString &name);

    // Rivoluzione esatta dei profili chiusi dello schizzo `sketchIndex`
    // attorno al suo asse `axis` (segmento dello schizzo, -1 asse X, -2 asse Y
    // del piano) di `angle` gradi. Restituisce l'errore.
    QString createRevolution(int sketchIndex, int axis, double angle, const QString &name);

    // Solido elementare (parallelepipedo, cilindro, sfera, cono, toro).
    QString createPrimitive(const PrimitiveParameters &parameters, const QString &name);

    // Raccordo o smusso: prima si scelgono gli spigoli del corpo selezionato
    // (clic sulla vista, Invio conferma, Esc annulla), poi `edgePickFinished`
    // chiede la misura e crea il corpo con createBlend. Restituisce l'errore.
    QString beginEdgePick(bool chamfer, bool deferPreview = false);
    // Base di un'elica: un clic su uno spigolo circolare (source 1) o su una
    // faccia cilindrica o conica (source 2) di un corpo visibile; poi
    // setHelixPickCallback riceve il corpo, il tipo e un punto dello spigolo o della faccia.
    QString beginHelixPick();
    void setHelixPickCallback(std::function<void(int, int, EdgePoint)> callback);
    // Estensione di una superficie: si scelgono i suoi bordi (come gli spigoli
    // dei raccordi), poi setExtendPickCallback chiede la distanza.
    QString beginExtendPick(bool linear);
    void setExtendPickCallback(std::function<void(int, QVector<EdgePoint>)> callback);
    // Tipo dell'estensione per l'anteprima durante la scelta dei bordi.
    void setEdgePickLinear(bool linear);
    // Di nuovo alla scelta dei bordi da estendere, con quelli dati gia' scelti.
    void resumeExtendPick(int body, const QVector<EdgePoint> &edges, bool linear);
    // Modifica degli spigoli del raccordo (o smusso) `blend`: la scelta riparte
    // sulla sua base con gli spigoli attuali; al posto del raccordo si vede
    // l'anteprima. Invio chiama il callback di setEdgeEditCallback.
    QString beginBlendEdit(int blend, double size, bool chamfer);
    void setEdgeEditCallback(std::function<void(int, QVector<EdgePoint>, double, bool)> callback);
    // Misura dell'anteprima durante la scelta degli spigoli.
    void setEdgePickSize(double size);
    double edgePickSize() const;
    void setEdgePickChamferSpec(const ChamferSpec &spec);
    const ChamferSpec &edgePickChamferSpec() const;

    // Anteprima di un corpo definito da `definition` (una funzione nuova, o al
    // posto del corpo `index` se si modifica), calcolata in background; le richieste superate si scartano. Si disegna in ambra; al
    // suo posto spariscono il corpo modificato e gli operandi (booleane, base
    // del raccordo), che si vedono finche' l'anteprima non e' pronta.
    // Cache limitata alla finestra della funzione. Le copie condivise del
    // contesto fanno cambiare identita' ai vettori appena il documento viene
    // modificato: non si riusa un risultato con schizzi/operandi diversi.
    bool preparePreviewCacheContext();
    void refreshPreviewDragSource();
    void rememberPreview();
    // Durante il movimento una semplice estrusione varia solo lungo la normale.
    // Aggiorna la mesh grafica subito, senza promuoverla a geometria confermabile.
    void updateDragPreview(const ExtrusionObject &definition, int index);
    void requestPreview(const ExtrusionObject &definition, int index = -1);
    // Il pannello del taglio ha gia' calcolato e tassellato tutte le parti.
    // Installa direttamente quella scelta, annullando il job che rifarebbe la
    // stessa intersezione solo per l'anteprima.
    void showTrimPreview(const ExtrusionObject &definition, const ForgeCad::ForgeBody &geometry,
                         const BodyDisplay &display, int index = -1);
    // Raccordo o smusso degli spigoli `edges` del corpo `base` (al posto del raccordo `hidden`, se c'e').
    void requestBlendPreview(int base, const QVector<EdgePoint> &edges, double size, bool chamfer, int hidden = -1, const ChamferSpec &spec = {});
    void clearPreview();
    void clearBlendPreview();
    // Errore dell'anteprima (vuoto se riuscita o in calcolo), anche a ogni risultato nuovo.
    QString previewError() const;
    void setPreviewCallback(std::function<void(const QString &)> callback);
    const QString &previewNotice() const;

    // Non serve scegliere prima il corpo: se quello selezionato va bene si parte
    // da lui, altrimenti lo decide il primo spigolo cliccato (su qualsiasi
    // solido visibile, o superficie per l'estensione).
    QString startEdgePick(bool chamfer, bool extend = false, bool helix = false, bool deferPreview = false);
    // Di nuovo alla scelta degli spigoli, con quelli dati gia' scelti (dalla
    // finestra della misura, per cambiarli dopo un raccordo non riuscito).
    void resumeEdgePick(int body, const QVector<EdgePoint> &edges, bool chamfer);
    void setEdgePickCallbacks(std::function<void(const QString &)> status,
                              std::function<void(int, QVector<EdgePoint>, bool)> finished);
    void setEdgePickChangedCallback(std::function<void(int, QVector<EdgePoint>)> callback);
    void setEdgePickPanelKeyCallbacks(std::function<void()> accept, std::function<void()> cancel);
    void enableEdgePickPreview();
    int edgePickBody() const;
    QVector<EdgePoint> edgePickPoints() const;
    bool edgePickPanelActive() const;
    void endEdgePick(bool keepPreview = false);

    QString createBlend(int baseIndex, const QVector<EdgePoint> &edges, double size, bool chamfer, const QString &name, const ChamferSpec &spec = {});

    // Taglio della pelle di `sheet` con `tool` (-1: piano o schizzo): resta la
    // parte che contiene `keep`. Con `both` si rifila e conserva anche la parte
    // dello strumento che contiene `toolKeep`.
    QString createSheetTrim(int sheet, int tool, int plane, const EdgePoint &keep, const QString &name, int sketch = -1,
                            bool both = false, const EdgePoint &toolKeep = {});
    // Come sopra, con tutti i parametri del taglio (anche la proiezione dello schizzo).
    QString createSheetTrim(const ExtrusionObject &definition, const QString &name);
    // Le parti in cui lo strumento divide la superficie, per sceglierne una.
    QVector<SheetPiece> sheetPieces(int sheet, int tool, int plane, int sketch, QString *error) const;
    // Estensione dei bordi `edges` della superficie `sheet` (nascosta). Restituisce l'errore.
    QString createSheetExtend(int sheet, const QVector<EdgePoint> &edges, double distance, bool linear, const QString &name);
    // Scala uniforme del corpo `base` (nascosto): fattore e centro (0 origine,
    // 1 baricentro del solido, 2 il punto `center`). Restituisce l'errore.
    QString createScale(int base, double factor, int mode, const EdgePoint &center, const QString &name);

    // Corpo nuovo dalla definizione (elica, sweep, loft); i corpi `hide` (il
    // percorso di uno sweep) e quelli assorbiti da una fusione si nascondono
    // nello stesso passo di Undo.
    // Restituisce l'errore.
    // Risultato dell'anteprima per la stessa definizione (stessa previewKey: le
    // facce, lo spessore... non sono cambiati): il B-rep e la tassellazione
    // sono immutabili e diventano il risultato senza rifare il calcolo. Se
    // l'anteprima di quella definizione e' ancora in corso la si aspetta (un
    // secondo calcolo uguale costerebbe di piu'). Falso se non c'e' (si
    // ricostruisce); `error` riceve l'errore dell'anteprima fallita.
    bool takePreviewResult(ExtrusionObject &body, int previewIndex, QString *error);

    QString createBody(ExtrusionObject body, const QVector<int> &hide = {});

    // Elimina facce puo' disconnettere davvero la topologia (per esempio un
    // filetto rimasto isolato dal collo). Ogni shell risultante diventa un
    // corpo di storyboard distinto, invece di restare nascosta dentro un solo
    // ExtrusionObject multi-shell.
    QString createDeleteFaces(ExtrusionObject definition);

    // --- Piani di costruzione e riferimenti -------------------------------------

    // Scelta di un riferimento nella vista (punto, retta, curva o piano secondo
    // `roles`, maschera di ForgeCad::DatumRole) per il piano di costruzione
    // `owner` (-1: nuovo; i riferimenti devono venire da corpi precedenti). Il
    // clic chiama setReferencePickCallback(true, riferimento); Esc (false, {}).
    // Senza nulla sotto il puntatore il trascinamento ruota la vista.
    QString beginReferencePick(int roles, int owner);
    // Corpi da scegliere nella vista (le finestre delle booleane): finche' la
    // lista non e' vuota si disegnano e si scelgono solo questi, anche se sono
    // stadi nascosti della storia.
    void setPickBodies(const QVector<int> &bodies);
    void cancelReferencePick();
    bool referencePicking() const;
    void setReferencePickCallback(std::function<void(bool, GeometryRef)> callback);
    // Tutte le regioni dei due operandi partecipano insieme al picking: il
    // passaggio del mouse deduce corpo e parte, senza una scelta separata.
    void setTrimPartPickCallback(int firstBody, const QVector<SheetPiece> &firstPieces,
                                 int secondBody, const QVector<SheetPiece> &secondPieces,
                                 std::function<void(int, int, EdgePoint)> callback);
    // Riferimenti gia' scelti, evidenziati nella vista (finestra del piano).
    void setReferenceMarks(const QVector<GeometryRef> &marks);
    // La voce attiva di un elenco ha un colore distinto dai riferimenti scelti.
    void setReferenceListHighlight(QObject *owner, const QVector<GeometryRef> &refs);
    void clearReferenceListHighlight(QObject *owner);
    bool referenceListHighlighted(QObject *owner) const;
    // Misura (finestra Misura): la distanza minima tra i due punti, con l'etichetta.
    void setMeasureOverlay(bool valid, const QVector3D &from = {}, const QVector3D &to = {}, const QString &label = {});
    void drawMeasureOverlay(QPainter &painter) const;

    // Anteprima del piano della finestra (al posto del piano `replaced`, se lo si modifica).
    void setDatumPreview(bool valid, const SketchFrame &frame, double size, int replaced);
    void clearDatumPreview();
    // Distanza proposta per un piano parallelo: un quarto dei piani di riferimento, arrotondata.
    double suggestedDatumOffset() const;
    // Finestre modali "leggere" (il piano di costruzione): la vista resta
    // attiva per la scelta dei riferimenti, ma niente menu contestuali.
    void setInteractionLocked(bool locked);
    bool interactionLocked() const;
    bool documentReadOnly() const;
    const ForgeCad::DocumentVersion &documentVersion() const;
    const QUuid &documentBaseCheckinId() const;
    void setDocumentReadOnly(bool readOnly);
    // Il piano per la finestra, con il documento com'e' (il corpo `index`, -1 se nuovo).
    bool previewDatum(const DatumParameters &parameters, int index, SketchFrame &frame, QString *error) const;
    // Nuovo schizzo sul piano di costruzione `body` (lo segue quando si rigenera).
    int createDatumSketch(int body, const QString &name);
    // Lettura e geometria su dati locali; il ciclo GUI continua a disegnare.
    QString readImportFile(const QString &path, QVector<ForgeCad::ImportedPart> &parts, QStringList &notes);
    // Preparazione delle mesh fuori dal thread GUI; pubblicazione atomica
    // nella storia solo a lavoro concluso (un solo passo di Undo).
    QStringList importParts(const QVector<ForgeCad::ImportedPart> &parts, const QString &source,
                            const std::function<void(int, int)> &progress = {});

    // Proprieta' di massa del corpo `index` (densita' 1), sulla geometria esatta.
    ForgeCad::MassReport massReport(int index) const;

    // Anteprima dell'estensione dei bordi `edges` di `base` (al posto del corpo `hidden`, se c'e').
    void requestExtendPreview(int base, const QVector<EdgePoint> &edges, double distance, bool linear, int hidden = -1);

    // Le entita' selezionate nello schizzo attivo diventano di costruzione
    // (o tornano normali se lo erano tutte). Restituisce l'errore.
    QString toggleConstruction();

    // I segmenti selezionati diventano assi di simmetria (e di costruzione), o
    // tornano linee di costruzione se lo erano tutti. Restituisce l'errore.
    QString toggleSymmetryAxis();
    // Una linea di costruzione usata come asse (quote di raggio o diametro, simmetrie) diventa asse di simmetria.
    static void markSymmetryAxis(SketchObject &sketch, const SketchConstraint &c);
    // Quota dall'asse mentre la si posiziona: dalla parte del punto e' un
    // raggio, dall'altra parte dell'asse un diametro (la misura resta quella).
    void placeAxisDimension(SketchConstraint &c, const QPointF &pointer);

    // Solidi e superfici separati del corpo (in cache per body: il B-rep e' immutabile).
    QPair<int, int> bodyComponents(int index) const;
    // Rinomina uno schizzo o un corpo (un passo di Undo).
    QString renameObject(SceneObjectKind kind, int index, const QString &name);

    // --- Riferimenti esterni dello schizzo ---------------------------------------

    // kind come GeometryRef: piano di riferimento 1, asse 2, datum 8.
    void selectSketchReference(int kind, int index);
    bool pickSketchReferencePlaneAxis(const QPoint &position, int &kind, int &index) const;


    // Spigolo (o curva: elica, spirale) di un corpo visibile sotto il puntatore:
    // la polilinea di display e il suo campione piu' vicino (sta sulla curva esatta).
    struct SketchReferenceHit {
        int body = -1, polyline = -1;
        int sketch = -1, entityKind = -1, entity = -1;
        QVector3D point;
    };
    bool pickSketchReference(const QPoint &position, SketchReferenceHit &hit) const;
    // Lo spigolo o la curva sotto il puntatore proiettati nello schizzo attivo (un passo di Undo).
    QString appendReferenceAt(const QPoint &position, SketchObject &work, bool construction);
    QString convertReferenceAt(const QPoint &position);
    bool selectOffsetReferenceAt(const QPoint &position);
    // Le sezioni dei solidi e delle superfici visibili con il piano dello schizzo attivo (un passo di Undo).
    QString addSectionReferences(int *added = nullptr);
    // Punti/curve di contatto tra gli altri schizzi visibili e il piano
    // attivo. I punti sono entita' di costruzione fisse e quindi possono
    // ricevere Coincidente o imporre Punto sull'entita' a segmenti e curve.
    QString addSketchContactReferences(int *added = nullptr);
    void setReferencesConstruction(bool construction);

    // --- Ripetizione delle entita' dello schizzo ---------------------------------

    // Entita' selezionate nello schizzo attivo (per la finestra della ripetizione).
    QVector<SketchElementSelection> sketchSelection() const;
    // Posizioni dei punti scelti con Ctrl+clic nello schizzo attivo.
    QVector<QPointF> selectedSketchPoints() const;
    // I punti scelti con Ctrl+clic come riferimenti dei vincoli (stesso ordine di selectedSketchPoints).
    QVector<ConstraintRef> selectedSketchPointRefs() const;
    // Estremi di un segmento dello schizzo attivo.
    bool activeSegment(int index, SketchSegment &segment) const;
    // Le entita' da ripetere: la selezione senza il segmento `excluded` (la retta dello specchio).
    QVector<ForgeCad::SketchEntity> patternEntities(int excluded) const;
    // Modifica delle entita' selezionate (ripetizione, offset, spostamento):
    // `edit` lavora su una copia dello schizzo attivo e da' le entita' nuove o spostate.
    using SketchEdit = std::function<ForgeCad::SketchEditResult(SketchObject &, QVector<ForgeCad::SketchEntity> *)>;
    // Anteprima (le entita' risultanti tratteggiate in arancio); restituisce l'errore.
    QString previewSketchEdit(const SketchEdit &edit);
    // La modifica nello schizzo attivo (un passo di Undo): le entita' risultanti diventano la selezione.
    QString applySketchEdit(const SketchEdit &edit);
    QString previewSketchPattern(const ForgeCad::SketchPattern &pattern, int excluded);
    QString applySketchPattern(const ForgeCad::SketchPattern &pattern, int excluded);
    void setSketchOffsetSelectionCallback(std::function<void()> callback);
    QString previewSketchOffset(const ForgeCad::SketchOffset &offset);
    QString applySketchOffset(const ForgeCad::SketchOffset &offset);
    QString previewSketchMove(const ForgeCad::SketchMove &move);
    // I vincoli rimasti valgono gia' (moveSketchEntities toglie quelli falsi): niente risolutore.
    QString applySketchMove(const ForgeCad::SketchMove &move);
    void clearSketchPatternPreview();

    // --- Vincoli geometrici (oggetti) -------------------------------------------

    // Lo schizzo attivo in modalita' schizzo (nullptr altrimenti).
    const SketchObject *activeSketchObject() const;
    // Riferimenti scelti per un vincolo: i punti (Ctrl+clic, anche l'origine) e le entita' selezionate.
    QVector<ConstraintRef> constraintSelection() const;
    int pierceSourceIndex(const GeometryRef &source) const;
    // Applica Pierce a un punto e alla faccia scelta, senza copie nello schizzo.
    QString addSurfacePierce(const ConstraintRef &point, GeometryRef source);
    QString beginSurfacePierce();
    // Vincolo del tipo dato sui riferimenti scelti; le quote con `value` (NaN: la misura attuale).
    QString addConstraint(ConstraintType type, double value = qQNaN());
    QString deleteConstraints(QVector<int> indices);
    // Nuovo valore di una quota: lo schizzo si adatta (se i vincoli lo permettono).
    // `type`: le quote dall'asse possono passare da raggio a diametro (e viceversa).
    QString setConstraintValue(int index, double value, std::optional<ConstraintType> type = std::nullopt);
    // Parametri nuovi di una ripetizione parametrica (un passo di Undo): le
    // entita' di partenza restano ferme se si puo', le copie si spostano (o si
    // rifanno, se cambia il numero di istanze).
    QString setSketchPatternValues(int index, const SketchPatternData &values);
    // Finestra dei parametri di una ripetizione dello schizzo.
    void editSketchPatternValues(int index);
    // Finestra del valore di una quota: resta aperta (si riapre) finche' il valore non va bene o si annulla.
    void editConstraintValue(int index);
    // Gradi di liberta' dello schizzo attivo (ricalcolati quando lo schizzo cambia).
    const ForgeCad::SketchAnalysis &sketchAnalysis() const;
    QVector<int> selectedConstraints() const;
    void setSelectedConstraints(const QVector<int> &indices);
    void setConstraintsVisible(bool visible);

    // --- Vista in sezione ------------------------------------------------------
    // Solo visualizzazione: i corpi si tagliano con un piano di clip di OpenGL
    // (resta la parte dalla parte opposta alla normale) e le sezioni dei solidi
    // si chiudono con lo stencil (riempimento tratteggiato). La selezione
    // ignora quello che sta dalla parte tolta.
    void setSection(bool enabled, const QVector3D &point = {}, const QVector3D &normal = QVector3D(0, 0, 1), bool caps = true, bool showPlane = true);
    bool sectionEnabled() const;
    // Trascinamento del piano di sezione nella vista: `callback(spostamento)`
    // lungo la normale della sezione (nullptr: il piano non si trascina).
    // Manipolatore temporaneo: modifica i parametri del pannello, mai il documento.
    void clearFeatureHandle();
    void setFeatureHandle(const ExtrusionObject &definition, std::function<void(double)> changed);
    double featureHandleValue() const;
    void commitFeatureHandleValue();
    QVector3D featureHandleSketchCenter(const SketchObject &sketch) const;
    bool featureHandleGeometry(QVector3D &origin, QVector3D &axis, QVector3D &radial) const;
    QVector3D featureHandleRadialAt(double degrees) const;
    void drawFeatureHandle(QPainter &painter);
    bool featureHandleAt(const QPointF &position) const;
    void dragFeatureHandle(const QPointF &position);

    void setSectionDragCallback(std::function<void(double)> callback);
    // Maniglia del piano di sezione sullo schermo: la freccia dal centro del
    // piano lungo la normale (un disco se la normale guarda l'osservatore).
    QPolygonF sectionHandleShape(bool *disc = nullptr) const;
    // Il puntatore sta sulla maniglia o sul bordo del piano.
    bool sectionHandleAt(const QPoint &position) const;
    // La maniglia disegnata come un oggetto selezionato: arancio pieno con il contorno giallo e l'alone.
    void drawSectionHandle(QPainter &painter) const;
    float sectionArrowLength() const;
    // Spostamento lungo la normale per un movimento del puntatore: la sua
    // componente lungo la normale vista sullo schermo; con la normale verso
    // l'osservatore, il movimento verticale.
    double sectionDragDelta(const QPoint &mouse) const;
    // Direzione dal centro della vista verso l'osservatore (coordinate del modello).
    QVector3D viewerDirection() const;
    SketchFrame curveTrimView() const;
    // Inserimento libero: il piano e' soltanto uno strumento temporaneo per
    // determinare la profondita' del clic; i punti memorizzati restano XYZ.
    void setSpatialEditHistory(std::function<void()> undo,std::function<void()> redo);
    void setSpatialDrawing(std::function<void(SpatialPoint)> add, std::function<void()> finish, int owner=-1);
    void configureSpatialDrawing(int plane,double depth,const SpatialPoint &anchor,const QVector<SpatialPoint> &nodes,bool snap);
    bool spatialPointAt(const QPoint &position,SpatialPoint &point,bool allowSnap=true) const;

    // Chiamato quando il documento cambia (il piano della sezione puo' essersi spostato).
    void setSectionRefreshCallback(std::function<void()> callback);
    // Estensione della geometria visibile lungo `normal` dal punto `point` (per il cursore della posizione).
    void sceneRangeAlong(const QVector3D &point, const QVector3D &normal, double &lo, double &hi) const;
    bool constraintsVisible() const;
    // La finestra dei vincoli si aggiorna (selezione, vincoli, schizzo attivo cambiati).
    void setConstraintPanelCallback(std::function<void()> callback);

    void selectPlane(int plane);

    int createSketch(int plane, const QString &name);

    // Nuovo schizzo sul piano di una faccia piana del corpo `body` (il piano
    // resta quello della faccia al momento della creazione). -1 se la faccia non e' piana.
    int createFaceSketch(int body, const FaceHit &face, const QString &name);
    // Faccia selezionata nella vista come riferimento persistente (corpo e
    // faccia); falso se non ce n'e' una.
    bool selectedFaceReference(int &body, EdgePoint &reference) const;
    // Menu Schizzo: nuovo schizzo sulla faccia selezionata nella vista. Restituisce l'errore.
    QString createSketchOnSelectedFace(const QString &name);

    void selectSketch(int index);

    void beginSketchMode(int plane);

    void endSketchMode();

    // Rotazione dal modello allo spazio della vista standard: righe destra, alto, verso l'osservatore.
    QMatrix4x4 basisMatrix() const;

    QMatrix4x4 sceneProjectionMatrix() const;

    QMatrix4x4 sceneModelViewMatrix() const;

    void setRenderMatrices(const QMatrix4x4 &projection, const QMatrix4x4 &modelView);

    void setViewPreset(int preset);

    // Vista con gli assi x, y dello schermo lungo `x`, `y` e l'osservatore dalla
    // parte di `normal` (terna destrorsa): la rotazione Rz(roll) Rx(pitch)
    // Ry(yaw) ha per righe x, y, normal.
    void setViewFrame(const QVector3D &worldX, const QVector3D &worldNormal);

    // Vista normale al piano di riferimento: dalla parte e con l'orientamento
    // degli schizzi nuovi su quel piano (X a destra, Y in alto).
    void setViewNormal(int plane);

    // Sensibilita' della rotazione classica, indipendente dalla dimensione della vista.
    static constexpr float kOrbitDegreesPerPixel = ForgeCad::Gui::ViewportCamera::kOrbitDegreesPerPixel;
    static float normalizedViewAngle(float degrees);

    // Il pivot viene scelto solo quando comincia realmente l'orbita.
    QVector3D orbitPointAt(const QPoint &position) const;

    void orbitView(const QPoint &delta);

    // Orientamento degli assi del documento (non e' una modifica annullabile).
    const AxesOrientation &orientation() const;
    void setOrientation(const AxesOrientation &orientation);
    // La vista corrente diventa la vista frontale (come "Aggiorna vista standard" di SolidWorks).
    AxesOrientation orientationFromCurrentView() const;

protected:
    void initializeGL() override;

    void resizeGL(int width, int height) override;

    void paintGL() override;

    void keyPressEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    // Estremo di segmento dello schizzo attivo entro la tolleranza di selezione.
    bool segmentEndpointAt(const QPointF &point, QPointF &endpoint) const;

    // Aggancio del punto trascinato: agli altri punti e segmenti (non a se
    // stesso ne' ai segmenti che vi arrivano), all'origine, alla griglia.
    QPointF dragSnapPoint(const QPointF &raw);

    // Sposta il punto trascinato (e i punti coincidenti) in `target`; i
    // segmenti orizzontali/verticali collegati restano tali (moveSketchPoint).
    void dragSketchPoint(const QPointF &target);

    // Trascinamento di un segmento intero: i due estremi si spostano insieme
    // (e con loro i punti collegati). Parte dopo qualche pixel di movimento.
    void dragSketchSegment(const QPointF &raw);

    bool isEditTool() const;

    QString editToolLabel() const;

    // Clic con uno strumento di modifica (taglia, estendi, spezza, raccordo,
    // smusso). La modifica si fa su una copia dello schizzo (cad_sketch_edit):
    // se riesce diventa un passo di Undo, altrimenti il messaggio va nella barra di stato.
    void applyEditTool(const QPointF &point);

    void showStatus(const QString &message);

    // Assi delle rivoluzioni dello schizzo dopo l'eliminazione di segmenti.
    void remapRevolutionAxes(int sketchIndex, const QVector<int> &segmentMap);
    // Entita' dello schizzo `sketchIndex` rinumerate (segmentMap / curveMap:
    // vecchio indice -> nuovo, -1 eliminata; vuota = invariati): gli assi delle
    // rivoluzioni e i riferimenti alle entita' (piani di costruzione,
    // ripetizioni, fine delle estrusioni, assi degli spostamenti, curve delle
    // rigate e bordi delle planari). Un riferimento a un'entita' eliminata
    // diventa -1: la rigenerazione lo segnala invece di passare a un'altra entita'.
    void remapSketchReferences(int sketchIndex, const QVector<int> &segmentMap, const QVector<int> &curveMap = {});

    // Vincolo che il segmento in costruzione ricevera': dall'aggancio del
    // punto (estremo, punto medio, su un segmento) e, in modalita'
    // automatica, dalla direzione (orizzontale, verticale, perpendicolare al
    // segmento da cui parte, parallelo a un segmento esistente, entro 4
    // gradi). Le quote di lunghezza e angolo e i vincoli espliciti H/V vincono.
    struct LineInference {
        QPointF point;
        int constraint = -1;  // -1 libero, 1 orizzontale, 2 verticale, 3 perpendicolare, 4 parallelo
        int reference = -1;   // segmento di riferimento (perpendicolare, parallelo, su segmento)
        QStringList labels;
    };

    // Segmento su cui sta il punto (estremo, punto medio o interno), -1 se nessuno.
    int segmentAt(const QPointF &point, int exclude = -1) const;

    // Etichetta dell'aggancio; sull'origine del piano "Origine".
    QString snapLabelAt(SnapKind kind, const QPointF &point) const;

    static QString snapLabel(SnapKind kind);

    LineInference inferLinePoint(const QPointF &raw);

    // Vincoli del segmento appena aggiunto con i punti su cui i suoi estremi si
    // sono agganciati: coincidenze con estremi di altri segmenti e punti delle
    // curve; punto medio o punto su un altro segmento, punto su un cerchio,
    // arco o ellisse.
    void recordCoincidences(SketchObject &sketch, int segment);

    // Vincoli del punto `here` (in `point`) con le altre entita': coincidente
    // con i punti che tocca, altrimenti punto medio o punto su un segmento, o
    // su un cerchio o arco.
    void recordPointCoincidences(SketchObject &sketch, const ConstraintRef &here, const QPointF &point);

    // Arco per tre punti (inizio, fine, un punto di passaggio) nel formato
    // dell'arco: centro, inizio, fine in senso antiorario. Il centro e' il
    // circocentro dei tre punti; se il passaggio sta a destra della corda
    // inizio -> fine l'arco gira in senso orario e i due estremi si scambiano.
    // Falso se i punti sono allineati.
    static bool threePointArc(const QPointF &start, const QPointF &end, const QPointF &through, CurveObject &arc);

    // Arco che parte da `start` tangente alla direzione `direction` (unitaria)
    // e finisce in `end`: il centro sta sulla normale in `start`, a distanza
    // r = |d|^2 / (2 n.d) con d = end - start (con segno: a sinistra della
    // direzione l'arco gira in senso antiorario). In `startPoint` il punto
    // dell'arco (1 o 2) che sta in `start`. Falso se `end` sta sulla tangente.
    static bool tangentArc(const QPointF &start, const QPointF &direction, const QPointF &end, CurveObject &arc, int *startPoint = nullptr);

    // Da dove puo' partire un arco tangente: gli estremi di segmenti e archi
    // in `point`, con la direzione che prosegue l'entita' oltre l'estremo.
    struct TangentStart {
        ConstraintRef entity;  // il segmento o l'arco (point -1)
        QPointF direction;
    };
    QVector<TangentStart> tangentStartsAt(const QPointF &point) const;
    // Tra le entita' che finiscono nel punto di partenza, quella la cui
    // direzione va di piu' verso il cursore (in un angolo tra due segmenti
    // l'arco continua quello da cui ci si allontana).
    TangentStart tangentStartFor(const QPointF &cursor) const;

    // Crea l'arco per tre punti o l'arco tangente: un Arc (centro, inizio,
    // fine) con le coincidenze degli estremi e, per quello tangente, il
    // vincolo di tangenza con l'entita' da cui parte.
    void finalizeDrawnArc();

    // Anteprima della curva in costruzione (cerchio, arco, poligono, spline,
    // NURBS) con il cursore come punto successivo. Solo per disegnare: la
    // curva vera si crea al clic (finalizePrimitive / finalizeCurve).
    bool previewCurve(CurveObject &curve) const;

    // Quote dell'anteprima, calcolate sulla geometria esatta.
    QStringList previewMeasures() const;

    // Anteprima tratteggiata della curva in costruzione e del raggio.
    void drawCurvePreview(QPainter &painter, const SketchObject &sketch) const;

    // Etichette del vincolo vicino al cursore (e il segmento di riferimento tratteggiato).
    void drawInferenceTags(QPainter &painter) const;

    // Quote di cerchi, archi e poligoni: raggio e centro (poligono: raggio
    // circoscritto e lati). I punti collegati (estremi di segmenti sul centro
    // o sui punti della curva) seguono con moveSketchPoint.
    // Quote dell'ellisse: semiassi, angolo del primo semiasse e centro.
    QString editEllipseDimension(int index);

    QString editCurveDimension(int index);

    // Doppio clic su un segmento dello schizzo: la sua quota.
    void mouseDoubleClickEvent(QMouseEvent *event) override;

    void contextMenuEvent(QContextMenuEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    // Evidenziazione al passaggio (riferimenti, spigoli, oggetti) nel punto `currentPosition`.
    void updateHover(const QPoint &currentPosition);

    // Il tasto del pan vince sulle scorciatoie dei menu (per esempio una lettera).
    bool event(QEvent *event) override;

    void keyReleaseEvent(QKeyEvent *event) override;

    void focusOutEvent(QFocusEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void leaveEvent(QEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

private:
    DocumentState documentState(bool normalizeVisibility = true) const;

    void selectionChanged();

    // Risolve i vincoli dello schizzo attivo con i bersagli dati; se non ci
    // riesce lo schizzo torna a `before`. Le curve si ricampionano.
    bool solveActive(const QVector<ForgeCad::PointTarget> &targets, const SketchObject &before, QString *error = nullptr);
    // Il cursore e' un obiettivo di trascinamento, non un nuovo vincolo:
    // se non appartiene alla traiettoria ammessa, proietta la proposta sui
    // vincoli esistenti. Quote e comandi numerici usano ancora solveActive.
    bool solveActiveDrag(const QVector<ForgeCad::PointTarget> &targets,const SketchObject &before,const QVector<ForgeCad::PointTarget> &anchors={});
    // Bersagli: tutti i punti dello schizzo che stanno in `position`, fermi li'.
    static QVector<ForgeCad::PointTarget> targetsAt(const SketchObject &sketch, const QPointF &position);
    // Bersagli: i punti della curva dove stanno ora.
    static QVector<ForgeCad::PointTarget> curveTargets(const SketchObject &sketch, int curve);
    // Le quote di raggio e diametro della curva prendono il valore attuale.
    static void syncCurveDimensions(SketchObject &sketch, int curve);

    // Quote come nel disegno tecnico (coordinate schermo): linee di misura, di
    // riferimento e di richiamo, archi, frecce e testo.
    struct DimensionGraphic {
        QPainterPath lines;
        QVector<QPolygonF> arrows;
        QString text;
        QPointF textCenter;
        double textAngle = 0.0;  // gradi
        QPolygonF textBox;       // per la scelta con il mouse
    };
    static QString dimensionText(const SketchConstraint &c);
    // Freccia con la punta in `tip` e il corpo dalla parte di `from` (schermo).
    static QPolygonF arrowHead(const QPointF &tip, const QPointF &from);
    bool dimensionGraphic(const SketchObject &sketch, int index, DimensionGraphic &g) const;
    // Anche per una quota che non e' (ancora) nello schizzo: l'anteprima dello strumento Quota.
    bool dimensionGraphic(const SketchObject &sketch, const SketchConstraint &c, DimensionGraphic &g) const;
    // Quota sotto il puntatore (testo o linee), -1 se nessuna.
    int dimensionAt(const QPoint &position) const;
    void drawDimensions(QPainter &painter) const;

    // Simboli dei vincoli accanto alle entita' (coordinate schermo), per disegnarli e sceglierli.
    struct ConstraintGlyph {
        QRectF rect;
        int constraint = -1;
        bool dot = false;  // coincidenza: un punto invece di un'etichetta
        QString text;
    };
    QVector<ConstraintGlyph> constraintGlyphs() const;
    int constraintAt(const QPoint &position) const;
    void drawSketchConstraints(QPainter &painter) const;
    // Entita' e punti del vincolo evidenziati.
    void highlightConstraintEntities(QPainter &painter, const SketchObject &sketch, int index, const QColor &color) const;

    // Salva lo stato corrente nella cronologia: va chiamata subito prima di
    // una modifica al documento, seguita da documentChanged() a modifica fatta.
    void recordUndo();

    void documentChanged();

    // Cache della selezione esatta dei corpi visibili preparate in un thread
    // (forgeWarmPickCaches), una volta per B-rep: altrimenti la prima
    // costruzione bloccava il primo clic su un corpo con superfici a distanza
    // molto fitte (Loft_offset: mezzo secondo prima che la vista rispondesse).
    void warmPickCaches();

    // Modifica allo schizzo attivo: rigenera i corpi che ne dipendono.
    void sketchEdited();

    // Rigenerazione parametrica: le estrusioni dello schizzo indicato e, a
    // cascata, le booleane che usano corpi rigenerati. I corpi dipendono solo
    // da corpi con indice minore, quindi basta una passata in ordine.
    void regenerateDependents(int sketchIndex);

    bool hasSurfacePierceSource(int index) const;
    void refreshSurfacePierceSketches(int sourceIndex,QSet<int> &changed,int targetSketch=-1);
    // Rigenera da `from` in poi i corpi che dipendono da corpi `dirty` o da
    // schizzi `dirtySketches` (tutti con `all`). Un piano di costruzione
    // rigenerato porta con se' gli schizzi che vi stanno sopra (followDatum),
    // e con loro i corpi che li usano (hanno indice maggiore del piano).
    // I corpi indipendenti tra loro (nessuno e' operando dell'altro) si
    // costruiscono in parallelo, a livelli: un corpo parte quando i suoi
    // operandi da rifare sono pronti. I piani di costruzione fanno da
    // separatori, perche' followDatum cambia gli schizzi che vi stanno sopra.
    void regenerateFrom(int from, QVector<bool> dirty, QSet<int> dirtySketches, bool all = false,
                        const std::function<void(int, int, const QString &)> &progress = {});

    // Costruisce i corpi `indices` (crescenti): ogni livello contiene quelli
    // i cui operandi da rifare stanno nei livelli precedenti. buildGeometry
    // legge solo gli schizzi e gli operandi (gia' pronti) e scrive il suo corpo.
    void rebuildParallel(const QVector<int> &indices,
                         const std::function<void(int, const QString &)> &advanced = {});

    // Gli schizzi sul piano di costruzione `index` prendono il suo piano: la
    // normale del piano, l'asse X precedente proiettato (lo schizzo non ruota
    // se il piano si sposta o si inclina poco) e l'origine precedente proiettata.
    void followDatum(int index, QSet<int> &changed);
    // Sistema di uno schizzo sul piano di costruzione: normale del piano; asse X
    // e origine quelli di `previous` proiettati sul piano (se c'e' e l'asse non
    // e' quasi normale), altrimenti l'asse X del piano e la proiezione
    // dell'origine del modello (come gli schizzi su una faccia).
    static SketchFrame datumSketchFrame(const SketchFrame &datum, const SketchFrame *previous);

    // Toglie i corpi (gli operandi di booleane e raccordi eliminati che restano
    // tornano visibili) e rinumera i riferimenti.
    void removeBodies(const QSet<int> &removed);

    // Elimina le entita' selezionate dello schizzo attivo, con i loro vincoli;
    // gli indici dei segmenti (vincoli, linee di costruzione, assi delle
    // rivoluzioni) si rinumerano.
    void deleteSketchElements();

    // Sposta di `delta` i punti dello schizzo in `from` (estremi dei segmenti,
    // punti delle curve); i segmenti orizzontali e verticali collegati
    // trascinano l'altro estremo quanto serve per restarlo. `fixed` non si
    // muove; il segmento `edited` non propaga all'indietro.
    void moveSketchPoint(SketchObject &sketch,int edited,const QPointF &from,const QPointF &delta,const QPointF &fixed);
    void moveSketchPoints(SketchObject &sketch, int edited, const QVector<QPointF> &origins, const QPointF &delta, const QPointF &fixed);

    // I corpi che dipendono (anche a cascata) dal corpo `changed`, gia' rigenerato.
    void regenerateAfter(int changed);

    // Tutti i corpi, in ordine (dopo un cambio di kernel).
    void regenerateAll(const std::function<void(int, int, const QString &)> &progress = {});

    // Anteprima locale della fusione: i corpi toccati restano opachi e si
    // disegnano solo le facce nuove (estrusione e rivoluzione).
    static bool localMergePreview(const ExtrusionObject &body);

    // Geometria esatta del corpo `index` (estrusione o booleana) con il kernel
    // attivo, poi la sua tassellazione. Gli operandi di una booleana hanno
    // indice minore e sono gia' rigenerati. In caso d'errore il corpo resta
    // senza geometria con il messaggio in `error`.
    // Il calcolo va in un thread su copie degli schizzi e dei corpi; il thread
    // dell'interfaccia intanto disegna (la finestra di avanzamento si anima e
    // la vista mostra i corpi di prima) senza accettare input, poi il corpo
    // nuovo prende il posto di `body`.
    void rebuildBody(ExtrusionObject &body, int index);

    // Esegue `task` in un altro thread mentre il ciclo di eventi continua a
    // girare (ridisegni, animazione della barra di avanzamento) senza input
    // dell'utente; ritorna quando ha finito, come una chiamata diretta. `task`
    // non deve toccare lo stato che il thread dell'interfaccia disegna.
    void runWhileResponsive(const std::function<void()> &task);
    void tessellateBody(ExtrusionObject &body, int index = -1) const;
    void restoreDocument(DocumentState state);

    // Sfondo sfumato disegnato in coordinate schermo: bande perpendicolari alla
    // direzione della sfumatura (angolo), con il punto di mescolanza al 50%
    // spostabile lungo la direzione (posizione).
    void drawBackgroundGradient();

    // Tolleranza di selezione in unita' di schizzo, equivalente a qualche pixel.
    double pickTolerance(double pixels = 8.0) const;

    static QVector<QPointF> curveEditMarkers(const CurveObject &curve,bool hover);
    static bool closedSpline(const CurveObject &curve);
    static void moveSplineHandle(CurveObject &curve,int control,int side,QPointF position);
    static bool visibleSplineHandle(const CurveObject &curve, int control, int side);

    bool findCurveEditPoint(const QPointF &point, int &curveIndex,
                            int &controlIndex, EditablePointKind &pointKind) const;

    static double distanceToSegment(const QPointF &point, const QPointF &first, const QPointF &second);

    // Curva libera (spline/NURBS) piu' vicina al punto, per inserire un punto di controllo.
    int findNearestFreeCurve(const QPointF &point) const;

    static bool canChangeControlPointCount(const CurveObject &curve);

    void addControlPointToCurve(const QPointF &point);

    // Applica una modifica strutturale a spline/NURBS e rinumera i riferimenti
    // dei vincoli ai punti e alle maniglie. `origins[new]` contiene l'indice
    // precedente, oppure -1 per un punto appena inserito.
    void commitFreeCurveEdit(int curveIndex, const CurveObject &working, const QVector<int> &origins);

    struct SketchCombCache {
        CurveObject source;
        double scale = -1;
        QVector<ForgeCad::OverlayVertex> lines;
        bool valid = false;
    };
    bool sketchCombVisible_ = false;
    double sketchCombScale_ = 0; // zero: automatica per curva; altrimenti mm²
    QVector<SketchCombCache> sketchCombCache_;
    int sketchCombPreviewIndex_ = -1;
    CurveObject sketchCombPreview_;

    const QVector<ForgeCad::OverlayVertex> &sketchCurveComb(int index, const CurveObject &curve);

    void drawSketchCurvature();

    void editFreeCurve(int curveIndex, int initialPoint = -1);

    SketchElementSelection findSketchElement(const QPointF &point) const;

    void selectSketchElement(const QPointF &point, bool additive);

    // Il punto medio resta un riferimento al segmento: il solver applica
    // Punto medio al punto scelto e al segmento, senza creare geometria fittizia.
    int segmentMidpointAt(const QPointF &point) const;

    // Ctrl+clic su due punti: vincolo di coincidenza (i punti vengono uniti
    // nel punto medio, calcolato in double).
    bool selectPointWithControl(const QPointF &point);

    bool isValidSelectedPoint(const SelectedPoint &point) const;

    // Punti di una curva che si scelgono singolarmente: tutti, tranne i poli
    // interni delle curve derivate (bordi copiati, offset), che sono un'entita' unica.
    static bool isPickableCurvePoint(const CurveObject &curve, int control);

    QPointF selectedPointPosition(const SelectedPoint &point) const;

    void setSelectedPointPosition(const SelectedPoint &point, const QPointF &position);

    void finalizeCurve();

    // Cerchio, arco e poligono sono memorizzati con i soli parametri esatti
    // (centro, punti, numero di lati); la geometria viene dal kernel.
    void finalizePrimitive();

    // Rettangolo (due angoli o centro e angolo): quattro segmenti orizzontali e
    // verticali, collegati dai vincoli di coincidenza (e agganciati agli altri
    // punti su cui cadono gli angoli).
    void finalizeRectangle();

    // Angolo opposto del rettangolo in costruzione: con Maiusc un quadrato.
    QPointF rectangleCorner(const QPointF &anchor, const QPointF &cursor) const;
    // Terzo punto dell'ellisse: sulla perpendicolare al primo semiasse, alla
    // distanza del punto `through` dalla retta di quel semiasse.
    static QPointF ellipseMinorPoint(const QPointF &center, const QPointF &major, const QPointF &through);
    // Dopo lo spostamento di un punto dell'ellisse: il centro trascina gli altri,
    // il terzo punto torna sulla perpendicolare al primo semiasse.
    static void normalizeEllipse(CurveObject &curve, int moved, const QPointF &previous);

    QPointF projectWorldPoint(const QVector3D &point) const;

    void drawReferenceLabels();

    struct TopologyLabelItem {
        QRectF box;
        QPointF anchor;
        QString text;
        int body = -1;
        int subshape = -1;
        bool edge = false;
    };
    QVector<TopologyLabelItem> topologyLabelItems() const;
    bool pickTopologyLabel(const QPoint &position, TopologyLabelItem &hit) const;
    void drawTopologyLabels(QPainter &painter) const;

    void drawSketchDimensions(QPainter &painter) const;

    QVector3D mapSketchPoint(const QPointF &point, const SketchObject &sketch) const;
    // Punto del piano dello schizzo attivo (in costruzione, cursore).
    QVector3D mapActiveSketchPoint(const QPointF &point) const;

    QPointF screenToSketchPoint(const QPoint &position) const;
    // Intersezione del raggio del puntatore con il piano dello schizzo attivo
    // (falso se il piano si vede di taglio).
    bool rayToSketchPlane(const QPoint &position, QPointF &point) const;

    // Punti esatti a cui agganciarsi oltre ai segmenti: estremi delle curve,
    // centri di cerchi/archi/poligoni e vertici dei poligoni.
    QVector<QPointF> snapCandidates(const SketchObject &sketch) const;

    QPointF snapPoint(const QPointF &point, bool snapToGrid = true);

    // Quadranti (0, 90, 180, 270 gradi, esatti) di cerchi e archi entro la tolleranza.
    bool nearestQuadrant(const SketchObject &sketch, const QPointF &point, double tolerance, QPointF &quadrant) const;

    // L'angolo sta nell'arco (verso antiorario dall'inizio alla fine, come curveGeometry)?
    static bool onArc(const CurveObject &arc, double angle);

    // Punto esatto piu' vicino su una curva dello schizzo, entro `tolerance`
    // (cerchi e archi in forma chiusa, il resto proiettando sulla curva
    // esatta). Restituisce l'indice della curva (-1 nessuna).
    int nearestCurvePoint(const SketchObject &sketch, const QPointF &point, double tolerance, QPointF &result) const;

    void drawSnapMarkers();

    // Raggio di vista (proiezione ortografica) che passa per un pixel.
    void viewRay(const QPoint &position, QVector3D &origin, QVector3D &direction) const;

    // Oggetto sotto il puntatore, in ordine di priorita': linee degli schizzi,
    // solidi estrusi (il piu' vicino all'osservatore), piani di riferimento.
    // Primo triangolo della tassellazione colpito dal raggio (distanza lungo la
    // direzione normalizzata). Solo per l'evidenziazione al passaggio del
    // mouse: e' la selezione a video, non un calcolo; il clic usa la forma esatta.
    static bool meshRayHit(const BodyDisplay &display, const QVector3D &origin, const QVector3D &direction, float &distance);

    // Identificatore del corpo sotto il pixel, prodotto dalla GPU. Il buffer
    // conserva colori interi (indice + 1) e la profondita' della stessa vista
    // del viewport; viene ridisegnato una sola volta per fotogramma della
    // scena, non a ogni movimento del mouse. -2 chiede il fallback CPU.
    int gpuPickBody(const QPoint &position) const;

    // `exact` falso: i corpi si provano sulla tassellazione (evidenziazione al
    // passaggio, che deve restare fluida anche su modelli con molte facce).
    SceneSelection pickSceneObject(const QPoint &position, bool exact = true) const;

    bool pickVisibleSketchEntity(const QPoint &position, int &sketchIndex, int &kind, int &entityIndex) const;

    static void strokeHighlight(QPainter &painter, const QVector<QPointF> &points,
                                bool closed, const QColor &color);

    QVector<QPointF> projectSketchPolyline(const QVector<QPointF> &points, const SketchObject &sketch) const;

    void highlightSketchElement(QPainter &painter, const SketchObject &sketch,
                                const SketchElementSelection &element, const QColor &color) const;

    void highlightSceneObject(QPainter &painter, const SceneSelection &target, const QColor &color) const;

    void drawSelectionHighlight();

    int pickReferencePlane(const QPoint &position) const;

    bool isPlaneShown(int plane) const;

    static QVector<QVector3D> planeCorners(int plane, float u, float v);
    static QVector<QVector3D> planeCorners(int plane, float h);
    QSizeF referencePlaneExtents(int plane) const;
    double referencePlaneHalf(int plane) const;
    QVector<QVector3D> planeCorners(int plane) const;

    // Maniglie solo sul piano selezionato: il trascinamento non sposta il piano geometrico.
    bool resizablePlane(SceneSelection &target, QVector<QVector3D> &corners, QVector3D &center, double &half) const;
    int planeResizeHandleAt(const QPoint &position) const;
    bool beginPlaneResize(const QPoint &position);
    void finishPlaneResize(bool cancel);
    void drawPlaneResizeHandles();

    void drawReferencePlanes();

    // --- Piani di costruzione a video ---------------------------------------------

    // Mezza misura a video di un piano di costruzione: quella data o meta' dei piani di riferimento.
    double datumHalf(double size) const;
    QVector<QVector3D> datumCorners(const SketchFrame &f, double size) const;
    bool isDatumShown(int index) const;
    // Piano di costruzione sotto il puntatore: il bordo entro qualche pixel
    // (`border`) o l'interno lungo il raggio (`distance`); -1 se nessuno.
    int pickDatumPlane(const QPoint &position, bool border, double *distance = nullptr) const;
    void drawDatumQuad(const QVector<QVector3D> &corners, const GLfloat *color, float emphasis);
    // Come i piani di riferimento: trasparenti, dietro ai corpi, con il bordo schiarito.
    void drawDatumPlanes();
    void drawDatumLabels(QPainter &painter) const;

    // --- Scelta dei riferimenti -----------------------------------------------------

    // Corpi da cui si puo' prendere un riferimento: visibili, con una forma, prima del piano in modifica.
    bool referenceBodyEligible(int index) const;
    bool referenceSketchEligible(int index) const;
    bool curveBodyEndpoint(int index, int end, QVector3D &point, int entity=0) const;
    // Riferimento sotto il puntatore, con la priorita': punti, poi rette e curve, poi facce e piani.
    bool pickReference(const QPoint &position, GeometryRef &ref, FaceHit *faceHit = nullptr, int *faceBody = nullptr) const;
    // Il riferimento evidenziato (QPainter): punto, polilinea, bordo del piano o della faccia.
    void drawGeometryRef(QPainter &painter, const GeometryRef &ref, const QColor &color, const FaceHit *face = nullptr, int faceBody = -1) const;
    // Posizione a video dei punti medi e dei centri degli spigoli (misura).
    bool snapRefPosition(const GeometryRef &ref, QVector3D &position) const;
    // Punto a meta' lunghezza di una polilinea (solo per il display e il picking).
    static QVector3D polylineMidpoint(const QVector<QVector3D> &polyline);

    // La fusione con GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA mescola anche il
    // canale alfa del framebuffer, che scende sotto 1 dove si disegna qualcosa
    // di trasparente (i piani, le anteprime). Il compositore di Wayland (niri)
    // usa quell'alfa per la finestra e vi fa vedere attraverso il desktop: alla
    // fine della scena l'alfa torna 1 ovunque, senza toccare i colori.
    void makeOpaque();

    void configureLighting();

    // Illuminazione dall'ambiente: lo sfondo agisce come una sorgente emisferica.
    // Due luci direzionali in coordinate vista arrivano dai lati dello schermo
    // dove si trovano i due colori, e la luce ambiente assume il loro colore medio.
    void configureEnvironmentLighting(ForgeCad::DisplayCache::Lighting &lighting);

    bool isSketchDrawn(int index) const;

    void drawSketch();

    // Solo visualizzazione: triangoli e spigoli ricavati dalla forma esatta.
    void drawExtrusionFaces(const ExtrusionObject &extrusion);
    void drawDisplayFaces(const BodyDisplay &display);
    void drawExtrusionEdges(const ExtrusionObject &extrusion);
    // Su una filettatura ottenuta per sweep, le polilinee campionate sono i
    // veri bordi elicoidali; i segmenti di due punti sono soltanto le cuciture
    // trasversali tra domini B-spline. Mostrare i primi rende leggibile il
    // profilo senza reintrodurre la trama da tassellazione.
    void drawThreadProfileEdges(const BodyDisplay &display, const QVector4D &color);

    // Bordo di evidenziazione: la sagoma dell'oggetto va nello stencil, poi le
    // sue facce vengono ridisegnate a linee spesse solo fuori dalla sagoma.
    void drawExtrusionOutline(const ExtrusionObject &extrusion, const QColor &color, float width);

    QString edgePickMessage() const;
    // Un punto per ogni spigolo scelto: un campione interno della polilinea
    // (i campioni stanno sulla curva esatta), il punto medio se e' un segmento.
    // Una faccia i cui bordi sono ancora tutti scelti diventa un riferimento di
    // faccia (role = kEdgePointFaceBoundary) al posto dei suoi spigoli.
    QVector<EdgePoint> pickedEdgePoints() const;
    // Gli spigoli scelti sono cambiati: messaggio e anteprima.
    void edgePicked();
    void cancelEdgePick(bool keepPreview = false);
    void finishEdgePick();
    // Il corpo puo' dare gli spigoli della scelta: visibile (o quello gia' scelto),
    // un solido o una superficie cucita per raccordi/smussi, una superficie per l'estensione.
    bool edgePickEligible(int index) const;
    struct ProjectedEdges {
        BodyDisplay source;
        std::array<double, 18> view{};
        QVector<QVector<QPointF>> lines;
        QVector<QRectF> bounds;
        bool valid = false;
    };
    const ProjectedEdges &projectedBodyEdges(int index) const;
    static bool edgeOccludedByMesh(const BodyDisplay &display, const QVector3D &origin, const QVector3D &direction,
                                   const QVector3D &edgePoint, double tolerance);
    // Spigolo sotto il puntatore (entro 8 pixel), -1 se nessuno, e il suo corpo in
    // `body`: quello in scelta o, finche' non ci sono spigoli scelti, qualsiasi corpo adatto.
    int pickEdge(const QPoint &position, int *body = nullptr) const;
    // Corpo adatto sotto il puntatore per il clic su una faccia (quello in scelta, se c'e').
    int edgePickFaceBody(const QPoint &position) const;
    struct RaySelectionCache {
        ForgeCad::ForgeBody body;
        QVector3D origin, direction;
        FaceHit face;
        bool valid = false, hit = false;
    };
    bool cachedRayFace(int index, const QVector3D &origin, const QVector3D &direction, FaceHit &hit) const;
    // Faccia del corpo `index` sotto il pixel (geometria esatta).
    bool pickBodyFace(int index, const QPoint &position, FaceHit &hit) const;
    // Funzione che ha creato la faccia `hit` del corpo `index` sotto il pixel
    // `position` (la storia del corpo, dalla prima funzione all'ultima: vince
    // la prima che ha gia' quella faccia). Senza faccia: il corpo stesso.
    int faceOwnerFeature(int index, const QPoint &position, const FaceHit &hit) const;
    // --- Evidenziazione delle feature scelte (albero o clic) ----------------------
    // Le facce del corpo visibile create dalla feature (forgeFeatureFaces, sul
    // B-rep esatto) si ridisegnano in magenta con i loro bordi. Il calcolo e'
    // in un thread; vale solo l'ultima richiesta (`featureHighlightGeneration_`).
    QSet<int> operandClosure(int body) const;
    void clearFeatureHighlights();
    void setBodyHighlight(int body);
    void setFeatureHighlights(const QVector<int> &features);
    // Facce evidenziate del corpo `index` (dopo le sue facce, con la stessa profondita').
    void drawFeatureHighlightFaces(int index);
    void drawFeatureHighlightEdges(int index);
    // Spigoli visualizzati (indici in display.edges) dei bordi della faccia:
    // per ogni suo spigolo la polilinea piu' vicina al suo punto.
    // Riferimento di faccia (tutti i bordi) della faccia `face` del corpo `body`.
    EdgePoint faceBoundaryReference(int body, int face, const ForgeCad::Kernel::Vec3 &point) const;
    // Un punto interno della faccia (quello delle etichette, dalla tassellazione).
    bool faceLabelPoint(int body, int face, ForgeCad::Kernel::Vec3 &point) const;
    // Scelta degli spigoli ricostruita dai riferimenti salvati (modifica o
    // ripresa): spigoli e facce si ritrovano sul corpo `body` come nella
    // costruzione della feature, anche dopo un riordino della storia.
    void pickReferences(int body, const QVector<EdgePoint> &references, bool sameState = false);
    QVector<int> faceDisplayEdges(int index, const FaceHit &hit) const;
    // Bordi della faccia selezionata (fuori dalla scelta degli spigoli).
    void drawSelectedFace();
    void drawTopologyHighlight();
    void drawPickedEdges();

    // Raggio di selezione con la sezione: l'origine dalla parte tolta va sul
    // piano (i punti prima non si vedono); `skipped` e' il tratto saltato.
    // Falso se il raggio non arriva alla parte che resta.
    bool sectionRay(QVector3D &origin, const QVector3D &direction, double &skipped) const;
    // Il punto sta nella parte tolta dalla sezione.
    bool sectionRemoves(const QVector3D &point) const;
    // Quadrato sul piano di sezione che copre la scena.
    QVector<QVector3D> sectionQuad(float scale = 1.0f) const;
    void enableSectionClip();
    void disableSectionClip();
    // Chiusura delle sezioni dei solidi: per ogni solido la parita' delle sue
    // facce tagliate nello stencil (dispari = dentro il solido), poi il
    // quadrato del piano dove lo stencil e' 1, pieno e tratteggiato.
    void drawSectionCaps(const QVector<int> &solids);
    // Il piano di sezione: trasparente con il bordo.
    void drawSectionPlane();

    void drawExtrusions();

    void drawTrimPartHover();

    // Anteprime semitrasparenti con isoparametriche U/V.
    // Per estrusione/rivoluzione una passata di profondita' elimina la
    // sovrapposizione dei triangoli posteriori senza rendere opaca la superficie.
    void drawPreview();

    // Anteprima della mesh d'esportazione. Le facce trasparenti fanno capire
    // quali corpi sono inclusi; la griglia mostra la topologia effettiva del
    // file (triangoli STL oppure quad/triangoli OBJ).
    void drawExportMeshPreview();

    // Chiave di una richiesta d'anteprima: due richieste uguali non si rifanno.
    static QString previewKey(const ExtrusionObject &d, int index);

    // Dati di un'anteprima, copiati nel thread dell'interfaccia: il thread del
    // pool non tocca il documento (i body del kernel sono immutabili e
    // condivisi, gli schizzi sono copie).
    struct PreviewInputs {
        ExtrusionObject definition;
        int index = 0;  // posizione del corpo (gli operandi hanno indice minore)
        QVector<SketchObject> sketches;
        QVector<ExtrusionObject> bodies;  // solo gli operandi, copiati
        int quality = 1;
        // Modifica senza valori cambiati (unchangedFeature): il body e la
        // tassellazione del corpo, niente kernel; si prepara solo la vista
        // (la patch del raccordo, le curve U/V).
        bool reuse = false;
        ForgeCad::ForgeBody existing;
        BodyDisplay existingDisplay;
        QVector<int> existingMerge;
    };
    // La geometria dell'anteprima, come rebuildBody, e la sua tassellazione.
    static bool computePreview(const PreviewInputs &in, BodyDisplay &display, BodyDisplay &retainedDisplay,
                               BodyDisplay &resultDisplay, ForgeCad::ForgeBody &geometry,
                               QString &error, QVector<int> *merged = nullptr, QString *notice = nullptr);

    // Calcolo dell'anteprima in un thread del pool. Il risultato arriva nel
    // thread dell'interfaccia e vale solo se nel frattempo non e' arrivata
    // un'altra richiesta.
    void startPreviewJob();

    QString preparePreviewError(QString error);

    // Punti della geometria di un schizzo (estremi dei segmenti, campioni
    // delle curve) nello spazio del modello: solo per inquadrare la vista.
    QVector<QVector3D> sketchGeometryPoints(const SketchObject &sketch) const;

    // Geometria visibile della scena (schizzi, spigoli o triangoli dei corpi);
    // se non c'e' nulla, i piani di riferimento.
    QVector<QVector3D> sceneGeometryPoints(bool planesIfEmpty = true, bool datumSizes = true) const;

    // Box della geometria visibile e mezzo lato comune dei piani di riferimento:
    // il piano che richiede la portata maggiore determina un quadrato applicato
    // a XY, XZ e YZ (piu' il 20%, arrotondato a 1, 2, 2.5 o 5 per 10^k).
    void updateSceneBounds() const;
    // Il piu' piccolo 1, 2, 2.5 o 5 per una potenza di 10 non minore di x (> 0).
    static float niceCeiling(float x);
    // Mezzo lato dei piani di riferimento (segue la dimensione della scena).
    float planeHalf() const;

    // Mezza profondita' del volume di vista: tutta la geometria (e i piani)
    // resta tra i piani di taglio anche nelle scene grandi.
    double sceneDepth() const;

    // Zoom che inquadra la sfera attorno al box della scena; i limiti sono
    // 4 volte questo (allontanandosi) e 1/5000 (avvicinandosi).
    void zoomLimits(float &minimum, float &maximum) const;

    void setZoom(float zoom);

    // Inquadra i punti nella vista corrente (orientamento invariato): pan sul
    // centro del loro box in coordinate vista e zoom con un margine del 10%.
    void fitView(const QVector<QVector3D> &points);

    // Framebuffer multisample della scena (nullptr: antialiasing spento o non disponibile).
    QOpenGLFramebufferObject *sceneBuffer(const QSize &size);

    void initializeGlassShader();

    void drawGpuGlassPanels(const QSize &pixels, QOpenGLFramebufferObject *resolvedScene);


    static const QColor &axisColor(int axis);

    // Assi cartesiani dall'origine: una linea per asse e una freccia conica
    // in punta (sotto gli oggetti, senza test di profondita').
    void drawAxes();

    void drawGrid();

    int displayMode_ = 2;
    bool hiddenEdgesVisible_ = false;  // disegno e selezione X-ray degli spigoli occultati
    bool topologyIdsVisible_ = false;  // etichette B/F/E per l'ispezione del B-rep
    int topologyHighlightBody_ = -1;
    int topologyHighlightEdge_ = -1;  // EdgeId evidenziato cliccando l'etichetta
    int topologyHighlightFace_ = -1;  // FaceId: se cliccato se ne illuminano tutti i bordi
    ForgeCad::Gui::ViewportCamera camera_;
    ForgeCad::DocumentVersion documentVersion_{QUuid::createUuid(), {}};
    QUuid documentBaseCheckinId_;
    int panKey_ = Qt::Key_Space;
    bool panKeyHeld_ = false, panning_ = false;
    QPoint lastMousePosition_;
    QPoint selectionPress_;
    QPoint hoverPosition_;
    int selectedFeature_ = -1;  // funzione che ha creato la parte cliccata (vedi faceOwnerFeature)
    struct FeatureHighlight {
        int feature = -1;
        int body = -1;          // corpo visibile su cui si disegna
        BodyDisplay display;    // le sue facce create dalla feature, con i bordi
    };
    QVector<FeatureHighlight> featureHighlights_;
    QColor featureHighlightColor_ = ForgeCad::Gui::kSelectionColor;  // view/featureHighlightColor
    quint64 featureHighlightGeneration_ = 0;
    int bodyHighlight_ = -1;  // corpo scelto (vista o voce "Corpi"): evidenziato tutto
    std::function<void(const QVector<SceneSelection> &)> multiSelectionCallback_;
    bool hoverScheduled_ = false;
    bool selectionPending_ = false, selectionDragged_ = false, releasingSelection_ = false;
    int lightingPreset_ = 0, constraintMode_ = 0, selectedPlane_ = 0, activePlane_ = 0, activeSketch_ = -1;
    double lineLength_ = 0.0;
    double lineAngle_ = -1.0;
    int polygonSides_ = 6;
    int tessellationQuality_ = 1;
    QSet<int> deferredSketches_;
    int circleRadiusDrag_=-1;
    double circlePointerRadius_=0,circleInitialRadius_=0;
    bool sketchMode_ = false, sketchCameraLocked_ = false, geometrySnap_ = true, gridSnap_ = true;
    bool wheelZoomEnabled_ = true;
    bool hasPendingPoint_ = false, referencePlanesVisible_ = true;
    DrawingTool drawingTool_ = DrawingTool::Select;
    ForgeCad::SplineShapeOptions splineShapeOptions_{true, false, false};
    SnapKind lastSnapKind_ = SnapKind::None;
    QString lastSnapNote_;   // etichetta particolare dell'aggancio (origine, quadrante, su curva)
    int lastSnapCurve_ = -1; // curva su cui si e' agganciato il punto (aggancio "su curva")
    const double snapSpacing_ = 0.25;
    QPointF pendingPoint_, cursorSketchPoint_, lastSnapPoint_;
    LineInference currentInference_;
    int startReference_ = -1;  // segmento su cui parte il segmento in costruzione
    QVector<QPointF> curveControlPoints_;
    QVector<TangentStart> tangentStarts_;  // arco tangente: le entita' da cui puo' partire
    bool draggingControlPoint_ = false;
    int draggingCurveIndex_ = -1;
    int draggingControlIndex_ = -1;
    EditablePointKind draggingPointKind_ = EditablePointKind::Control;
    SceneSelection selection_, hover_;
    SketchElementSelection sketchHover_;
    QVector<SketchElementSelection> sketchSelections_;
    QVector<SelectedPoint> selectedPoints_;
    QVector<int> selectedConstraints_;  // vincoli selezionati (indici in geometricConstraints dello schizzo attivo)
    int constraintHover_ = -1;
    int dimensionDrag_ = -1;  // quota che si sta spostando
    int dimensionPlacing_ = -1;  // quota dall'asse appena creata che segue il puntatore
    // Strumento Quota: orientamento scelto (0 automatico, 1 allineata, 2 orizzontale,
    // 3 verticale) e quota in anteprima per le entita' scelte, che segue il puntatore.
    int dimensionOrientation_ = 0;
    SketchConstraint dimensionPreview_;
    bool dimensionPreviewValid_ = false;
    ConstraintRef dimensionHover_;
    // Durante estrusione e rivoluzione dallo schizzo la vista si puo' ruotare
    // (camera dello schizzo salvata, ripresa alla fine).
    bool sketchViewUnlocked_ = false;
    struct SavedCamera {
        float yaw = 0.0f, pitch = 0.0f, roll = 0.0f, zoom = 8.0f, panX = 0.0f, panY = 0.0f;
        bool rotated = false;
    } sketchCamera_;
    // Vista dello schizzo ruotata dall'utente (tasto destro trascinato): i punti
    // del puntatore vanno sul piano lungo il raggio (screenToSketchPoint).
    bool sketchViewRotated_ = false;
    QPoint rightPressPosition_;
    bool rightDragged_ = false, contextPending_ = false;
    // Selezione a riquadro: nello schizzo trascinando dal vuoto con la Selezione
    // (le entita' dello schizzo), fuori con Maiusc+trascinamento (schizzi e corpi).
    // Da sinistra a destra valgono gli oggetti tutti dentro, da destra a sinistra
    // anche quelli toccati. selectedObjects_: gli oggetti scelti (piu' di uno).
    bool boxArmed_ = false, boxSelecting_ = false, boxAdditive_ = false;
    QPoint boxStart_, boxEnd_;
    QVector<SceneSelection> selectedObjects_;
    // Opacita' dei corpi in modalita' schizzo (1 = opachi): lo schizzo resta leggibile dietro di loro.
    double sketchBodyOpacity_ = 0.35;
    mutable ForgeCad::SketchAnalysis analysis_;
    mutable bool analysisDirty_ = true;
    mutable int analysisSketch_ = -1;
    bool constraintsVisible_ = true;
    std::function<void()> constraintPanelCallback_;
    QVector<SketchObject> sketches_;
    int historyPosition_ = -1;
    QSet<quint64> consumedBodies_; // corpi nascosti dal filtro della scena, preferenza conservata
    int insertionSourceSize_ = -1;
    QVector<ExtrusionObject> extrusions_;
    QVector<ModelBody> modelBodies_;
    ForgeCad::History history_;
    DocumentState dragSnapshot_;
    bool dragRecorded_ = false;
    int edgePickBody_ = -1, hoverEdge_ = -1, hoverEdgeBody_ = -1;
    bool edgePicking_ = false;  // scelta degli spigoli in corso (edgePickBody_ < 0: corpo non ancora deciso)
    bool edgePickChamfer_ = false;
    bool edgePickExtend_ = false, edgePickLinear_ = false;
    bool edgePickHelix_ = false;  // scelta della base di un'elica (un clic su uno spigolo o una faccia)
    bool edgePickPreviewEnabled_ = true;  // falso finche' il pannello del raccordo non e' apparso
    // Scelta dei riferimenti dei piani di costruzione (beginReferencePick).
    std::function<void(SpatialPoint)> spatialAdd_;
    std::function<void()> spatialFinish_,spatialUndo_,spatialRedo_;
    int spatialOwner_=-1,spatialPlane_=0;
    double spatialDepth_=0;
    SpatialPoint spatialAnchor_,spatialHover_;
    QVector<SpatialPoint> spatialNodes_;
    bool spatialSnap_=true,spatialPressed_=false,spatialDragged_=false,spatialHoverValid_=false;
    QPoint spatialPress_;
    bool refPicking_ = false;
    QVector<int> pickBodies_;
    int refPickRoles_ = 0;
    int refPickOwner_ = -1;
    GeometryRef refHover_;
    bool refHoverValid_ = false;
    FaceHit refHoverFace_;  // faccia sotto il puntatore (per evidenziarne i bordi)
    int refHoverFaceBody_ = -1;
    QVector<GeometryRef> refMarks_;
    QPointer<QObject> referenceListOwner_;
    QVector<GeometryRef> referenceListMarks_;
    bool measureValid_ = false;  // linea della misura (finestra Misura)
    QVector3D measureFrom_, measureTo_;
    QString measureLabel_;
    std::function<void(bool, GeometryRef)> refPickFinished_;
    bool interactionLocked_ = false;
    bool documentReadOnly_ = false;
    // Anteprima del piano di costruzione della finestra.
    SketchFrame datumPreviewFrame_;
    bool datumPreviewValid_ = false;
    double datumPreviewSize_ = 0.0;
    int datumPreviewIndex_ = -1;
    std::function<void(int, int, EdgePoint)> helixPickFinished_;
    ChamferSpec edgePickSpec_;  // smusso: modo delle distanze per l'anteprima durante la scelta  // scelta dei bordi di una superficie da estendere
    std::function<void(int, QVector<EdgePoint>)> extendPickFinished_;
    QVector<int> pickedEdges_, hoverFaceEdges_;
    // Facce scelte per tutti i loro bordi (raccordi e smussi): finche' i bordi
    // restano tutti scelti, la feature salva la faccia e non gli spigoli.
    struct PickedFace {
        int face = -1;
        EdgePoint reference;
        QVector<int> border;
    };
    QVector<PickedFace> pickedFaces_;
    int edgePickEdit_ = -1;         // raccordo di cui si modificano gli spigoli (-1: raccordo nuovo)
    double edgePickSize_ = 0.5;     // misura dell'anteprima durante la scelta
    std::function<void(int, QVector<EdgePoint>, double, bool)> edgeEditFinished_;
    std::function<void(int, QVector<EdgePoint>)> edgePickChanged_; // pannello aperto: selezione aggiornata nella vista
    std::function<void()> edgePickPanelAccept_, edgePickPanelCancel_;
    // Anteprima di una funzione (vedi requestPreview).
    struct Preview {
        QString key;  // vuota: nessuna anteprima
        ExtrusionObject definition;
        int index = -1;           // corpo che l'anteprima sostituisce (modifica), -1 nuovo
        QVector<int> replaced;    // operandi (booleane) o base (raccordi): al loro posto l'anteprima
        // Modifica di una funzione intermedia: la storia torna a quel punto.
        // Le funzioni che ne dipendono (anche il risultato finale del corpo)
        // non si disegnano, i corpi che solo loro consumavano si' di nuovo.
        QSet<int> later, restored;
        bool valid = false;
        QString error;
        QString notice;  // esito non bloccante (ExtrusionObject::notice)
        ForgeCad::ForgeBody geometry;
        BodyDisplay display;        // cio' che si disegna (per un raccordo, solo la patch)
        BodyDisplay retainedDisplay; // sottrazione: facce conservate del risultato, opache e gia' tagliate
        BodyDisplay resultDisplay;  // corpo esistente; per una nuova patch la mesh completa nasce con OK
        quint64 generation = 0;
    };
    Preview preview_;
    std::vector<Preview> previewCache_;
    QVector<SketchObject> previewCacheSketches_;
    QVector<ExtrusionObject> previewCacheBodies_;
    int previewCacheQuality_=-1;
    bool previewCacheContextValid_=false;
    BodyDisplay previewDragDisplay_;
    std::shared_ptr<const BodyDisplay> previewDragSource_;
    ExtrusionObject previewDragDefinition_;
    QString previewDragKey_;
    bool previewDragVisible_ = false;
    BodyDisplay exportMeshPreview_;
    bool exportMeshPreviewQuadrangular_ = false;
    bool exportMeshPreviewLimited_ = false;
    LengthUnit lengthUnit_ = LengthUnit::Millimeter;
    int previewErrorSketch_ = -1;  // sezione della loft evidenziata quando una guida non la incontra
    QTimer *previewTimer_ = nullptr;
    ForgeCad::Kernel::CancellationToken previewCancellation_;
    bool previewRunning_ = false, previewRerun_ = false;
    bool previewHandlePending_ = false;
    ExtrusionObject previewHandleDefinition_;
    int previewHandleIndex_ = -1;
    std::function<void(const QString &)> previewCallback_;
    // Faccia selezionata con il clic su un corpo (schizzo su faccia, bordi da raccordare).
    struct SelectedFace {
        int body = -1;
        FaceHit hit;
    };
    SelectedFace selectedFace_;
    std::function<void(const QString &)> edgePickStatus_;
    std::function<void(int)> editBodyCallback_;
    std::function<void(int, QVector<EdgePoint>, bool)> edgePickFinished_;
    bool planeVisible_[3] = {true, true, true};
    BackgroundSettings background_;
    std::function<void()> sketchOffsetSelectionCallback_;
    std::optional<SketchObject> sketchOffsetOriginal_;
    QVector<SketchElementSelection> sketchOffsetOriginalSelection_;
    bool sketchOffsetViewUnlocked_ = false;
    std::function<void(SceneSelection)> selectionCallback_;
    std::function<void(int)> sketchPickCallback_;
    std::function<void(int, int, int)> sketchEntityPickCallback_;
    QVector<int> trimPartPickBodies_;
    QVector<BodyDisplay> trimPartPickDisplays_;
    QVector<int> trimPartPickSides_, trimPartPickIndices_;
    int trimPartHover_ = -1;
    std::function<void(int, int, EdgePoint)> trimPartPickFinished_;
    std::function<void()> documentChangedCallback_;
    std::function<void(int)> planeContextCallback_;
    std::function<void(bool)> sketchModeCallback_;
    std::function<void(DrawingTool)> toolChangedCallback_;
    std::function<void(const QString &)> rendererCallback_;
    std::function<void(bool, const QString &, bool)> workCallback_;
    int responsiveWork_ = 0;  // calcoli in corso in runWhileResponsive
    // Avanzamento che si mostra solo dopo kDeferredWorkDelayMs di calcolo
    // (rigenerazione dei corpi): ScopedWork lo mostrerebbe subito.
    static constexpr int kDeferredWorkDelayMs = 200;
    struct DeferredWork {
        DeferredWork(CadViewport *viewport, const QString &text) : owner(viewport), message(text), outer(viewport->deferredWork_) {
            owner->deferredWork_ = this;
        }
        ~DeferredWork() {
            owner->deferredWork_ = outer;
            if (shown && owner->workCallback_) owner->workCallback_(false, message, false);
        }
        DeferredWork(const DeferredWork &) = delete;
        DeferredWork &operator=(const DeferredWork &) = delete;
        CadViewport *owner;
        QString message;
        DeferredWork *outer;
        bool shown = false;
    };
    DeferredWork *deferredWork_ = nullptr;
    // Strumenti di modifica dello schizzo: misure di raccordo e smusso, primo
    // segmento scelto (se non si e' cliccato uno spigolo) e tratto che il taglio toglierebbe.
    double sketchFilletRadius_ = 1.0, sketchChamferDistance_ = 1.0;
    int blendFirst_ = -1;
    QPointF blendFirstPick_;
    QVector<QPointF> trimPreview_;
    QVector<QVector<QPointF>> sketchPatternPreview_;  // copie della ripetizione in anteprima
    std::function<void(const QString &)> statusCallback_;
    // Antialiasing: campioni MSAA (0 = spento). La scena OpenGL si disegna in
    // un framebuffer multisample, risolto in uno normale e copiato nel widget.
    int antialiasing_ = 4, maxSamples_ = 0, bufferSamples_ = 0;
    std::unique_ptr<QOpenGLFramebufferObject> msaaBuffer_, resolveBuffer_;
    ForgeCad::Gui::GlassCompositor glassCompositor_;
    bool gridVisible_ = true;
    double referencePlaneScales_[3] = {1.0, 1.0, 1.0};
    mutable QSizeF referencePlaneAuto_[3] = {
        QSizeF(kDefaultPlaneHalf, kDefaultPlaneHalf),
        QSizeF(kDefaultPlaneHalf, kDefaultPlaneHalf),
        QSizeF(kDefaultPlaneHalf, kDefaultPlaneHalf)};
    bool planeResizing_ = false;
    SceneSelection planeResizeTarget_;
    QPoint planeResizeStart_;
    QPointF planeResizeDirection_;
    double planeResizeHalf_ = 0.0, planeResizeOriginal_ = 0.0;
    DocumentState planeResizeSnapshot_;
    double axisLength_ = kDefaultAxisLength;
    mutable ForgeCad::DisplayCache displayCache_;
    ForgeCad::OverlayRenderer overlayRenderer_;
    mutable std::unique_ptr<QOpenGLFramebufferObject> pickingBuffer_;
    mutable quint64 pickingBufferSerial_ = std::numeric_limits<quint64>::max();
    mutable QVector<RaySelectionCache> raySelectionCache_;
    std::vector<std::weak_ptr<const ForgeCad::Kernel::Body>> warmedBodies_;  // B-rep con le cache di selezione gia' preparate
    mutable QVector<ProjectedEdges> projectedEdges_;
    bool axesVisible_ = true, axesOnTop_ = true;  // assi mostrati; sopra solidi, superfici e schizzi
    bool originSnap_ = true;                      // lo schizzo si aggancia all'origine del piano
    // Box della geometria visibile (coordinate del modello), per zoom e profondita' della vista.
    mutable QVector3D sceneMin_, sceneMax_;
    struct SectionState {
        bool enabled = false;
        QVector3D point, normal{0.0f, 0.0f, 1.0f};
        bool caps = true, showPlane = true;
    };
    SectionState section_;
    SketchReferenceHit convertHover_;
    mutable std::map<const void *, std::pair<std::weak_ptr<const ForgeCad::Kernel::Body>, QPair<int, int>>> componentCache_;
    bool referencesConstruction_ = true;
    std::function<void()> sectionRefresh_;
    ExtrusionObject featureHandleDefinition_;
    std::function<void(double)> featureHandleChanged_;
    ForgeCad::ExpressionSpinBox *featureHandleBox_ = nullptr;
    QPolygonF featureHandlePath_;
    QVector3D featureHandleOrigin_, featureHandleAxis_, featureHandleRadial_;
    float featureHandleRadius_ = 1;
    bool featureHandleDragging_ = false, featureHandleMouseAngleValid_ = false;
    double featureHandleDragValue_ = 0, featureHandleMouseAngle_ = 0;
    QPointF featureHandleLastMouse_;
    std::function<void(double)> sectionDragged_;
    bool sectionDragging_ = false, sectionHover_ = false;
    mutable bool sceneBoundsDirty_ = true;
    static constexpr float kDefaultPlaneHalf = 4.0f;  // mezzo lato dei piani senza geometria
    mutable float planeHalf_ = kDefaultPlaneHalf;
    bool painted_ = false;
    quint64 renderedFrameSerial_ = 0;
    quint64 panelBackdropSerial_ = std::numeric_limits<quint64>::max();
    QImage panelBackdropCache_;
    // Trascinamento con lo strumento Selezione: un punto (estremo di segmento
    // con i punti coincidenti) o un segmento intero.
    bool pointDragActive_ = false, bodyDragMoved_ = false;
    QPointF pointDragPosition_, bodyDragLast_;
    int bodyDragSegment_ = -1;
    QVector<QVector3D> pendingFit_;
};

#endif
