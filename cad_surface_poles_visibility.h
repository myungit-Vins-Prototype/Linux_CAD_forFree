#pragma once
#include "cad_types.h"
#include <QBitArray>
#include <QLineF>
#include <QMatrix4x4>
#include <QOpenGLFramebufferObject>
#include <functional>
#include <memory>
#include <vector>

namespace ForgeCad::Gui {
// Cache di sola visualizzazione: profondita delle mesh e gabbia campionata.
// Non visita i milioni di poli del modello e non modifica la geometria esatta.
class SurfacePolesVisibility {
public:
    void update(const QVector<QVector3D> &points, const QVector<std::array<int,3>> &grids,
                const QVector<BodyDisplay> &occluders, const QMatrix4x4 &projection, const QMatrix4x4 &modelView,
                const QSize &logicalSize, qreal ratio, const QVector4D &clipPlane, bool clip, bool through,
                const std::function<void()> &drawOccluders);
    const QBitArray &visible() const { return visible_; }
    const QVector<QLineF> &lines() const { return lines_; }
    void clearResources(); // contesto corrente se e' stato creato il framebuffer
    quint64 depthPasses() const { return depthPasses_; }
private:
    bool readDepth(const QSize &pixels, const std::function<void()> &draw);
    bool exposed(const QVector3D &ndc) const;
    void appendLine(QVector3D a, QVector3D b, const QSize &logicalSize);
    std::unique_ptr<QOpenGLFramebufferObject> buffer_;
    std::vector<float> depth_;
    QSize pixels_;
    QVector<BodyDisplay> occluders_; // trattiene i buffer COW delle chiavi
    QVector<QVector3D> points_;
    QVector<std::array<int,3>> grids_;
    QBitArray visible_;
    QVector<QLineF> lines_;
    quint64 depthKey_ = 0, layoutKey_ = 0, depthPasses_ = 0;
    bool depthValid_ = false, layoutValid_ = false;
};
}
