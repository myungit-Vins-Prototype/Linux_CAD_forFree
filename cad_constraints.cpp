#include "cad_constraints.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>

#include "cad_curve_solver.h"
#include "fk_curve_algo.h"

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

// La spline ha le maniglie tangenti (una coppia per punto): sono punti dei vincoli.
bool splineHandles(const CurveObject &curve) {
    return curve.tool == DrawingTool::Spline && !curve.controlPoints.isEmpty() && curve.tangentHandles.size() == curve.controlPoints.size();
}

Shape shapeOf(const SketchObject &sketch, const ConstraintRef &ref) {
    if (ref.kind == 2) return ref.element == 0 ? Shape::Point : (ref.element == 1 || ref.element == 2) ? Shape::Line : Shape::None;
    if (ref.kind == 0) {
        if (ref.element < 0 || ref.element >= sketch.segments.size()) return Shape::None;
        return ref.point < 0 ? Shape::Line : ref.point <= 1 ? Shape::Point : Shape::None;
    }
    if (ref.kind != 1 || ref.element < 0 || ref.element >= sketch.curves.size()) return Shape::None;
    const CurveObject &curve = sketch.curves.at(ref.element);
    if (ref.point >= 0) {
        if (isHandlePoint(ref.point))
            return splineHandles(curve) && (ref.point - kHandlePoint) / 2 < curve.controlPoints.size() ? Shape::Point : Shape::None;
        return ref.point < curve.controlPoints.size() ? Shape::Point : Shape::None;
    }
    switch (curve.tool) {
    case DrawingTool::Circle:
    case DrawingTool::Arc:
    case DrawingTool::Polygon: return curve.controlPoints.size() >= 2 ? Shape::Circle : Shape::None;
    case DrawingTool::Ellipse: return curve.controlPoints.size() >= 3 ? Shape::Ellipse : Shape::None;
    default: return curve.controlPoints.size() >= 2 ? Shape::Curve : Shape::None;
    }
}

// Coordinate delle incognite in un vettore (x0, y0, x1, y1...): prima gli
// estremi dei segmenti, poi i punti di controllo delle curve (i "punti veri"),
// poi le maniglie delle spline come scarti dal loro punto (spostando il punto
// la maniglia lo segue), poi una coppia per ogni ripetizione (passo o angolo,
// secondo passo).
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
        realPoints_ = pointCount();
        for (const CurveObject &curve : sketch.curves) {
            if (!splineHandles(curve)) {
                handleBase_.append(-1);
                continue;
            }
            handleBase_.append(pointCount());
            for (int k = 0; k < curve.controlPoints.size(); ++k) {
                const QPointF in = curve.tangentHandles.at(k).first - curve.controlPoints.at(k);
                const QPointF out = curve.tangentHandles.at(k).second - curve.controlPoints.at(k);
                x_ << in.x() << in.y() << out.x() << out.y();
            }
        }
        patternBase_.fill(-1, sketch.geometricConstraints.size());
        for (int i = 0; i < sketch.geometricConstraints.size(); ++i) {
            const SketchConstraint &c = sketch.geometricConstraints.at(i);
            if (c.type != ConstraintType::Pattern) continue;
            patternBase_[i] = pointCount();
            x_ << storedParameter(c.pattern) << c.pattern.spacing2;
        }
    }
    // Valore salvato della prima incognita di una ripetizione.
    static double storedParameter(const SketchPatternData &p) { return p.kind == 0 ? p.spacing : p.kind == 1 ? p.angle : 0.0; }

    QVector<double> &values() { return x_; }
    const QVector<double> &values() const { return x_; }
    int pointCount() const { return int(x_.size()) / 2; }
    int realPointCount() const { return realPoints_; }
    QPointF point(int index) const { return {x_.at(2 * index), x_.at(2 * index + 1)}; }

    // Indice del punto (-1: l'origine, una maniglia o un riferimento non valido).
    int pointIndex(const ConstraintRef &ref) const {
        if (ref.point < 0 || isHandlePoint(ref.point)) return -1;
        if (ref.kind == 0 && ref.element >= 0 && ref.element < segmentBase_.size() && ref.point <= 1) return segmentBase_.at(ref.element) + ref.point;
        if (ref.kind == 1 && ref.element >= 0 && ref.element < curveBase_.size() && ref.point < sketch_.curves.at(ref.element).controlPoints.size())
            return curveBase_.at(ref.element) + ref.point;
        return -1;
    }
    // Incognita dello scarto di una maniglia (-1 se il riferimento non e' una maniglia).
    int handleIndex(const ConstraintRef &ref) const {
        if (ref.kind != 1 || !isHandlePoint(ref.point) || ref.element < 0 || ref.element >= handleBase_.size()) return -1;
        const int base = handleBase_.at(ref.element), h = ref.point - kHandlePoint;
        if (base < 0 || h / 2 >= sketch_.curves.at(ref.element).controlPoints.size()) return -1;
        return base + h;
    }
    QPointF refPoint(const ConstraintRef &ref) const {
        const int h = handleIndex(ref);
        if (h >= 0) return point(curvePoint(ref.element, (ref.point - kHandlePoint) / 2)) + point(h);
        const int index = pointIndex(ref);
        return index >= 0 ? point(index) : QPointF(0.0, 0.0);
    }
    // Incognite da cui dipende un punto.
    void pointDependencies(const ConstraintRef &ref, QVector<int> &out) const {
        const int h = handleIndex(ref);
        if (h >= 0) {
            out << curvePoint(ref.element, (ref.point - kHandlePoint) / 2) << h;
            return;
        }
        const int index = pointIndex(ref);
        if (index >= 0) out.append(index);
    }
    // Punti di un'entita' (estremi del segmento; punti della curva, poi le
    // maniglie), o il punto stesso.
    QVector<ConstraintRef> entityPoints(const ConstraintRef &ref) const {
        QVector<ConstraintRef> points;
        if (ref.kind == 2) return points;
        if (ref.point >= 0) return {ref};
        if (ref.kind == 0 && ref.element >= 0 && ref.element < segmentBase_.size()) return {{0, ref.element, 0}, {0, ref.element, 1}};
        if (ref.kind == 1 && ref.element >= 0 && ref.element < curveBase_.size()) {
            const int n = sketch_.curves.at(ref.element).controlPoints.size();
            for (int k = 0; k < n; ++k) points.append({1, ref.element, k});
            if (handleBase_.at(ref.element) >= 0)
                for (int k = 0; k < n; ++k) points << ConstraintRef{1, ref.element, handlePoint(k, 0)} << ConstraintRef{1, ref.element, handlePoint(k, 1)};
        }
        return points;
    }
    int curvePoint(int curve, int k) const { return curveBase_.at(curve) + k; }
    int handleBase(int curve) const { return handleBase_.at(curve); }
    int patternBase(int constraint) const { return constraint >= 0 && constraint < patternBase_.size() ? patternBase_.at(constraint) : -1; }
    // Indice del vincolo nello schizzo (-1 se e' una copia).
    int constraintIndex(const SketchConstraint &c) const {
        const auto begin = reinterpret_cast<std::uintptr_t>(sketch_.geometricConstraints.constData());
        const auto at = reinterpret_cast<std::uintptr_t>(&c);
        if (at < begin || (at - begin) % sizeof(SketchConstraint) != 0) return -1;
        const std::uintptr_t index = (at - begin) / sizeof(SketchConstraint);
        return index < std::uintptr_t(sketch_.geometricConstraints.size()) ? int(index) : -1;
    }
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
    // Geometria esatta della curva con i valori attuali delle incognite.
    std::vector<Kernel::ProfileSegment> geometry(int curve) const {
        const CurveObject &source = sketch_.curves.at(curve);
        CurveObject object;
        object.tool = source.tool;
        object.weights = source.weights;
        object.sides = source.sides;
        object.knots = source.knots;
        object.degree = source.degree;
        const int n = source.controlPoints.size();
        for (int k = 0; k < n; ++k) object.controlPoints.append(point(curvePoint(curve, k)));
        if (handleBase_.at(curve) >= 0)
            for (int k = 0; k < n; ++k)
                object.tangentHandles.append({object.controlPoints.at(k) + point(handleBase_.at(curve) + 2 * k),
                                              object.controlPoints.at(k) + point(handleBase_.at(curve) + 2 * k + 1)});
        return curveGeometry(object);
    }
    // Incognite da cui dipende il riferimento.
    void dependencies(const ConstraintRef &ref, QVector<int> &out) const {
        for (const ConstraintRef &p : entityPoints(ref)) pointDependencies(p, out);
    }
    const SketchObject &sketch() const { return sketch_; }

