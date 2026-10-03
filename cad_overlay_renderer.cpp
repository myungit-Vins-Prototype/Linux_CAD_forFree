#include "cad_overlay_renderer.h"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <cstddef>

namespace ForgeCad {
void OverlayRenderer::clear() {
    if (array_.isCreated()) array_.destroy();
    if (texturedArray_.isCreated()) texturedArray_.destroy();
    if (buffer_.isCreated()) buffer_.destroy();
    shader_.reset();
    tried_ = false;
}

bool OverlayRenderer::ensureReady() {
    if (tried_) return shader_ && array_.isCreated() && buffer_.isCreated();
    tried_ = true;
    if (!QOpenGLContext::currentContext()) return false;
    shader_ = std::make_unique<QOpenGLShaderProgram>();
    static const char *vertex = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 vertexColor;
uniform mat4 modelViewProjection;
uniform bool useInstances;
uniform mat4 instances[64];
uniform vec4 clipPlane;
uniform bool clipEnabled;
out vec4 color;
out vec2 screenDirectionSeed;
void main() {
    mat4 object = useInstances ? instances[gl_InstanceID] : mat4(1.0);
    vec4 world = object * vec4(position, 1.0);
    gl_Position = modelViewProjection * world;
    gl_ClipDistance[0] = clipEnabled ? dot(clipPlane, world) : 1.0;
    color = vertexColor;
    screenDirectionSeed = position.xy;
}
)";
    static const char *fragment = R"(
#version 330 core
in vec4 color;
in vec2 screenDirectionSeed;
uniform bool stippleEnabled;
uniform uint stipplePattern;
uniform int stippleFactor;
uniform bool hatchEnabled;
out vec4 fragmentColor;
void main() {
    if (stippleEnabled) {
        float coordinate = gl_FragCoord.x + gl_FragCoord.y;
        uint bit = uint(floor(coordinate / float(max(stippleFactor, 1)))) & 15u;
        if ((stipplePattern & (1u << bit)) == 0u) discard;
    }
    if (hatchEnabled) {
        int stripe = (int(floor(gl_FragCoord.x)) + int(floor(gl_FragCoord.y))) & 7;
        if (stripe >= 3) discard;
    }
    fragmentColor = color;
}
)";
    if (!shader_->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
        || !shader_->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
        || !shader_->link() || !buffer_.create() || !array_.create() || !texturedArray_.create()) {
        clear();
        tried_ = true;
        return false;
    }
    QOpenGLVertexArrayObject::Binder array(&array_);
    buffer_.bind();
    shader_->bind();
    shader_->enableAttributeArray(0);
    shader_->setAttributeBuffer(0, GL_FLOAT, offsetof(OverlayVertex, position), 3, sizeof(OverlayVertex));
    shader_->enableAttributeArray(1);
    shader_->setAttributeBuffer(1, GL_FLOAT, offsetof(OverlayVertex, color), 4, sizeof(OverlayVertex));
    shader_->release();
    buffer_.release();
    return true;
}

bool OverlayRenderer::available() { return ensureReady(); }

void OverlayRenderer::setMatrices(const QMatrix4x4 &projection, const QMatrix4x4 &modelView) {
    projection_ = projection;
    modelView_ = modelView;
}
void OverlayRenderer::setClipPlane(const QVector4D &plane, bool enabled) {
    clipPlane_ = plane;
    clipEnabled_ = enabled;
}
void OverlayRenderer::setStipple(bool enabled, quint16 pattern, int factor) {
    stippleEnabled_ = enabled;
    stipplePattern_ = pattern;
    stippleFactor_ = qMax(1, factor);
}

