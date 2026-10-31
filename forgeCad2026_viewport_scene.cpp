#include "forgeCad2026_viewport_internal.h"
#include <QDebug>


QVector3D CadViewport::viewerDirection() const {
    QVector3D origin, direction;
    viewRay(QPoint(width() / 2, height() / 2), origin, direction);
    return -direction.normalized();
}

SketchFrame CadViewport::curveTrimView() const {
    QVector3D a, b, d;
    viewRay(QPoint(width()/2, height()/2), a, d);
    viewRay(QPoint(width()/2+100, height()/2), b, d);
    const QVector3D x=(b-a).normalized(), n=-d.normalized();
    SketchFrame frame;
    for (int k=0;k<3;++k) {frame.origin[k]=0;frame.xAxis[k]=x[k];frame.normal[k]=n[k];}
    return frame;
}

QMatrix4x4 CadViewport::basisMatrix() const {
    return camera_.basisMatrix();
}

QMatrix4x4 CadViewport::sceneProjectionMatrix() const {
    return camera_.projectionMatrix(size(), sceneDepth());
}

QMatrix4x4 CadViewport::sceneModelViewMatrix() const {
    return camera_.modelViewMatrix();
}

void CadViewport::setRenderMatrices(const QMatrix4x4 &projection, const QMatrix4x4 &modelView) {
    displayCache_.setMatrices(projection, modelView);
    overlayRenderer_.setMatrices(projection, modelView);
}

void CadViewport::setViewPreset(int preset) {
    stopWheelZoom();
    camera_.setPreset(preset);
    update();
}

void CadViewport::setViewFrame(const QVector3D &worldX, const QVector3D &worldNormal) {
    stopWheelZoom();
    camera_.setFrame(worldX, worldNormal);
    update();
}

void CadViewport::setViewNormal(int plane) {
    const SketchFrame frame = ForgeCad::referenceSketchFrame(plane, camera_.orientation_);
    setViewFrame(QVector3D(float(frame.xAxis[0]), float(frame.xAxis[1]), float(frame.xAxis[2])),
                 QVector3D(float(frame.normal[0]), float(frame.normal[1]), float(frame.normal[2])));
}

float CadViewport::normalizedViewAngle(float degrees) {
    return ForgeCad::Gui::ViewportCamera::normalizedViewAngle(degrees);
}

QVector3D CadViewport::orbitPointAt(const QPoint &position) const {
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    direction.normalize();
    double skipped = 0.0;
    const bool reaches = sectionRay(origin, direction, skipped);
    float nearest = std::numeric_limits<float>::max();
    if (reaches) for (int index=0;index<extrusions_.size();++index) {
        const auto &body=extrusions_.at(index);
        if (!isBodyDrawn(index) || !ForgeCad::isShapeBody(body)) continue;
        float distance = 0.0f;
        if (meshRayHit(body.display, origin, direction, distance) && distance < nearest
            && !sectionRemoves(origin + distance*direction)) nearest = distance;
    }
    if (nearest < std::numeric_limits<float>::max()) return origin + nearest*direction;
    // Sullo sfondo usa il piano parallelo allo schermo che passa per
    // il centro del box della scena, alla stessa posizione del mouse.
    updateSceneBounds();
    const QVector3D center = (sceneMin_ + sceneMax_)*0.5f;
    return origin + QVector3D::dotProduct(center-origin, direction)*direction;
}

void CadViewport::orbitView(const QPoint &delta) {
    if (delta.isNull()) return;
    if (!camera_.orbitPivotValid_) {
        camera_.orbitPivot_ = orbitPointAt(lastMousePosition_);
        camera_.orbitPivotValid_ = true;
    }
    camera_.orbitView(delta);
}

const AxesOrientation &CadViewport::orientation() const { return camera_.orientation_; }

void CadViewport::setOrientation(const AxesOrientation &orientation) {
    if(documentReadOnly_)return;
    camera_.orientation_ = orientation;
    setViewPreset(4);
    fitAll();
    update();
}

AxesOrientation CadViewport::orientationFromCurrentView() const {
    return camera_.orientationFromCurrentView();
}

double CadViewport::pickTolerance(double pixels ) const {
    return pixels * 8.0 * double(camera_.zoom_) / 8.0 / double(qMax(1, height()));
}

