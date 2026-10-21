#ifndef FORGECAD2026_CAMERA_H
#define FORGECAD2026_CAMERA_H

#include "cad_types.h"
#include <QMatrix4x4>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QVector3D>
#include <array>

class CadViewport;
class ViewportInteractionTest;

namespace ForgeCad::Gui {
// Camera ortografica: stato e calcoli della vista, indipendenti da widget,
// eventi, documento e OpenGL. Dimensioni e profondita' arrivano dal viewport.
class ViewportCamera {
public:
    QMatrix4x4 basisMatrix() const;
    QMatrix4x4 projectionMatrix(const QSize &viewportSize, double depth) const;
    QMatrix4x4 modelViewMatrix() const;
    void setPreset(int preset);
    void setFrame(const QVector3D &worldX, const QVector3D &worldNormal);
    static constexpr float kOrbitDegreesPerPixel = 0.5f;
    static float normalizedViewAngle(float degrees);
    // Il viewport sceglie il pivot dalla geometria all'inizio del gesto.
    void orbitView(const QPoint &delta);
    AxesOrientation orientationFromCurrentView() const;
    QPointF projectWorldPoint(const QVector3D &point, const QSize &viewportSize) const;
    void viewRay(const QPoint &position, const QSize &viewportSize, double depth,
                 QVector3D &origin, QVector3D &direction) const;
    // Richiede punti non vuoti. Aggiorna il pan e restituisce lo zoom richiesto:
    // il viewport applica i limiti dipendenti dalla geometria della scena.
    float fitPoints(const QVector<QVector3D> &points, const QSize &viewportSize);

private:
    friend class ::CadViewport;
    friend class ::ViewportInteractionTest;
    float yaw_ = -32.0f, pitch_ = 22.0f, zoom_ = 8.0f;
    float roll_ = 0.0f;
    AxesOrientation orientation_;
    bool autoFit_ = false;
    float panX_ = 0.0f, panY_ = 0.0f;
    QVector3D orbitPivot_;
    bool orbitPivotValid_ = false;
    mutable std::array<double, 18> projectionKey_{};
    mutable QMatrix4x4 projectionMatrix_;
};
}

#endif
