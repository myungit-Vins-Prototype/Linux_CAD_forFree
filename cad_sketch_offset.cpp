#include "cad_sketch_offset.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

#include "cad_curve_solver.h"
#include "cad_kernel.h"
#include "fk_bspline.h"
#include "fk_curve.h"
#include "fk_intersect.h"
#include "fk_offset.h"

namespace ForgeCad {

namespace {

using Kernel::Vec2;
using Kernel::Vec3;

constexpr double kPi = 3.14159265358979323846;
constexpr double kFitTolerance = 1e-7;  // scarto delle copie delle curve libere (kLinearResolution)

Vec2 vec(const QPointF &p) { return Vec2(p.x(), p.y()); }
QPointF point(const Vec2 &p) { return QPointF(p.x(), p.y()); }
Vec2 leftOf(const Vec2 &t) { return Vec2(-t.y(), t.x()); }
double wrap(double angle) {
    while (angle > kPi) angle -= 2.0 * kPi;
    while (angle <= -kPi) angle += 2.0 * kPi;
    return angle;
}

// Un tratto di entita' nella catena, con la geometria esatta del kernel.
struct Piece {
    SketchEntity entity;
    Kernel::CurvePtr<2> curve;
    Kernel::Interval range;
    bool forward = true;  // nella catena da range.lo a range.hi
    double start() const { return forward ? range.lo : range.hi; }
    double end() const { return forward ? range.hi : range.lo; }
    Vec2 at(double t) const { return curve->point(t); }
    Vec2 tangent(double t) const {
        Vec2 d[2];
        curve->evaluate(t, 1, d);
        const Vec2 unit = d[1] / norm(d[1]);
        return forward ? unit : -unit;
    }
    bool isLine() const { return curve->type() == Kernel::CurveType::Line; }
    bool isCircle() const { return curve->type() == Kernel::CurveType::Circle; }
    bool closed() const { return distance(at(range.lo), at(range.hi)) <= kSketchConnectionTolerance; }
};

struct Chain {
    std::vector<Piece> pieces;
    bool closed = false;
};

// Copia a distanza di un tratto, nel verso della catena.
struct OffsetPiece {
    enum Kind { Line, Arc, Circle, Free } kind = Line;
    SketchEntity source;
    bool sourceIsSegment = false, sourceIsRound = false;
    Vec2 p, q;                  // Line: inizio e fine
    Vec2 center;                // Arc, Circle
    double radius = 0.0, a0 = 0.0, a1 = 0.0;  // Arc: angoli di inizio e fine (a1 - a0 con segno: il verso)
    std::function<Vec2(double)> f;            // Free: la curva a distanza
    std::vector<double> breaks;
    double t0 = 0.0, t1 = 0.0;                // Free: parametri di inizio e fine
    Vec2 startPoint() const {
        switch (kind) {
        case Line: return p;
        case Arc:
        case Circle: return center + radius * Vec2(std::cos(a0), std::sin(a0));
        case Free: return f(t0);
        }
        return p;
    }
    Vec2 endPoint() const {
        switch (kind) {
        case Line: return q;
        case Arc: return center + radius * Vec2(std::cos(a1), std::sin(a1));
        case Circle: return center + radius * Vec2(std::cos(a0), std::sin(a0));
        case Free: return f(t1);
        }
        return q;
    }
};

std::vector<Piece> piecesOf(const SketchObject &sketch, const QVector<SketchEntity> &entities) {
    std::vector<Piece> pieces;
    for (const SketchEntity &entity : entities) {
        if (entity.kind == 0 && entity.index >= 0 && entity.index < sketch.segments.size()) {
            const Vec2 a = vec(sketch.segments.at(entity.index).first), b = vec(sketch.segments.at(entity.index).second);
            const double length = distance(a, b);
            if (length <= kSketchConnectionTolerance) continue;
            pieces.push_back({entity, std::make_shared<Kernel::Line<2>>(a, (b - a) / length), {0.0, length}});
        } else if (entity.kind == 1 && entity.index >= 0 && entity.index < sketch.curves.size()) {
            for (const Kernel::ProfileSegment &segment : curveGeometry(sketch.curves.at(entity.index)))
                pieces.push_back({entity, segment.curve, segment.range});
        }
    }
    return pieces;
}

// Catene: tratti consecutivi con un estremo comune a due soli tratti.
std::vector<Chain> chainsOf(std::vector<Piece> pieces) {
    std::vector<Vec2> ends;
    for (const Piece &piece : pieces) {
        ends.push_back(piece.at(piece.range.lo));
        ends.push_back(piece.at(piece.range.hi));
    }
    const auto degree = [&](const Vec2 &p) {
        int n = 0;
        for (const Vec2 &e : ends) n += distance(e, p) <= kSketchConnectionTolerance;
        return n;
    };
    std::vector<bool> used(pieces.size(), false);
    std::vector<Chain> chains;
    for (std::size_t seed = 0; seed < pieces.size(); ++seed) {
        if (used[seed]) continue;
        used[seed] = true;
        Chain chain;
        chain.pieces.push_back(pieces[seed]);
        if (!pieces[seed].closed()) {
            // In avanti dalla fine, poi all'indietro dall'inizio.
            for (int direction = 0; direction < 2; ++direction) {
                for (;;) {
                    const Piece &tip = direction == 0 ? chain.pieces.back() : chain.pieces.front();
                    const Vec2 p = direction == 0 ? tip.at(tip.end()) : tip.at(tip.start());
                    if (degree(p) != 2) break;
                    int next = -1;
                    bool forward = true;
                    for (std::size_t k = 0; k < pieces.size() && next < 0; ++k) {
                        if (used[k]) continue;
                        if (distance(pieces[k].at(pieces[k].range.lo), p) <= kSketchConnectionTolerance) next = int(k), forward = direction == 0;
                        else if (distance(pieces[k].at(pieces[k].range.hi), p) <= kSketchConnectionTolerance) next = int(k), forward = direction != 0;
                    }
                    if (next < 0) break;
                    used[std::size_t(next)] = true;
                    Piece piece = pieces[std::size_t(next)];
                    piece.forward = forward;
                    if (direction == 0) chain.pieces.push_back(piece);
                    else chain.pieces.insert(chain.pieces.begin(), piece);
                }
            }
        }
        const Piece &first = chain.pieces.front(), &last = chain.pieces.back();
        chain.closed = distance(first.at(first.start()), last.at(last.end())) <= kSketchConnectionTolerance;
        chains.push_back(std::move(chain));
    }
    return chains;
}

double signedArea(const Chain &chain) {
    double area = 0.0;
    Vec2 previous;
    bool started = false;
    for (const Piece &piece : chain.pieces)
        for (int k = 0; k <= 32; ++k) {
            const Vec2 p = piece.at(piece.start() + (piece.end() - piece.start()) * k / 32.0);
            if (started) area += 0.5 * (previous.x() * p.y() - p.x() * previous.y());
            previous = p;
            started = true;
        }
    const Vec2 first = chain.pieces.front().at(chain.pieces.front().start());
    area += 0.5 * (previous.x() * first.y() - first.x() * previous.y());
    return area;
}

OffsetPiece offsetOf(const SketchObject &sketch, const Piece &piece, double s) {
    OffsetPiece result;
    result.source = piece.entity;
    result.sourceIsSegment = piece.entity.kind == 0;
    if (piece.entity.kind == 1) {
        const DrawingTool tool = sketch.curves.at(piece.entity.index).tool;
        result.sourceIsRound = tool == DrawingTool::Arc || tool == DrawingTool::Circle;
    }
    if (piece.isLine()) {
        const Vec2 n = leftOf(piece.tangent(piece.start()));
        result.kind = OffsetPiece::Line;
        result.p = piece.at(piece.start()) + s * n;
        result.q = piece.at(piece.end()) + s * n;
        return result;
    }
    if (piece.isCircle()) {
        const auto &circle = static_cast<const Kernel::Circle<2> &>(*piece.curve);
        // Antiorario: la sinistra e' verso il centro.
        const double r = circle.radius() + (piece.forward ? -s : s);
        if (!(r > kSketchConnectionTolerance))
            throw std::domain_error("il raggio di un arco o di un cerchio diventa nullo o negativo: distanza troppo grande");
        const Vec2 a = piece.at(piece.start()) - circle.center(), b = piece.at(piece.end()) - circle.center();
        result.center = circle.center();
        result.radius = r;
        result.a0 = std::atan2(a.y(), a.x());
        const double sweep = piece.range.hi - piece.range.lo;
        result.a1 = result.a0 + (piece.forward ? sweep : -sweep);
        (void)b;
        result.kind = piece.closed() ? OffsetPiece::Circle : OffsetPiece::Arc;
        return result;
    }
    // Curve libere: f(t) = C(t) + s N(t), senza cuspidi (1 - s k > 0).
    const Kernel::CurvePtr<2> curve = piece.curve;
    const bool forward = piece.forward;
    result.kind = OffsetPiece::Free;
    result.f = [curve, forward, s](double t) {
        Vec2 d[2];
        curve->evaluate(t, 1, d);
        const Vec2 unit = (forward ? 1.0 : -1.0) * d[1] / norm(d[1]);
        return d[0] + s * leftOf(unit);
    };
    result.breaks = curve->breakpoints(piece.range);
    result.t0 = piece.start();
    result.t1 = piece.end();
    for (int k = 0; k <= 64; ++k) {
        const double t = piece.range.lo + piece.range.length() * k / 64.0;
        Vec2 d[3];
        curve->evaluate(t, 2, d);
        const double speed = norm(d[1]);
        const double curvature = (forward ? 1.0 : -1.0) * cross(d[1], d[2]) / (speed * speed * speed);
        if (!(1.0 - s * curvature > 1e-6))
            throw std::domain_error("la distanza supera il raggio di curvatura di una curva (la copia avrebbe una cuspide)");
    }
    return result;
}

// Curva del kernel della copia (per le intersezioni negli angoli concavi),
// prolungata per segmenti e archi.
std::pair<Kernel::CurvePtr<2>, Kernel::Interval> extended(const OffsetPiece &piece) {
    switch (piece.kind) {
    case OffsetPiece::Line: {
        const Vec2 d = (piece.q - piece.p) / norm(piece.q - piece.p);
        return {std::make_shared<Kernel::Line<2>>(piece.p, d), {-1e6, 1e6}};
    }
    case OffsetPiece::Arc:
    case OffsetPiece::Circle:
        return {std::make_shared<Kernel::Circle<2>>(Kernel::makeCircle(piece.center, piece.radius)), {0.0, 2.0 * kPi}};
    case OffsetPiece::Free: {
        const Kernel::Interval range{std::min(piece.t0, piece.t1), std::max(piece.t0, piece.t1)};
        const auto f3 = [&](double t) {
            const Vec2 p = piece.f(t);
            return Vec3(p.x(), p.y(), 0.0);
        };
        const auto fitted = Kernel::fitCurve(f3, range, piece.breaks, kFitTolerance);
        std::vector<Vec2> poles;
        for (const Vec3 &p : fitted->poles()) poles.push_back(Vec2(p.x(), p.y()));
        return {std::make_shared<Kernel::BSplineCurve<2>>(3, fitted->knots(), poles), range};
    }
    }
    return {};
}

// Sposta la fine (atEnd) o l'inizio della copia nel punto x (sulla sua curva).
void moveEnd(OffsetPiece &piece, bool atEnd, const Vec2 &x, double parameter) {
    switch (piece.kind) {
    case OffsetPiece::Line:
        (atEnd ? piece.q : piece.p) = x;
        break;
    case OffsetPiece::Arc: {
        const double angle = std::atan2(x.y() - piece.center.y(), x.x() - piece.center.x());
        double &a = atEnd ? piece.a1 : piece.a0;
        a += wrap(angle - a);
        break;
    }
    case OffsetPiece::Circle:
        break;
    case OffsetPiece::Free:
        (atEnd ? piece.t1 : piece.t0) = parameter;
        break;
    }
}

bool degenerate(const OffsetPiece &piece, const OffsetPiece &original) {
    switch (piece.kind) {
    case OffsetPiece::Line: return dot(piece.q - piece.p, original.q - original.p) <= 0.0;
    case OffsetPiece::Arc: return (piece.a1 - piece.a0) * (original.a1 - original.a0) <= 0.0;
    case OffsetPiece::Circle: return false;
    case OffsetPiece::Free: return (piece.t1 - piece.t0) * (original.t1 - original.t0) <= 0.0;
    }
    return false;
}

// Punti comuni delle due copie prolungate (rette e cerchi in forma chiusa,
// le curve libere con intersectCurves), con il parametro sulla curva libera.
struct Crossing {
    Vec2 point;
    double first = 0.0, second = 0.0;
};
std::vector<Crossing> crossings(const OffsetPiece &a, const OffsetPiece &b) {
    const auto [ca, ra] = extended(a);
    const auto [cb, rb] = extended(b);
    std::vector<Crossing> result;
    const Kernel::CurveCurveIntersection hits = Kernel::intersectCurves(*ca, ra, *cb, rb, 1e-10);
    for (const Kernel::CurveCurvePoint &hit : hits.points) result.push_back({hit.point, hit.s, hit.t});
    return result;
}

void addCoincidence(SketchObject &sketch, const ConstraintRef &a, const ConstraintRef &b) {
    SketchConstraint c;
    c.type = ConstraintType::Coincident;
    c.first = a;
    c.second = b;
    sketch.geometricConstraints.append(c);
}

void addPair(SketchObject &sketch, ConstraintType type, const ConstraintRef &a, const ConstraintRef &b) {
    SketchConstraint c;
    c.type = type;
    c.first = a;
    c.second = b;
    sketch.geometricConstraints.append(c);
}

}  // namespace

SketchEditResult offsetSketchEntities(SketchObject &sketch, const QVector<SketchEntity> &entities, const SketchOffset &offset,
                                      QVector<SketchEntity> *created) {
    SketchEditResult result;
    if (!(offset.distance > 0.0)) {
        result.error = QStringLiteral("La distanza dell'offset deve essere maggiore di zero.");
        return result;
    }
    const std::vector<Piece> pieces = piecesOf(sketch, entities);
    if (pieces.empty()) {
        result.error = QStringLiteral("Seleziona i segmenti e le curve da copiare a distanza.");
        return result;
    }
    SketchObject work = sketch;
    QVector<SketchEntity> made;
    try {
        for (const Chain &chain : chainsOf(pieces)) {
            // Lato: le catene chiuse verso l'esterno, le aperte a sinistra.
            double side = 1.0;
            if (chain.closed) side = signedArea(chain) > 0.0 ? -1.0 : 1.0;
            if (offset.reverse) side = -side;
            std::vector<double> distances{side * offset.distance};
            if (offset.bothSides) distances.push_back(-side * offset.distance);
            for (double s : distances) {
                std::vector<OffsetPiece> copies;
                for (const Piece &piece : chain.pieces) copies.push_back(offsetOf(work, piece, s));
                const std::vector<OffsetPiece> originals = copies;
                // Giunti: indice del tratto prima del giunto e arco di raccordo (se c'e').
                struct Joint {
                    bool arc = false;
                    Vec2 center, from, to;
                    double sweep = 0.0;
                };
                const std::size_t count = chain.pieces.size();
                const std::size_t joints = chain.closed && !(count == 1) ? count : count - 1;
                std::vector<Joint> after(count);
                for (std::size_t j = 0; j < joints; ++j) {
                    const std::size_t i = j, k = (j + 1) % count;
                    const Piece &pi = chain.pieces[i], &pk = chain.pieces[k];
                    const Vec2 vertex = pi.at(pi.end());
                    const Vec2 ti = pi.tangent(pi.end()), tk = pk.tangent(pk.start());
                    const double turn = cross(ti, tk);
                    if (dot(ti, tk) > 1.0 - 1e-12) {
                        // Tangenti: le copie si toccano gia' (a meno dell'arrotondamento).
                        const Vec2 middle = 0.5 * (copies[i].endPoint() + copies[k].startPoint());
                        moveEnd(copies[i], true, middle, copies[i].t1);
                        moveEnd(copies[k], false, middle, copies[k].t0);
                        continue;
                    }
                    const bool concave = turn * s > 0.0;
                    const bool free = copies[i].kind == OffsetPiece::Free || copies[k].kind == OffsetPiece::Free;
                    if (!concave && (offset.roundCorners || free)) {
                        Joint &joint = after[i];
                        joint.arc = true;
                        joint.center = vertex;
                        joint.from = copies[i].endPoint();
                        joint.to = copies[k].startPoint();
                        joint.sweep = wrap(std::atan2(joint.to.y() - vertex.y(), joint.to.x() - vertex.x())
                                           - std::atan2(joint.from.y() - vertex.y(), joint.from.x() - vertex.x()));
                        continue;
                    }
                    // Il punto comune delle copie piu' vicino al vertice.
                    const std::vector<Crossing> hits = crossings(copies[i], copies[k]);
                    if (hits.empty()) throw std::domain_error("in un angolo le copie dei tratti non si incontrano");
                    const Crossing *best = &hits.front();
                    for (const Crossing &hit : hits)
                        if (distance(hit.point, vertex) < distance(best->point, vertex)) best = &hit;
                    moveEnd(copies[i], true, best->point, best->first);
                    moveEnd(copies[k], false, best->point, best->second);
                }
                for (std::size_t i = 0; i < count; ++i)
                    if (degenerate(copies[i], originals[i]))
                        throw std::domain_error("un tratto e' piu' corto di quanto la distanza lo accorcia in un angolo concavo");

                // Entita' nuove, con i riferimenti ai loro estremi nel verso della catena.
                struct Ends {
                    ConstraintRef start, end;
                    bool closed = false;
                };
                std::vector<Ends> ends;
                const auto addArc = [&](const Vec2 &center, const Vec2 &from, const Vec2 &to, double sweep) {
                    CurveObject arc;
                    arc.tool = DrawingTool::Arc;
                    const bool ccw = sweep > 0.0;
                    arc.controlPoints = {point(center), point(ccw ? from : to), point(ccw ? to : from)};
                    work.curves.append(arc);
                    const int index = int(work.curves.size()) - 1;
                    made.append({1, index});
                    ends.push_back({{1, index, ccw ? 1 : 2}, {1, index, ccw ? 2 : 1}});
                    return index;
                };
                for (std::size_t i = 0; i < count; ++i) {
                    const OffsetPiece &copy = copies[i];
                    switch (copy.kind) {
                    case OffsetPiece::Line: {
                        work.segments.append({point(copy.p), point(copy.q)});
                        work.constraints.append(-1);
                        work.segmentLengths.append(0.0);
                        work.segmentAngles.append(-1.0);
                        const int index = int(work.segments.size()) - 1;
                        made.append({0, index});
                        ends.push_back({{0, index, 0}, {0, index, 1}});
                        if (copy.sourceIsSegment) addPair(work, ConstraintType::Parallel, {0, index, -1}, {0, copy.source.index, -1});
                        break;
                    }
                    case OffsetPiece::Arc: {
                        const int index = addArc(copy.center, copy.startPoint(), copy.endPoint(), copy.a1 - copy.a0);
                        if (copy.sourceIsRound) addPair(work, ConstraintType::Concentric, {1, index, -1}, {1, copy.source.index, -1});
                        break;
                    }
                    case OffsetPiece::Circle: {
                        CurveObject circle;
                        circle.tool = DrawingTool::Circle;
                        circle.controlPoints = {point(copy.center), point(copy.center + Vec2(copy.radius, 0.0))};
                        work.curves.append(circle);
                        const int index = int(work.curves.size()) - 1;
                        made.append({1, index});
                        ends.push_back({{}, {}, true});
                        if (copy.sourceIsRound) addPair(work, ConstraintType::Concentric, {1, index, -1}, {1, copy.source.index, -1});
                        break;
                    }
                    case OffsetPiece::Free: {
                        const Kernel::Interval range{std::min(copy.t0, copy.t1), std::max(copy.t0, copy.t1)};
                        std::vector<double> breaks;
                        for (double b : copy.breaks)
                            if (b > range.lo && b < range.hi) breaks.push_back(b);
                        const auto f3 = [&](double t) {
                            const Vec2 p = copy.f(t);
                            return Vec3(p.x(), p.y(), 0.0);
                        };
                        const auto fitted = Kernel::fitCurve(f3, range, breaks, kFitTolerance);
                        CurveObject curve;
                        curve.tool = DrawingTool::Converted;
                        curve.degree = 3;
                        for (const Vec3 &p : fitted->poles()) curve.controlPoints.append(QPointF(p.x(), p.y()));
                        for (double knot : fitted->knots()) curve.knots.append(knot);
                        work.curves.append(curve);
                        const int index = int(work.curves.size()) - 1;
                        made.append({1, index});
                        const int last = int(curve.controlPoints.size()) - 1;
                        const bool increasing = copy.t1 > copy.t0;
                        const bool closedCurve = chain.closed && count == 1;
                        ends.push_back({{1, index, increasing ? 0 : last}, {1, index, increasing ? last : 0}, closedCurve});
                        break;
                    }
                    }
                    if (after[i].arc) addArc(after[i].center, after[i].from, after[i].to, after[i].sweep);
                }
                // Coincidenze tra copie consecutive (anche la chiusura).
                for (std::size_t k = 0; k + 1 < ends.size(); ++k)
                    if (!ends[k].closed && !ends[k + 1].closed) addCoincidence(work, ends[k].end, ends[k + 1].start);
                if (chain.closed && ends.size() > 1 && !ends.back().closed && !ends.front().closed)
                    addCoincidence(work, ends.back().end, ends.front().start);
            }
        }
    } catch (const std::exception &failure) {
        result.error = QStringLiteral("Offset non riuscito: %1.").arg(QString::fromUtf8(failure.what()));
        return result;
    }
    if (offset.construction)
        for (const SketchEntity &entity : entities) {
            if (entity.kind == 0 && !work.constructionSegments.contains(entity.index)) work.constructionSegments.append(entity.index);
            if (entity.kind == 1 && entity.index >= 0 && entity.index < work.curves.size()) work.curves[entity.index].construction = true;
        }
    sketch = work;
    if (created) *created = made;
    return result;
}

}
