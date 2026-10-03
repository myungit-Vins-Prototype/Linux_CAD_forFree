#include "cad_display_cache.h"
#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFunctions>
#include <QVector4D>
#include <algorithm>
#include <limits>

namespace ForgeCad {
namespace {
void setPrimitiveRestartIndex(GLuint index) {
    using Function = void (*)(GLuint);
    const auto function = reinterpret_cast<Function>(
        QOpenGLContext::currentContext()->getProcAddress("glPrimitiveRestartIndex"));
    if (function) function(index);
}
}

void DisplayCache::clear() {
    entries_.clear();
    faceShader_.reset();
    lineShader_.reset();
    shadersTried_ = false;
}
void DisplayCache::beginFrame() {
    ++frame_;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [this](const auto &e) {
        return e->used + 2 < frame_;
    }), entries_.end());
}
void DisplayCache::setMatrices(const QMatrix4x4 &projection, const QMatrix4x4 &modelView) {
    projection_ = projection;
    modelView_ = modelView;
}
void DisplayCache::setClipPlane(const QVector4D &plane, bool enabled) {
    clipPlane_ = plane;
    clipEnabled_ = enabled;
}
DisplayCache::Entry &DisplayCache::get(const BodyDisplay &display) {
    for (const auto &entry : entries_)
        if (entry->source.vertices.constData() == display.vertices.constData()
            && entry->source.normals.constData() == display.normals.constData()
            && entry->source.edges.constData() == display.edges.constData()) {
            entry->used = frame_;
            return *entry;
        }
    auto entry = std::make_unique<Entry>();
    entry->source = display;
    entry->used = frame_;
    QVector<QVector3D> lines;
    QVector<GLuint> lineIndices;
    for (const auto &edge : display.edges) {
        if (edge.size() < 2) continue;
        for (const QVector3D &point : edge) {
            lineIndices.append(GLuint(lines.size()));
            lines.append(point);
        }
        lineIndices.append(std::numeric_limits<GLuint>::max());
    }
    entry->lineIndicesCount = int(lineIndices.size());
    const auto upload = [](QOpenGLBuffer &buffer, const QVector<QVector3D> &values) {
        if (!buffer.create() || !buffer.bind()) return false;
        buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        buffer.allocate(values.constData(), int(values.size() * sizeof(QVector3D)));
        buffer.release();
        return true;
    };
    const auto uploadIndices = [](QOpenGLBuffer &buffer, const QVector<GLuint> &values) {
        if (!buffer.create() || !buffer.bind()) return false;
        buffer.setUsagePattern(QOpenGLBuffer::StaticDraw);
        buffer.allocate(values.constData(), int(values.size() * sizeof(GLuint)));
        buffer.release();
        return true;
    };
    entry->ready = upload(entry->positions, display.vertices) && upload(entry->normals, display.normals)
        && upload(entry->lines, lines) && uploadIndices(entry->lineIndices, lineIndices);
    if (entry->ready && ensureShaders()) {
        entry->faceArray.create();
        entry->lineArray.create();
        if (entry->faceArray.isCreated()) {
            QOpenGLVertexArrayObject::Binder array(&entry->faceArray);
            faceShader_->bind();
            entry->positions.bind();
            faceShader_->enableAttributeArray(0);
            faceShader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
            entry->normals.bind();
            faceShader_->enableAttributeArray(1);
            faceShader_->setAttributeBuffer(1, GL_FLOAT, 0, 3, sizeof(QVector3D));
            entry->normals.release();
            faceShader_->release();
        }
        if (entry->lineArray.isCreated()) {
            QOpenGLVertexArrayObject::Binder array(&entry->lineArray);
            lineShader_->bind();
            entry->lines.bind();
            lineShader_->enableAttributeArray(0);
            lineShader_->setAttributeBuffer(0, GL_FLOAT, 0, 3, sizeof(QVector3D));
            entry->lines.release();
            lineShader_->release();
        }
    }
    entries_.push_back(std::move(entry));
    return *entries_.back();
}

