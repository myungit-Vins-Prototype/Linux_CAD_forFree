#ifndef FORGECAD_OVERLAY_RENDERER_H
#define FORGECAD_OVERLAY_RENDERER_H

#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <memory>

namespace ForgeCad {

struct OverlayVertex {
    QVector3D position;
    QVector4D color;
};

struct TexturedOverlayVertex {
    QVector3D position;
    QVector2D texCoord;
};

// Buffer dinamico per linee, punti e triangoli del viewport. Tutti i metodi
// richiedono il contesto OpenGL corrente.
class OverlayRenderer {
public:
    void clear();
    bool available();
    void setMatrices(const QMatrix4x4 &projection, const QMatrix4x4 &modelView);
    void setClipPlane(const QVector4D &plane, bool enabled);
    void setStipple(bool enabled, quint16 pattern = 0xFFFF, int factor = 1);
    void setHatch(bool enabled) { hatchEnabled_ = enabled; }
    void draw(GLenum primitive, const QVector<OverlayVertex> &vertices);
    void draw(GLenum primitive, const QVector<QVector3D> &positions, const QVector4D &color);
    // Disegna una polilinea con spessore reale in pixel. Nel profilo Core i
    // driver possono limitare glLineWidth a 1, quindi i segmenti vengono
    // espansi in triangoli da un geometry shader.
    void drawWideLineStrip(const QVector<QVector3D> &positions, const QVector4D &color, float widthPixels);
    void drawInstanced(GLenum primitive, const QVector<QVector3D> &positions, const QVector4D &color,
                       const QVector<QMatrix4x4> &transforms);
    void drawTextured(QOpenGLShaderProgram &shader, GLenum primitive, const QVector<TexturedOverlayVertex> &vertices);

private:
    bool ensureReady();
    bool ensureWideReady();
    QOpenGLBuffer buffer_;
    QOpenGLVertexArrayObject array_;
    QOpenGLVertexArrayObject texturedArray_;
    std::unique_ptr<QOpenGLShaderProgram> shader_;
    std::unique_ptr<QOpenGLShaderProgram> wideShader_;
    bool tried_ = false;
    bool triedWide_ = false;
    QMatrix4x4 projection_, modelView_;
    QVector4D clipPlane_;
    quint16 stipplePattern_ = 0xFFFF;
    int stippleFactor_ = 1;
    bool stippleEnabled_ = false;
    bool hatchEnabled_ = false;
    bool clipEnabled_ = false;
};

}

#endif
