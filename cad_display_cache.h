#ifndef FORGECAD_DISPLAY_CACHE_H
#define FORGECAD_DISPLAY_CACHE_H

#include "cad_types.h"
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <array>
#include <memory>
#include <vector>

namespace ForgeCad {
// Solo dati di visualizzazione. Tutti i metodi richiedono il contesto GL corrente.
class DisplayCache {
public:
    struct Lighting {
        QVector4D ambient{0.2f, 0.2f, 0.2f, 1.0f};
        QVector4D specular{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<QVector4D, 4> positions{};
        std::array<QVector4D, 4> colors{};
        std::array<int, 4> enabled{};
        float shininess = 110.0f;
    };
    void setZebra(bool enabled, float frequency, float angle) { zebra_ = enabled; zebraFrequency_ = frequency; zebraAngle_ = angle; }
    void beginFrame();
    void clear();
    void setMatrices(const QMatrix4x4 &projection, const QMatrix4x4 &modelView);
    void setClipPlane(const QVector4D &plane, bool enabled);
    void setColor(const QVector4D &color) { color_ = color; }
    void setEmission(const QVector4D &emission) { emission_ = emission; }
    void setLightingEnabled(bool enabled) { lightingEnabled_ = enabled; }
    void setLighting(const Lighting &lighting);
    void faces(const BodyDisplay &display);
    void edges(const BodyDisplay &display);
    void instancedFaces(const BodyDisplay &display, const QVector<QMatrix4x4> &transforms);
    void instancedEdges(const BodyDisplay &display, const QVector<QMatrix4x4> &transforms);
    // Disegno piatto senza illuminazione per il framebuffer degli identificatori.
    void pickingFaces(const BodyDisplay &display, const QVector4D &identifierColor);
    void pickingInstancedFaces(const BodyDisplay &display, const QVector<QMatrix4x4> &transforms,
                               const QVector4D &identifierColor);
    bool shaderAvailable();
private:
    struct Entry {
        BodyDisplay source; // mantiene vivi i dati implicitamente condivisi della chiave
        QOpenGLBuffer positions, normals, lines;
        QOpenGLBuffer lineIndices{QOpenGLBuffer::IndexBuffer};
        QOpenGLVertexArrayObject faceArray, lineArray;
        int lineIndicesCount = 0;
        unsigned long used = 0;
        bool ready = false;
    };
    Entry &get(const BodyDisplay &display);
    bool ensureShaders();
    void setFaceUniforms();
    std::vector<std::unique_ptr<Entry>> entries_;
    std::unique_ptr<QOpenGLShaderProgram> faceShader_, lineShader_;
    bool shadersTried_ = false;
    unsigned long frame_ = 0;
    QMatrix4x4 projection_, modelView_;
    QVector4D clipPlane_;
    QVector4D color_{1.0f, 1.0f, 1.0f, 1.0f};
    QVector4D emission_{0.0f, 0.0f, 0.0f, 1.0f};
    Lighting lighting_;
    bool faceMatricesDirty_ = true;
    bool lineMatricesDirty_ = true;
    bool lightingDirty_ = true;
    bool lightingEnabled_ = false;
    bool clipEnabled_ = false;
    bool zebra_ = false;
    float zebraFrequency_ = 12, zebraAngle_ = 0;
};
}
#endif
