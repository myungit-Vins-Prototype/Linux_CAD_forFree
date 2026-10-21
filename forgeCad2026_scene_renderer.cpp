#include "forgeCad2026_scene_renderer.h"

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
}
