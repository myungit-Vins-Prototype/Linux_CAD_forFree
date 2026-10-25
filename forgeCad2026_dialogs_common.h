#ifndef FORGECAD2026_DIALOGS_COMMON_H
#define FORGECAD2026_DIALOGS_COMMON_H

// Componenti interni condivisi da pannelli, anteprime e selezione riferimenti.
#include "forgeCad2026_viewport.h"
#include <QtWidgets>
#include <functional>
#include <memory>
#include <optional>
#include <limits>

// Anteprima del documento selezionato nella finestra Apri. Il file viene
// decodificato su QThreadPool; soltanto l'ultimo risultato selezionato arriva
// al piccolo viewport, senza toccare il documento della finestra principale.
class DocumentFilePreview final : public QFrame {
public:
    explicit DocumentFilePreview(QWidget *parent = nullptr);

    void setPath(const QString &path);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Loaded {
        DocumentState state;
        QString error;
        QString details;
        int cachedBodies = 0;
        bool hasSketches = false;
    };
    void clearPreview(const QString &message);
    void startLoading();

    CadViewport *viewport_ = nullptr;
    QLabel *message_ = nullptr;
    QLabel *details_ = nullptr;
    QProgressBar *progress_ = nullptr;
    QTimer timer_;
    QString currentPath_;
    quint64 generation_ = 0;
};

// Aspetto comune delle finestre delle funzioni. Il fondale arriva direttamente
// dal framebuffer del viewport CAD, quindi non dipende da cio' che Wayland
// considera dietro la finestra e segue ogni nuovo frame della scena.
struct WindowLock {
    QList<QPointer<QWidget>> widgets;
    QList<QPointer<QAction>> actions;
    CadViewport *viewport = nullptr;
    bool previousLock = false;
    WindowLock(QMainWindow *window, CadViewport *target, QWidget *keep);
    ~WindowLock();
};

