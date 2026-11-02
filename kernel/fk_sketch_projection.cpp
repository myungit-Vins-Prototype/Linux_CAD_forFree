#include "fk_sketch_projection.h"
#include "fk_bernstein.h"
#include "fk_nurbs.h"
#include "fk_precision.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ForgeCad::Kernel {
namespace {

constexpr double roundoff = 128.0 * kMachineEpsilon;
constexpr std::size_t maximumPieces = 1024;

bool admissiblePoint(const Vec3 &point) {
    return insideSizeBox(point.x()) && insideSizeBox(point.y()) && insideSizeBox(point.z());
}
void requirePoint(const Vec3 &point) {
    if (!admissiblePoint(point)) throw std::domain_error("La curva proiettata supera le dimensioni ammesse del modello; verifica la distanza e l'inclinazione dei piani.");
}

struct Branch {
    CurvePtr<3> curve;
    Interval parameter, coordinate;
    double start = 0.0, finish = 0.0;
    bool constant = false;
};

struct Lifting {
    Vec3 firstNormal, transverse;
    double sine = 1.0;
    Vec3 operator()(const Vec3 &a, const Vec3 &b) const {
        const Vec3 delta = b - a;
        // n2 x asse e' ortogonale a n2 e ha componente sine lungo n1.
        // Questa forma evita la sottrazione 1-cos^2 e la cancellazione
        // di due proiezioni quasi uguali quando i piani sono poco inclinati.
        return a + (dot(delta, transverse) / sine) * firstNormal;
    }
};

std::vector<Branch> branches(const std::vector<SketchProjectionSpan> &source, const Vec3 &axis) {
    std::vector<Branch> result;
    for (const auto &span : source) {
        cancellationPoint();
        if (!span.curve || !span.range.isFinite() || span.range.length() <= 0.0)
            throw std::domain_error("La proiezione richiede curve limitate non degeneri.");
        for (const auto &original : rationalBezierPieces(*span.curve, span.range)) {
            // Un fattore comune dei pesi non cambia curva o parametro.
            // Evita overflow nei prodotti omogenei e nei polinomi critici.
            std::vector<double> normalizedWeights;
            double maximumWeight = 0.0;
            for (int i = 0; i < original.poleCount(); ++i) maximumWeight = std::max(maximumWeight, original.weight(i));
            for (int i = 0; i < original.poleCount(); ++i) {
                const double weight = original.weight(i) / maximumWeight;
                if (!std::isfinite(weight) || weight <= 0.0) throw std::domain_error("Rapporto fra i pesi della curva non risolvibile in double.");
                normalizedWeights.push_back(weight);
            }
            const BSplineCurve<3> piece(original.degree(), original.knots(), original.poles(), normalizedWeights);
            std::vector<double> numerators, weights;
            for (int i = 0; i < piece.poleCount(); ++i) {
                const double w = piece.weight(i);
                if (!(w > 0.0) || !std::isfinite(w))
                    throw std::domain_error("La curva sorgente ha pesi razionali non positivi.");
                const auto &pole = piece.poles()[std::size_t(i)];
                for (int k = 0; k < 3; ++k) if (!insideSizeBox(pole[k]))
                    throw std::domain_error("La curva sorgente supera le dimensioni ammesse del modello.");
                numerators.push_back(w * dot(pole, axis));
                weights.push_back(w);
            }
            const BernsteinPolynomial n(numerators), w(weights);
            const auto critical = (n.derivative() * w - n * w.derivative()).roots();
            const Interval range = piece.domain();
            std::vector<double> split{range.lo};
            for (double root : critical) {
                const double t = range.lo + root * range.length();
                if (t > range.lo && t < range.hi && (t - split.back()) > roundoff * range.length()) split.push_back(t);
            }
            split.push_back(range.hi);
            const auto curve = std::make_shared<BSplineCurve<3>>(piece);
            for (std::size_t i = 1; i < split.size(); ++i) {
                Branch branch;
                branch.curve = curve;
                branch.parameter = {split[i - 1], split[i]};
                branch.start = dot(curve->point(branch.parameter.lo), axis);
                branch.finish = dot(curve->point(branch.parameter.hi), axis);
                branch.coordinate = {std::min(branch.start, branch.finish), std::max(branch.start, branch.finish)};
                // La classificazione costante deriva dai poli omogenei,
                // non dal solo confronto degli estremi (un arco puo' tornare).
                const double h = numerators.front() / weights.front();
                branch.constant = true;
                for (std::size_t j = 0; j < weights.size(); ++j) {
                    const double localScale = weights[j] * std::max({1.0,std::fabs(h),std::fabs(dot(piece.poles()[j],axis))});
                    branch.constant &= std::fabs(numerators[j] - h * weights[j])
                        <= roundoff * localScale;
                }
                if (!branch.constant && branch.coordinate.length() <= roundoff * std::max(1.0, std::fabs(h)))
                    throw std::domain_error("Un ramo e' troppo vicino a una tangenza per risolverlo in double senza approssimazione.");
                result.push_back(std::move(branch));
                if (result.size() > 4096) throw std::domain_error("Troppi rami nelle curve sorgenti per la proiezione esatta.");
            }
        }
    }
    return result;
}

double inverse(const Branch &branch, const Vec3 &axis, double coordinate) {
    const double scale = std::max({1.0, std::fabs(coordinate), std::fabs(branch.start), std::fabs(branch.finish)});
    if (coordinate < branch.coordinate.lo - roundoff * scale || coordinate > branch.coordinate.hi + roundoff * scale)
        throw std::domain_error("Parametro fuori dall'intervallo comune delle curve proiettate.");
    coordinate = std::clamp(coordinate, branch.coordinate.lo, branch.coordinate.hi);
    if (coordinate == branch.start) return branch.parameter.lo;
    if (coordinate == branch.finish) return branch.parameter.hi;
    double lo = branch.parameter.lo, hi = branch.parameter.hi;
    const bool increasing = branch.finish > branch.start;
    double t = lo + (hi - lo) * (coordinate - branch.start) / (branch.finish - branch.start);
    for (int iteration = 0; iteration < 100; ++iteration) {
        cancellationPoint();
        Vec3 jet[2]; branch.curve->evaluate(t, 1, jet);
        const double residual = dot(jet[0], axis) - coordinate;
        if (residual == 0.0) return t;
        if ((residual < 0.0) == increasing) lo = t; else hi = t;
        const double middle = lo + 0.5 * (hi - lo);
        if (middle == lo || middle == hi) {
            const double a = std::fabs(dot(branch.curve->point(lo), axis) - coordinate);
            const double b = std::fabs(dot(branch.curve->point(hi), axis) - coordinate);
            if (std::min(a,b) > roundoff * scale)
                throw std::domain_error("Il parametro richiesto della curva proiettata non e' rappresentabile in double con un residuo affidabile.");
            return a < b ? lo : hi;
        }
        const double slope = dot(jet[1], axis);
        const double next = slope != 0.0 ? t - residual / slope : middle;
        t = std::isfinite(next) && next > lo && next < hi ? next : middle;
    }
    throw std::domain_error("L'inversione della curva proiettata non converge in double.");
}

// Marcatore anche per catene di tratti impliciti. Non e' una NURBS fittata.
class ImplicitProjection : public Curve<3> {
public:
    CurveType type() const override { return CurveType::Other; }
};

class IntersectionBranch final : public ImplicitProjection {
public:
    IntersectionBranch(Branch driver, Branch dependent, Vec3 axis, Lifting lift, Interval common, bool swapped)
        : driver_(std::move(driver)), dependent_(std::move(dependent)), axis_(axis), lift_(lift), swapped_(swapped) {
        const double a = inverse(driver_, axis_, common.lo), b = inverse(driver_, axis_, common.hi);
        range_ = {std::min(a, b), std::max(a, b)};
    }
    Interval domain() const override { return range_; }
    void evaluate(double t, int order, Vec3 *out) const override {
        if (order < 0 || order > 3) throw std::domain_error("Curva proiettata implicita: derivate disponibili fino all'ordine 3.");
        Vec3 a[4], b[4]; driver_.curve->evaluate(t, order, a);
        const double u = inverse(dependent_, axis_, dot(a[0], axis_));
        dependent_.curve->evaluate(u, order, b);
        out[0] = swapped_ ? lift_(b[0], a[0]) : lift_(a[0], b[0]);
        requirePoint(out[0]);
        if (!order) return;
        const double q = dot(b[1], axis_);
        if (std::fabs(q) <= roundoff * std::max(1.0, norm(b[1])))
            throw std::domain_error("Tangenza comune non regolare: derivata implicita non risolvibile senza approssimazione.");
        const double v1 = dot(a[1], axis_) / q;
        const Vec3 b1 = v1 * b[1];
        out[1] = swapped_ ? lift_(b1, a[1]) : lift_(a[1], b1);
        if (order < 2) return;
        const double v2 = (dot(a[2], axis_) - dot(b[2], axis_) * v1 * v1) / q;
        const Vec3 b2 = v1 * v1 * b[2] + v2 * b[1];
        out[2] = swapped_ ? lift_(b2, a[2]) : lift_(a[2], b2);
        if (order < 3) return;
        const double v3 = (dot(a[3], axis_) - dot(b[3], axis_) * v1 * v1 * v1 - 3.0 * dot(b[2], axis_) * v1 * v2) / q;
        const Vec3 b3 = v1 * v1 * v1 * b[3] + 3.0 * v1 * v2 * b[2] + v3 * b[1];
        out[3] = swapped_ ? lift_(b3, a[3]) : lift_(a[3], b3);
    }
private:
    Branch driver_, dependent_;
    Vec3 axis_;
    Lifting lift_;
    Interval range_;
    bool swapped_ = false;
};

struct ChainPart {CurvePtr<3> curve; bool reverse = false;};
class IntersectionChain final : public ImplicitProjection {
public:
    explicit IntersectionChain(std::vector<ChainPart> parts) : parts_(std::move(parts)) {}
    Interval domain() const override { return {0.0, double(parts_.size())}; }
    std::vector<double> breakpoints(const Interval &range) const override {
        std::vector<double> values{range.lo};
        for (double t = std::floor(range.lo) + 1.0; t < range.hi; t += 1.0) values.push_back(t);
        values.push_back(range.hi); return values;
    }
    void evaluate(double t, int order, Vec3 *out) const override { evaluateSide(t, order, out, false); }
    void evaluateLeft(double t, int order, Vec3 *out) const override { evaluateSide(t, order, out, true); }
private:
    void evaluateSide(double t, int order, Vec3 *out, bool left) const {
        int k = std::clamp(int(std::floor(t)), 0, int(parts_.size()) - 1);
        if (left && t == double(k) && k > 0) --k;
        const auto &part = parts_[std::size_t(k)];
        const Interval d = part.curve->domain();
        const double factor = (part.reverse ? -1.0 : 1.0) * d.length();
        const double u = part.reverse ? d.hi + (t - k) * factor : d.lo + (t - k) * factor;
        if (left != part.reverse) part.curve->evaluateLeft(u, order, out); else part.curve->evaluate(u, order, out);
        double scale = 1.0;
        for (int i = 1; i <= order; ++i) {scale *= factor; out[i] *= scale;}
    }
    std::vector<ChainPart> parts_;
};

CurvePtr<3> liftAgainstLine(const Branch &source, const Branch &line, const Vec3 &axis, const Lifting &lift, Interval common, bool swapped) {
    const auto *spline = dynamic_cast<const BSplineCurve<3> *>(line.curve.get());
    if (!spline || spline->degree() != 1) return {};
    const Vec3 a = line.curve->point(line.parameter.lo), b = line.curve->point(line.parameter.hi);
    const double h = dot(b - a, axis);
    if (h == 0.0) return {};
    const double t0 = inverse(source, axis, common.lo), t1 = inverse(source, axis, common.hi);
    const Interval range{std::min(t0, t1), std::max(t0, t1)};
    std::vector<BSplineCurve<3>> mapped;
    for (const auto &piece : rationalBezierPieces(*source.curve, range)) {
        std::vector<Vec3> poles;
        for (const Vec3 &p : piece.poles()) {
            const Vec3 q = a + ((dot(p - a, axis)) / h) * (b - a);
            const Vec3 point = swapped ? lift(q, p) : lift(p, q);
            requirePoint(point); poles.push_back(point);
        }
        mapped.emplace_back(piece.degree(), piece.knots(), poles, piece.weights());
    }
    return std::make_shared<BSplineCurve<3>>(joinBezierPieces(mapped));
}

// Se le coordinate comuni coincidono come funzioni razionali del parametro
// normalizzato, la combinazione omogenea produce una NURBS esatta, anche
// con pesi differenti. Il confronto avviene sui coefficienti Bernstein
// dell'identita' N1 W2 - N2 W1, mai su campioni della curva.
CurvePtr<3> liftWithCommonParameter(const Branch &first, const Branch &second, const Vec3 &axis, const Lifting &lift, Interval common) {
    const auto restrict = [&](const Branch &branch) {
        const double lo = inverse(branch, axis, common.lo), hi = inverse(branch, axis, common.hi);
        auto pieces = rationalBezierPieces(*branch.curve, {std::min(lo, hi), std::max(lo, hi)});
        if (pieces.size() != 1) throw std::domain_error("Suddivisione razionale della curva sorgente non coerente.");
        auto piece = pieces.front();
        if (hi < lo) {
            auto poles = piece.poles(); auto weights = piece.weights();
            std::reverse(poles.begin(), poles.end()); std::reverse(weights.begin(), weights.end());
            piece = BSplineCurve<3>(piece.degree(), piece.knots(), poles, weights);
        }
        return piece;
    };
    const auto one = restrict(first), two = restrict(second);
    const auto weights = [](const BSplineCurve<3> &curve) {
        std::vector<double> coefficients;
        for (int i = 0; i < curve.poleCount(); ++i) coefficients.push_back(curve.weight(i));
        return BernsteinPolynomial(coefficients);
    };
    const auto coordinate = [](const BSplineCurve<3> &curve, const Vec3 &direction) {
        std::vector<double> coefficients;
        for (int i = 0; i < curve.poleCount(); ++i) coefficients.push_back(curve.weight(i) * dot(curve.poles()[std::size_t(i)], direction));
        return BernsteinPolynomial(coefficients);
    };
    const auto w1 = weights(one), w2 = weights(two);
    const auto a = coordinate(one, axis) * w2, b = coordinate(two, axis) * w1;
    const auto identity = a - b;
    const auto w = w1 * w2;
    double coordinateScale = 1.0;
    for (const auto &piece : {&one,&two}) for (const auto &pole : piece->poles()) coordinateScale = std::max(coordinateScale,std::fabs(dot(pole,axis)));
    for (std::size_t i = 0; i < identity.coefficients().size(); ++i)
        if (std::fabs(identity.coefficients()[i]) > roundoff * w.coefficients()[i] * coordinateScale) return {};
    const int degree = w.degree();
    std::vector<Vec3> numerator1(std::size_t(degree + 1)), numerator2(std::size_t(degree + 1)), poles;
    for (int k = 0; k < 3; ++k) {
        Vec3 e; e[k] = 1.0;
        const auto c1 = coordinate(one, e) * w2, c2 = coordinate(two, e) * w1;
        for (int i = 0; i <= degree; ++i) {
            numerator1[std::size_t(i)][k] = c1.coefficients()[std::size_t(i)];
            numerator2[std::size_t(i)][k] = c2.coefficients()[std::size_t(i)];
        }
    }
    for (int i = 0; i <= degree; ++i) {
        const Vec3 point = lift(numerator1[std::size_t(i)], numerator2[std::size_t(i)]) / w.coefficients()[std::size_t(i)];
        requirePoint(point); poles.push_back(point);
    }
    std::vector<double> knots(std::size_t(degree + 1), 0.0);
    knots.insert(knots.end(), std::size_t(degree + 1), 1.0);
    return std::make_shared<BSplineCurve<3>>(degree, knots, poles, w.coefficients());
}

void certifyEnvelope(const Branch &first, const Branch &second, const Vec3 &axis, const Lifting &lift,
    Interval common, int &budget, int depth = 0) {
    cancellationPoint();
    if (--budget < 0 || depth > 24)
        throw std::domain_error("Non e' possibile certificare la curva entro le dimensioni del modello in double; i piani sono troppo vicini al parallelismo.");
    const double middle = common.lo + 0.5 * common.length();
    for (double value : {common.lo, middle, common.hi})
        requirePoint(lift(first.curve->point(inverse(first, axis, value)), second.curve->point(inverse(second, axis, value))));
    const auto poles = [&](const Branch &branch) {
        const double a = inverse(branch, axis, common.lo), b = inverse(branch, axis, common.hi);
        std::vector<Vec3> points;
        for (const auto &piece : rationalBezierPieces(*branch.curve, {std::min(a,b), std::max(a,b)}))
            points.insert(points.end(), piece.poles().begin(), piece.poles().end());
        return points;
    };
    const auto a = poles(first), b = poles(second);
    bool inside = !a.empty() && !b.empty();
    for (const auto &p : a) for (const auto &q : b) {
        cancellationPoint();
        inside &= admissiblePoint(lift(p, q));
    }
    if (inside) return; // inviluppo convesso, pesi positivi: prova globale
    if (middle == common.lo || middle == common.hi) throw std::domain_error("Intervallo della proiezione non risolvibile entro le dimensioni ammesse.");
    certifyEnvelope(first,second,axis,lift,{common.lo,middle},budget,depth+1);
    certifyEnvelope(first,second,axis,lift,{middle,common.hi},budget,depth+1);
}

std::vector<CurvePtr<3>> connect(std::vector<CurvePtr<3>> pieces) {
    // Gli input coincidono geometricamente solo entro la risoluzione del
    // kernel. Il grafo connette estremi; non decide mai la geometria da mesh.
    if (pieces.size() > maximumPieces) throw std::domain_error("Troppi tratti risultanti nella proiezione esatta.");
    std::vector<Vec3> endpoints;
    for (const auto &curve : pieces) {
        cancellationPoint();
        endpoints.push_back(curve->point(curve->domain().lo)); endpoints.push_back(curve->point(curve->domain().hi));
    }
    std::vector<std::vector<int>> neighbours(endpoints.size());
    for (int i = 0; i < int(endpoints.size()); ++i) {
        cancellationPoint();
        for (int j = i; j < int(endpoints.size()); ++j)
            if (distance(endpoints[std::size_t(i)],endpoints[std::size_t(j)]) <= kLinearResolution) {
                neighbours[std::size_t(i)].push_back(j);
                if (j != i) neighbours[std::size_t(j)].push_back(i);
            }
    }
    std::vector<bool> used(pieces.size(), false);
    std::vector<CurvePtr<3>> result;
    while (true) {
        cancellationPoint();
        int start = -1; bool reverse = false;
        for (int i = 0; i < int(pieces.size()) && start < 0; ++i) if (!used[std::size_t(i)])
            for (bool high : {false, true}) if (neighbours[std::size_t(2*i+high)].size() != 2) {start = i; reverse = high; break;}
        if (start < 0) for (int i = 0; i < int(pieces.size()); ++i) if (!used[std::size_t(i)]) {start = i; break;}
        if (start < 0) break;
        std::vector<ChainPart> chain;
        int current = start;
        while (current >= 0) {
            cancellationPoint();
            used[std::size_t(current)] = true;
            chain.push_back({pieces[std::size_t(current)], reverse});
            const auto &adjacent = neighbours[std::size_t(2*current+!reverse)];
            int next = -1;
            if (adjacent.size() == 2) for (int endpoint : adjacent) if (!used[std::size_t(endpoint/2)]) {next = endpoint/2; reverse = (endpoint%2)!=0;}
            current = next;
        }
        bool rational = true;
        for (const auto &part : chain) rational &= !isImplicitSketchProjection(*part.curve);
        if (rational) {
            int degreeMax = 1;
            for (const auto &part : chain) for (const auto &piece : rationalBezierPieces(*part.curve, part.curve->domain())) degreeMax = std::max(degreeMax, piece.degree());
            std::vector<BSplineCurve<3>> exact;
            for (const auto &part : chain) {
                auto segments = standardBezierPieces(*part.curve, part.curve->domain(), degreeMax);
                if (part.reverse) {
                    std::reverse(segments.begin(), segments.end());
                    for (auto &segment : segments) {
                        auto poles = segment.poles(); auto weights = segment.weights();
                        std::reverse(poles.begin(), poles.end()); std::reverse(weights.begin(), weights.end());
                        segment = BSplineCurve<3>(degreeMax, segment.knots(), poles, weights);
                    }
                }
                exact.insert(exact.end(), segments.begin(), segments.end());
            }
            result.push_back(std::make_shared<BSplineCurve<3>>(joinBezierPieces(exact)));
        } else result.push_back(std::make_shared<IntersectionChain>(std::move(chain)));
    }
    return result;
}

}

