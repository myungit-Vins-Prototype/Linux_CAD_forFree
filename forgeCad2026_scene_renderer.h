#ifndef FORGECAD2026_SCENE_RENDERER_H
#define FORGECAD2026_SCENE_RENDERER_H

#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QSize>
#include <functional>
#include <memory>

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
private:
    friend class ::ViewportInteractionTest;
    QOpenGLFramebufferObject *sceneBuffer(const QSize &size);
    void makeOpaque();
    int antialiasing_ = 4, maxSamples_ = 0, bufferSamples_ = 0;
    std::unique_ptr<QOpenGLFramebufferObject> msaaBuffer_, resolveBuffer_;
};
}

#endif
