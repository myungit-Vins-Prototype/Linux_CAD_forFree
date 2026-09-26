#include "cad_constraints.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <functional>

namespace ForgeCad {
namespace {

constexpr double kPi = 3.14159265358979323846;

double length(const QPointF &p) { return std::hypot(p.x(), p.y()); }
double cross(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }
double dot(const QPointF &a, const QPointF &b) { return a.x() * b.x() + a.y() * b.y(); }

// Forma geometrica di un riferimento.
enum class Shape { None, Point, Line, Circle, Ellipse, Curve };

bool centered(DrawingTool tool) {
    return tool == DrawingTool::Circle || tool == DrawingTool::Arc || tool == DrawingTool::Polygon || tool == DrawingTool::Ellipse;
}

Shape shapeOf(const SketchObject &sketch, const ConstraintRef &ref) {
    if (ref.kind == 2) return ref.element == 0 ? Shape::Point : (ref.element == 1 || ref.element == 2) ? Shape::Line : Shape::None;
    if (ref.kind == 0) {
        if (ref.element < 0 || ref.element >= sketch.segments.size()) return Shape::None;
        return ref.point < 0 ? Shape::Line : ref.point <= 1 ? Shape::Point : Shape::None;
    }
    if (ref.kind != 1 || ref.element < 0 || ref.element >= sketch.curves.size()) return Shape::None;
    const CurveObject &curve = sketch.curves.at(ref.element);
    if (ref.point >= 0) return ref.point < curve.controlPoints.size() ? Shape::Point : Shape::None;
    switch (curve.tool) {
    case DrawingTool::Circle:
    case DrawingTool::Arc:
    case DrawingTool::Polygon: return curve.controlPoints.size() >= 2 ? Shape::Circle : Shape::None;
    case DrawingTool::Ellipse: return curve.controlPoints.size() >= 3 ? Shape::Ellipse : Shape::None;
    default: return Shape::Curve;
    }
}

// Coordinate dei punti dello schizzo in un vettore (x0, y0, x1, y1...):
// prima gli estremi dei segmenti, poi i punti di controllo delle curve.
class System {
public:
    explicit System(const SketchObject &sketch) : sketch_(sketch) {
        for (const SketchSegment &segment : sketch.segments) {
            segmentBase_.append(int(x_.size()) / 2);
            x_ << segment.first.x() << segment.first.y() << segment.second.x() << segment.second.y();
        }
        for (const CurveObject &curve : sketch.curves) {
            curveBase_.append(int(x_.size()) / 2);
            for (const QPointF &p : curve.controlPoints) x_ << p.x() << p.y();
        }
    }
    QVector<double> &values() { return x_; }
    const QVector<double> &values() const { return x_; }
    int pointCount() const { return int(x_.size()) / 2; }
    QPointF point(int index) const { return {x_.at(2 * index), x_.at(2 * index + 1)}; }