private:
    const SketchObject &sketch_;
    QVector<double> x_;
    QVector<int> segmentBase_, curveBase_, handleBase_, patternBase_;
    int realPoints_ = 0;
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

// Punto di contatto di una tangenza: un punto della prima entita' e uno della
// seconda uniti da un vincolo di coincidenza, entrambi sul cerchio o sulla
// retta (estremi dei segmenti, estremi degli archi, punto del raggio dei
// cerchi). Con il contatto la tangenza si scrive nel punto: la retta
// perpendicolare al raggio, o i due centri allineati con il punto. La forma
// senza contatto (distanza centro-retta uguale al raggio, distanza dei centri
// uguale alla somma o alla differenza dei raggi) insieme alla coincidenza nel
// punto di contatto e' degenere: se la retta gira attorno al punto la distanza
// cambia solo al secondo ordine, il Jacobiano perde rango (gradi di liberta'
// contati in piu') e il risolutore converge male.
struct TangentContact {
    bool found = false;
    ConstraintRef first, second;  // i punti coincidenti della prima e della seconda entita'
};

bool onTangentShape(const SketchObject &sketch, const ConstraintRef &point, const ConstraintRef &entity) {
    if (point.kind != entity.kind || point.element != entity.element || point.point < 0 || isHandlePoint(point.point)) return false;
    if (point.kind == 0) return point.point <= 1;
    if (point.kind != 1 || point.element < 0 || point.element >= sketch.curves.size()) return false;
    const CurveObject &curve = sketch.curves.at(point.element);
    const DrawingTool tool = curve.tool;
    const int n = curve.controlPoints.size();
    // Spline: in un suo punto qualsiasi (la tangente e' quella delle maniglie); NURBS: agli estremi.
    if (tool == DrawingTool::Spline) return splineHandles(curve) && point.point < n;
    if (tool == DrawingTool::Nurbs || tool == DrawingTool::Converted) return n >= 2 && (point.point == 0 || point.point == n - 1);
    return (tool == DrawingTool::Arc && (point.point == 1 || point.point == 2)) || (tool == DrawingTool::Circle && point.point == 1);
}

TangentContact tangentContact(const SketchObject &sketch, const SketchConstraint &c) {
    TangentContact contact;
    if (c.type != ConstraintType::Tangent) return contact;
    for (const SketchConstraint &k : sketch.geometricConstraints) {
        if (k.type != ConstraintType::Coincident) continue;
        if (onTangentShape(sketch, k.first, c.first) && onTangentShape(sketch, k.second, c.second)) {
            contact = {true, k.first, k.second};
            return contact;
        }
        if (onTangentShape(sketch, k.second, c.first) && onTangentShape(sketch, k.first, c.second)) {
            contact = {true, k.second, k.first};
            return contact;
        }
    }
    return contact;
}

// Tangente dell'entita' nel suo punto di contatto `at` (retta: la direzione;
// cerchio: normale al raggio; spline: le maniglie; NURBS: il primo o l'ultimo lato dei poli).
bool tangentAt(const System &s, const ConstraintRef &entity, const ConstraintRef &at, QPointF &tangent) {
    const SketchObject &sketch = s.sketch();
    switch (shapeOf(sketch, entity)) {
    case Shape::Line: {
        QPointF a, b;
        s.line(entity, a, b);
        tangent = b - a;
        return true;
    }
    case Shape::Circle: {
        QPointF center;
        double r;
        s.circle(entity, center, r);
        const QPointF radial = s.refPoint(at) - center;
        tangent = QPointF(-radial.y(), radial.x());
        return true;
    }
    case Shape::Curve: {
        const CurveObject &curve = sketch.curves.at(entity.element);
        const int n = curve.controlPoints.size(), k = at.point;
        if (k < 0 || k >= n) return false;
        const QPointF p = s.refPoint(at);
        if (curve.tool == DrawingTool::Spline && splineHandles(curve)) {
            const QPointF in = s.refPoint({1, entity.element, handlePoint(k, 0)}), out = s.refPoint({1, entity.element, handlePoint(k, 1)});
            tangent = k == 0 ? out - p : k == n - 1 ? p - in : out - in;
            return true;
        }
        if ((curve.tool == DrawingTool::Nurbs || curve.tool == DrawingTool::Converted) && n >= 2) {
            tangent = k == 0 ? s.refPoint({1, entity.element, 1}) - p : p - s.refPoint({1, entity.element, n - 2});
            return true;
        }
        return false;
    }
    default: return false;
    }
}

using Geometry = std::vector<Kernel::ProfileSegment>;

// Punto della curva piu' vicino a p e tangente li'.
bool curveFoot(const Geometry &geometry, const QPointF &p, QPointF &foot, QPointF &tangent) {
    double best = std::numeric_limits<double>::infinity();
    const Kernel::Vec2 q(p.x(), p.y());
    for (const Kernel::ProfileSegment &piece : geometry) {
        const Kernel::CurveProjection<2> projection = Kernel::projectPoint(*piece.curve, q, piece.range);
        if (!(projection.distance < best)) continue;
        best = projection.distance;
        foot = QPointF(projection.point.x(), projection.point.y());
        const Kernel::Vec2 d = piece.curve->derivative(projection.parameter);
        tangent = QPointF(d.x(), d.y());
    }
    return std::isfinite(best);
}

