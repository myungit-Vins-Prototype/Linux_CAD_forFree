#include "forgeCad2026_scene_renderer.h"
#include "cad_display_cache.h"
#include "cad_overlay_renderer.h"

namespace ForgeCad::Gui {
void SceneRenderer::initialize() {
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glClearColor(0.025f, 0.035f, 0.050f, 1.0f);
    // 0 se i framebuffer multisample non ci sono (la chiamata fallisce e il valore resta 0).
    GLint samples = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &samples);
    while (glGetError() != GL_NO_ERROR) {
    }
    maxSamples_ = samples;
}

void SceneRenderer::clearResources() {
    msaaBuffer_.reset();
    resolveBuffer_.reset();
}

void SceneRenderer::setAntialiasing(int samples) { antialiasing_ = qMax(0, samples); }
int SceneRenderer::antialiasing() const { return maxSamples_ > 0 ? qMin(antialiasing_, maxSamples_) : antialiasing_; }
int SceneRenderer::maxAntialiasing() const { return maxSamples_; }

QSize SceneRenderer::render(const QSize &logicalSize, const std::function<void()> &drawScene) {
    // Con l'antialiasing la scena OpenGL va nel framebuffer multisample;
    // le sovrapposizioni QPainter si disegnano dopo, direttamente nel widget.
    GLint viewport[4] = {0, 0, logicalSize.width(), logicalSize.height()};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const QSize pixels(qMax(1, viewport[2]), qMax(1, viewport[3]));
    QOpenGLFramebufferObject *scene = sceneBuffer(pixels);
    if (scene) {
        scene->bind();
        glViewport(0, 0, pixels.width(), pixels.height());
        glEnable(GL_MULTISAMPLE);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    drawScene();
    makeOpaque();
    if (scene) {
        scene->release();
        QOpenGLFramebufferObject::blitFramebuffer(resolveBuffer_.get(), msaaBuffer_.get());
        QOpenGLFramebufferObject::blitFramebuffer(nullptr, QRect(QPoint(), pixels), resolveBuffer_.get(), QRect(QPoint(), pixels));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
    return pixels;
}

QOpenGLFramebufferObject *SceneRenderer::sceneBuffer(const QSize &size) {
    const int samples = antialiasing();
    if (samples <= 0 || maxSamples_ <= 0 || !QOpenGLFramebufferObject::hasOpenGLFramebufferBlit()) {
        msaaBuffer_.reset();
        resolveBuffer_.reset();
        return nullptr;
    }
    if (!msaaBuffer_ || msaaBuffer_->size() != size || bufferSamples_ != samples) {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(samples);
        msaaBuffer_ = std::make_unique<QOpenGLFramebufferObject>(size, format);
        resolveBuffer_ = std::make_unique<QOpenGLFramebufferObject>(size);
        bufferSamples_ = samples;
        if (!msaaBuffer_->isValid() || !resolveBuffer_->isValid()) {
            msaaBuffer_.reset();
            resolveBuffer_.reset();
            antialiasing_ = 0;
            return nullptr;
        }
    }
    return msaaBuffer_.get();
}

void SceneRenderer::makeOpaque() {
    GLfloat clear[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
}

void SceneRenderer::setSectionClip(DisplayCache &display, OverlayRenderer &overlay,
                                   const QVector4D &plane, bool enabled) {
    display.setClipPlane(plane, enabled);
    overlay.setClipPlane(plane, enabled);
    if (enabled) glEnable(GL_CLIP_DISTANCE0);
    else glDisable(GL_CLIP_DISTANCE0);
}

void SceneRenderer::drawSectionCaps(DisplayCache &display, OverlayRenderer &overlay,
                                    const QVector<const BodyDisplay *> &solids,
                                    const QVector<QVector3D> &quad, const QVector4D &plane) {
    display.setLightingEnabled(false);
    glEnable(GL_STENCIL_TEST);
    for (const BodyDisplay *body : solids) {
        const BodyDisplay &bodyDisplay = *body;
        glClear(GL_STENCIL_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
        glStencilFunc(GL_ALWAYS, 0, 1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
        setSectionClip(display, overlay, plane, true);
        display.displayFaces(bodyDisplay);
        setSectionClip(display, overlay, QVector4D(), false);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glStencilFunc(GL_EQUAL, 1, 1);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        QVector<QVector3D> triangles;
        for (int corner : {0, 1, 2, 0, 2, 3}) triangles.push_back(quad.at(corner));
        overlay.draw(GL_TRIANGLES, triangles, QVector4D(0.78f, 0.36f, 0.30f, 1.0f));
        glDepthFunc(GL_LEQUAL);
        overlay.setHatch(true);
        overlay.draw(GL_TRIANGLES, triangles, QVector4D(0.45f, 0.14f, 0.12f, 1.0f));
        overlay.setHatch(false);
    }
    // Resta GL_LEQUAL (come in initializeGL): con GL_LESS le linee degli schizzi
    // che stanno sugli spigoli dei corpi non si vedrebbero piu'.
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_STENCIL_TEST);
    glStencilFunc(GL_ALWAYS, 0, 0xff);
}


void SceneRenderer::drawSectionPlane(OverlayRenderer &overlay, const QVector<QVector3D> &quad, bool active) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    QVector<QVector3D> triangles;
    for (int corner : {0, 1, 2, 0, 2, 3}) triangles.push_back(quad.at(corner));
    overlay.draw(GL_TRIANGLES, triangles, QVector4D(0.95f, 0.45f, 0.35f, 0.10f));
    glLineWidth(active ? 2.5f : 1.5f);
    QVector<QVector3D> outline = quad;
    outline.push_back(outline.front());
    overlay.draw(GL_LINE_STRIP, outline, QVector4D(1.0f, 0.55f, 0.45f, active ? 1.0f : 0.75f));
    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
}

}
