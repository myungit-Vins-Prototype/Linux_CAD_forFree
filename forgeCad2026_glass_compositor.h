#ifndef FORGECAD2026_GLASS_COMPOSITOR_H
#define FORGECAD2026_GLASS_COMPOSITOR_H

#include <QHash>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QRect>
#include <QSize>
#include <QVector2D>
#include <memory>

class ViewportInteractionTest;
namespace ForgeCad { class OverlayRenderer; }

namespace ForgeCad::Gui {
// Shader, framebuffer e registro dei fondali sfocati dei pannelli.
// I metodi GPU richiedono il contesto OpenGL corrente; non possiede il widget
// né il renderer degli overlay. Dimensioni e destinazione arrivano dal chiamante.
class GlassCompositor : protected QOpenGLFunctions {
public:
    void initialize();
    // Chiamare con il contesto corrente, prima che il viewport lo distrugga.
    void clearResources();
    void setPanel(quintptr id, const QRect &rect, int blur, int radius, bool visible);
    void removePanel(quintptr id);
    bool available() const;
    void draw(const QSize &pixels, const QSize &logicalSize, qreal devicePixelRatio,
              GLuint targetFramebuffer, OverlayRenderer &renderer,
              QOpenGLFramebufferObject *resolvedScene = nullptr);

private:
    friend class ::ViewportInteractionTest;
    void initializeGlassShader();
    void ensureGlassBuffers(const QSize &fullSize);
    void glassBlurPass(OverlayRenderer &renderer, QOpenGLFramebufferObject *target,
                       GLuint texture, const QVector2D &direction);
    void drawRoundedGlassRect(OverlayRenderer &renderer, const QSize &logicalSize,
                              const QRect &rect, int radius, const QSize &pixels);
    struct GlassPanel { QRect rect; int blur = 0; int radius = 0; };
    QHash<quintptr, GlassPanel> glassPanels_;
    std::unique_ptr<QOpenGLShaderProgram> glassShader_;
    std::unique_ptr<QOpenGLFramebufferObject> glassSource_, glassPing_, glassBlur_;
    bool gpuGlassReady_ = false;
};
}

#endif