// Distanza con segno di p dalla curva (positiva a sinistra della tangente nel piede).
double curveSignedDistance(const Geometry &geometry, const QPointF &p) {
    QPointF foot, tangent;
    if (!curveFoot(geometry, p, foot, tangent)) return 0.0;
    const double d = length(p - foot);
    return cross(tangent, p - foot) < 0.0 ? -d : d;
}

// Minimo su tutta la curva di phi(C(t)) (phi, phi', phi'' da C, C', C''):
// campioni per trovare i minimi locali, poi Newton su phi' = 0 (esatto).
template <class F>
double curveMinimum(const Geometry &geometry, const F &f) {
    double best = std::numeric_limits<double>::infinity();
    for (const Kernel::ProfileSegment &piece : geometry) {
        std::vector<double> breaks = piece.curve->breakpoints(piece.range);
        if (breaks.size() < 2) breaks = {piece.range.lo, piece.range.hi};
        const auto value = [&](double t, double *d1, double *d2) {
            Kernel::Vec2 c[3];
            piece.curve->evaluate(t, 2, c);
            double v = 0.0, a = 0.0, b = 0.0;
            f(QPointF(c[0].x(), c[0].y()), QPointF(c[1].x(), c[1].y()), QPointF(c[2].x(), c[2].y()), v, a, b);
            if (d1) *d1 = a;
            if (d2) *d2 = b;
            return v;
        };
        for (std::size_t span = 0; span + 1 < breaks.size(); ++span) {
            const double lo = breaks[span], hi = breaks[span + 1];
            if (!(hi > lo)) continue;
            constexpr int samples = 24;
            double v[samples + 1];
            for (int i = 0; i <= samples; ++i) {
                v[i] = value(lo + (hi - lo) * i / samples, nullptr, nullptr);
                best = std::min(best, v[i]);
            }
            for (int i = 0; i <= samples; ++i) {
                if ((i > 0 && v[i] > v[i - 1]) || (i < samples && v[i] > v[i + 1])) continue;
                double t = lo + (hi - lo) * i / samples;
                for (int iteration = 0; iteration < 40; ++iteration) {
                    double d1 = 0.0, d2 = 0.0;
                    value(t, &d1, &d2);
                    double step = d2 > 0.0 ? -d1 / d2 : (d1 > 0.0 ? -1.0 : 1.0) * (hi - lo) / (4.0 * samples);
                    const double next = std::clamp(t + step, lo, hi);
                    step = next - t;
                    t = next;
                    if (std::fabs(step) <= 1e-15 * std::max(1.0, std::fabs(t))) break;
                }
                best = std::min(best, value(t, nullptr, nullptr));
            }
        }
    }
    return best;
}

// Scarto della tangenza curva-retta senza punto di contatto: il minimo della
// distanza con segno side * cross(u, C - a) (zero quando la curva tocca la retta).
double curveLineGap(const Geometry &geometry, const QPointF &a, const QPointF &b, double side) {
    const double l = length(b - a);
    if (!(l > 0.0)) return 0.0;
    const QPointF u = (b - a) / l;
    return curveMinimum(geometry, [&](const QPointF &c, const QPointF &d, const QPointF &dd, double &v, double &v1, double &v2) {
        v = side * cross(u, c - a);
        v1 = side * cross(u, d);
        v2 = side * cross(u, dd);
    });
}

// Scarto della tangenza curva-cerchio: il minimo di side * (|C - c| - r).
double curveCircleGap(const Geometry &geometry, const QPointF &center, double r, double side) {
    return curveMinimum(geometry, [&](const QPointF &c, const QPointF &d, const QPointF &dd, double &v, double &v1, double &v2) {
        const QPointF w = c - center;
        const double l = std::max(length(w), 1e-300);
        v = side * (l - r);
        v1 = side * dot(w, d) / l;
        v2 = side * ((dot(d, d) + dot(w, dd)) / l - dot(w, d) * dot(w, d) / (l * l * l));
    });
}

// Movimento del piano p -> A p + t delle istanze di una ripetizione.
struct PlaneMotion {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0;
    QPointF t;
    QPointF apply(const QPointF &p) const { return QPointF(a * p.x() + b * p.y() + t.x(), c * p.x() + d * p.y() + t.y()); }
};

// Direzione unitaria di una ripetizione: la retta `ref` o l'angolo fisso.
QPointF patternDirection(const System &s, const ConstraintRef &ref, double degrees) {
    if (ref.kind >= 0) {
        QPointF a, b;
        s.line(ref, a, b);
        const double l = length(b - a);
        return l > 0.0 ? (b - a) / l : QPointF(1.0, 0.0);
    }
    const double angle = degrees * kPi / 180.0;
    return QPointF(std::cos(angle), std::sin(angle));
}

// Passo angolare (gradi) di una ripetizione circolare con angolo `angle`
// (la regola del giro intero viene dal valore salvato: niente salti nelle derivate).
double patternStep(const SketchPatternData &p, double angle) {
    if (!p.spread) return angle;
    const bool fullTurn = std::fabs(std::fabs(p.angle) - 360.0) < 1e-9;
    return fullTurn ? angle / p.count : angle / std::max(1, p.count - 1);
}

// Movimenti delle istanze con i parametri (passo/angolo, secondo passo) dati.
QVector<PlaneMotion> patternMotions(const System &s, const SketchPatternData &p, double first, double second) {
    QVector<PlaneMotion> motions;
    if (p.kind == 0) {
        const QPointF u = patternDirection(s, p.direction, p.directionAngle), w = patternDirection(s, p.direction2, p.directionAngle2);
        for (int j = 0; j < std::max(1, p.count2); ++j)
            for (int i = 0; i < p.count; ++i) {
                if (i == 0 && j == 0) continue;
                PlaneMotion m;
                m.t = i * first * u + j * second * w;
                motions.append(m);
            }
    } else if (p.kind == 1) {
        const QPointF center = p.center.kind >= 0 ? s.refPoint(p.center) : p.centerPoint;
        const double step = patternStep(p, first) * kPi / 180.0;
        for (int i = 1; i < p.count; ++i) {
            PlaneMotion m;
            const double cs = std::cos(i * step), sn = std::sin(i * step);
            m.a = cs, m.b = -sn, m.c = sn, m.d = cs;
            m.t = center - QPointF(cs * center.x() - sn * center.y(), sn * center.x() + cs * center.y());
            motions.append(m);
        }
    } else {
        QPointF point = p.axisPoint, direction = p.axisDirection;
        if (p.axis.kind >= 0) {
            QPointF b;
            s.line(p.axis, point, b);
            direction = b - point;
        }
        const double l = length(direction);
        const double ux = l > 0.0 ? direction.x() / l : 0.0, uy = l > 0.0 ? direction.y() / l : 1.0;
        PlaneMotion m;
        m.a = 2 * ux * ux - 1, m.b = 2 * ux * uy, m.c = 2 * ux * uy, m.d = 2 * uy * uy - 1;
        m.t = point - QPointF(m.a * point.x() + m.b * point.y(), m.c * point.x() + m.d * point.y());
        motions.append(m);
    }
    return motions;
}