bool DisplayCache::ensureShaders() {
    if (shadersTried_) return faceShader_ && lineShader_;
    shadersTried_ = true;
    if (!QOpenGLContext::currentContext()) return false;
    const char *faceVertex = R"(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
uniform mat4 modelView;
uniform mat4 projection;
uniform mat3 normalMatrix;
uniform bool useInstances;
uniform mat4 instances[64];
uniform vec4 clipPlane;
uniform bool clipEnabled;
out vec3 eyePosition;
out vec3 eyeNormal;
void main() {
    mat4 object = useInstances ? instances[gl_InstanceID] : mat4(1.0);
    vec4 world = object * vec4(position, 1.0);
    vec4 eye = modelView * world;
    eyePosition = eye.xyz;
    eyeNormal = normalize(normalMatrix * mat3(object) * normal);
    gl_Position = projection * eye;
    gl_ClipDistance[0] = clipEnabled ? dot(clipPlane, world) : 1.0;
}
)";
    const char *faceFragment = R"(
#version 330 core
in vec3 eyePosition;
in vec3 eyeNormal;
uniform vec4 baseColor;
uniform vec4 ambient;
uniform vec4 emission;
uniform vec4 lightPosition[4];
uniform vec4 lightColor[4];
uniform int lightEnabled[4];
uniform bool lightingEnabled;
uniform vec4 specularColor;
uniform float shininess;
out vec4 fragmentColor;
void main() {
    if (!lightingEnabled) {
        fragmentColor = baseColor;
        return;
    }
    vec3 n = normalize(eyeNormal);
    vec3 rgb = ambient.rgb * baseColor.rgb + emission.rgb;
    vec3 viewDirection = normalize(-eyePosition);
    for (int i = 0; i < 4; ++i) {
        if (lightEnabled[i] == 0) continue;
        vec3 lightDirection = lightPosition[i].w == 0.0
            ? normalize(lightPosition[i].xyz)
            : normalize(lightPosition[i].xyz - eyePosition);
        float diffuse = max(dot(n, lightDirection), 0.0);
        rgb += diffuse * lightColor[i].rgb * baseColor.rgb;
        if (diffuse > 0.0 && shininess > 0.0) {
            vec3 reflected = reflect(-lightDirection, n);
            rgb += pow(max(dot(reflected, viewDirection), 0.0), shininess) * lightColor[i].rgb * specularColor.rgb;
        }
    }
    fragmentColor = vec4(rgb, baseColor.a);
}
)";
    const char *lineVertex = R"(
#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 modelViewProjection;
uniform bool useInstances;
uniform mat4 instances[64];
uniform vec4 clipPlane;
uniform bool clipEnabled;
void main() {
    mat4 object = useInstances ? instances[gl_InstanceID] : mat4(1.0);
    vec4 world = object * vec4(position, 1.0);
    gl_Position = modelViewProjection * world;
    gl_ClipDistance[0] = clipEnabled ? dot(clipPlane, world) : 1.0;
}
)";
    const char *lineFragment = R"(
#version 330 core
uniform vec4 color;
out vec4 fragmentColor;
void main() { fragmentColor = color; }
)";
    auto build = [](const char *vertex, const char *fragment) {
        auto program = std::make_unique<QOpenGLShaderProgram>();
        if (!program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex)
            || !program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment)
            || !program->link()) return std::unique_ptr<QOpenGLShaderProgram>();
        return program;
    };
    faceShader_ = build(faceVertex, faceFragment);
    lineShader_ = build(lineVertex, lineFragment);
    if (!faceShader_ || !lineShader_) {
        faceShader_.reset();
        lineShader_.reset();
        return false;
    }
    return true;
}

bool DisplayCache::shaderAvailable() { return ensureShaders(); }

