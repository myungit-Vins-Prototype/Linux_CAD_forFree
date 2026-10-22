#include "forgeCad2026_glass_compositor.h"
#include "cad_overlay_renderer.h"
#include <QMatrix4x4>
#include <QVector4D>
#include <cmath>

namespace ForgeCad::Gui {

void GlassCompositor::initialize() {
    initializeOpenGLFunctions();
    initializeGlassShader();
}

void GlassCompositor::clearResources() {
    glassShader_.reset();
    glassSource_.reset(); glassPing_.reset(); glassBlur_.reset();
}

void GlassCompositor::setPanel(quintptr id, const QRect &rect, int blur, int radius, bool visible) {
    if (!visible) glassPanels_.remove(id);
    else glassPanels_[id] = {rect, blur, radius};
}

void GlassCompositor::removePanel(quintptr id) { glassPanels_.remove(id); }

bool GlassCompositor::available() const { return gpuGlassReady_; }

void GlassCompositor::initializeGlassShader() {
    glassShader_ = std::make_unique<QOpenGLShaderProgram>();
    static const char *vertex =
        "#version 330 core\n"
        "layout(location = 0) in vec3 position;\n"
        "layout(location = 1) in vec2 texCoord;\n"
        "uniform mat4 transform;\n"
        "out vec2 uv;\n"
        "void main() { gl_Position = transform * vec4(position, 1.0); uv = texCoord; }\n";
    static const char *fragment =
        "#version 330 core\n"
        "uniform sampler2D sourceTexture;\n"
        "uniform vec2 direction;\n"
        "uniform vec2 logicalSize;\n"
        "uniform vec4 panelRect;\n"
        "uniform float panelRadius;\n"
        "in vec2 uv;\n"
        "out vec4 fragmentColor;\n"
        "void main() {\n"
        // La composizione finale usa direction=(0,0): in quel passaggio
        // basta una lettura, non tredici letture identiche.
        " if (direction.x == 0.0 && direction.y == 0.0) {\n"
        "   vec2 q = abs(uv * logicalSize - panelRect.xy) - panelRect.zw + vec2(panelRadius);\n"
        "   float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - panelRadius;\n"
        "   float aa = max(fwidth(d), 0.0001);\n"
        "   float coverage = 1.0 - smoothstep(-0.5 * aa, 0.5 * aa, d);\n"
        "   fragmentColor = vec4(texture(sourceTexture, uv).rgb, coverage); return;\n"
        " }\n"
        // Tredici campioni contigui: con soli cinque campioni molto
        // distanziati i contorni netti apparivano come immagini ripetute.
        " vec4 c = texture(sourceTexture, uv) * 0.137022816;\n"
        " c += texture(sourceTexture, uv + direction * 1.0) * 0.129618031;\n"
        " c += texture(sourceTexture, uv - direction * 1.0) * 0.129618031;\n"
        " c += texture(sourceTexture, uv + direction * 2.0) * 0.109719294;\n"
        " c += texture(sourceTexture, uv - direction * 2.0) * 0.109719294;\n"
        " c += texture(sourceTexture, uv + direction * 3.0) * 0.083108539;\n"
        " c += texture(sourceTexture, uv - direction * 3.0) * 0.083108539;\n"
        " c += texture(sourceTexture, uv + direction * 4.0) * 0.056331764;\n"
        " c += texture(sourceTexture, uv - direction * 4.0) * 0.056331764;\n"
        " c += texture(sourceTexture, uv + direction * 5.0) * 0.034166942;\n"
        " c += texture(sourceTexture, uv - direction * 5.0) * 0.034166942;\n"
        " c += texture(sourceTexture, uv + direction * 6.0) * 0.018544022;\n"
        " c += texture(sourceTexture, uv - direction * 6.0) * 0.018544022;\n"
        " fragmentColor = c;\n"
        "}\n";
    gpuGlassReady_ = glassShader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
        && glassShader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
        && glassShader_->link() && QOpenGLFramebufferObject::hasOpenGLFramebufferBlit();
    if (!gpuGlassReady_) glassShader_.reset();
}

void GlassCompositor::ensureGlassBuffers(const QSize &fullSize) {
    // A meta' risoluzione i 13 campioni ravvicinati costano circa quanto i
    // vecchi 5 campioni al 75%, ma producono una sfocatura continua.
    const QSize work(qMax(1, (fullSize.width() + 1) / 2),
                     qMax(1, (fullSize.height() + 1) / 2));
    bool configureSource = false, configureWork = false;
    if (!glassSource_ || glassSource_->size() != fullSize) {
        glassSource_ = std::make_unique<QOpenGLFramebufferObject>(fullSize);
        configureSource = true;
    }
    if (!glassPing_ || glassPing_->size() != work) {
        glassPing_ = std::make_unique<QOpenGLFramebufferObject>(work);
        glassBlur_ = std::make_unique<QOpenGLFramebufferObject>(work);
        configureWork = true;
    }
    const auto configureTexture = [this](GLuint texture) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    if (configureSource) configureTexture(glassSource_->texture());
    if (configureWork) {
        configureTexture(glassPing_->texture());
        configureTexture(glassBlur_->texture());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    if (!glassSource_->isValid() || !glassPing_->isValid() || !glassBlur_->isValid()) gpuGlassReady_ = false;
}

void GlassCompositor::glassBlurPass(OverlayRenderer &renderer, QOpenGLFramebufferObject *target, GLuint texture, const QVector2D &direction) {
    target->bind();
    glViewport(0, 0, target->width(), target->height());
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
    glassShader_->bind();
    glassShader_->setUniformValue("sourceTexture", 0);
    glassShader_->setUniformValue("direction", direction);
    glassShader_->setUniformValue("transform", QMatrix4x4());
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texture);
    const QVector<ForgeCad::TexturedOverlayVertex> quad{
        {QVector3D(-1, -1, 0), QVector2D(0, 0)}, {QVector3D(1, -1, 0), QVector2D(1, 0)},
        {QVector3D(1, 1, 0), QVector2D(1, 1)}, {QVector3D(-1, -1, 0), QVector2D(0, 0)},
        {QVector3D(1, 1, 0), QVector2D(1, 1)}, {QVector3D(-1, 1, 0), QVector2D(0, 1)}};
    renderer.drawTextured(*glassShader_, GL_TRIANGLES, quad);
    glBindTexture(GL_TEXTURE_2D, 0);
    glassShader_->release();
    target->release();
}

void GlassCompositor::drawRoundedGlassRect(OverlayRenderer &renderer, const QSize &logicalSize, const QRect &rect, int radius, const QSize &pixels) {
    const float left = rect.left(), right = rect.right() + 1.0f;
    const float bottom = logicalSize.height() - (rect.bottom() + 1.0f), top = logicalSize.height() - rect.top();
    const float r = qMin(float(qMax(0, radius)), 0.5f * qMin(rect.width(), rect.height()));
    glassShader_->setUniformValue("panelRect", QVector4D((left + right) * 0.5f, (bottom + top) * 0.5f,
                                                         (right - left) * 0.5f, (top - bottom) * 0.5f));
    glassShader_->setUniformValue("panelRadius", r);
    // La curva viene valutata per pixel nello shader: nessuna spezzata
    // a cinque segmenti e antialias anche dopo il resolve multisample.
    QVector<ForgeCad::TexturedOverlayVertex> vertices;
    const auto vertex = [&](float x, float y) {
        vertices.push_back({QVector3D(x, y, 0.0f), QVector2D(x / qMax(1, logicalSize.width()), y / qMax(1, logicalSize.height()))});
    };
    vertex(left, bottom); vertex(right, bottom); vertex(left, top); vertex(right, top);
    renderer.drawTextured(*glassShader_, GL_TRIANGLE_STRIP, vertices);
    Q_UNUSED(pixels);
}

void GlassCompositor::draw(const QSize &pixels, const QSize &logicalSize, qreal devicePixelRatio, GLuint targetFramebuffer, OverlayRenderer &renderer, QOpenGLFramebufferObject *resolvedScene) {
    if (!gpuGlassReady_ || glassPanels_.isEmpty()) return;
    ensureGlassBuffers(pixels);
    if (!gpuGlassReady_) return;
    if (resolvedScene) {
        QOpenGLFramebufferObject::blitFramebuffer(glassSource_.get(), resolvedScene);
    } else {
        QOpenGLFramebufferObject::blitFramebuffer(
            glassSource_.get(), QRect(QPoint(), pixels), nullptr, QRect(QPoint(), pixels), GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    int blur = 0;
    for (const GlassPanel &panel : glassPanels_) blur = qMax(blur, panel.blur);
    if (blur <= 0) {
        glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
        return;
    }
    // blur_ e' espresso in pixel logici. Conserva la stessa estensione
    // visiva al variare sia del DPR sia della risoluzione di lavoro.
    const float workRatio = float(glassPing_->width()) / qMax(1, pixels.width());
    const float radiusAtWorkSize = blur * float(devicePixelRatio) * workRatio;
    const float scale = qMax(0.25f, radiusAtWorkSize / 6.0f);
    glassBlurPass(renderer, glassPing_.get(), glassSource_->texture(),
                  QVector2D(scale / glassPing_->width(), 0));
    glassBlurPass(renderer, glassBlur_.get(), glassPing_->texture(),
                  QVector2D(0, scale / glassBlur_->height()));

    glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer);
    glViewport(0, 0, pixels.width(), pixels.height());
    glDisable(GL_DEPTH_TEST); glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, glassBlur_->texture());
    QMatrix4x4 transform;
    transform.ortho(0.0f, float(logicalSize.width()), 0.0f, float(logicalSize.height()), -1.0f, 1.0f);
    glassShader_->bind();
    glassShader_->setUniformValue("sourceTexture", 0);
    glassShader_->setUniformValue("direction", QVector2D());
    glassShader_->setUniformValue("logicalSize", QVector2D(logicalSize.width(), logicalSize.height()));
    glassShader_->setUniformValue("transform", transform);
    for (const GlassPanel &panel : glassPanels_) drawRoundedGlassRect(renderer, logicalSize, panel.rect, panel.radius, pixels);
    glassShader_->release();
    glBindTexture(GL_TEXTURE_2D, 0); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
}

}
