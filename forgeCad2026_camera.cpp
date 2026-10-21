#include "forgeCad2026_camera.h"
#include <cmath>

namespace ForgeCad::Gui {

QMatrix4x4 ViewportCamera::basisMatrix() const {
    const AxesOrientation &o = orientation_;
    return QMatrix4x4(float(o.right[0]), float(o.right[1]), float(o.right[2]), 0.0f,
                      float(o.up[0]), float(o.up[1]), float(o.up[2]), 0.0f,
                      float(o.toward[0]), float(o.toward[1]), float(o.toward[2]), 0.0f,
                      0.0f, 0.0f, 0.0f, 1.0f);
}

QMatrix4x4 ViewportCamera::projectionMatrix(const QSize &viewportSize, double depth) const {
    const float aspect = float(viewportSize.width()) / float(qMax(1, viewportSize.height()));
    const float viewScale = zoom_ / 8.0f;
    QMatrix4x4 projection;
    projection.ortho(-4.0f * aspect * viewScale, 4.0f * aspect * viewScale,
                     -4.0f * viewScale, 4.0f * viewScale,
                     float(double(zoom_) - depth), float(double(zoom_) + depth));
    return projection;
}

QMatrix4x4 ViewportCamera::modelViewMatrix() const {
    QMatrix4x4 modelView;
    modelView.translate(panX_, panY_, -zoom_);
    modelView.rotate(roll_, 0.0f, 0.0f, 1.0f);
    modelView.rotate(pitch_, 1.0f, 0.0f, 0.0f);
    modelView.rotate(yaw_, 0.0f, 1.0f, 0.0f);
    modelView *= basisMatrix();
    return modelView;
}

void ViewportCamera::setPreset(int preset) {
    orbitPivotValid_ = false;
    roll_ = 0.0f;
    switch (preset) {
    case 0: yaw_ = 0.0f; pitch_ = 0.0f; break;
    case 1: yaw_ = 180.0f; pitch_ = 0.0f; break;
    case 2: yaw_ = -90.0f; pitch_ = 0.0f; break;  // destra: dall'asse a destra della vista frontale
    case 3: yaw_ = 0.0f; pitch_ = 90.0f; break;   // superiore: dall'alto, il fondo in alto
    case 4: yaw_ = -32.0f; pitch_ = 22.0f; break;
    case 5: yaw_ = -45.0f; pitch_ = 12.0f; break;
    default: break;
    }
}

void ViewportCamera::setFrame(const QVector3D &worldX, const QVector3D &worldNormal) {
    orbitPivotValid_ = false;
    // Le direzioni del modello nello spazio della vista standard (orientamento degli assi).
    const QMatrix4x4 basis = basisMatrix();
    const QVector3D x = basis.mapVector(worldX), normal = basis.mapVector(worldNormal);
    const double degrees = 180.0 / M_PI;
    const double a = std::asin(qBound(-1.0, double(normal.y()), 1.0));
    const double b = std::atan2(-double(normal.x()), double(normal.z()));
    const QVector3D u1(float(std::cos(b)), 0.0f, float(std::sin(b)));
    const QVector3D u2(float(std::sin(a) * std::sin(b)), float(std::cos(a)), float(-std::sin(a) * std::cos(b)));
    pitch_ = float(a * degrees);
    yaw_ = float(b * degrees);
    roll_ = float(std::atan2(-double(QVector3D::dotProduct(x, u2)), double(QVector3D::dotProduct(x, u1))) * degrees);
}

float ViewportCamera::normalizedViewAngle(float degrees) {
    return std::remainder(degrees, 360.0f);
}

void ViewportCamera::orbitView(const QPoint &delta) {
    if (delta.isNull()) return;
    const QVector3D previousPivot = modelViewMatrix().map(orbitPivot_);
    yaw_ = normalizedViewAngle(yaw_ + delta.x()*kOrbitDegreesPerPixel);
    pitch_ = normalizedViewAngle(pitch_ + delta.y()*kOrbitDegreesPerPixel);
    roll_ *= 0.9f;
    // Compensa la traslazione: il punto scelto resta nello stesso pixel.
    // La proiezione ortografica non richiede una compensazione in Z.
    const QVector3D movedPivot = modelViewMatrix().map(orbitPivot_);
    panX_ += previousPivot.x() - movedPivot.x();
    panY_ += previousPivot.y() - movedPivot.y();
}

AxesOrientation ViewportCamera::orientationFromCurrentView() const {
    QMatrix4x4 rotation;
    rotation.rotate(roll_, 0.0f, 0.0f, 1.0f);
    rotation.rotate(pitch_, 1.0f, 0.0f, 0.0f);
    rotation.rotate(yaw_, 0.0f, 1.0f, 0.0f);
    rotation *= basisMatrix();
    // Righe della rotazione: le direzioni del modello a destra, in alto e verso l'osservatore.
    AxesOrientation result;
    double *rows[3] = {result.right, result.up, result.toward};
    for (int r = 0; r < 3; ++r) {
        double length = 0.0;
        for (int k = 0; k < 3; ++k) length += double(rotation(r, k)) * double(rotation(r, k));
        length = std::sqrt(length);
        for (int k = 0; k < 3; ++k) rows[r][k] = double(rotation(r, k)) / length;
    }
    return result;
}

QPointF ViewportCamera::projectWorldPoint(const QVector3D &point, const QSize &viewportSize) const {
    const std::array<double, 18> key = {double(viewportSize.width()), double(viewportSize.height()), zoom_, panX_, panY_, yaw_, pitch_, roll_,
        orientation_.right[0], orientation_.right[1], orientation_.right[2],
        orientation_.up[0], orientation_.up[1], orientation_.up[2],
        orientation_.toward[0], orientation_.toward[1], orientation_.toward[2], 0.0};
    if (key != projectionKey_) {
        projectionKey_ = key;
        projectionMatrix_.setToIdentity();
        projectionMatrix_.translate(panX_, panY_, -zoom_);
        projectionMatrix_.rotate(roll_, 0.0f, 0.0f, 1.0f);
        projectionMatrix_.rotate(pitch_, 1.0f, 0.0f, 0.0f);
        projectionMatrix_.rotate(yaw_, 0.0f, 1.0f, 0.0f);
        projectionMatrix_ *= basisMatrix();
    }
    const QVector3D cameraPoint = projectionMatrix_.map(point);
    const double pixels = double(qMax(1, viewportSize.height())) / zoom_;
    return QPointF(viewportSize.width() * 0.5 + cameraPoint.x() * pixels, viewportSize.height() * 0.5 - cameraPoint.y() * pixels);
}

void ViewportCamera::viewRay(const QPoint &position, const QSize &viewportSize, double depth, QVector3D &origin, QVector3D &direction) const {
    const float aspect = float(viewportSize.width()) / float(qMax(1, viewportSize.height()));
    const float viewScale = zoom_ / 8.0f;
    QMatrix4x4 rotation;
    rotation.rotate(roll_, 0.0f, 0.0f, 1.0f);
    rotation.rotate(pitch_, 1.0f, 0.0f, 0.0f);
    rotation.rotate(yaw_, 0.0f, 1.0f, 0.0f);
    rotation *= basisMatrix();
    const QMatrix4x4 inverse = rotation.inverted();
    origin = inverse.map(QVector3D(
        (float(position.x()) / float(qMax(1, viewportSize.width())) - 0.5f) * 8.0f * aspect * viewScale - panX_,
        (0.5f - float(position.y()) / float(qMax(1, viewportSize.height()))) * 8.0f * viewScale - panY_,
        float(depth)));
    direction = inverse.mapVector(QVector3D(0.0f, 0.0f, -1.0f));
}

float ViewportCamera::fitPoints(const QVector<QVector3D> &points, const QSize &viewportSize) {
    QMatrix4x4 rotation;
    rotation.rotate(roll_, 0.0f, 0.0f, 1.0f);
    rotation.rotate(pitch_, 1.0f, 0.0f, 0.0f);
    rotation.rotate(yaw_, 0.0f, 1.0f, 0.0f);
    rotation *= basisMatrix();
    QVector3D low = rotation.map(points.first()), high = low;
    for (const QVector3D &p : points) {
        const QVector3D v = rotation.map(p);
        low = QVector3D(qMin(low.x(), v.x()), qMin(low.y(), v.y()), 0.0f);
        high = QVector3D(qMax(high.x(), v.x()), qMax(high.y(), v.y()), 0.0f);
    }
    const float aspect = float(viewportSize.width()) / float(qMax(1, viewportSize.height()));
    const float halfWidth = 0.5f * (high.x() - low.x()), halfHeight = 0.5f * (high.y() - low.y());
    panX_ = -0.5f * (low.x() + high.x());
    panY_ = -0.5f * (low.y() + high.y());
    // Mezza altezza visibile = zoom_ / 2, mezza larghezza = aspect * zoom_ / 2.
    const float needed = 2.0f * 1.1f * qMax(halfHeight, halfWidth / aspect);
    return needed > 0.0f ? needed : zoom_;
}

}