// Punto della sorgente che corrisponde al punto k della copia (nello specchio
// gli archi si scambiano inizio e fine: restano antiorari).
int mirroredPoint(const SketchObject &sketch, const ConstraintRef &source, int k, bool mirror) {
    if (!mirror || source.kind != 1) return k;
    if (sketch.curves.at(source.element).tool == DrawingTool::Arc && (k == 1 || k == 2)) return 3 - k;
    return k;
}

// Equazioni della ripetizione: ogni punto delle copie e' il movimento del
// punto della sorgente; le quote fissano i parametri.
void patternEquations(const System &s, const SketchConstraint &c, QVector<double> &out) {
    const SketchPatternData &p = c.pattern;
    const int base = s.patternBase(s.constraintIndex(c));
    const double first = base >= 0 ? s.point(base).x() : System::storedParameter(p);
    const double second = base >= 0 ? s.point(base).y() : p.spacing2;
    const QVector<PlaneMotion> motions = patternMotions(s, p, first, second);
    const int sources = p.sources.size();
    for (int m = 0; m < motions.size(); ++m)
        for (int j = 0; j < sources; ++j) {
            const int index = m * sources + j;
            if (index >= p.copies.size() || p.copies.at(index).kind < 0) continue;
            const QVector<ConstraintRef> from = s.entityPoints(p.sources.at(j)), to = s.entityPoints(p.copies.at(index));
            if (from.size() != to.size()) continue;
            for (int k = 0; k < to.size(); ++k) {
                const QPointF d = s.refPoint(to.at(k)) - motions.at(m).apply(s.refPoint(from.at(mirroredPoint(s.sketch(), p.sources.at(j), k, p.kind == 2))));
                out << d.x() << d.y();
            }
        }
    if (base < 0) return;
    if (p.kind == 2 || p.dimensioned) out << first - System::storedParameter(p);
    if (p.kind != 0 || p.count2 <= 1 || p.dimensioned2) out << second - p.spacing2;
}

void patternDependencies(const System &s, const SketchConstraint &c, QVector<int> &out) {
    const SketchPatternData &p = c.pattern;
    for (const ConstraintRef &ref : p.sources) s.dependencies(ref, out);
    for (const ConstraintRef &ref : p.copies)
        if (ref.kind >= 0) s.dependencies(ref, out);
    for (const ConstraintRef &ref : {p.direction, p.direction2, p.axis})
        if (ref.kind >= 0) s.dependencies(ref, out);
    if (p.center.kind >= 0) s.pointDependencies(p.center, out);
    const int base = s.patternBase(s.constraintIndex(c));
    if (base >= 0) out.append(base);
}

// La ripetizione e' ben formata: sorgenti e copie valide e dello stesso tipo, riferimenti validi.
bool patternWellFormed(const SketchObject &sketch, const SketchPatternData &p) {
    if (p.kind < 0 || p.kind > 2 || p.sources.isEmpty()) return false;
    if (p.kind == 0 && (p.count < 1 || p.count2 < 1 || p.count * p.count2 < 2)) return false;
    if (p.kind == 1 && p.count < 2) return false;
    if (p.copies.size() != p.instances() * p.sources.size()) return false;
    const auto entity = [&](const ConstraintRef &r) {
        if (r.point >= 0) return false;
        if (r.kind == 0) return r.element >= 0 && r.element < sketch.segments.size();
        return r.kind == 1 && r.element >= 0 && r.element < sketch.curves.size();
    };
    for (int j = 0; j < p.sources.size(); ++j) {
        if (!entity(p.sources.at(j))) return false;
        for (int m = 0; m < p.instances(); ++m) {
            const ConstraintRef &copy = p.copies.at(m * p.sources.size() + j);
            if (copy.kind < 0) continue;
            if (!entity(copy) || copy.kind != p.sources.at(j).kind) return false;
            if (copy.kind == 1) {
                const CurveObject &a = sketch.curves.at(copy.element), &b = sketch.curves.at(p.sources.at(j).element);
                if (a.tool != b.tool || a.controlPoints.size() != b.controlPoints.size() || splineHandles(a) != splineHandles(b)) return false;
            }
        }
    }
    const auto line = [&](const ConstraintRef &r) { return r.kind < 0 || shapeOf(sketch, r) == Shape::Line; };
    if (p.kind == 0 && (!line(p.direction) || (p.count2 > 1 && !line(p.direction2)))) return false;
    if (p.kind == 1 && p.center.kind >= 0 && shapeOf(sketch, p.center) != Shape::Point) return false;
    if (p.kind == 2 && !line(p.axis)) return false;
    return true;
}