// Il messaggio ha un riquadro limitato: righe lunghe e molti dettagli non
// aumentano le dimensioni del pannello della funzione.
class FunctionMessage final : public QPlainTextEdit {
public:
    explicit FunctionMessage(QWidget *parent = nullptr);
    FunctionMessage(const QString &text, QWidget *parent = nullptr);
    void setText(const QString &text);
    QString text() const { return toPlainText(); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
};

class FunctionDialogPanel : public QDialog {
public:
    explicit FunctionDialogPanel(QWidget *parent = nullptr, bool exclusive = true);
    ~FunctionDialogPanel() override;
    // FloatingPanel ha una barra del titolo propria.
    void setHeaderVisible(bool visible);
    QFormLayout *createScrollableForm();
    // Identità semantica indipendente dal titolo e condivisa fra creazione/modifica.
    void setSizeIdentity(const QString &identity, const QStringList &legacyNames = {});
    QString panelSizeSettingsKey() const;
    QColor panelColor() const;
    int panelOpacity() const;
    int backdropBlur() const;
    int cornerRadius() const;
    bool isEmbedded() const;
    void beginEmbeddedMove(const QPoint &globalPosition);
    void updateEmbeddedMove(const QPoint &globalPosition);
    void endEmbeddedMove();
    void placeAtLeft();
    void setPanelOpacity(int percent);
    void setBackdropBlur(int pixels);
    void setCornerRadius(int pixels);
    void reloadAppearance();
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QString sizeIdentity_;
    QStringList legacySizeNames_;
    QSize preferredPanelSize_;
    bool exclusive_ = true;
    std::unique_ptr<WindowLock> windowLock_;
    static QWidget *overlayParent(QWidget *requested);
    void keepInsideViewport();
    void updateRoundedMask();
    static QColor readableText(const QColor &background);
    void applyTransparentStyle();
    QSize naturalPanelSize();
    QSize maximumPanelSize() const;
    void initializePanelSize();
    void storeManualSize();
    void focusFirstEditor();
    void locateViewport();
    void scheduleBackdropCapture(int delay);
    void syncGpuGlass();
    static QImage boxBlurPass(const QImage &source, int radius, bool horizontal);
    static QImage blurScene(QImage image, int radius);
    void captureSceneBackdrop();
    void updatePalette();
    bool updatingPalette_ = false;
    QColor panelColor_;
    int opacity_ = 88;
    int blur_ = 12;
    int cornerRadius_ = 10;
    QPointer<CadViewport> viewport_;
    QImage backdrop_;
    QTimer captureTimer_;
    quint64 capturedFrameSerial_ = std::numeric_limits<quint64>::max();
    QFormLayout *form_ = nullptr;
    QSizeGrip *resizeGrip_ = nullptr;
    static constexpr int kHeaderHeight = 30;
    QToolButton *headerClose_ = nullptr;
    bool headerVisible_ = true;
    QPoint dragOffset_;
    QPoint resizeStartGlobal_;
    QSize resizeStartSize_;
    bool embedded_ = false, dragging_ = false, resizing_ = false, placed_ = false, panelSizeInitialized_ = false;
};

class FeatureOperationDiagram final : public QWidget {
public:
    enum Kind { Extrusion, Revolution, Sweep };
    explicit FeatureOperationDiagram(Kind kind, QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    Kind kind_;
};

// Anteprima dal vivo in una finestra di funzione: una riga "Anteprima" con lo
// stato, `request` a ogni cambio dei valori; alla chiusura l'anteprima sparisce.
struct PreviewScope {
    CadViewport *viewport = nullptr;
    FunctionMessage *label = nullptr;
    int index = -1;  // corpo che l'anteprima sostituisce (modifica), -1 nuovo
    PreviewScope(CadViewport *target, QDialog &dialog, QFormLayout *form, int replacedIndex, QBoxLayout *outside = nullptr);
    void request(const ExtrusionObject &definition) const;
    ~PreviewScope();
    PreviewScope(const PreviewScope &) = delete;
    PreviewScope &operator=(const PreviewScope &) = delete;
};

// Dove e come mostrare l'anteprima di una finestra di funzione: il viewport
// (nullptr: niente anteprima), il corpo sostituito e la definizione dai valori.
template <class... Values>
struct PreviewSpec {
    CadViewport *viewport = nullptr;
    int index = -1;
    std::function<ExtrusionObject(Values...)> define;
};

class GeometryReferenceList final : public QListWidget {
public:
    GeometryReferenceList(QDialog &dialog, CadViewport *viewport, const QString &name);
    void setReferences(const QVector<GeometryRef> &refs);
    void setEdges(int body, const QVector<EdgePoint> &edges);
protected:
    void changeEvent(QEvent *event) override;
private:
    static bool sameReference(const GeometryRef &a, const GeometryRef &b);
    CadViewport *viewport_;
    QVector<GeometryRef> refs_;
};

// Scelta dei corpi nella vista per le finestre con un elenco di corpi: come per
// i raccordi, il corpo lo da' la faccia cliccata. Con un elenco a tendina il
// clic sceglie il corpo e la scelta finisce; con un elenco a spunte il clic
// accende o spegne il corpo e la scelta continua (Esc nella vista la chiude).
// `start(true)` (funzione nuova senza un corpo gia' selezionato) apre la
// finestra in attesa del clic: `waiting()` finche' non si sceglie un corpo,
// nella vista o nell'elenco, e intanto l'anteprima aspetta. Le finestre che
// scelgono anche altri riferimenti nella vista passano prima i clic a
// `handle`; le altre usano `install`. `changed` aggiorna la finestra.
class BodyPicker {
public:
    BodyPicker(QDialog &dialog, CadViewport *viewport, int replaced, const QVector<int> &candidates, QComboBox *combo,
               QListWidget *list = nullptr);
    ~BodyPicker();
    BodyPicker(const BodyPicker &) = delete;
    BodyPicker &operator=(const BodyPicker &) = delete;

    // L'elenco con il pulsante accanto.
    QWidget *row(QWidget *field) const;
    bool active() const;
    bool waiting() const;
    // L'elenco contiene anche gli schizzi da `start` in poi (nell'ordine del
    // documento): un clic su un'entita' di uno schizzo sceglie lo schizzo.
    void acceptSketches(int start);
    // Dentro `handle`: il cambio dell'elenco viene dal clic, la finestra si
    // aggiorna una volta sola alla fine (`changed`).
    bool handling() const;
    QString hint() const;
    void start(bool waiting = false);
    void stop();
    // Il clic nella vista: vero se riguardava la scelta del corpo.
    bool handle(bool picked, const GeometryRef &ref);
    // Per le finestre che non scelgono altro nella vista.
    void install();

