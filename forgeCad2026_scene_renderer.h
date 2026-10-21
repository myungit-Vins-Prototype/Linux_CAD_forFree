#ifndef FORGECAD2026_SCENE_RENDERER_H
#define FORGECAD2026_SCENE_RENDERER_H

#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QSize>
#include <QVector>
#include <QVector3D>
#include <QVector4D>
#include <functional>
#include <memory>

struct BodyDisplay;
namespace ForgeCad {
class DisplayCache;
class OverlayRenderer;
}
class ViewportInteractionTest;
namespace ForgeCad::Gui {
// Passata della scena: target MSAA, risoluzione e opacita' del framebuffer.
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
private:
    friend class ::ViewportInteractionTest;
    QOpenGLFramebufferObject *sceneBuffer(const QSize &size);
    void makeOpaque();
    int antialiasing_ = 4, maxSamples_ = 0, bufferSamples_ = 0;
    std::unique_ptr<QOpenGLFramebufferObject> msaaBuffer_, resolveBuffer_;
};
}

#endif