double CadViewport::distanceToSegment(const QPointF &point, const QPointF &first, const QPointF &second) {
    const QPointF line = second - first;
    const double lengthSquared = QPointF::dotProduct(line, line);
    const double parameter = lengthSquared > 0.0
        ? qBound(0.0, QPointF::dotProduct(point - first, line) / lengthSquared, 1.0) : 0.0;
    return pointDistance(point, first + line * parameter);
}

QPointF CadViewport::projectWorldPoint(const QVector3D &point) const {
    return camera_.projectWorldPoint(point, size());
}

QVector3D CadViewport::mapSketchPoint(const QPointF &point, const SketchObject &sketch) const {
    return ForgeCad::sketchToDisplay(point, sketch);
}

QVector3D CadViewport::mapActiveSketchPoint(const QPointF &point) const {
    if (activeSketch_ >= 0 && activeSketch_ < sketches_.size()) return mapSketchPoint(point, sketches_.at(activeSketch_));
    return ForgeCad::sketchToDisplay(point, activePlane_);
}

QPointF CadViewport::screenToSketchPoint(const QPoint &position) const {
    // Vista ruotata nello schizzo: il punto del piano dello schizzo lungo il raggio del puntatore.
    if (sketchViewRotated_ && activeSketchObject()) {
        QPointF point;
        if (rayToSketchPlane(position, point)) return point;
    }
    const double aspect = double(width()) / double(qMax(1, height()));
    const double viewScale = double(camera_.zoom_) / 8.0;
    return QPointF((double(position.x()) / double(qMax(1, width())) - 0.5) * 8.0 * aspect * viewScale - double(camera_.panX_),
                   (0.5 - double(position.y()) / double(qMax(1, height()))) * 8.0 * viewScale - double(camera_.panY_));
}

bool CadViewport::rayToSketchPlane(const QPoint &position, QPointF &point) const {
    const SketchObject *sketch = activeSketchObject();
    if (!sketch) return false;
    QVector3D origin, direction;
    viewRay(position, origin, direction);
    const ForgeCad::Kernel::Frame3 axes = ForgeCad::sketchAxes(*sketch);
    const ForgeCad::Kernel::Vec3 &o = axes.origin(), &n = axes.zDir();
    const double ox = double(origin.x()), oy = double(origin.y()), oz = double(origin.z());
    const double dx = double(direction.x()), dy = double(direction.y()), dz = double(direction.z());
    const double dn = dx * n.x() + dy * n.y() + dz * n.z();
    if (std::fabs(dn) < 1e-6 * std::sqrt(dx * dx + dy * dy + dz * dz)) return false;
    const double t = ((o.x() - ox) * n.x() + (o.y() - oy) * n.y() + (o.z() - oz) * n.z()) / dn;
    point = ForgeCad::worldToSketch(ForgeCad::Kernel::Vec3(ox + t * dx, oy + t * dy, oz + t * dz), *sketch);
    return true;
}

void CadViewport::viewRay(const QPoint &position, QVector3D &origin, QVector3D &direction) const {
    camera_.viewRay(position, size(), sceneDepth(), origin, direction);
}

QVector<QVector3D> CadViewport::planeCorners(int plane, float u, float v) {
    if (plane == 0) return {QVector3D(-u, -v, 0), QVector3D(u, -v, 0), QVector3D(u, v, 0), QVector3D(-u, v, 0)};
    if (plane == 1) return {QVector3D(-u, 0, -v), QVector3D(u, 0, -v), QVector3D(u, 0, v), QVector3D(-u, 0, v)};
    return {QVector3D(0, -u, -v), QVector3D(0, u, -v), QVector3D(0, u, v), QVector3D(0, -u, v)};
}

QVector<QVector3D> CadViewport::planeCorners(int plane, float h) { return planeCorners(plane, h, h); }

QSizeF CadViewport::referencePlaneExtents(int plane) const {
    updateSceneBounds();
    return referencePlaneAuto_[plane] * referencePlaneScales_[plane];
}

double CadViewport::referencePlaneHalf(int plane) const {
    const QSizeF size = referencePlaneExtents(plane);
    return qMax(size.width(), size.height());
}

QVector<QVector3D> CadViewport::planeCorners(int plane) const {
    const QSizeF size = referencePlaneExtents(plane);
    return planeCorners(plane, float(size.width()), float(size.height()));
}

double CadViewport::datumHalf(double size) const { return size > 0.0 ? size : 0.5 * double(planeHalf()); }