// Equazioni del vincolo (lunghezze) nelle coordinate del sistema. Per la
// tangenza `contact` e' il punto di contatto (se nullo si cerca).
void equations(const System &s, const SketchConstraint &c, QVector<double> &out, const TangentContact *contact = nullptr) {
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
        } else if (b == Shape::Curve) {
            out << curveSignedDistance(s.geometry(c.second.element), p);
        }
        return;
    }
    case ConstraintType::Tangent: {
        const TangentContact found = contact ? *contact : tangentContact(sketch, c);
        if (a == Shape::Curve || b == Shape::Curve) {
            if (found.found) {
                // Nel punto di contatto le due tangenti sono parallele.
                QPointF t1, t2;
                if (tangentAt(s, c.first, found.first, t1) && tangentAt(s, c.second, found.second, t2))
                    out << cross(t1, t2) / std::max(length(t2), 1e-300);
                else
                    out << 0.0;
                return;
            }
            // Senza contatto: la curva tocca la retta o il cerchio dalla parte scelta (value = +1 o -1).
            const bool curveFirst = a == Shape::Curve;
            const ConstraintRef &curveRef = curveFirst ? c.first : c.second, &otherRef = curveFirst ? c.second : c.first;
            const Shape other = curveFirst ? b : a;
            const double side = c.value < 0.0 ? -1.0 : 1.0;
            if (other == Shape::Line) {
                QPointF q0, q1;
                lineOf(otherRef, q0, q1);
                out << curveLineGap(s.geometry(curveRef.element), q0, q1, side);
            } else if (other == Shape::Circle) {
                QPointF center;
                double r;
                s.circle(otherRef, center, r);
                out << curveCircleGap(s.geometry(curveRef.element), center, r, side);
            } else {
                out << 0.0;
            }
            return;
        }
        if (found.found && (a == Shape::Line || b == Shape::Line)) {
            const bool lineFirst = a == Shape::Line;
            QPointF q0, q1, center;
            double r;
            lineOf(lineFirst ? c.first : c.second, q0, q1);
            s.circle(lineFirst ? c.second : c.first, center, r);
            const QPointF d = q1 - q0, touch = s.refPoint(lineFirst ? found.second : found.first);
            out << dot(d, center - touch) / std::max(length(d), 1e-300);
            return;
        }
        if (found.found) {
            QPointF c1, c2;
            double r1, r2;
            s.circle(c.first, c1, r1);
            s.circle(c.second, c2, r2);
            const QPointF touch = s.refPoint(found.first);
            out << cross(c1 - touch, c2 - touch) / std::max(length(c1 - touch), 1e-300);
            return;
        }
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
        const QVector<ConstraintRef> points = s.entityPoints(c.first);
        for (int k = 0; k < points.size() && k < c.positions.size(); ++k) {
            const QPointF d = s.refPoint(points.at(k)) - c.positions.at(k);
            out << d.x() << d.y();
        }
        return;
    }
    case ConstraintType::HorizontalDistance:
    case ConstraintType::VerticalDistance: {
        QPointF p, q;
        if (b == Shape::None) {
            lineOf(c.first, p, q);
        } else {
            p = s.refPoint(c.first);
            q = s.refPoint(c.second);
        }
        const QPointF d = q - p;
        out << std::fabs(c.type == ConstraintType::HorizontalDistance ? d.x() : d.y()) - c.value;
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
        } else if (a == Shape::Point && b == Shape::Curve) {
            out << std::fabs(curveSignedDistance(s.geometry(c.second.element), s.refPoint(c.first))) - c.value;
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
    case ConstraintType::Pattern:
        patternEquations(s, c, out);
        return;
    case ConstraintType::Symmetric: {
        QPointF a0, a1;
        s.line(c.third, a0, a1);
        const double l = std::max(length(a1 - a0), 1e-300);
        const QPointF u = (a1 - a0) / l;
        // Due punti simmetrici: la congiungente normale all'asse, il punto medio sull'asse.
        const auto pair = [&](const QPointF &p, const QPointF &q) { out << dot(q - p, u) << cross(u, 0.5 * (p + q) - a0); };
        if (a == Shape::Point) {
            pair(s.refPoint(c.first), s.refPoint(c.second));
        } else if (a == Shape::Line) {
            QPointF p0, p1, q0, q1;
            lineOf(c.first, p0, p1);
            lineOf(c.second, q0, q1);
            if (c.value > 0.5) std::swap(q0, q1);
            pair(p0, q0);
            pair(p1, q1);
        } else {
            QPointF c1, c2;
            double r1, r2;
            s.circle(c.first, c1, r1);
            s.circle(c.second, c2, r2);
            pair(c1, c2);
            out << r1 - r2;
        }
        return;
    }
    case ConstraintType::AxisRadius:
    case ConstraintType::AxisDiameter: {
        QPointF q0, q1, p = s.refPoint(c.first);
        lineOf(c.second, q0, q1);
        if (a == Shape::Line) {
            QPointF p1;
            lineOf(c.first, p, p1);
        }
        out << (c.type == ConstraintType::AxisDiameter ? 2.0 : 1.0) * std::fabs(pointLineDistance(p, q0, q1)) - c.value;
        return;
    }
    }
}

// Il vincolo e' ben formato per lo schizzo (riferimenti validi e delle forme giuste).
bool wellFormed(const SketchObject &sketch, const SketchConstraint &c) {
    if (c.type == ConstraintType::Pattern) return patternWellFormed(sketch, c.pattern);
    const Shape a = shapeOf(sketch, c.first), b = shapeOf(sketch, c.second);
    if (a == Shape::None) return false;
    if (c.second.kind >= 0 && b == Shape::None) return false;
    if (c.type == ConstraintType::Symmetric)
        return shapeOf(sketch, c.third) == Shape::Line && a == b && (a == Shape::Point || a == Shape::Line || a == Shape::Circle) && c.first != c.second
            && !(a == Shape::Line && (c.first == c.third || c.second == c.third));
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
    case DrawingTool::Converted: return QStringLiteral("Riferimento");
    default: return QStringLiteral("Spline");
    }
}

}

namespace {

// Ruoli di una simmetria tra tre riferimenti: due entita' della stessa forma
// (punti, segmenti, cerchi o archi) e la retta. Tra tre rette l'asse e'
// quello di simmetria, poi una linea di costruzione, poi un asse del piano,
// altrimenti l'ultima scelta.
bool symmetryRoles(const SketchObject &sketch, const QVector<ConstraintRef> &refs, ConstraintRef &a, ConstraintRef &b, ConstraintRef &axis) {
    if (refs.size() != 3) return false;
    int chosen = -1, best = -1;
    for (int k = 0; k < 3; ++k) {
        const ConstraintRef &r = refs.at(k);
        if (shapeOf(sketch, r) != Shape::Line) continue;
        int score = 1;
        if (r.kind == 2) score = 2;
        if (r.kind == 0 && sketch.isConstructionSegment(r.element)) score = 3;
        if (r.kind == 0 && sketch.symmetryAxes.contains(r.element)) score = 4;
        if (score >= best) best = score, chosen = k;
    }
    if (chosen < 0) return false;
    axis = refs.at(chosen);
    QVector<ConstraintRef> others;
    for (int k = 0; k < 3; ++k)
        if (k != chosen) others.append(refs.at(k));
    a = others.at(0);
    b = others.at(1);
    const Shape sa = shapeOf(sketch, a), sb = shapeOf(sketch, b);
    if (sa != sb || a == b || a.kind == 2 || b.kind == 2) return false;
    return sa == Shape::Point || (sa == Shape::Line && a.kind == 0 && b.kind == 0) || sa == Shape::Circle;
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
    case ConstraintType::Pattern: return QStringLiteral("Ripetizione");
    case ConstraintType::Symmetric: return QStringLiteral("Simmetrico");
    case ConstraintType::AxisRadius: return QStringLiteral("Raggio dall'asse");
    case ConstraintType::AxisDiameter: return QStringLiteral("Diametro dall'asse");
    case ConstraintType::HorizontalDistance: return QStringLiteral("Distanza orizzontale");
    case ConstraintType::VerticalDistance: return QStringLiteral("Distanza verticale");
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
    case ConstraintType::Pattern: return QStringLiteral("⁂");
    case ConstraintType::Symmetric: return QStringLiteral("⇹");
    case ConstraintType::AxisRadius: return QStringLiteral("R");
    case ConstraintType::AxisDiameter: return QStringLiteral("⌀");
    case ConstraintType::HorizontalDistance: return QStringLiteral("⟷");
    case ConstraintType::VerticalDistance: return QStringLiteral("↕");
    }
    return {};
}

bool isDimension(ConstraintType type) {
    return type == ConstraintType::Distance || type == ConstraintType::Angle || type == ConstraintType::Radius || type == ConstraintType::Diameter
        || type == ConstraintType::AxisRadius || type == ConstraintType::AxisDiameter || type == ConstraintType::HorizontalDistance
        || type == ConstraintType::VerticalDistance;
}

