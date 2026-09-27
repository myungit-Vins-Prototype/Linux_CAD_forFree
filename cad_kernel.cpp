#include "cad_kernel.h"

#include <algorithm>
#include <cmath>

namespace ForgeCad {
namespace {

using Kernel::Frame3;
using Kernel::Vec3;

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

QVector3D toDisplay(const Vec3 &v) { return QVector3D(float(v.x()), float(v.y()), float(v.z())); }

}

Frame3 sketchAxes(int plane) {
    if (plane == 1) return Frame3(Vec3(0, 0, 0), Vec3(0, -1, 0), Vec3(1, 0, 0));
    if (plane == 2) return Frame3(Vec3(0, 0, 0), Vec3(-1, 0, 0), Vec3(0, 0, 1));
    return Frame3(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
}

Vec3 sketchToWorld(const QPointF &point, int plane) { return sketchAxes(plane).toGlobal(Vec3(point.x(), point.y(), 0.0)); }

QVector3D sketchToDisplay(const QPointF &point, int plane) { return toDisplay(sketchToWorld(point, plane)); }

QPointF worldToSketch(const Vec3 &point, int plane) {
    const Vec3 local = sketchAxes(plane).toLocal(point);
    return QPointF(local.x(), local.y());
}

Vec3 extrusionVector(int plane, double distance) {
    if (plane == 1) return Vec3(0.0, distance, 0.0);
    if (plane == 2) return Vec3(distance, 0.0, 0.0);
    return Vec3(0.0, 0.0, distance);
}

Frame3 sketchAxes(const SketchObject &sketch) {
    if (sketch.plane != kFacePlane && !sketch.customFrame) return sketchAxes(sketch.plane);
    const SketchFrame &f = sketch.frame;
    return Frame3(Vec3(f.origin[0], f.origin[1], f.origin[2]), Vec3(f.normal[0], f.normal[1], f.normal[2]), Vec3(f.xAxis[0], f.xAxis[1], f.xAxis[2]));
}

Vec3 sketchToWorld(const QPointF &point, const SketchObject &sketch) { return sketchAxes(sketch).toGlobal(Vec3(point.x(), point.y(), 0.0)); }

QVector3D sketchToDisplay(const QPointF &point, const SketchObject &sketch) { return toDisplay(sketchToWorld(point, sketch)); }

QPointF worldToSketch(const Vec3 &point, const SketchObject &sketch) {
    const Vec3 local = sketchAxes(sketch).toLocal(point);
    return QPointF(local.x(), local.y());
}

Vec3 extrusionVector(const SketchObject &sketch, double distance) {
    if (sketch.plane != kFacePlane && !sketch.customFrame) return extrusionVector(sketch.plane, distance);
    return sketchAxes(sketch).zDir() * distance;
}

SketchFrame referenceSketchFrame(int plane, const AxesOrientation &o) {
    const Vec3 right(o.right[0], o.right[1], o.right[2]), up(o.up[0], o.up[1], o.up[2]), toward(o.toward[0], o.toward[1], o.toward[2]);
    const Vec3 axis = plane == 0 ? Vec3(0, 0, 1) : plane == 1 ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
    // In quale vista standard il piano si vede di fronte: frontale (verso
    // l'osservatore), superiore (in alto) o destra; la normale guarda chi osserva.
    const double c[3] = {dot(axis, right), dot(axis, up), dot(axis, toward)};
    int k = 0;
    for (int i = 1; i < 3; ++i)
        if (std::abs(c[i]) > std::abs(c[k])) k = i;
    const Vec3 normal = axis * (c[k] < 0.0 ? -1.0 : 1.0);
    Vec3 screenUp = k == 1 ? -toward : up;  // nella vista superiore in alto c'e' il fondo
    screenUp = screenUp - normal * dot(screenUp, normal);
    if (norm(screenUp) < 1e-9) screenUp = cross(right, normal);
    screenUp = normalized(screenUp);
    const Vec3 x = cross(screenUp, normal);
    SketchFrame frame;
    for (int i = 0; i < 3; ++i) {
        frame.origin[i] = 0.0;
        frame.xAxis[i] = x[i];
        frame.normal[i] = normal[i];
    }
    return frame;
}

SketchFrame faceSketchFrame(const Vec3 &point, const Vec3 &normal, const Vec3 &upDirection) {
    const Vec3 n = normalized(normal);
    // Origine: proiezione dell'origine del modello sul piano.
    const Vec3 origin = n * dot(point, n);
    Vec3 up = upDirection - n * dot(n, upDirection);
    if (norm(up) < 1e-9) up = Vec3(0, 1, 0) - n * n.y();
    if (norm(up) < 1e-9) up = Vec3(1, 0, 0) - n * n.x();
    up = normalized(up);
    const Vec3 x = cross(up, n);
    SketchFrame frame;
    for (int k = 0; k < 3; ++k) {
        frame.origin[k] = origin[k];
        frame.xAxis[k] = x[k];
        frame.normal[k] = n[k];
    }
    return frame;
}

bool sketchRevolutionAxis(const SketchObject &sketch, int axis, QPointF &point, QPointF &direction, QString *error) {
    if (axis == -1 || axis == -2) {
        point = QPointF(0.0, 0.0);
        direction = axis == -1 ? QPointF(1.0, 0.0) : QPointF(0.0, 1.0);
        return true;
    }
    if (axis < 0 || axis >= sketch.segments.size()) {
        setError(error, QStringLiteral("L'asse di rivoluzione non esiste piu' nello schizzo."));
        return false;
    }
    const SketchSegment &segment = sketch.segments.at(axis);
    const QPointF delta = segment.second - segment.first;
    const double length = std::hypot(delta.x(), delta.y());
    if (length <= kSketchConnectionTolerance) {
        setError(error, QStringLiteral("L'asse di rivoluzione ha lunghezza nulla."));
        return false;
    }
    point = segment.first;
    direction = delta / length;
    return true;
}

int revolutionProfileSide(const SketchObject &sketch, const QPointF &point, const QPointF &direction, QString *error) {
    double lowest = 0.0, highest = 0.0;
    const auto consider = [&](const QPointF &q) {
        const QPointF r = q - point;
        const double side = direction.x() * r.y() - direction.y() * r.x();
        lowest = std::min(lowest, side);
        highest = std::max(highest, side);
    };
    for (int index = 0; index < sketch.segments.size(); ++index) {
        if (sketch.isConstructionSegment(index)) continue;
        consider(sketch.segments.at(index).first);
        consider(sketch.segments.at(index).second);
    }
    // I campioni di visualizzazione servono solo a decidere il lato (la
    // geometria resta quella esatta).
    for (const CurveObject &curve : sketch.curves) {
        if (curve.construction) continue;
        for (const QPointF &sample : curve.samples) consider(sample);
    }
    const double tolerance = kSketchConnectionTolerance;
    if (lowest < -tolerance && highest > tolerance) {
        setError(error, QStringLiteral("Il profilo attraversa l'asse di rivoluzione: deve stare tutto da una parte."));
        return 0;
    }
    if (lowest >= -tolerance && highest <= tolerance) {
        setError(error, QStringLiteral("Il profilo giace sull'asse di rivoluzione."));
        return 0;
    }
    return highest > tolerance ? 1 : -1;
}

Frame3 primitiveAxes(const PrimitiveParameters &parameters) {
    const Frame3 axes = sketchAxes(parameters.plane);
    return Frame3(Vec3(parameters.origin[0], parameters.origin[1], parameters.origin[2]), axes.zDir(), axes.xDir());
}

QString primitiveError(const PrimitiveParameters &parameters) {
    const double *size = parameters.size;
    const double confusion = 1.0e-7;
    switch (parameters.kind) {
    case PrimitiveKind::Box:
        if (size[0] <= confusion || size[1] <= confusion || size[2] <= confusion)
            return QStringLiteral("Le dimensioni del parallelepipedo devono essere positive.");
        break;
    case PrimitiveKind::Cylinder:
        if (size[0] <= confusion || size[1] <= confusion) return QStringLiteral("Raggio e altezza del cilindro devono essere positivi.");
        break;
    case PrimitiveKind::Sphere:
        if (size[0] <= confusion) return QStringLiteral("Il raggio della sfera deve essere positivo.");
        break;
    case PrimitiveKind::Cone:
        if (size[2] <= confusion || size[0] < 0.0 || size[1] < 0.0 || std::max(size[0], size[1]) <= confusion)
            return QStringLiteral("Il cono richiede un'altezza positiva e almeno un raggio positivo.");
        if (std::abs(size[0] - size[1]) <= confusion) return QStringLiteral("Con i due raggi uguali usa il cilindro.");
        break;
    case PrimitiveKind::Torus:
        if (size[1] <= confusion || size[0] <= size[1] + confusion)
            return QStringLiteral("Il raggio minore del toro deve essere positivo e minore del maggiore.");
        break;
    }
    return {};
}

bool chamferDistances(const ChamferSpec &spec, double size, const double normal0[3], const double normal1[3], bool &firstIsReference,
                      double &onReference, double &onOther, QString *error) {
    // Faccia di riferimento: normale piu' verso +Z, poi +X, poi +Y.
    firstIsReference = true;
    for (int axis : {2, 0, 1}) {
        if (std::fabs(normal0[axis] - normal1[axis]) <= 1e-9) continue;
        firstIsReference = normal0[axis] > normal1[axis];
        break;
    }
    if (spec.flip) firstIsReference = !firstIsReference;
    onReference = size;
    onOther = spec.mode == 1 ? spec.second : size;
    if (spec.mode == 2) {
        // Triangolo della sezione: angolo phi tra le facce (quello minore, dove sta lo
        // smusso), theta tra lo smusso e la faccia di riferimento: d2 = d sin(theta) / sin(phi + theta).
        const double cosine = std::clamp(normal0[0] * normal1[0] + normal0[1] * normal1[1] + normal0[2] * normal1[2], -1.0, 1.0);
        const double phi = M_PI - std::acos(cosine), theta = spec.second * M_PI / 180.0;
        if (!(theta > 0.0) || !(phi + theta < M_PI - 1e-9)) {
            setError(error, QStringLiteral("Angolo dello smusso non valido per l'angolo tra le facce (%1 gradi).").arg(phi * 180.0 / M_PI, 0, 'f', 2));
            return false;
        }
        onOther = size * std::sin(theta) / std::sin(phi + theta);
    }
    if (!(onOther > 1.0e-7)) {
        setError(error, QStringLiteral("La seconda distanza dello smusso deve essere positiva."));
        return false;
    }
    return true;
}

}