QVector<QVector3D> CadViewport::datumCorners(const SketchFrame &f, double size) const {
    const QVector3D c(float(f.origin[0]), float(f.origin[1]), float(f.origin[2]));
    const QVector3D x(float(f.xAxis[0]), float(f.xAxis[1]), float(f.xAxis[2]));
    const QVector3D n(float(f.normal[0]), float(f.normal[1]), float(f.normal[2]));
    const QVector3D y = QVector3D::crossProduct(n, x);
    const float h = float(datumHalf(size));
    return {c - h * x - h * y, c + h * x - h * y, c + h * x + h * y, c - h * x + h * y};
}

QVector<QVector3D> CadViewport::sketchGeometryPoints(const SketchObject &sketch) const {
    QVector<QVector3D> points;
    for (int index = 0; index < sketch.segments.size(); ++index) {
        if (sketch.isConstructionSegment(index) && isLinkedSketchEntity(sketch, 0, index)) continue;
        const auto &segment = sketch.segments[index];
        points.append(mapSketchPoint(segment.first, sketch));
        points.append(mapSketchPoint(segment.second, sketch));
    }
    for (int index = 0; index < sketch.curves.size(); ++index) {
        const auto &curve = sketch.curves[index];
        if (curve.construction && isLinkedSketchEntity(sketch, 1, index)) continue;
        for (const QPointF &p : curve.samples) points.append(mapSketchPoint(p, sketch));
        if (curve.samples.isEmpty())
            for (const QPointF &p : curve.controlPoints) points.append(mapSketchPoint(p, sketch));
    }
    return points;
}

QVector<QVector3D> CadViewport::sceneGeometryPoints(bool planesIfEmpty , bool datumSizes ) const {
    QVector<QVector3D> points;
    for (int index=0;index<extrusions_.size();++index) {
        const auto &body=extrusions_.at(index);
        if (!isBodyDrawn(index)) continue;
        for (const QVector<QVector3D> &edge : body.display.edges) points += edge;
        if (body.display.edges.isEmpty()) points += body.display.vertices;
    }
    for (int index = 0; index < sketches_.size(); ++index)
        if (isSketchDrawn(index)) points += sketchGeometryPoints(sketches_.at(index));
    // Piani di costruzione: il centro (la misura automatica dipende dalla scena), gli angoli se la misura e' data.
    for (int index=0;index<extrusions_.size();++index) {
        const auto &body=extrusions_.at(index);
        if (!ForgeCad::isDatumBody(body) || !isBodyDrawn(index) || !body.datumValid) continue;
        points.append(QVector3D(float(body.datumFrame.origin[0]), float(body.datumFrame.origin[1]), float(body.datumFrame.origin[2])));
        if (datumSizes && body.datum.size > 0.0) points += datumCorners(body.datumFrame, body.datum.size);
    }
    if (points.isEmpty() && planesIfEmpty)
        for (int plane = 0; plane < 3; ++plane)
            points += planeCorners(plane, kDefaultPlaneHalf * float(referencePlaneScales_[plane]),
                                   kDefaultPlaneHalf * float(referencePlaneScales_[plane]));
    return points;
}

void CadViewport::updateSceneBounds() const {
    if (!sceneBoundsDirty_) return;
    // Le dimensioni grafiche dei datum non devono ingrandire a cascata i
    // piani standard: per questi conta la geometria e la posizione dei datum.
    const QVector<QVector3D> sizingPoints = sceneGeometryPoints(false, false);
    QVector<QVector3D> points = sceneGeometryPoints(false, true);
    if (sizingPoints.isEmpty()) {
        planeHalf_ = kDefaultPlaneHalf;
        for (int plane = 0; plane < 3; ++plane)
            referencePlaneAuto_[plane] = QSizeF(kDefaultPlaneHalf, kDefaultPlaneHalf);
        if (points.isEmpty())
            for (int plane = 0; plane < 3; ++plane)
                points += planeCorners(plane, kDefaultPlaneHalf * float(referencePlaneScales_[plane]));
    } else {
        float reach[3] = {};
        for (const QVector3D &p : sizingPoints) {
            reach[0] = qMax(reach[0], qAbs(p.x()));
            reach[1] = qMax(reach[1], qAbs(p.y()));
            reach[2] = qMax(reach[2], qAbs(p.z()));
        }
        const float commonExtent = niceCeiling(qMax(1.2f * qMax(reach[0], qMax(reach[1], reach[2])), 1e-3f));
        for (QSizeF &size : referencePlaneAuto_) size = QSizeF(commonExtent, commonExtent);
        planeHalf_ = commonExtent;
    }
    sceneMin_ = sceneMax_ = points.first();
    for (const QVector3D &p : points) {
        sceneMin_ = QVector3D(qMin(sceneMin_.x(), p.x()), qMin(sceneMin_.y(), p.y()), qMin(sceneMin_.z(), p.z()));
        sceneMax_ = QVector3D(qMax(sceneMax_.x(), p.x()), qMax(sceneMax_.y(), p.y()), qMax(sceneMax_.z(), p.z()));
    }
    sceneBoundsDirty_ = false;
}