bool isAxisReference(const SketchObject &sketch, const ConstraintRef &ref) {
    if (ref.point >= 0) return false;
    if (ref.kind == 2) return ref.element == 1 || ref.element == 2;
    return ref.kind == 0 && ref.element >= 0 && ref.element < sketch.segments.size()
        && (sketch.isConstructionSegment(ref.element) || sketch.symmetryAxes.contains(ref.element));
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
        if (isHandlePoint(ref.point)) {
            const int h = ref.point - kHandlePoint;
            return (h % 2 == 0 ? QStringLiteral("maniglia entrante del punto %1 di ") : QStringLiteral("maniglia uscente del punto %1 di ")).arg(h / 2 + 1) + name;
        }
        if (ref.point == 0 && centered(curve.tool)) return QStringLiteral("centro di ") + name;
        return QStringLiteral("punto %1 di ").arg(ref.point + 1) + name;
    }
    return QStringLiteral("?");
}

QString patternSummary(const SketchPatternData &p) {
    const QString free = QStringLiteral(" (libero)");
    switch (p.kind) {
    case 0: {
        QString text = QStringLiteral("%1 × %2").arg(p.count).arg(p.spacing, 0, 'f', 3) + (p.dimensioned ? QString() : free);
        if (p.count2 > 1) text += QStringLiteral(", %1 × %2").arg(p.count2).arg(p.spacing2, 0, 'f', 3) + (p.dimensioned2 ? QString() : free);
        return text;
    }
    case 1: return QStringLiteral("%1 × %2°%3").arg(p.count).arg(p.angle, 0, 'f', 2).arg(p.spread ? QStringLiteral(" totali") : QString()) + (p.dimensioned ? QString() : free);
    default: return QStringLiteral("specchio");
    }
}

QString describeConstraint(const SketchObject &sketch, const SketchConstraint &c) {
    if (c.type == ConstraintType::Pattern) {
        const QString kind = c.pattern.kind == 0 ? QStringLiteral("lineare") : c.pattern.kind == 1 ? QStringLiteral("circolare") : QString();
        return constraintSymbol(c.type) + QLatin1Char(' ') + constraintName(c.type) + (kind.isEmpty() ? QString() : QLatin1Char(' ') + kind)
            + QStringLiteral(": ") + patternSummary(c.pattern) + QStringLiteral(" (%1 entita')").arg(c.pattern.sources.size());
    }
    QString text = constraintSymbol(c.type) + QLatin1Char(' ') + constraintName(c.type) + QStringLiteral(": ") + describeRef(sketch, c.first);
    if (c.second.kind >= 0) text += QStringLiteral(" · ") + describeRef(sketch, c.second);
    if (c.third.kind >= 0) text += QStringLiteral(" rispetto a ") + describeRef(sketch, c.third);
    if (c.type == ConstraintType::Angle) text += QStringLiteral(" = %1°").arg(c.value, 0, 'f', 4);
    else if (isDimension(c.type)) text += QStringLiteral(" = %1").arg(c.value, 0, 'f', 4);
    return text;
}

