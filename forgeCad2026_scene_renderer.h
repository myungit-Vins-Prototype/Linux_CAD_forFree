#ifndef FORGECAD2026_SCENE_RENDERER_H
#define FORGECAD2026_SCENE_RENDERER_H

#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QSize>
#include <QColor>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <array>
#include <functional>
#include <memory>

struct BodyDisplay;
struct BackgroundSettings;
struct AxesOrientation;
namespace ForgeCad {
class DisplayCache;
class OverlayRenderer;
struct OverlayVertex;
}
class ViewportInteractionTest;
namespace ForgeCad::Gui {
// Rendering della scena: fondale, passata MSAA, corpi e sovrapposizioni.
// Richiede il contesto corrente. Il disegno ricevuto e' eseguito subito e
// non viene conservato; gli overlay del widget restano successivi alla passata.
class SceneRenderer : protected QOpenGLFunctions {
public:
    void initialize();
    void clearResources();
    void setAntialiasing(int samples);
    int antialiasing() const;
    int maxAntialiasing() const;
    QSize render(const QSize &logicalSize, const std::function<void()> &drawScene);
    // Operazioni immediate: il viewport sceglie corpi e geometria del piano.
    void setSectionClip(DisplayCache &display, OverlayRenderer &overlay, const QVector4D &plane, bool enabled);
    void drawSectionCaps(DisplayCache &display, OverlayRenderer &overlay,
                         const QVector<const BodyDisplay *> &solids,
                         const QVector<QVector3D> &quad, const QVector4D &plane);
    void drawSectionPlane(OverlayRenderer &overlay, const QVector<QVector3D> &quad, bool active);
    struct PreviewStyle {
        int displayMode = 0;
        bool transparent = false, nearestSurface = false, xray = false;
        bool spatial = false, thread = false;
        float devicePixelRatio = 1.0f;
        QColor retainedColor;
    };
    // Disegno immediato di anteprime gia' calcolate, senza stato del documento.
    void drawFeaturePreview(DisplayCache &cache, OverlayRenderer &overlay,
                            const BodyDisplay &display, const BodyDisplay &retained,
                            const PreviewStyle &style);
    void drawTrimPreview(DisplayCache &cache, const BodyDisplay &display, int displayMode);
    void drawExportMeshPreview(DisplayCache &cache, const BodyDisplay &display, bool quadrangular);
    // Nel filetto le polilinee campionate sono bordi elicoidali; i segmenti
    // di due punti sono cuciture fra domini B-spline e vengono esclusi.
    void drawThreadProfileEdges(OverlayRenderer &overlay, const BodyDisplay &display, const QVector4D &color);
    struct BodyFaceStyle {
        QColor color, hoverColor;
        bool hovered = false, seeThrough = false, analyzing = false, faceOnly = false;
        float opacity = 1.0f;
        int analysisMode = -1;
        float zebraFrequency = 12.0f, zebraAngle = 0.0f;
    };
    struct BodyEdgeStyle {
        bool curve = false, spatial = false, hovered = false, hiddenEdgesVisible = false, thread = false;
        QColor hoverColor;
        int displayMode = 0;
        float devicePixelRatio = 1.0f;
    };
    // Evidenziazione eseguita subito, fra facce e ripristino dello stato GL.
    // Il renderer non conserva il callback o le opzioni ricevute.
    void drawBodyFaces(DisplayCache &cache, OverlayRenderer &overlay, const BodyDisplay &display,
                       const BodyFaceStyle &style, const BodyDisplay &analysisFace,
                       const QVector<OverlayVertex> &analysisTriangles,
                       const std::function<void()> &highlightFaces);
    void drawBodyEdges(DisplayCache &cache, OverlayRenderer &overlay,
                       const BodyDisplay &display, const BodyEdgeStyle &style);
    // Display e indici sono scelti dal viewport e usati soltanto durante la chiamata.
    void drawHighlightFaces(DisplayCache &cache, const BodyDisplay &display, bool whole, const QColor &color);
    void drawHighlightEdges(DisplayCache &cache, const BodyDisplay &display, bool whole, const QColor &color);
    void drawSelectedFace(OverlayRenderer &overlay, const BodyDisplay &display,
                          const QVector<int> &indices, const QColor &color);
    void drawTopologyHighlight(OverlayRenderer &overlay, const BodyDisplay &display,
                               const QVector<int> &indices, const QColor &color, float devicePixelRatio);
    struct EdgePickStyle {
        bool hiddenEdgesVisible = false, hoveredSelected = false;
        int hoverEdge = -1;
        QColor hoverColor, selectionColor;
        float devicePixelRatio = 1.0f;
    };
    void drawPickedEdges(OverlayRenderer &overlay, const BodyDisplay *body,
                         const BodyDisplay *hoverBody, const QVector<int> &hoverFaceEdges,
                         const QVector<int> &pickedEdges, const EdgePickStyle &style);
    void drawBackgroundGradient(OverlayRenderer &overlay, const QSize &logicalSize,
                                 const BackgroundSettings &background);
    void drawAnalysisLines(OverlayRenderer &overlay, const QVector<OverlayVertex> &grid,
                           const QVector<OverlayVertex> &lines, bool through);
    // Riferimenti gia' dimensionati/orientati dal viewport, nessuno stato della camera.
    void drawAxes(OverlayRenderer &overlay, float length, const std::array<QColor, 3> &colors);
    void drawGrid(OverlayRenderer &overlay, const AxesOrientation &orientation, float step);
    void drawDatumQuad(OverlayRenderer &overlay, const QVector<QVector3D> &corners,
                       const GLfloat *color, float emphasis);
    struct ReferencePlane {
        QVector<QVector3D> corners;
        float emphasis = 0.0f;
        bool visible = false;
    };
    void drawReferencePlanes(OverlayRenderer &overlay, const std::array<ReferencePlane, 3> &planes);
    // Il disegno dei datum scelti dal viewport e' eseguito subito e non conservato.
    void drawPlanePass(const std::function<void()> &drawPlanes);
private:
    friend class ::ViewportInteractionTest;
    QOpenGLFramebufferObject *sceneBuffer(const QSize &size);
    void makeOpaque();
    int antialiasing_ = 4, maxSamples_ = 0, bufferSamples_ = 0;
    std::unique_ptr<QOpenGLFramebufferObject> msaaBuffer_, resolveBuffer_;
};
}

#endif