    // Indice del punto (-1: l'origine o un riferimento non valido).
    int pointIndex(const ConstraintRef &ref) const {
        if (ref.point < 0) return -1;
        if (ref.kind == 0 && ref.element >= 0 && ref.element < segmentBase_.size() && ref.point <= 1) return segmentBase_.at(ref.element) + ref.point;
        if (ref.kind == 1 && ref.element >= 0 && ref.element < curveBase_.size() && ref.point < sketch_.curves.at(ref.element).controlPoints.size())
            return curveBase_.at(ref.element) + ref.point;
        return -1;
    }
    QPointF refPoint(const ConstraintRef &ref) const {
        const int index = pointIndex(ref);
        return index >= 0 ? point(index) : QPointF(0.0, 0.0);
    }
    int curvePoint(int curve, int k) const { return curveBase_.at(curve) + k; }
    // Retta: segmento o asse del piano.
    void line(const ConstraintRef &ref, QPointF &a, QPointF &b) const {
        if (ref.kind == 2) {
            a = QPointF(0.0, 0.0);
            b = ref.element == 1 ? QPointF(1.0, 0.0) : QPointF(0.0, 1.0);
            return;
        }
        a = point(segmentBase_.at(ref.element));
        b = point(segmentBase_.at(ref.element) + 1);
    }
    void circle(const ConstraintRef &ref, QPointF &center, double &radius) const {
        center = point(curvePoint(ref.element, 0));
        radius = length(point(curvePoint(ref.element, 1)) - center);
    }
    void ellipse(const ConstraintRef &ref, QPointF &center, QPointF &u, double &a, double &b) const {
        center = point(curvePoint(ref.element, 0));
        const QPointF major = point(curvePoint(ref.element, 1)) - center;
        a = length(major);
        b = length(point(curvePoint(ref.element, 2)) - center);
        u = a > 0.0 ? major / a : QPointF(1.0, 0.0);
    }
    // Punti da cui dipende il riferimento.
    void dependencies(const ConstraintRef &ref, QVector<int> &out) const {
        if (ref.kind == 0 && ref.element >= 0 && ref.element < segmentBase_.size()) {
            if (ref.point >= 0) out.append(segmentBase_.at(ref.element) + ref.point);
            else out << segmentBase_.at(ref.element) << segmentBase_.at(ref.element) + 1;
        } else if (ref.kind == 1 && ref.element >= 0 && ref.element < curveBase_.size()) {
            if (ref.point >= 0) {
                out.append(curveBase_.at(ref.element) + ref.point);
            } else {
                for (int k = 0; k < sketch_.curves.at(ref.element).controlPoints.size(); ++k) out.append(curveBase_.at(ref.element) + k);
            }
        }
    }
    const SketchObject &sketch() const { return sketch_; }

private:
    const SketchObject &sketch_;
    QVector<double> x_;
    QVector<int> segmentBase_, curveBase_;
};

double wrapAngle(double angle) {
    while (angle > kPi) angle -= 2.0 * kPi;
    while (angle < -kPi) angle += 2.0 * kPi;
    return angle;
}

double pointLineDistance(const QPointF &p, const QPointF &a, const QPointF &b) {
    const double l = length(b - a);
    return l > 0.0 ? cross(b - a, p - a) / l : length(p - a);
}

// Riferimenti ordinati: i punti prima (il vincolo li tratta in quest'ordine).
QVector<ConstraintRef> ordered(const SketchObject &sketch, QVector<ConstraintRef> refs) {
    std::stable_sort(refs.begin(), refs.end(), [&](const ConstraintRef &a, const ConstraintRef &b) {
        return int(shapeOf(sketch, a) == Shape::Point) > int(shapeOf(sketch, b) == Shape::Point);
    });
    return refs;
}

// Equazioni del vincolo (lunghezze) nelle coordinate del sistema.
void equations(const System &s, const SketchConstraint &c, QVector<double> &out) {
    const SketchObject &sketch = s.sketch();
    const Shape a = shapeOf(sketch, c.first), b = shapeOf(sketch, c.second);
    const auto lineOf = [&](const ConstraintRef &ref, QPointF &p, QPointF &q) { s.line(ref, p, q); };
    switch (c.type) {
    case ConstraintType::Coincident: {
        const QPointF d = s.refPoint(c.first) - s.refPoint(c.second);
        out << d.x() << d.y();
        return;
    }
    case ConstraintType::Horizontal:
    case ConstraintType::Vertical: {
        QPointF p, q;
        if (a == Shape::Line) lineOf(c.first, p, q);
        else {
            p = s.refPoint(c.first);
            q = s.refPoint(c.second);
        }
        out << (c.type == ConstraintType::Horizontal ? q.y() - p.y() : q.x() - p.x());
        return;
    }
    case ConstraintType::Parallel:
    case ConstraintType::Perpendicular:
    case ConstraintType::Collinear: {
        QPointF p0, p1, q0, q1;
        lineOf(c.first, p0, p1);
        lineOf(c.second, q0, q1);
        const QPointF d = p1 - p0, e = q1 - q0;
        const double le = std::max(length(e), 1e-300), ld = std::max(length(d), 1e-300);
        if (c.type == ConstraintType::Perpendicular) {
            out << dot(d, e) / le;
            return;
        }
        if (c.type == ConstraintType::Parallel) {
            out << cross(d, e) / le;
            return;
        }
        out << cross(d, q0 - p0) / ld << cross(d, q1 - p0) / ld;
        return;
    }
    case ConstraintType::Equal: {
        if (a == Shape::Line) {
            QPointF p0, p1, q0, q1;
            lineOf(c.first, p0, p1);
            lineOf(c.second, q0, q1);
            out << length(p1 - p0) - length(q1 - q0);
        } else {
            QPointF c1, c2;
            double r1, r2;
            s.circle(c.first, c1, r1);
            s.circle(c.second, c2, r2);
            out << r1 - r2;
        }
        return;
    }
    case ConstraintType::Concentric: {
        const QPointF d = s.point(s.curvePoint(c.first.element, 0)) - s.point(s.curvePoint(c.second.element, 0));
        out << d.x() << d.y();
        return;
    }
    case ConstraintType::Midpoint: {
        QPointF p0, p1;
        lineOf(c.second, p0, p1);
        const QPointF d = s.refPoint(c.first) - 0.5 * (p0 + p1);
        out << d.x() << d.y();
        return;
    }
    case ConstraintType::PointOnCurve: {
        const QPointF p = s.refPoint(c.first);
        if (b == Shape::Line) {
            QPointF q0, q1;
            lineOf(c.second, q0, q1);
            out << pointLineDistance(p, q0, q1);
        } else if (b == Shape::Circle) {
            QPointF center;
            double r;
            s.circle(c.second, center, r);
            out << length(p - center) - r;
        } else if (b == Shape::Ellipse) {
            QPointF center, u;
            double ea, eb;
            s.ellipse(c.second, center, u, ea, eb);
            const QPointF r = p - center;
            const double x = dot(r, u), y = cross(u, r);
            if (ea > 0.0 && eb > 0.0) out << (std::hypot(x / ea, y / eb) - 1.0) * std::min(ea, eb);
            else out << length(r);
        }
        return;
    }
    case ConstraintType::Tangent: {
        if (a == Shape::Line || b == Shape::Line) {
            const ConstraintRef &lineRef = a == Shape::Line ? c.first : c.second, &circleRef = a == Shape::Line ? c.second : c.first;
            QPointF q0, q1, center;
            double r;
            lineOf(lineRef, q0, q1);
            s.circle(circleRef, center, r);
            out << std::fabs(pointLineDistance(center, q0, q1)) - r;
        } else {
            QPointF c1, c2;
            double r1, r2;
            s.circle(c.first, c1, r1);
            s.circle(c.second, c2, r2);
            const double d = length(c1 - c2);
            out << (c.value > 0.5 ? d - std::fabs(r1 - r2) : d - (r1 + r2));
        }
        return;
    }
    case ConstraintType::Fix: {
        QVector<int> points;
        s.dependencies(c.first, points);
        for (int k = 0; k < points.size() && k < c.positions.size(); ++k) {
            const QPointF d = s.point(points.at(k)) - c.positions.at(k);
            out << d.x() << d.y();
        }
        return;
    }
    case ConstraintType::Distance: {
        if (b == Shape::None) {  // lunghezza del segmento
            QPointF p0, p1;
            lineOf(c.first, p0, p1);
            out << length(p1 - p0) - c.value;
        } else if (a == Shape::Point && b == Shape::Point) {
            out << length(s.refPoint(c.first) - s.refPoint(c.second)) - c.value;
        } else if (a == Shape::Point && b == Shape::Line) {
            QPointF q0, q1;
            lineOf(c.second, q0, q1);
            out << std::fabs(pointLineDistance(s.refPoint(c.first), q0, q1)) - c.value;
        } else {  // due rette: distanza del primo estremo della seconda dalla prima
            QPointF p0, p1, q0, q1;
            lineOf(c.first, p0, p1);
            lineOf(c.second, q0, q1);
            out << std::fabs(pointLineDistance(q0, p0, p1)) - c.value;
        }
        return;
    }
    case ConstraintType::Angle: {
        QPointF p0, p1, q0, q1;
        lineOf(c.first, p0, p1);
        lineOf(c.second, q0, q1);
        const QPointF d = p1 - p0, e = q1 - q0;
        const double angle = std::atan2(cross(d, e), dot(d, e));
        const double scale = 0.5 * ((c.first.kind == 2 ? 0.0 : length(d)) + (c.second.kind == 2 ? 0.0 : length(e)));
        out << wrapAngle(angle - c.value * kPi / 180.0) * std::max(scale, 1e-3);
        return;
    }
    case ConstraintType::Radius:
    case ConstraintType::Diameter: {
        QPointF center;
        double r;
        s.circle(c.first, center, r);
        out << (c.type == ConstraintType::Radius ? r : 2.0 * r) - c.value;
        return;
    }
    }
}

// Il vincolo e' ben formato per lo schizzo (riferimenti validi e delle forme giuste).
bool wellFormed(const SketchObject &sketch, const SketchConstraint &c) {
    const Shape a = shapeOf(sketch, c.first), b = shapeOf(sketch, c.second);
    if (a == Shape::None) return false;
    if (c.second.kind >= 0 && b == Shape::None) return false;
    const QVector<ConstraintType> allowed = applicableConstraints(sketch, c.second.kind >= 0 ? QVector<ConstraintRef>{c.first, c.second}
                                                                                          : QVector<ConstraintRef>{c.first});
    return allowed.contains(c.type);
}

// Equazioni implicite delle curve.
void implicitEquations(const System &s, int curve, QVector<double> &out) {
    const CurveObject &object = s.sketch().curves.at(curve);
    if (object.tool == DrawingTool::Arc && object.controlPoints.size() >= 3) {
        const QPointF c = s.point(s.curvePoint(curve, 0));
        out << length(s.point(s.curvePoint(curve, 2)) - c) - length(s.point(s.curvePoint(curve, 1)) - c);
    } else if (object.tool == DrawingTool::Ellipse && object.controlPoints.size() >= 3) {
        const QPointF c = s.point(s.curvePoint(curve, 0)), u = s.point(s.curvePoint(curve, 1)) - c;
        out << dot(u, s.point(s.curvePoint(curve, 2)) - c) / std::max(length(u), 1e-300);
    }
}

void implicitDependencies(const System &s, int curve, QVector<int> &out) {
    const CurveObject &object = s.sketch().curves.at(curve);
    if ((object.tool == DrawingTool::Arc || object.tool == DrawingTool::Ellipse) && object.controlPoints.size() >= 3)
        out << s.curvePoint(curve, 0) << s.curvePoint(curve, 1) << s.curvePoint(curve, 2);
}

QString curveName(const CurveObject &curve) {
    switch (curve.tool) {
    case DrawingTool::Circle: return QStringLiteral("Cerchio");
    case DrawingTool::Arc: return QStringLiteral("Arco");
    case DrawingTool::Polygon: return QStringLiteral("Poligono");
    case DrawingTool::Ellipse: return QStringLiteral("Ellisse");
    case DrawingTool::Nurbs: return QStringLiteral("NURBS");
    default: return QStringLiteral("Spline");
    }
}

}

QString constraintName(ConstraintType type) {
    switch (type) {
    case ConstraintType::Coincident: return QStringLiteral("Coincidente");
    case ConstraintType::Horizontal: return QStringLiteral("Orizzontale");
    case ConstraintType::Vertical: return QStringLiteral("Verticale");
    case ConstraintType::Parallel: return QStringLiteral("Parallelo");
    case ConstraintType::Perpendicular: return QStringLiteral("Perpendicolare");
    case ConstraintType::Collinear: return QStringLiteral("Collineare");
    case ConstraintType::Tangent: return QStringLiteral("Tangente");
    case ConstraintType::Equal: return QStringLiteral("Uguale");
    case ConstraintType::Concentric: return QStringLiteral("Concentrico");
    case ConstraintType::Midpoint: return QStringLiteral("Punto medio");
    case ConstraintType::PointOnCurve: return QStringLiteral("Punto sull'entita'");
    case ConstraintType::Fix: return QStringLiteral("Fisso");
    case ConstraintType::Distance: return QStringLiteral("Distanza");
    case ConstraintType::Angle: return QStringLiteral("Angolo");
    case ConstraintType::Radius: return QStringLiteral("Raggio");
    case ConstraintType::Diameter: return QStringLiteral("Diametro");
    }
    return {};
}

QString constraintSymbol(ConstraintType type) {
    switch (type) {
    case ConstraintType::Coincident: return QStringLiteral("●");
    case ConstraintType::Horizontal: return QStringLiteral("H");
    case ConstraintType::Vertical: return QStringLiteral("V");
    case ConstraintType::Parallel: return QStringLiteral("∥");
    case ConstraintType::Perpendicular: return QStringLiteral("⟂");
    case ConstraintType::Collinear: return QStringLiteral("≡");
    case ConstraintType::Tangent: return QStringLiteral("T");
    case ConstraintType::Equal: return QStringLiteral("=");
    case ConstraintType::Concentric: return QStringLiteral("◎");
    case ConstraintType::Midpoint: return QStringLiteral("M");
    case ConstraintType::PointOnCurve: return QStringLiteral("∈");
    case ConstraintType::Fix: return QStringLiteral("⚓");
    case ConstraintType::Distance: return QStringLiteral("↔");
    case ConstraintType::Angle: return QStringLiteral("∠");
    case ConstraintType::Radius: return QStringLiteral("R");
    case ConstraintType::Diameter: return QStringLiteral("⌀");
    }
    return {};
}

bool isDimension(ConstraintType type) {
    return type == ConstraintType::Distance || type == ConstraintType::Angle || type == ConstraintType::Radius || type == ConstraintType::Diameter;
}

QString describeRef(const SketchObject &sketch, const ConstraintRef &ref) {
    if (ref.kind == 2) return ref.element == 0 ? QStringLiteral("Origine") : ref.element == 1 ? QStringLiteral("Asse X") : QStringLiteral("Asse Y");
    if (ref.kind == 0) {
        const QString name = QStringLiteral("segmento %1").arg(ref.element + 1);
        if (ref.point < 0) return QStringLiteral("Segmento %1").arg(ref.element + 1);
        return (ref.point == 0 ? QStringLiteral("inizio del ") : QStringLiteral("fine del ")) + name;
    }
    if (ref.kind == 1 && ref.element >= 0 && ref.element < sketch.curves.size()) {
        const CurveObject &curve = sketch.curves.at(ref.element);
        const QString name = curveName(curve) + QStringLiteral(" %1").arg(ref.element + 1);
        if (ref.point < 0) return name;
        if (ref.point == 0 && centered(curve.tool)) return QStringLiteral("centro di ") + name;
        return QStringLiteral("punto %1 di ").arg(ref.point + 1) + name;
    }
    return QStringLiteral("?");
}

QString describeConstraint(const SketchObject &sketch, const SketchConstraint &c) {
    QString text = constraintSymbol(c.type) + QLatin1Char(' ') + constraintName(c.type) + QStringLiteral(": ") + describeRef(sketch, c.first);
    if (c.second.kind >= 0) text += QStringLiteral(" · ") + describeRef(sketch, c.second);
    if (c.type == ConstraintType::Angle) text += QStringLiteral(" = %1°").arg(c.value, 0, 'f', 4);
    else if (isDimension(c.type)) text += QStringLiteral(" = %1").arg(c.value, 0, 'f', 4);
    return text;
}

QVector<ConstraintType> applicableConstraints(const SketchObject &sketch, const QVector<ConstraintRef> &input) {
    using T = ConstraintType;
    const QVector<ConstraintRef> refs = ordered(sketch, input);
    if (refs.isEmpty() || refs.size() > 2) return {};
    const Shape a = shapeOf(sketch, refs.at(0));
    if (a == Shape::None) return {};
    if (refs.size() == 1) {
        if (refs.at(0).kind == 2) return {};
        switch (a) {
        case Shape::Point: return {T::Fix};
        case Shape::Line: return {T::Horizontal, T::Vertical, T::Distance, T::Fix};
        case Shape::Circle: return {T::Radius, T::Diameter, T::Fix};
        default: return {T::Fix};
        }
    }
    const Shape b = shapeOf(sketch, refs.at(1));
    if (b == Shape::None || refs.at(0) == refs.at(1)) return {};
    if (refs.at(0).kind == 2 && refs.at(1).kind == 2) return {};
    // Due punti della stessa entita' non si fanno coincidere (resta solo la distanza, o H/V per i segmenti).
    const bool sameEntity = refs.at(0).kind == refs.at(1).kind && refs.at(0).element == refs.at(1).element && refs.at(0).kind != 2;
    if (a == Shape::Point && b == Shape::Point) {
        if (sameEntity) return {T::Horizontal, T::Vertical, T::Distance};
        return {T::Coincident, T::Horizontal, T::Vertical, T::Distance};
    }
    if (a == Shape::Point) {
        if (sameEntity) return {};
        if (b == Shape::Line) {
            QVector<T> result{T::PointOnCurve, T::Distance};
            if (refs.at(1).kind == 0) result.insert(1, T::Midpoint);
            return result;
        }
        if (b == Shape::Circle || b == Shape::Ellipse) return {T::PointOnCurve};
        return {};
    }
    if (a == Shape::Line && b == Shape::Line) {
        QVector<T> result{T::Parallel, T::Perpendicular, T::Collinear};
        if (refs.at(0).kind == 0 && refs.at(1).kind == 0) result << T::Equal;
        result << T::Angle << T::Distance;
        return result;
    }
    if ((a == Shape::Line && b == Shape::Circle) || (a == Shape::Circle && b == Shape::Line)) return {T::Tangent};
    if (a == Shape::Circle && b == Shape::Circle) return {T::Concentric, T::Equal, T::Tangent};
    if ((a == Shape::Circle || a == Shape::Ellipse) && (b == Shape::Circle || b == Shape::Ellipse)) return {T::Concentric};
    return {};
}

SketchConstraint makeConstraint(const SketchObject &sketch, ConstraintType type, const QVector<ConstraintRef> &input) {
    const QVector<ConstraintRef> refs = ordered(sketch, input);
    SketchConstraint c;
    c.type = type;
    c.first = refs.value(0);
    if (refs.size() > 1) c.second = refs.at(1);
    // La retta dei vincoli tra un segmento e un asse va per prima (l'asse e' il riferimento).
    if (c.second.kind >= 0 && c.first.kind == 2 && c.second.kind != 2 && shapeOf(sketch, c.first) == shapeOf(sketch, c.second)) std::swap(c.first, c.second);
    if (type == ConstraintType::Fix) {
        const System s(sketch);
        QVector<int> points;
        s.dependencies(c.first, points);
        for (int index : points) c.positions.append(s.point(index));
    } else if (type == ConstraintType::Tangent && shapeOf(sketch, c.first) == Shape::Circle && shapeOf(sketch, c.second) == Shape::Circle) {
        const System s(sketch);
        QPointF c1, c2;
        double r1, r2;
        s.circle(c.first, c1, r1);
        s.circle(c.second, c2, r2);
        c.value = length(c1 - c2) < std::max(r1, r2) ? 1.0 : 0.0;
    } else if (isDimension(type)) {
        c.value = currentMeasure(sketch, c);
    }
    return c;
}

double currentMeasure(const SketchObject &sketch, const SketchConstraint &constraint) {
    SketchConstraint probe = constraint;
    probe.value = 0.0;
    const System s(sketch);
    QVector<double> r;
    equations(s, probe, r);
    if (r.isEmpty()) return 0.0;
    if (constraint.type == ConstraintType::Angle) {
        QPointF p0, p1, q0, q1;
        s.line(constraint.first, p0, p1);
        s.line(constraint.second, q0, q1);
        return std::atan2(cross(p1 - p0, q1 - q0), dot(p1 - p0, q1 - q0)) * 180.0 / kPi;
    }
    return r.first();  // residuo con valore 0 = la misura
}

double constraintError(const SketchObject &sketch, const SketchConstraint &constraint) {
    if (!wellFormed(sketch, constraint)) return 0.0;
    const System s(sketch);
    QVector<double> r;
    equations(s, constraint, r);
    double worst = 0.0;
    for (double v : r) worst = std::max(worst, std::fabs(v));
    return worst;
}

bool refersTo(const SketchConstraint &c, int kind, int element) {
    return (c.first.kind == kind && c.first.element == element) || (c.second.kind == kind && c.second.element == element);
}

bool refPoint(const SketchObject &sketch, const ConstraintRef &ref, QPointF &point) {
    if (ref.kind == 2 && ref.element == 0) {
        point = QPointF(0.0, 0.0);
        return true;
    }
    if (shapeOf(sketch, ref) != Shape::Point) return false;
    point = System(sketch).refPoint(ref);
    return true;
}

QVector<ConstraintAnchor> constraintAnchors(const SketchObject &sketch, const SketchConstraint &c) {
    QVector<ConstraintAnchor> result;
    if (!wellFormed(sketch, c)) return result;
    const System s(sketch);
    const auto anchorOf = [&](const ConstraintRef &ref) {
        ConstraintAnchor anchor;
        switch (shapeOf(sketch, ref)) {
        case Shape::Point:
            anchor.point = s.refPoint(ref);
            anchor.onPoint = true;
            break;
        case Shape::Line: {
            QPointF a, b;
            s.line(ref, a, b);
            anchor.point = 0.5 * (a + b);
            anchor.direction = b - a;
            break;
        }
        case Shape::Circle: {
            QPointF center;
            double r;
            s.circle(ref, center, r);
            const CurveObject &curve = sketch.curves.at(ref.element);
            double angle = kPi / 4.0;
            if (curve.tool == DrawingTool::Arc && curve.controlPoints.size() >= 3) {
                const QPointF p = curve.controlPoints.at(1) - center, q = curve.controlPoints.at(2) - center;
                double a0 = std::atan2(p.y(), p.x()), a1 = std::atan2(q.y(), q.x());
                while (a1 <= a0) a1 += 2.0 * kPi;
                angle = 0.5 * (a0 + a1);
            }
            anchor.point = center + r * QPointF(std::cos(angle), std::sin(angle));
            anchor.direction = QPointF(-std::sin(angle), std::cos(angle));
            break;
        }
        case Shape::Ellipse:
            anchor.point = s.point(s.curvePoint(ref.element, 1));
            anchor.direction = s.point(s.curvePoint(ref.element, 2)) - s.point(s.curvePoint(ref.element, 0));
            break;
        case Shape::Curve: {
            const QVector<QPointF> &samples = sketch.curves.at(ref.element).samples;
            if (samples.size() >= 2) {
                const int m = samples.size() / 2;
                anchor.point = samples.at(m);
                anchor.direction = samples.at(std::min(m + 1, int(samples.size()) - 1)) - samples.at(std::max(m - 1, 0));
            }
            break;
        }
        case Shape::None: break;
        }
        return anchor;
    };
    if (c.first.kind != 2) result.append(anchorOf(c.first));
    // La coincidenza ha un solo simbolo (i punti stanno nello stesso posto).
    if (c.second.kind >= 0 && c.second.kind != 2 && c.type != ConstraintType::Coincident) result.append(anchorOf(c.second));
    if (result.isEmpty() && c.second.kind >= 0) result.append(anchorOf(c.second));
    return result;
}

namespace {

// Blocco di equazioni: residui e punti da cui dipendono.
struct Block {
    std::function<void(QVector<double> &)> evaluate;
    QVector<int> points;
};

double sketchScale(const System &system) {
    double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
    for (int i = 0; i < system.pointCount(); ++i) {
        const QPointF p = system.point(i);
        lo[0] = std::min(lo[0], p.x());
        lo[1] = std::min(lo[1], p.y());
        hi[0] = std::max(hi[0], p.x());
        hi[1] = std::max(hi[1], p.y());
    }
    return system.pointCount() ? std::max(1.0, std::hypot(hi[0] - lo[0], hi[1] - lo[1])) : 1.0;
}

// Equazioni dei vincoli validi, implicite delle curve e dei bersagli. Con
// `gauges` anche le equazioni che tolgono le liberta' senza effetto sulla
// geometria (il punto del raggio di un cerchio puo' girare): solo per contare
// i gradi di liberta'.
QVector<Block> buildBlocks(const System &system, const SketchObject &sketch, const QVector<PointTarget> &targets, bool gauges) {
    QVector<const SketchConstraint *> active;
    for (const SketchConstraint &c : sketch.geometricConstraints)
        if (wellFormed(sketch, c)) active.append(&c);
    QVector<Block> blocks;
    for (const SketchConstraint *c : active) {
        Block block;
        system.dependencies(c->first, block.points);
        system.dependencies(c->second, block.points);
        if (c->type == ConstraintType::Concentric) {
            block.points << system.curvePoint(c->first.element, 0) << system.curvePoint(c->second.element, 0);
        }
        block.evaluate = [&system, c](QVector<double> &out) { equations(system, *c, out); };
        blocks.append(block);
    }
    for (int curve = 0; curve < sketch.curves.size(); ++curve) {
        Block block;
        implicitDependencies(system, curve, block.points);
        if (block.points.isEmpty()) continue;
        block.evaluate = [&system, curve](QVector<double> &out) { implicitEquations(system, curve, out); };
        blocks.append(block);
    }
    for (const PointTarget &target : targets) {
        const int index = system.pointIndex(target.point);
        if (index < 0) continue;
        Block block;
        block.points << index;
        const QPointF goal = target.position;
        block.evaluate = [&system, index, goal](QVector<double> &out) {
            const QPointF d = system.point(index) - goal;
            out << d.x() << d.y();
        };
        blocks.append(block);
    }
    if (gauges) {
        for (int curve = 0; curve < sketch.curves.size(); ++curve) {
            const CurveObject &object = sketch.curves.at(curve);
            if (object.tool != DrawingTool::Circle || object.controlPoints.size() < 2) continue;
            bool used = false;
            for (const SketchConstraint *c : active)
                used = used || (c->first == ConstraintRef{1, curve, 1}) || (c->second == ConstraintRef{1, curve, 1});
            if (used) continue;
            // L'angolo del punto del raggio attorno al centro resta quello attuale.
            const QPointF r0 = object.controlPoints.at(1) - object.controlPoints.at(0);
            const QPointF u = length(r0) > 0.0 ? r0 / length(r0) : QPointF(1.0, 0.0);
            Block block;
            block.points << system.curvePoint(curve, 0) << system.curvePoint(curve, 1);
            block.evaluate = [&system, curve, u](QVector<double> &out) {
                out << cross(u, system.point(system.curvePoint(curve, 1)) - system.point(system.curvePoint(curve, 0)));
            };
            blocks.append(block);
        }
    }
    for (Block &block : blocks) {
        std::sort(block.points.begin(), block.points.end());
        block.points.erase(std::unique(block.points.begin(), block.points.end()), block.points.end());
    }
    return blocks;
}

// Jacobiano (m x n, per righe) per differenze centrali, blocco per blocco.
QVector<double> jacobian(const QVector<Block> &blocks, QVector<double> &x, int m, double step) {
    const int n = x.size();
    QVector<double> J(m * n, 0.0);
    int row = 0;
    QVector<double> base, plus, minus;
    for (const Block &block : blocks) {
        base.clear();
        block.evaluate(base);
        for (int point : block.points)
            for (int axis = 0; axis < 2; ++axis) {
                const int column = 2 * point + axis;
                const double saved = x[column];
                x[column] = saved + step;
                plus.clear();
                block.evaluate(plus);
                x[column] = saved - step;
                minus.clear();
                block.evaluate(minus);
                x[column] = saved;
                for (int k = 0; k < base.size(); ++k) J[(row + k) * n + column] = (plus.at(k) - minus.at(k)) / (2.0 * step);
            }
        row += base.size();
    }
    return J;
}

}

bool dimensionPoints(const SketchObject &sketch, const SketchConstraint &c, QPointF &p, QPointF &q) {
    if (c.type != ConstraintType::Distance || !wellFormed(sketch, c)) return false;
    const System s(sketch);
    const Shape a = shapeOf(sketch, c.first), b = shapeOf(sketch, c.second);
    const auto foot = [](const QPointF &x, const QPointF &l0, const QPointF &l1) {
        const QPointF d = l1 - l0;
        const double l2 = dot(d, d);
        return l2 > 0.0 ? l0 + d * (dot(x - l0, d) / l2) : l0;
    };
    if (b == Shape::None) {
        s.line(c.first, p, q);
    } else if (a == Shape::Point && b == Shape::Point) {
        p = s.refPoint(c.first);
        q = s.refPoint(c.second);
    } else if (a == Shape::Point && b == Shape::Line) {
        QPointF l0, l1;
        s.line(c.second, l0, l1);
        p = s.refPoint(c.first);
        q = foot(p, l0, l1);
    } else {
        QPointF l0, l1, m0, m1;
        s.line(c.first, l0, l1);
        s.line(c.second, m0, m1);
        p = m0;
        q = foot(m0, l0, l1);
    }
    return true;
}

bool constraintLines(const SketchObject &sketch, const SketchConstraint &c, QPointF &p0, QPointF &p1, QPointF &q0, QPointF &q1) {
    if (!wellFormed(sketch, c) || shapeOf(sketch, c.first) != Shape::Line || shapeOf(sketch, c.second) != Shape::Line) return false;
    const System s(sketch);
    s.line(c.first, p0, p1);
    s.line(c.second, q0, q1);
    return true;
}

bool circleOf(const SketchObject &sketch, const ConstraintRef &ref, QPointF &center, double &radius) {
    if (shapeOf(sketch, ref) != Shape::Circle) return false;
    System(sketch).circle(ref, center, radius);
    return true;
}

SketchAnalysis analyzeSketch(const SketchObject &sketch) {
    SketchAnalysis analysis;
    analysis.segmentDefined.fill(false, sketch.segments.size());
    analysis.curveDefined.fill(false, sketch.curves.size());
    System system(sketch);
    const int n = system.values().size();
    analysis.variables = n;
    if (n == 0) return analysis;
    const double step = 1e-6 * sketchScale(system);
    const QVector<Block> blocks = buildBlocks(system, sketch, {}, true);
    QVector<double> r;
    for (const Block &block : blocks) block.evaluate(r);
    const int m = r.size();
    QVector<double> &x = const_cast<QVector<double> &>(system.values());
    const QVector<double> J = jacobian(blocks, x, m, step);
    // Base ortonormale dello spazio delle righe (Gram-Schmidt modificato, due
    // passate): la sua dimensione e' il rango; una coordinata e' determinata se
    // il suo versore sta nello spazio delle righe (norma della sua proiezione 1).
    double largest = 0.0;
    for (double v : J) largest = std::max(largest, std::fabs(v));
    QVector<QVector<double>> basis;
    for (int i = 0; i < m; ++i) {
        QVector<double> v(n);
        for (int k = 0; k < n; ++k) v[k] = J[i * n + k];
        double original = 0.0;
        for (double c : v) original += c * c;
        original = std::sqrt(original);
        if (!(original > 1e-12 * std::max(largest, 1e-300))) continue;
        for (int pass = 0; pass < 2; ++pass)
            for (const QVector<double> &b : basis) {
                double d = 0.0;
                for (int k = 0; k < n; ++k) d += b[k] * v[k];
                for (int k = 0; k < n; ++k) v[k] -= d * b[k];
            }
        double remaining = 0.0;
        for (double c : v) remaining += c * c;
        remaining = std::sqrt(remaining);
        if (remaining <= 1e-8 * original) continue;
        for (double &c : v) c /= remaining;
        basis.append(v);
    }
    analysis.rank = basis.size();
    analysis.degreesOfFreedom = n - analysis.rank;
    QVector<bool> determined(n, false);
    for (int k = 0; k < n; ++k) {
        double projection = 0.0;
        for (const QVector<double> &b : basis) projection += b[k] * b[k];
        determined[k] = projection >= 1.0 - 1e-6;
    }
    const auto pointDefined = [&](int index) { return determined[2 * index] && determined[2 * index + 1]; };
    for (int i = 0; i < sketch.segments.size(); ++i)
        analysis.segmentDefined[i] = pointDefined(system.pointIndex({0, i, 0})) && pointDefined(system.pointIndex({0, i, 1}));
    for (int c = 0; c < sketch.curves.size(); ++c) {
        bool all = !sketch.curves.at(c).controlPoints.isEmpty();
        for (int k = 0; k < sketch.curves.at(c).controlPoints.size(); ++k) all = all && pointDefined(system.curvePoint(c, k));
        analysis.curveDefined[c] = all;
    }
    return analysis;
}

SolveResult solveSketch(SketchObject &sketch, const QVector<PointTarget> &targets) {
    SolveResult result;
    System system(sketch);
    const int n = system.values().size();
    if (n == 0) return result;
    const double scale = sketchScale(system);
    const double tolerance = 1e-13 * scale, step = 1e-6 * scale;
    const QVector<Block> blocks = buildBlocks(system, sketch, targets, false);
    if (blocks.isEmpty()) return result;

    QVector<double> &x = system.values();
    const QVector<double> original = x;
    const auto residuals = [&](QVector<double> &r) {
        r.clear();
        for (const Block &block : blocks) block.evaluate(r);
    };
    const auto norm = [](const QVector<double> &r) {
        double worst = 0.0;
        for (double v : r) worst = std::max(worst, std::fabs(v));
        return worst;
    };
    QVector<double> r;
    residuals(r);
    const int m = r.size();
    for (int iteration = 0; iteration < 100; ++iteration) {
        result.iterations = iteration;
        const double current = norm(r);
        if (current <= tolerance) {
            result.residual = current;
            break;
        }
        const QVector<double> J = jacobian(blocks, x, m, step);
        // Passo di norma minima: dx = -J^T (J J^T + mu I)^-1 r.
        QVector<double> A(m * m, 0.0);
        double trace = 0.0;
        for (int i = 0; i < m; ++i)
            for (int j = i; j < m; ++j) {
                double sum = 0.0;
                for (int k = 0; k < n; ++k) sum += J[i * n + k] * J[j * n + k];
                A[i * m + j] = A[j * m + i] = sum;
                if (i == j) trace += sum;
            }
        const double mu = 1e-12 * std::max(trace / std::max(m, 1), 1e-300);
        for (int i = 0; i < m; ++i) A[i * m + i] += mu;
        // Cholesky (A e' simmetrica definita positiva grazie a mu).
        QVector<double> L(m * m, 0.0);
        bool factored = true;
        for (int i = 0; i < m && factored; ++i)
            for (int j = 0; j <= i; ++j) {
                double sum = A[i * m + j];
                for (int k = 0; k < j; ++k) sum -= L[i * m + k] * L[j * m + k];
                if (i == j) {
                    if (!(sum > 0.0)) {
                        factored = false;
                        break;
                    }
                    L[i * m + i] = std::sqrt(sum);
                } else {
                    L[i * m + j] = sum / L[j * m + j];
                }
            }
        if (!factored) break;
        QVector<double> y(m), z(m);
        for (int i = 0; i < m; ++i) {
            double sum = r[i];
            for (int k = 0; k < i; ++k) sum -= L[i * m + k] * y[k];
            y[i] = sum / L[i * m + i];
        }
        for (int i = m - 1; i >= 0; --i) {
            double sum = y[i];
            for (int k = i + 1; k < m; ++k) sum -= L[k * m + i] * z[k];
            z[i] = sum / L[i * m + i];
        }
        QVector<double> dx(n, 0.0);
        for (int k = 0; k < n; ++k) {
            double sum = 0.0;
            for (int i = 0; i < m; ++i) sum += J[i * n + k] * z[i];
            dx[k] = -sum;
        }
        // Ricerca lineare: il passo si dimezza finche' lo scarto non scende.
        const QVector<double> before = x;
        double lambda = 1.0;
        bool improved = false;
        QVector<double> trial;
        for (int attempt = 0; attempt < 12; ++attempt) {
            for (int k = 0; k < n; ++k) x[k] = before[k] + lambda * dx[k];
            residuals(trial);
            if (norm(trial) < current) {
                improved = true;
                break;
            }
            lambda *= 0.5;
        }
        if (!improved) {
            x = before;
            break;
        }
        r = trial;
        result.residual = norm(r);
    }
    result.residual = norm(r);
    if (result.residual > 1e-9 * scale) {
        x = original;
        result.ok = false;
        result.error = QStringLiteral("I vincoli non si possono soddisfare insieme (sono in conflitto).");
        return result;
    }
    // I punti nuovi nello schizzo; le maniglie delle spline seguono i loro punti.
    for (int i = 0; i < sketch.segments.size(); ++i) {
        const int base = system.pointIndex({0, i, 0});
        sketch.segments[i].first = system.point(base);
        sketch.segments[i].second = system.point(base + 1);
    }
    for (int curve = 0; curve < sketch.curves.size(); ++curve) {
        CurveObject &object = sketch.curves[curve];
        for (int k = 0; k < object.controlPoints.size(); ++k) {
            const QPointF moved = system.point(system.curvePoint(curve, k));
            const QPointF delta = moved - object.controlPoints.at(k);
            if (k < object.tangentHandles.size()) {
                object.tangentHandles[k].first += delta;
                object.tangentHandles[k].second += delta;
            }
            object.controlPoints[k] = moved;
        }
    }
    return result;
}

void migrateLegacyConstraints(SketchObject &sketch) {
    for (int i = 0; i < sketch.segments.size(); ++i) {
        const int code = sketch.constraints.value(i, -1);
        if (code == 1 || code == 2) {
            SketchConstraint c;
            c.type = code == 1 ? ConstraintType::Horizontal : ConstraintType::Vertical;
            c.first = {0, i, -1};
            sketch.geometricConstraints.append(c);
        }
        if (sketch.segmentLengths.value(i, 0.0) > 0.0) {
            SketchConstraint c;
            c.type = ConstraintType::Distance;
            c.first = {0, i, -1};
            c.value = sketch.segmentLengths.at(i);
            sketch.geometricConstraints.append(c);
        }
        if (sketch.segmentAngles.value(i, -1.0) >= 0.0 && code != 1 && code != 2) {
            SketchConstraint c;
            c.type = ConstraintType::Angle;
            c.first = {0, i, -1};
            c.second = {2, 1, -1};
            c.value = -sketch.segmentAngles.at(i);  // dall'asse X al segmento
            c.value = currentMeasure(sketch, c);
            sketch.geometricConstraints.append(c);
        }
    }
    for (const CoincidentConstraint &old : sketch.coincidentConstraints) {
        SketchConstraint c;
        c.type = ConstraintType::Coincident;
        c.first = {old.firstKind, old.firstElement, old.firstPoint};
        c.second = {old.secondKind, old.secondElement, old.secondPoint};
        sketch.geometricConstraints.append(c);
    }
    sketch.coincidentConstraints.clear();
    for (int i = 0; i < sketch.segments.size(); ++i) {
        if (i < sketch.constraints.size()) sketch.constraints[i] = -1;
        if (i < sketch.segmentLengths.size()) sketch.segmentLengths[i] = 0.0;
        if (i < sketch.segmentAngles.size()) sketch.segmentAngles[i] = -1.0;
    }
    // Via i vincoli non validi e quelli ripetuti.
    QVector<SketchConstraint> kept;
    for (const SketchConstraint &c : sketch.geometricConstraints) {
        if (!wellFormed(sketch, c)) continue;
        bool duplicate = false;
        for (const SketchConstraint &k : kept)
            duplicate = duplicate || (k.type == c.type && ((k.first == c.first && k.second == c.second) || (k.first == c.second && k.second == c.first)));
        if (!duplicate) kept.append(c);
    }
    sketch.geometricConstraints = kept;
}

void remapConstraints(SketchObject &sketch, const QVector<int> &segmentMap, const QVector<int> &curveMap) {
    const auto remap = [&](ConstraintRef &ref) {
        if (ref.kind == 0 && !segmentMap.isEmpty()) {
            if (ref.element < 0 || ref.element >= segmentMap.size() || segmentMap.at(ref.element) < 0) return false;
            ref.element = segmentMap.at(ref.element);
        } else if (ref.kind == 1 && !curveMap.isEmpty()) {
            if (ref.element < 0 || ref.element >= curveMap.size() || curveMap.at(ref.element) < 0) return false;
            ref.element = curveMap.at(ref.element);
        }
        return true;
    };
    QVector<SketchConstraint> kept;
    for (SketchConstraint c : sketch.geometricConstraints) {
        if (!remap(c.first)) continue;
        if (c.second.kind >= 0 && !remap(c.second)) continue;
        kept.append(c);
    }
    sketch.geometricConstraints = kept;
}

}