QVector<ConstraintType> applicableConstraints(const SketchObject &sketch, const QVector<ConstraintRef> &input) {
    using T = ConstraintType;
    const QVector<ConstraintRef> refs = ordered(sketch, input);
    if (refs.size() == 3) {
        ConstraintRef a, b, axis;
        return symmetryRoles(sketch, input, a, b, axis) ? QVector<T>{T::Symmetric} : QVector<T>{};
    }
    if (refs.isEmpty() || refs.size() > 2) return {};
    const Shape a = shapeOf(sketch, refs.at(0));
    if (a == Shape::None) return {};
    if (refs.size() == 1) {
        if (refs.at(0).kind == 2) return {};
        switch (a) {
        case Shape::Point: return {T::Fix};
        case Shape::Line:
            if (refs.at(0).kind == 0) return {T::Horizontal, T::Vertical, T::Distance, T::HorizontalDistance, T::VerticalDistance, T::Fix};
            return {T::Horizontal, T::Vertical, T::Distance, T::Fix};
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
        if (sameEntity) return {T::Horizontal, T::Vertical, T::Distance, T::HorizontalDistance, T::VerticalDistance};
        return {T::Coincident, T::Horizontal, T::Vertical, T::Distance, T::HorizontalDistance, T::VerticalDistance};
    }
    if (a == Shape::Point) {
        if (sameEntity) return {};
        if (b == Shape::Curve) return {T::PointOnCurve, T::Distance};
        if (b == Shape::Line) {
            QVector<T> result{T::PointOnCurve, T::Distance};
            if (refs.at(1).kind == 0) result.insert(1, T::Midpoint);
            if (isAxisReference(sketch, refs.at(1))) result << T::AxisRadius << T::AxisDiameter;
            return result;
        }
        if (b == Shape::Circle || b == Shape::Ellipse) return {T::PointOnCurve};
        return {};
    }
    if (a == Shape::Line && b == Shape::Line) {
        QVector<T> result{T::Parallel, T::Perpendicular, T::Collinear};
        if (refs.at(0).kind == 0 && refs.at(1).kind == 0) result << T::Equal;
        result << T::Angle << T::Distance;
        // Un segmento e un asse: raggio e diametro (il segmento parallelo all'asse).
        if (isAxisReference(sketch, refs.at(0)) != isAxisReference(sketch, refs.at(1)) && (refs.at(0).kind == 0 || refs.at(1).kind == 0))
            result << T::AxisRadius << T::AxisDiameter;
        return result;
    }
    if ((a == Shape::Line && b == Shape::Circle) || (a == Shape::Circle && b == Shape::Line)) return {T::Tangent};
    // Spline e NURBS: tangenti a rette e cerchi (nel punto comune o dove la
    // toccano), tra loro solo in un punto comune.
    if ((a == Shape::Curve && (b == Shape::Line || b == Shape::Circle)) || (b == Shape::Curve && (a == Shape::Line || a == Shape::Circle))) return {T::Tangent};
    if (a == Shape::Curve && b == Shape::Curve) {
        SketchConstraint probe;
        probe.type = T::Tangent;
        probe.first = refs.at(0);
        probe.second = refs.at(1);
        if (tangentContact(sketch, probe).found) return {T::Tangent};
        return {};
    }
    if (a == Shape::Circle && b == Shape::Circle) return {T::Concentric, T::Equal, T::Tangent};
    if ((a == Shape::Circle || a == Shape::Ellipse) && (b == Shape::Circle || b == Shape::Ellipse)) return {T::Concentric};
    return {};
}

SketchConstraint makeConstraint(const SketchObject &sketch, ConstraintType type, const QVector<ConstraintRef> &input) {
    if (type == ConstraintType::Symmetric) {
        SketchConstraint c;
        c.type = type;
        if (!symmetryRoles(sketch, input, c.first, c.second, c.third)) return c;
        // Segmenti: gli estremi si accoppiano come stanno ora (il riflesso piu' vicino).
        if (shapeOf(sketch, c.first) == Shape::Line) {
            const System s(sketch);
            QPointF a0, a1, p0, p1, q0, q1;
            s.line(c.third, a0, a1);
            s.line(c.first, p0, p1);
            s.line(c.second, q0, q1);
            const QPointF u = (a1 - a0) / std::max(length(a1 - a0), 1e-300);
            const auto mirror = [&](const QPointF &p) {
                const QPointF r = p - a0;
                return a0 + 2.0 * dot(r, u) * u - r;
            };
            c.value = length(mirror(p0) - q0) + length(mirror(p1) - q1) <= length(mirror(p0) - q1) + length(mirror(p1) - q0) ? 0.0 : 1.0;
        }
        return c;
    }
    const QVector<ConstraintRef> refs = ordered(sketch, input);
    SketchConstraint c;
    c.type = type;
    c.first = refs.value(0);
    if (refs.size() > 1) c.second = refs.at(1);
    // La retta dei vincoli tra un segmento e un asse va per prima (l'asse e' il riferimento).
    if (c.second.kind >= 0 && c.first.kind == 2 && c.second.kind != 2 && shapeOf(sketch, c.first) == shapeOf(sketch, c.second)) std::swap(c.first, c.second);
    // Quote dall'asse: l'asse va per secondo.
    if ((type == ConstraintType::AxisRadius || type == ConstraintType::AxisDiameter) && isAxisReference(sketch, c.first) && !isAxisReference(sketch, c.second))
        std::swap(c.first, c.second);
    if (type == ConstraintType::Fix) {
        const System s(sketch);
        for (const ConstraintRef &point : s.entityPoints(c.first)) c.positions.append(s.refPoint(point));
    } else if (type == ConstraintType::Tangent && shapeOf(sketch, c.first) == Shape::Circle && shapeOf(sketch, c.second) == Shape::Circle) {
        const System s(sketch);
        QPointF c1, c2;
        double r1, r2;
        s.circle(c.first, c1, r1);
        s.circle(c.second, c2, r2);
        c.value = length(c1 - c2) < std::max(r1, r2) ? 1.0 : 0.0;
    } else if (type == ConstraintType::Tangent && (shapeOf(sketch, c.first) == Shape::Curve || shapeOf(sketch, c.second) == Shape::Curve)
               && !tangentContact(sketch, c).found) {
        // Da che parte della retta o del cerchio sta la curva: quella dove e' piu' vicina.
        const System s(sketch);
        const bool curveFirst = shapeOf(sketch, c.first) == Shape::Curve;
        const ConstraintRef &curveRef = curveFirst ? c.first : c.second, &otherRef = curveFirst ? c.second : c.first;
        const Geometry geometry = s.geometry(curveRef.element);
        double plus = 0.0, minus = 0.0;
        if (shapeOf(sketch, otherRef) == Shape::Line) {
            QPointF q0, q1;
            s.line(otherRef, q0, q1);
            plus = curveLineGap(geometry, q0, q1, 1.0);
            minus = curveLineGap(geometry, q0, q1, -1.0);
        } else {
            QPointF center;
            double r;
            s.circle(otherRef, center, r);
            plus = curveCircleGap(geometry, center, r, 1.0);
            minus = curveCircleGap(geometry, center, r, -1.0);
        }
        c.value = std::fabs(plus) <= std::fabs(minus) ? 1.0 : -1.0;
    } else if (isDimension(type)) {
        c.value = currentMeasure(sketch, c);
    }
    return c;
}

double currentMeasure(const SketchObject &sketch, const SketchConstraint &constraint) {
    if (constraint.type == ConstraintType::Pattern) return System::storedParameter(constraint.pattern);
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
    if (c.type == ConstraintType::Pattern) {
        for (const QVector<ConstraintRef> *refs : {&c.pattern.sources, &c.pattern.copies})
            for (const ConstraintRef &r : *refs)
                if (r.kind == kind && r.element == element) return true;
        return false;
    }
    return (c.first.kind == kind && c.first.element == element) || (c.second.kind == kind && c.second.element == element)
        || (c.third.kind == kind && c.third.element == element);
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
    if (c.type == ConstraintType::Pattern) {
        // Un simbolo solo, accanto alla prima entita' ripetuta.
        if (!c.pattern.sources.isEmpty()) result.append(anchorOf(c.pattern.sources.first()));
        return result;
    }
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
    for (int i = 0; i < system.realPointCount(); ++i) {
        const QPointF p = system.point(i);
        lo[0] = std::min(lo[0], p.x());
        lo[1] = std::min(lo[1], p.y());
        hi[0] = std::max(hi[0], p.x());
        hi[1] = std::max(hi[1], p.y());
    }
    return system.realPointCount() ? std::max(1.0, std::hypot(hi[0] - lo[0], hi[1] - lo[1])) : 1.0;
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
        if (c->type == ConstraintType::Pattern) {
            patternDependencies(system, *c, block.points);
            block.evaluate = [&system, c](QVector<double> &out) { patternEquations(system, *c, out); };
            blocks.append(block);
            continue;
        }
        system.dependencies(c->first, block.points);
        system.dependencies(c->second, block.points);
        system.dependencies(c->third, block.points);
        if (c->type == ConstraintType::Concentric) {
            block.points << system.curvePoint(c->first.element, 0) << system.curvePoint(c->second.element, 0);
        }
        const TangentContact contact = tangentContact(sketch, *c);
        block.evaluate = [&system, c, contact](QVector<double> &out) { equations(system, *c, out, &contact); };
        blocks.append(block);
    }
    for (int curve = 0; curve < sketch.curves.size(); ++curve) {
        Block block;
        implicitDependencies(system, curve, block.points);
        if (block.points.isEmpty()) continue;
        block.evaluate = [&system, curve](QVector<double> &out) { implicitEquations(system, curve, out); };
        blocks.append(block);
    }
    // Una coppia di maniglie collegata definisce una sola tangente: i due
    // vettori restano collineari. Il verso opposto e' mantenuto dalla
    // manipolazione grafica; l'equazione lo conserva durante la soluzione di
    // quote e degli altri vincoli.
    for (int curve = 0; curve < sketch.curves.size(); ++curve) {
        const CurveObject &object = sketch.curves.at(curve);
        const int base = system.handleBase(curve);
        if (base < 0) continue;
        for (int point = 0; point < object.controlPoints.size(); ++point) {
            if (!object.tangentLinked.value(point)) continue;
            Block block;
            block.points << base + 2 * point << base + 2 * point + 1;
            block.evaluate = [&system, base, point](QVector<double> &out) {
                const QPointF in = system.point(base + 2 * point), away = system.point(base + 2 * point + 1);
                out << cross(in, away) / std::max({length(in), length(away), 1e-9});
            };
            blocks.append(block);
        }
    }
    for (const PointTarget &target : targets) {
        Block block;
        system.pointDependencies(target.point, block.points);
        if (block.points.isEmpty()) continue;
        const QPointF goal = target.position;
        const ConstraintRef point = target.point;
        block.evaluate = [&system, point, goal](QVector<double> &out) {
            const QPointF d = system.refPoint(point) - goal;
            out << d.x() << d.y();
        };
        blocks.append(block);
    }
    if (gauges) {
        // Le copie delle ripetizioni sono determinate dalla ripetizione: niente
        // "gauge" (fisserebbero anche la rotazione della loro sorgente o dello specchio).
        QSet<int> copies;
        for (const SketchConstraint *c : active)
            if (c->type == ConstraintType::Pattern)
                for (const ConstraintRef &r : c->pattern.copies)
                    if (r.kind == 1) copies.insert(r.element);
        for (int curve = 0; curve < sketch.curves.size(); ++curve) {
            const CurveObject &object = sketch.curves.at(curve);
            if (object.tool != DrawingTool::Circle || object.controlPoints.size() < 2 || copies.contains(curve)) continue;
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
        // Le maniglie delle spline che nessun vincolo usa restano dove sono:
        // non sono gradi di liberta' (come in SolidWorks, finche' non si vincolano).
        QSet<QPair<int, int>> usedHandles;  // (curva, indice della maniglia)
        for (const SketchConstraint *c : active) {
            for (const ConstraintRef &r : {c->first, c->second})
                if (r.kind == 1 && isHandlePoint(r.point)) usedHandles.insert({r.element, r.point - kHandlePoint});
            if (c->type == ConstraintType::Tangent) {
                const TangentContact contact = tangentContact(sketch, *c);
                if (!contact.found) continue;
                for (const ConstraintRef &r : {contact.first, contact.second})
                    if (r.kind == 1 && splineHandles(sketch.curves.at(r.element)))
                        usedHandles << QPair<int, int>{r.element, 2 * r.point} << QPair<int, int>{r.element, 2 * r.point + 1};
            }
        }
        for (int curve = 0; curve < sketch.curves.size(); ++curve) {
            const int base = system.handleBase(curve);
            if (base < 0 || copies.contains(curve)) continue;
            for (int h = 0; h < 2 * sketch.curves.at(curve).controlPoints.size(); ++h) {
                if (usedHandles.contains({curve, h})) continue;
                const QPointF current = system.point(base + h);
                Block block;
                block.points << base + h;
                block.evaluate = [&system, base, h, current](QVector<double> &out) {
                    const QPointF d = system.point(base + h) - current;
                    out << d.x() << d.y();
                };
                blocks.append(block);
            }
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
    const bool axial = c.type == ConstraintType::AxisRadius || c.type == ConstraintType::AxisDiameter;
    const bool projected = c.type == ConstraintType::HorizontalDistance || c.type == ConstraintType::VerticalDistance;
    if ((c.type != ConstraintType::Distance && !axial && !projected) || !wellFormed(sketch, c)) return false;
    const System s(sketch);
    const Shape a = shapeOf(sketch, c.first), b = shapeOf(sketch, c.second);
    if (projected) {
        // I due punti veri: la quota si disegna lungo X o Y dello schizzo.
        if (b == Shape::None) {
            s.line(c.first, p, q);
        } else {
            p = s.refPoint(c.first);
            q = s.refPoint(c.second);
        }
        return true;
    }
    if (axial) {
        // Dal punto al piede sull'asse (raggio) o al suo simmetrico (diametro).
        QPointF l0, l1, p1;
        s.line(c.second, l0, l1);
        if (a == Shape::Line) s.line(c.first, p, p1);
        else p = s.refPoint(c.first);
        const QPointF d = l1 - l0;
        const double l2 = dot(d, d);
        const QPointF foot = l2 > 0.0 ? l0 + d * (dot(p - l0, d) / l2) : l0;
        q = c.type == ConstraintType::AxisRadius ? foot : 2.0 * foot - p;
        return true;
    }
    if (a == Shape::Point && b == Shape::Curve) {
        QPointF tangent;
        p = s.refPoint(c.first);
        return curveFoot(s.geometry(c.second.element), p, q, tangent);
    }
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
        if (system.handleBase(c) >= 0)
            for (int h = 0; h < 2 * sketch.curves.at(c).controlPoints.size(); ++h) all = all && pointDefined(system.handleBase(c) + h);
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
    // I punti nuovi nello schizzo; le maniglie delle spline con i loro scarti.
    for (int i = 0; i < sketch.segments.size(); ++i) {
        const int base = system.pointIndex({0, i, 0});
        sketch.segments[i].first = system.point(base);
        sketch.segments[i].second = system.point(base + 1);
    }
    for (int curve = 0; curve < sketch.curves.size(); ++curve) {
        CurveObject &object = sketch.curves[curve];
        const int handles = system.handleBase(curve);
        for (int k = 0; k < object.controlPoints.size(); ++k) {
            const QPointF moved = system.point(system.curvePoint(curve, k));
            if (handles >= 0) {
                object.tangentHandles[k].first = moved + system.point(handles + 2 * k);
                object.tangentHandles[k].second = moved + system.point(handles + 2 * k + 1);
            } else if (k < object.tangentHandles.size()) {
                const QPointF delta = moved - object.controlPoints.at(k);
                object.tangentHandles[k].first += delta;
                object.tangentHandles[k].second += delta;
            }
            object.controlPoints[k] = moved;
        }
    }
    // I parametri liberi delle ripetizioni (passo, angolo) dove li ha portati il risolutore.
    for (int i = 0; i < sketch.geometricConstraints.size(); ++i) {
        const int base = system.patternBase(i);
        if (base < 0) continue;
        SketchPatternData &p = sketch.geometricConstraints[i].pattern;
        const QPointF value = system.point(base);
        if (!p.dimensioned && p.kind == 0) p.spacing = value.x();
        if (!p.dimensioned && p.kind == 1) p.angle = value.x();
        if (!p.dimensioned2 && p.kind == 0) p.spacing2 = value.y();
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
        if (c.type == ConstraintType::Pattern) {
            // Senza una sorgente o un riferimento la ripetizione sparisce (le
            // copie restano entita' libere); una copia eliminata esce dalla ripetizione.
            SketchPatternData &p = c.pattern;
            bool valid = true;
            for (ConstraintRef &r : p.sources) valid = valid && remap(r);
            for (ConstraintRef *r : {&p.direction, &p.direction2, &p.axis, &p.center})
                if (r->kind >= 0 && r->kind != 2) valid = valid && remap(*r);
            if (!valid) continue;
            for (ConstraintRef &r : p.copies)
                if (r.kind >= 0 && !remap(r)) r = ConstraintRef();
            c.first = p.sources.first();
            kept.append(c);
            continue;
        }
        if (!remap(c.first)) continue;
        if (c.second.kind >= 0 && !remap(c.second)) continue;
        if (c.third.kind >= 0 && !remap(c.third)) continue;
        kept.append(c);
    }
    sketch.geometricConstraints = kept;
}

}