    std::function<void()> changed, starting;
    std::function<void(const QString &, bool)> message;

private:
    CadViewport *viewport_;
    int replaced_;
    QVector<int> candidates_;
    QComboBox *combo_;
    QListWidget *list_;
    QPushButton *button_ = nullptr;
    bool active_ = false, waiting_ = false, installed_ = false, handling_ = false;
    int sketchStart_ = -1;
};

class LoftSelectionDiagram final : public QWidget {
public:
    explicit LoftSelectionDiagram(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *) override;
};

// Loft: sezioni ordinate, guide trasversali e condizioni delle estremita',
// con anteprima della stessa definizione che viene salvata nel documento.
// Controlli condivisi dai loft e dalle superfici tra due profili.
class SurfaceEndControls {
public:
    SurfaceEndControls(QDialog &dialog, QFormLayout &form, CadViewport *viewport, int owner, ExtrusionObject &definition);
    ~SurfaceEndControls();
    std::function<void()> changed;
    void stop();
    void copyTo(ExtrusionObject &d) const;
private:
    QVector<GeometryRef> &refs(int side);
    void sync();
    void start(int side);
    CadViewport *viewport_;
    int owner_, active_ = -1;
    ExtrusionObject &definition_;
    QComboBox *modes_[2]{};
    QDoubleSpinBox *influences_[2]{};
    QLabel *faces_[2]{};
    FunctionMessage *status_ = nullptr;
    QPushButton *picks_[2]{};
};

// Estrusione o rivoluzione dallo schizzo: finche' la finestra e' aperta la
// vista si puo' ruotare (per vedere il verso), poi torna quella dello schizzo.
struct SketchViewUnlock {
    CadViewport *viewport;
    explicit SketchViewUnlock(CadViewport *target);
    ~SketchViewUnlock();
    SketchViewUnlock(const SketchViewUnlock &) = delete;
    SketchViewUnlock &operator=(const SketchViewUnlock &) = delete;
};

class ThreadDiagram final : public QWidget {
public:
    enum Phase { Surface, Profile, Extent };
    explicit ThreadDiagram(Phase phase, QWidget *parent = nullptr);
    void setValues(bool internal, double angle, double pitch, double length, bool leftHanded);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    Phase phase_;
    bool internal_ = false, leftHanded_ = false;
    double angle_ = 60.0, pitch_ = 1.5, length_ = 10.0;
};

// Scelta nella vista di catene di curve (spigoli dei corpi, entita' degli
// schizzi, eliche) per le superfici rigata e planare: ogni clic aggiunge o
// toglie un tratto della lista attiva; Esc nella vista termina la scelta.
// Valgono solo i riferimenti che danno tratti esatti limitati (geometryRefPath).
struct CurvePicker {
    CadViewport *viewport = nullptr;
    int owner = -1;
    QVector<GeometryRef> *active = nullptr;  // lista in scelta (nullptr: nessuna)
    std::function<void(const QString &, bool)> status;
    std::function<void()> changed;
    QCheckBox *wholeLoop = nullptr;  // acceso: un bordo libero di una superficie porta con se' tutto il suo loop
    QVector<GeometryRef> *faceList = nullptr;  // lista di facce (riferimenti kind 5) invece che di curve

    // Stessa faccia dello stesso corpo.
    bool sameFace(const GeometryRef &a, const GeometryRef &b) const;

    // Il loop di bordo libero dello spigolo `ref` (kind 4) di una lamina, con
    // almeno due tratti; vuoto altrimenti.
    QVector<GeometryRef> boundaryLoop(const GeometryRef &ref) const;

    QString text(const QVector<GeometryRef> &refs) const;
    void start(QVector<GeometryRef> *list);
    void stop();
    void install();
};

// Pannello fluttuante dentro la finestra principale (la finestra dei vincoli).
// Su Wayland un'applicazione non puo' scegliere la posizione delle sue
// finestre: il compositor (niri) apre ogni finestra fluttuante al centro. Il
// pannello invece e' un figlio della finestra principale, si trascina dalla
// barra del titolo e si ridimensiona dall'angolo in basso a destra;
// posizione e dimensione restano in QSettings (`settingsKey`) e si
// riprendono alla riapertura, anche dopo il riavvio. Resta dentro la finestra
// quando questa cambia dimensione.
class FloatingPanel final : public FunctionDialogPanel {
public:
    FloatingPanel(QWidget *window, const QString &title, const QString &settingsKey, const QSize &defaultSize);
    QWidget *content() const;

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void keepInside();
    void store();

    QString settingsKey_;
    QSize defaultSize_;
    QWidget *header_ = nullptr, *content_ = nullptr;
    bool draggingHeader_ = false, placed_ = false;
};

// In una lista: il tasto Canc va alla lista (elimina gli elementi scelti) e
// non alla scorciatoia Canc della finestra principale.
class DeleteKeyFilter final : public QObject {
public:
    DeleteKeyFilter(QObject *parent, std::function<void()> action);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    std::function<void()> action_;
};

// Finestre di dialogo spostabili trascinandone lo sfondo (o un'etichetta):
// su Wayland i compositor che non disegnano le barre del titolo (niri, e
// altri con "prefer-no-csd") le lasciano senza un punto da cui trascinarle, e
// un'applicazione non puo' spostare da se' una sua finestra; puo' pero'
// chiedere al compositor di iniziare lo spostamento (startSystemMove).
class DialogMover final : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPointer<FunctionDialogPanel> embeddedPanel_;
};

#endif