void DisplayCache::setFaceUniforms() {
    faceShader_->bind();
    faceShader_->setUniformValue("modelView", modelView_);
    faceShader_->setUniformValue("projection", projection_);
    faceShader_->setUniformValue("normalMatrix", modelView_.normalMatrix());
    faceShader_->setUniformValue("clipPlane", clipPlane_);
    faceShader_->setUniformValue("clipEnabled", clipEnabled_);
    faceShader_->setUniformValue("baseColor", color_);
    faceShader_->setUniformValue("lightingEnabled", lightingEnabled_);
    faceShader_->setUniformValue("ambient", lighting_.ambient);
    faceShader_->setUniformValue("emission", emission_);
    faceShader_->setUniformValue("specularColor", lighting_.specular);
    faceShader_->setUniformValue("shininess", lighting_.shininess);
    faceShader_->setUniformValueArray("lightPosition", lighting_.positions.data(), 4);
    faceShader_->setUniformValueArray("lightColor", lighting_.colors.data(), 4);
    faceShader_->setUniformValueArray("lightEnabled", lighting_.enabled.data(), 4);
}

void DisplayCache::faces(const BodyDisplay &display) {
    if (display.vertices.isEmpty()) return;
    Entry &entry = get(display);
    if (!entry.ready || !entry.faceArray.isCreated() || !ensureShaders()) return;
    setFaceUniforms();
    faceShader_->setUniformValue("useInstances", false);
    {
        QOpenGLVertexArrayObject::Binder array(&entry.faceArray);
        glDrawArrays(GL_TRIANGLES, 0, int(display.vertices.size()));
    }
    faceShader_->release();
}

void DisplayCache::edges(const BodyDisplay &display) {
    if (display.edges.isEmpty()) return;
    Entry &entry = get(display);
    if (!entry.ready || !entry.lineArray.isCreated() || !ensureShaders()) return;
    lineShader_->bind();
    lineShader_->setUniformValue("modelViewProjection", projection_ * modelView_);
    lineShader_->setUniformValue("color", color_);
    lineShader_->setUniformValue("clipPlane", clipPlane_);
    lineShader_->setUniformValue("clipEnabled", clipEnabled_);
    lineShader_->setUniformValue("useInstances", false);
    {
        QOpenGLVertexArrayObject::Binder array(&entry.lineArray);
        entry.lineIndices.bind();
        glEnable(GL_PRIMITIVE_RESTART);
        setPrimitiveRestartIndex(std::numeric_limits<GLuint>::max());
        glDrawElements(GL_LINE_STRIP, entry.lineIndicesCount, GL_UNSIGNED_INT, nullptr);
        glDisable(GL_PRIMITIVE_RESTART);
        entry.lineIndices.release();
    }
    lineShader_->release();
}

void DisplayCache::instancedFaces(const BodyDisplay &display, const QVector<QMatrix4x4> &transforms) {
    if (transforms.isEmpty()) return;
    Entry &entry = get(display);
    if (!entry.ready || !entry.faceArray.isCreated() || !ensureShaders()) return;
    setFaceUniforms();
    faceShader_->setUniformValue("useInstances", true);
    QOpenGLExtraFunctions *functions = QOpenGLContext::currentContext()->extraFunctions();
    QOpenGLVertexArrayObject::Binder array(&entry.faceArray);
    for (int first = 0; first < transforms.size(); first += 64) {
        const int count = qMin(64, transforms.size() - first);
        faceShader_->setUniformValueArray("instances", transforms.constData() + first, count);
        functions->glDrawArraysInstanced(GL_TRIANGLES, 0, int(display.vertices.size()), count);
    }
    faceShader_->setUniformValue("useInstances", false);
    faceShader_->release();
}