void OverlayRenderer::draw(GLenum primitive, const QVector<OverlayVertex> &vertices) {
    if (vertices.isEmpty() || !ensureReady()) return;
    shader_->bind();
    shader_->setUniformValue("modelViewProjection", projection_ * modelView_);
    shader_->setUniformValue("clipPlane", clipPlane_);
    shader_->setUniformValue("clipEnabled", clipEnabled_);
    shader_->setUniformValue("useInstances", false);
    shader_->setUniformValue("stippleEnabled", stippleEnabled_);
    shader_->setUniformValue("stipplePattern", GLuint(stipplePattern_));
    shader_->setUniformValue("stippleFactor", stippleFactor_);
    shader_->setUniformValue("hatchEnabled", hatchEnabled_);
    QOpenGLVertexArrayObject::Binder array(&array_);
    buffer_.bind();
    buffer_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    buffer_.allocate(vertices.constData(), int(vertices.size() * sizeof(OverlayVertex)));
    glDrawArrays(primitive, 0, vertices.size());
    buffer_.release();
    shader_->release();
}

void OverlayRenderer::draw(GLenum primitive, const QVector<QVector3D> &positions, const QVector4D &color) {
    QVector<OverlayVertex> vertices;
    vertices.reserve(positions.size());
    for (const QVector3D &position : positions) vertices.push_back({position, color});
    draw(primitive, vertices);
}

void OverlayRenderer::drawInstanced(GLenum primitive, const QVector<QVector3D> &positions, const QVector4D &color,
                                    const QVector<QMatrix4x4> &transforms) {
    if (positions.isEmpty() || transforms.isEmpty() || !ensureReady()) return;
    QVector<OverlayVertex> vertices;
    vertices.reserve(positions.size());
    for (const QVector3D &position : positions) vertices.push_back({position, color});
    shader_->bind();
    shader_->setUniformValue("modelViewProjection", projection_ * modelView_);
    shader_->setUniformValue("clipPlane", clipPlane_);
    shader_->setUniformValue("clipEnabled", clipEnabled_);
    shader_->setUniformValue("stippleEnabled", stippleEnabled_);
    shader_->setUniformValue("stipplePattern", GLuint(stipplePattern_));
    shader_->setUniformValue("stippleFactor", stippleFactor_);
    shader_->setUniformValue("hatchEnabled", hatchEnabled_);
    shader_->setUniformValue("useInstances", true);
    QOpenGLVertexArrayObject::Binder array(&array_);
    buffer_.bind();
    buffer_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    buffer_.allocate(vertices.constData(), int(vertices.size() * sizeof(OverlayVertex)));
    QOpenGLExtraFunctions *functions = QOpenGLContext::currentContext()->extraFunctions();
    for (int first = 0; first < transforms.size(); first += 64) {
        const int count = qMin(64, transforms.size() - first);
        shader_->setUniformValueArray("instances", transforms.constData() + first, count);
        functions->glDrawArraysInstanced(primitive, 0, vertices.size(), count);
    }
    shader_->setUniformValue("useInstances", false);
    buffer_.release();
    shader_->release();
}

void OverlayRenderer::drawTextured(QOpenGLShaderProgram &shader, GLenum primitive,
                                   const QVector<TexturedOverlayVertex> &vertices) {
    if (vertices.isEmpty() || !ensureReady()) return;
    shader.bind();
    QOpenGLVertexArrayObject::Binder array(&texturedArray_);
    buffer_.bind();
    buffer_.setUsagePattern(QOpenGLBuffer::DynamicDraw);
    buffer_.allocate(vertices.constData(), int(vertices.size() * sizeof(TexturedOverlayVertex)));
    shader.enableAttributeArray(0);
    shader.setAttributeBuffer(0, GL_FLOAT, offsetof(TexturedOverlayVertex, position), 3, sizeof(TexturedOverlayVertex));
    shader.enableAttributeArray(1);
    shader.setAttributeBuffer(1, GL_FLOAT, offsetof(TexturedOverlayVertex, texCoord), 2, sizeof(TexturedOverlayVertex));
    glDrawArrays(primitive, 0, vertices.size());
    buffer_.release();
}

}