float CadViewport::niceCeiling(float x) {
    const float power = std::pow(10.0f, std::floor(std::log10(x)));
    for (float step : {1.0f, 2.0f, 2.5f, 5.0f, 10.0f})
        if (step * power >= x * 0.9999f) return step * power;
    return 10.0f * power;
}

float CadViewport::planeHalf() const {
    updateSceneBounds();
    return planeHalf_;
}

double CadViewport::sceneDepth() const {
    updateSceneBounds();
    double reach = qMax(10.0, 2.5 * double(planeHalf_));
    reach = qMax(reach, axisDisplayLength() * 1.2);
    for (int plane = 0; plane < 3; ++plane) {
        const QSizeF size = referencePlaneExtents(plane);
        reach = qMax(reach, 2.5 * qMax(size.width(), size.height()));
    }
    for (const QVector3D &corner : {sceneMin_, sceneMax_})
        reach = qMax(reach, double(qMax(qAbs(corner.x()), qMax(qAbs(corner.y()), qAbs(corner.z())))));
    return 1.8 * reach + 30.0;
}

void CadViewport::zoomLimits(float &minimum, float &maximum) const {
    updateSceneBounds();
    const float aspect = float(width()) / float(qMax(1, height()));
    const float radius = qMax(0.5f * (sceneMax_ - sceneMin_).length(), 1e-3f);
    const float fit = 2.2f * radius / qMin(1.0f, aspect);
    minimum = fit / 5000.0f;
    maximum = 4.0f * fit;
}

void CadViewport::setZoom(float zoom) {
    stopWheelZoom();
    float minimum = 0.0f, maximum = 0.0f;
    zoomLimits(minimum, maximum);
    camera_.zoom_ = qBound(minimum, zoom, maximum);
    update();
}

void CadViewport::stopWheelZoom() { wheelZoom_.stop(); }

void CadViewport::applyWheelZoom(double zoom, const QPointF &anchor) {
    float minimum = 0.0f, maximum = 0.0f;
    zoomLimits(minimum, maximum);
    float next = float(qBound(double(minimum), zoom, double(maximum)));
    // Soltanto arrotondamento della camera float, senza tolleranze geometriche.
    constexpr float boundaryRoundoff = 8 * std::numeric_limits<float>::epsilon();
    if (next <= minimum * (1 + boundaryRoundoff)) next = minimum;
    if (next >= maximum * (1 - boundaryRoundoff)) next = maximum;
    // Nella camera ortografica basta compensare il pan in coordinate vista:
    // il punto del raggio sotto il cursore rimane nello stesso pixel a ogni
    // profondita'. Usa il delta effettivo dopo il clamp, anche ai fine corsa.
    const double unit = (double(next) - camera_.zoom_) / qMax(1, height());
    camera_.panX_ += float((anchor.x() - width() * 0.5) * unit);
    camera_.panY_ += float((height() * 0.5 - anchor.y()) * unit);
    camera_.zoom_ = next;
    update();
}

void CadViewport::advanceWheelZoom(double seconds) { wheelZoom_.advance(seconds); }

void CadViewport::fitView(const QVector<QVector3D> &points) {
    if (points.isEmpty()) return;
    // Prima del primo disegno il widget non ha ancora la sua dimensione:
    // l'inquadratura si ripete al primo paintGL.
    if (!painted_) pendingFit_ = points;
    setZoom(camera_.fitPoints(points, size()));
}

const QColor &CadViewport::axisColor(int axis) {
    static const QColor colors[3] = {QColor(235, 70, 70), QColor(80, 215, 95), QColor(70, 135, 255)};
    return colors[qBound(0, axis, 2)];
}