void DisplayCache::instancedEdges(const BodyDisplay &display, const QVector<QMatrix4x4> &transforms) {
    if (transforms.isEmpty()) return;
    Entry &entry = get(display);
    if (!entry.ready || !entry.lineArray.isCreated() || !ensureShaders()) return;
    lineShader_->bind();
    lineShader_->setUniformValue("modelViewProjection", projection_ * modelView_);
    lineShader_->setUniformValue("color", color_);
    lineShader_->setUniformValue("clipPlane", clipPlane_);
    lineShader_->setUniformValue("clipEnabled", clipEnabled_);
    lineShader_->setUniformValue("useInstances", true);
    QOpenGLExtraFunctions *functions = QOpenGLContext::currentContext()->extraFunctions();
    QOpenGLVertexArrayObject::Binder array(&entry.lineArray);
    for (int first = 0; first < transforms.size(); first += 64) {
        const int count = qMin(64, transforms.size() - first);
        lineShader_->setUniformValueArray("instances", transforms.constData() + first, count);
        entry.lineIndices.bind();
        glEnable(GL_PRIMITIVE_RESTART);
        setPrimitiveRestartIndex(std::numeric_limits<GLuint>::max());
        functions->glDrawElementsInstanced(GL_LINE_STRIP, entry.lineIndicesCount, GL_UNSIGNED_INT, nullptr, count);
        glDisable(GL_PRIMITIVE_RESTART);
        entry.lineIndices.release();
    }
    lineShader_->setUniformValue("useInstances", false);
    lineShader_->release();
}

void DisplayCache::pickingFaces(const BodyDisplay &display, const QVector4D &identifierColor) {
    if (display.vertices.isEmpty()) return;
    Entry &entry = get(display);
    if (!entry.ready || !entry.faceArray.isCreated() || !ensureShaders()) return;
    faceShader_->bind();
    faceShader_->setUniformValue("modelView", modelView_);
    faceShader_->setUniformValue("projection", projection_);
    faceShader_->setUniformValue("normalMatrix", modelView_.normalMatrix());
    faceShader_->setUniformValue("clipPlane", QVector4D());
    faceShader_->setUniformValue("clipEnabled", false);
    faceShader_->setUniformValue("baseColor", identifierColor);
    faceShader_->setUniformValue("lightingEnabled", false);
    faceShader_->setUniformValue("useInstances", false);
    {
        QOpenGLVertexArrayObject::Binder array(&entry.faceArray);
        glDrawArrays(GL_TRIANGLES, 0, int(display.vertices.size()));
    }
    faceShader_->release();
}

void DisplayCache::pickingInstancedFaces(const BodyDisplay &display, const QVector<QMatrix4x4> &transforms,
                                         const QVector4D &identifierColor) {
    if (display.vertices.isEmpty() || transforms.isEmpty()) return;
    Entry &entry = get(display);
    if (!entry.ready || !entry.faceArray.isCreated() || !ensureShaders()) return;
    faceShader_->bind();
    faceShader_->setUniformValue("modelView", modelView_);
    faceShader_->setUniformValue("projection", projection_);
    faceShader_->setUniformValue("normalMatrix", modelView_.normalMatrix());
    faceShader_->setUniformValue("clipPlane", QVector4D());
    faceShader_->setUniformValue("clipEnabled", false);
    faceShader_->setUniformValue("baseColor", identifierColor);
    faceShader_->setUniformValue("lightingEnabled", false);
    faceShader_->setUniformValue("useInstances", true);
    QOpenGLExtraFunctions *functions = QOpenGLContext::currentContext()->extraFunctions();
    QOpenGLVertexArrayObject::Binder array(&entry.faceArray);
    for (int first = 0; first < transforms.size(); first += 64) {
        const int count = qMin(64, transforms.size() - first);
        faceShader_->setUniformValueArray("instances", transforms.constData() + first, count);
        functions->glDrawArraysInstanced(GL_TRIANGLES, 0, int(display.vertices.size()), count);
    }
    faceShader_->setUniformValue("useInstances", false);
    faceShader_->release();
}
}