bool isImplicitSketchProjection(const Curve<3> &curve) {
    if (dynamic_cast<const ImplicitProjection *>(&curve)) return true;
    if (curve.type() == CurveType::Trimmed) return isImplicitSketchProjection(*static_cast<const TrimmedCurve<3> &>(curve).basis());
    if (curve.type() == CurveType::Transformed) return isImplicitSketchProjection(*static_cast<const TransformedCurve &>(curve).basis());
    return false;
}

std::vector<CurvePtr<3>> projectSketchOnSketch(const std::vector<SketchProjectionSpan> &first, const Vec3 &firstNormal,
    const std::vector<SketchProjectionSpan> &second, const Vec3 &secondNormal) {
    if (first.empty() || second.empty()) throw std::domain_error("Scegli almeno una curva in ciascuno dei due schizzi.");
    if (!std::isfinite(norm(firstNormal)) || !std::isfinite(norm(secondNormal)) || norm(firstNormal) <= kAngularResolution || norm(secondNormal) <= kAngularResolution)
        throw std::domain_error("Normale del piano di schizzo non valida.");
    const Vec3 n1 = normalized(firstNormal), n2 = normalized(secondNormal);
    const Vec3 direction = cross(n1, n2);
    const double sine = norm(direction);
    if (sine <= kAngularResolution) throw std::domain_error("I due piani sono paralleli: la proiezione schizzo su schizzo non definisce una curva 3D univoca.");
    const Vec3 axis = direction / sine;
    const Lifting lift{n1, cross(n2, axis), sine};
    const auto a = branches(first, axis), b = branches(second, axis);
    if (a.size() * b.size() > 1048576) throw std::domain_error("Troppe coppie di rami nella proiezione esatta; seleziona meno entita'.");
    std::vector<CurvePtr<3>> pieces;
    int envelopeBudget = 8192;
    const auto append = [&](CurvePtr<3> curve) {
        if (pieces.size() >= maximumPieces) throw std::domain_error("La proiezione genera piu' di 1024 tratti; seleziona meno entita'.");
        pieces.push_back(std::move(curve));
    };
    struct ConstantHit {const Curve<3> *curve; Interval range; Vec3 fixed;};
    std::vector<ConstantHit> constantHits;
    for (const Branch &one : a) for (const Branch &two : b) {
        cancellationPoint();
        if (one.constant || two.constant) {
            if (one.constant && two.constant) {
                if (std::fabs(one.start - two.start) <= kLinearResolution)
                    throw std::domain_error("Le superfici proiettate coincidono su una regione: il risultato non e' una curva univoca.");
                continue;
            }
            const Branch &constant = one.constant ? one : two, &other = one.constant ? two : one;
            if (constant.start < other.coordinate.lo || constant.start > other.coordinate.hi) continue;
            const Vec3 fixed = other.curve->point(inverse(other, axis, constant.start));
            if (std::any_of(constantHits.begin(), constantHits.end(), [&](const ConstantHit &hit) {
                return hit.curve == constant.curve.get() && hit.range.lo == constant.parameter.lo && hit.range.hi == constant.parameter.hi
                    && distance(hit.fixed, fixed) <= roundoff * std::max(1.0, norm(fixed));
            })) continue;
            constantHits.push_back({constant.curve.get(), constant.parameter, fixed});
            for (const auto &part : rationalBezierPieces(*constant.curve, constant.parameter)) {
                std::vector<Vec3> poles;
                for (const Vec3 &p : part.poles()) {
                    const Vec3 point = one.constant ? lift(p, fixed) : lift(fixed, p);
                    requirePoint(point); poles.push_back(point);
                }
                if (distance(poles.front(), poles.back()) > kLinearResolution)
                    append(std::make_shared<BSplineCurve<3>>(part.degree(), part.knots(), poles, part.weights()));
            }
            continue;
        }
        const Interval common{std::max(one.coordinate.lo, two.coordinate.lo), std::min(one.coordinate.hi, two.coordinate.hi)};
        if (common.hi <= common.lo) continue;
        if (auto exact = liftAgainstLine(one, two, axis, lift, common, false)) {append(exact); continue;}
        if (auto exact = liftAgainstLine(two, one, axis, lift, common, true)) {append(exact); continue;}
        if (auto exact = liftWithCommonParameter(one, two, axis, lift, common)) {append(exact); continue;}
        certifyEnvelope(one,two,axis,lift,common,envelopeBudget);
        // Ogni meta' sceglie come driver la curva piu' vicina a una
        // generatrice critica all'estremo esterno. L'altra resta invertibile.
        const double middle = common.lo + 0.5 * common.length();
        for (bool high : {false, true}) {
            const Interval half = high ? Interval{middle, common.hi} : Interval{common.lo, middle};
            const double end = high ? common.hi : common.lo;
            const double p = inverse(one, axis, end), q = inverse(two, axis, end);
            const double slope1 = std::fabs(dot(one.curve->derivative(p), axis)) * one.parameter.length();
            const double slope2 = std::fabs(dot(two.curve->derivative(q), axis)) * two.parameter.length();
            if (std::max(slope1, slope2) <= roundoff * std::max(1.0, common.length()))
                throw std::domain_error("Le due curve hanno una tangenza comune non regolare: questo caso non e' supportato senza approssimazione.");
            auto curve = slope1 <= slope2
                ? std::make_shared<IntersectionBranch>(one, two, axis, lift, half, false)
                : std::make_shared<IntersectionBranch>(two, one, axis, lift, half, true);
            // Verifica le derivate anche agli estremi prima di pubblicare
            // il risultato: nessuna curva parziale o errore differito nel display.
            Vec3 jet[4]; curve->evaluate(curve->domain().lo, 3, jet); curve->evaluate(curve->domain().hi, 3, jet);
            append(curve);
        }
    }
    if (pieces.empty()) throw std::domain_error("Le curve selezionate non hanno un intervallo comune di proiezione; nessuna curva 3D risultante.");
    return connect(std::move(pieces));
}

}
