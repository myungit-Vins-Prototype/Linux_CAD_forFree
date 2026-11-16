#include "fk_blend_mitre.h"
#include "fk_bspline.h"
#include "fk_offset.h"
#include "fk_curve_fit_bounds.h"
#include "fk_curve_algo.h"
#include "fk_precision.h"
#include "fk_surface_algo.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
namespace ForgeCad::Kernel::detail {
namespace {
// Sistema lineare N x N con pivot parziale (falso se singolare).
template <int N> bool solveLinear(double (&a)[N][N], double (&b)[N]) {
    for (int col = 0; col < N; ++col) {
        int pivot = col;
        for (int row = col + 1; row < N; ++row)
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col]))
                pivot = row;
        if (!(std::fabs(a[pivot][col]) > 0.0))
            return false;
        if (pivot != col) {
            for (int k = 0; k < N; ++k)
                std::swap(a[col][k], a[pivot][k]);
            std::swap(b[col], b[pivot]);
        }
        for (int row = col + 1; row < N; ++row) {
            const double f = a[row][col] / a[col][col];
            for (int k = col; k < N; ++k)
                a[row][k] -= f * a[col][k];
            b[row] -= f * b[col];
        }
    }
    for (int row = N - 1; row >= 0; --row) {
        double s = b[row];
        for (int k = row + 1; k < N; ++k)
            s -= a[row][k] * b[k];
        b[row] = s / a[row][row];
    }
    return true;
}

} // namespace
void meetBlendContacts(const BlendContactFunction &a, const BlendContactFunction &b, double size, double scale, double &ta, double &tb) {
    for (int iteration = 0; iteration < 120; ++iteration) {
        const auto sa = a(ta), sb = b(tb);
        const Vec3 f = sa.point - sb.point, da = sa.derivative, db = sb.derivative;
        if (norm(f) < 1e-12 * scale)
            break;
        const double aa = dot(da, da), ab = -dot(da, db), bb = dot(db, db), det = aa * bb - ab * ab;
        if (!(std::abs(det) > 0.0))
            break;
        double du = (-dot(da, f) * bb - ab * dot(db, f)) / det;
        double dv = (aa * dot(db, f) + ab * dot(da, f)) / det;
        const double travel = std::max(std::abs(du) * norm(da), std::abs(dv) * norm(db));
        if (travel > 2.0 * size) {
            du *= 2.0 * size / travel;
            dv *= 2.0 * size / travel;
        }
        bool moved = false;
        for (double step = 1.0; step >= 1.0 / 4096.0; step *= 0.5) {
            try {
                const double x = ta + step * du, y = tb + step * dv;
                if (distance(a(x).point, b(y).point) < norm(f)) {
                    ta = x;
                    tb = y;
                    moved = true;
                    break;
                }
            } catch (const std::domain_error &) {
            }
        }
        if (!moved)
            break;
    }
}
std::shared_ptr<const BSplineSurface> orientBlendMitre(const std::shared_ptr<const BSplineSurface> &surface, bool reverse) {
    if (!reverse)
        return surface;
    const auto &s = *surface;
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int u = 0; u < s.uPoleCount(); ++u)
        for (int v = s.vPoleCount() - 1; v >= 0; --v) {
            poles.push_back(s.pole(u, v));
            weights.push_back(s.weight(u, v));
        }
    auto knots = s.vKnots();
    std::reverse(knots.begin(), knots.end());
    const double sum = s.vDomain().lo + s.vDomain().hi;
    for (double &v : knots)
        v = sum - v;
    return std::make_shared<BSplineSurface>(s.uDegree(), s.vDegree(), s.uKnots(), knots, s.uPoleCount(), s.vPoleCount(), poles, weights);
}
CurvePtr<2> originalBlendMitreUV(const CurvePtr<2> &uv, bool reversed, const Interval &vDomain) {
    cancellationPoint();
    const auto *map = dynamic_cast<const BSplineCurve<2> *>(uv.get());
    if (!map || map->degree() != 1 || map->poleCount() != 2)
        throw std::domain_error("blendEdges: mappa ISO della mitra non riconosciuta");
    if (!reversed) return uv;
    if (!vDomain.isFinite() || !(vDomain.lo < vDomain.hi) || !std::isfinite(vDomain.lo + vDomain.hi))
        throw std::domain_error("blendEdges: riflessione UV della mitra non valida");
    auto poles = map->poles();
    const double sum = vDomain.lo + vDomain.hi;
    for (Vec2 &pole : poles) pole[1] = sum - pole[1];
    return std::make_shared<BSplineCurve<2>>(1, map->knots(), poles, map->weights());
}

void installBlendMitreCertificates(std::vector<Body::BuildFace> &faces,
                                   const std::vector<BlendMitreUse> &plans) {
    for (const auto &plan : plans) {
        cancellationPoint();
        int uses = 0;
        for (auto &face : faces) {
            if (face.surface != plan.surface) continue;
            for (auto &loop : face.loops)
                for (auto &fin : loop) if (fin.edge == plan.edge) {
                    fin.pcurve = plan.pcurve;
                    fin.pcurveTolerance = plan.budget;
                    ++uses;
                }
        }
        if (uses != 1)
            throw std::domain_error("blendEdges: incidenza della mitra pianificata persa o ambigua");
    }
}

void requireEmittedBlendMitres(const Body &body, const std::vector<BlendMitreUse> &plans) {
    for (const auto &plan : plans) {
        cancellationPoint();
        int uses = 0;
        for (FaceId face : body.faces())
            for (LoopId loop : body.face(face).loops)
                for (FinId finId : body.loopFins(loop)) {
                    const Fin &fin = body.fin(finId);
                    const Edge &edge = body.edge(fin.edge);
                    if (edge.curve != plan.curve || body.face(face).surface != plan.surface) continue;
                    if (fin.pcurve != plan.pcurve || fin.pcurveTolerance != plan.budget
                        || edge.tolerance != plan.budget || edge.range.lo != plan.range.lo || edge.range.hi != plan.range.hi)
                        throw std::domain_error("blendEdges: contratto della mitra emessa modificato");
                    detail::requireBlendMitreCertificate(edge.curve, body.face(face).surface,
                                                         fin.pcurve, edge.range, plan.budget);
                    for (const auto &end : {std::make_pair(body.edgeStart(fin.edge), edge.range.lo),
                                           std::make_pair(body.edgeEnd(fin.edge), edge.range.hi)}) {
                        const Vertex &vertex = body.vertex(end.first);
                        if (!isFinite(vertex.point) || !std::isfinite(vertex.tolerance)
                            || vertex.tolerance < 0.0 || vertex.tolerance > plan.budget
                            || distance(vertex.point, edge.curve->point(end.second)) > plan.budget)
                            throw std::domain_error("blendEdges: estremo della mitra oltre il budget fissato");
                    }
                    ++uses;
                }
        if (uses != 1)
            throw std::domain_error("blendEdges: genealogia della fin di mitra persa o ambigua");
    }
}

// The affine ISO branch below has a continuous position certificate. The
// transverse Newton/Hermite continuation remains a sampled approximation;
// its residual estimates are not a proof on a whole interval.
double requireBlendMitreCertificate(const CurvePtr<3> &curve, const SurfacePtr &surface,
                                    const CurvePtr<2> &pcurve, const Interval &range, double budget) {
    cancellationPoint();
    if (!std::isfinite(budget) || !(budget > 0.0) || !range.isFinite() || !(range.lo < range.hi))
        throw std::domain_error("blendEdges: contratto continuo della mitra non valido");
    const auto *spline = dynamic_cast<const BSplineCurve<3> *>(curve.get());
    if (!spline || spline->degree() != 3 || spline->isRational())
        throw std::domain_error("blendEdges: rappresentazione della mitra certificata modificata");
    const auto data = surfaceCompositionErrorBound(surface, pcurve, range);
    if (!data.bound)
        throw std::domain_error("blendEdges: supporto della mitra certificata non riconosciuto");
    const auto &knots = spline->knots();
    const auto &poles = spline->poles();
    if (poles.size() < 4 || (poles.size() - 1) % 3 || knots.size() != poles.size() + 4)
        throw std::domain_error("blendEdges: campate della mitra certificata non riconosciute");
    double covered = range.lo, worst = 0.0;
    for (std::size_t k = 0; k + 3 < poles.size(); k += 3) {
        cancellationPoint();
        const Interval span{knots[k + 3], knots[k + 4]};
        for (int j = 0; j < 3; ++j)
            if (knots[k + j + 1] != span.lo || knots[k + j + 4] != span.hi)
                throw std::domain_error("blendEdges: nodi della mitra certificata non riconosciuti");
        if (span.hi <= range.lo || span.lo >= range.hi) continue;
        if (span.lo != covered || span.hi > range.hi || !(span.lo < span.hi))
            throw std::domain_error("blendEdges: parametro della mitra certificata modificato");
        const double error = data.bound(span, {poles[k], poles[k + 1], poles[k + 2], poles[k + 3]});
        if (!std::isfinite(error) || !(error >= 0.0) || error > budget)
            throw std::domain_error("blendEdges: mitra oltre il budget continuo");
        worst = std::max(worst, error);
        covered = span.hi;
    }
    if (covered != range.hi)
        throw std::domain_error("blendEdges: copertura della mitra certificata incompleta");
    return worst;
}
BlendCut traceBlendMitre(const Surface &S, const Surface &T, double t0, double s0, const double X[4], double scale, double fitTolerance) {
    cancellationPoint();
    if (!std::isfinite(fitTolerance) || !(fitTolerance > 0.0))
        throw std::domain_error("blendEdges: budget di fit della mitra non valido");
    const double gX = X[1] + X[3];
    if (!std::isfinite(t0) || !std::isfinite(s0) || !std::isfinite(scale) || !(scale > 0.0)
        || !std::all_of(X, X + 4, [](double value) { return std::isfinite(value); })
        || !std::isfinite(gX) || !(gX > 0.0))
        throw std::domain_error("blendEdges: angolo a mitra degenere o dati non validi");
    // These samples only SELECT the nearly tangent algorithm. They do not
    // authorize acceptance: the supported ISO branch must prove both original
    // carrier compositions, without a catch that recovers a failed proof.
    const auto pointOn = [&](const Surface &surface, double start, double end, double v, double f) {
        return surface.point(start + f * (end - start), f * v);
    };
    bool coincident = true;
    for (int i = 0; i <= 64 && coincident; ++i) {
        const double f = i / 64.0;
        const Vec3 a = pointOn(S, t0, X[0], X[1], f), b = pointOn(T, s0, X[2], X[3], f);
        coincident = isFinite(a) && isFinite(b) && distance(a, b) <= linearResolution();
        if (coincident) {
            try {
                const Vec3 na = S.normal(t0 + f * (X[0] - t0), f * X[1]);
                const Vec3 nb = T.normal(s0 + f * (X[2] - s0), f * X[3]);
                coincident = norm(cross(na, nb)) <= 1e-5;
            } catch (const std::domain_error &) {
                coincident = false;
            }
        }
    }
    if (coincident) {
        const auto *s = dynamic_cast<const BSplineSurface *>(&S);
        const auto *t = dynamic_cast<const BSplineSurface *>(&T);
        const bool isoS = (t0 == X[0]) != (X[1] == 0.0);
        const bool isoT = (s0 == X[2]) != (X[3] == 0.0);
        if (s && t && isoS && isoT) {
            const Interval range{0.0, 1.0};
            const std::vector<double> knots{0.0, 0.0, 1.0, 1.0};
            const auto uvS = std::make_shared<BSplineCurve<2>>(1, knots, std::vector<Vec2>{{t0, 0.0}, {X[0], X[1]}});
            const auto uvT = std::make_shared<BSplineCurve<2>>(1, knots, std::vector<Vec2>{{s0, 0.0}, {X[2], X[3]}});
            const SurfacePtr carrierS = std::make_shared<BSplineSurface>(*s);
            const SurfacePtr carrierT = std::make_shared<BSplineSurface>(*t);
            const auto data = surfaceCompositionErrorBound(carrierS, uvS, range);
            if (!data.bound)
                throw std::domain_error("blendEdges: composizione ISO della mitra non certificabile");
            // The cap is fixed before construction; fitting consumes only a
            // quarter of it on S. The remaining slack can cover S/T mismatch.
            const double budget = linearResolution();
            const auto curve = fitCurveCertified([&](double f) {
                const Vec2 uv = uvS->point(f);
                return S.point(uv.x(), uv.y());
            }, range, data.breaks, std::min(fitTolerance, 0.25 * budget), data.bound);
            const double gapS = requireBlendMitreCertificate(curve, carrierS, uvS, range, budget);
            const double gapT = requireBlendMitreCertificate(curve, carrierT, uvT, range, budget);
            BlendCut cut;
            cut.curve = curve;
            cut.range = range;
            cut.forward = true;
            cut.gap = std::max(gapS, gapT);
            cut.pcurveS = uvS;
            cut.pcurveT = uvT;
            cut.certificateBudget = budget;
            return cut;
        }
        // Legacy compatibility ONLY for unsupported affine compositions.
        // This averaged fit and its 129-point residual remain sampled, with
        // certificateBudget == 0. They do not certify the whole interval.
        try {
            const auto curve = fitCurve([&](double f) { return 0.5 * (pointOn(S, t0, X[0], X[1], f) + pointOn(T, s0, X[2], X[3], f)); },
                                        {0.0, 1.0}, {}, scaledAccuracy(1e-9));
            double gap = 0.0;
            for (int i = 0; i <= 128; ++i) {
                const double f = i / 128.0;
                const Vec3 p = curve->point(f);
                if (!isFinite(p)) {
                    gap = std::numeric_limits<double>::infinity();
                    break;
                }
                gap = std::max({gap, distance(p, pointOn(S, t0, X[0], X[1], f)), distance(p, pointOn(T, s0, X[2], X[3], f))});
            }
            if (gap <= linearResolution()) {
                BlendCut cut;
                cut.curve = curve;
                cut.range = {0.0, 1.0};
                cut.forward = true;
                cut.gap = gap;
                return cut;
            }
        } catch (const std::domain_error &) {
            // Unsupported sampled fit failed: try the sampled Newton continuation.
        }
    }
    struct Node {
        double g = 0.0, x[4] = {0, 0, 0, 0}, dx[4] = {0, 0, 0, 0};
        Vec3 p, dp;
    };
    const auto solve = [&](double g, Node &n) {
        for (int iteration = 0; iteration < 50; ++iteration) {
            cancellationPoint();
            Vec3 a[4], b[4];
            S.evaluate(n.x[0], n.x[1], 1, a);
            T.evaluate(n.x[2], n.x[3], 1, b);
            const Vec3 f = a[0] - b[0];
            const double f4 = n.x[1] + n.x[3] - g;
            const Vec3 c0 = a[Surface::derivativeIndex(1, 0, 1)], c1 = a[Surface::derivativeIndex(0, 1, 1)];
            const Vec3 c2 = -b[Surface::derivativeIndex(1, 0, 1)], c3 = -b[Surface::derivativeIndex(0, 1, 1)];
            double J[4][4] = {
                {c0.x(), c1.x(), c2.x(), c3.x()}, {c0.y(), c1.y(), c2.y(), c3.y()}, {c0.z(), c1.z(), c2.z(), c3.z()}, {0, 1, 0, 1}};
            double r[4] = {-f.x(), -f.y(), -f.z(), -f4};
            const bool done = norm(f) < 1e-13 * scale && std::fabs(f4) < 1e-15;
            if (!solveLinear<4>(J, r))
                return false;
            for (int k = 0; k < 4; ++k)
                n.x[k] += r[k];
            if (done)
                break;
        }
        Vec3 a[4], b[4];
        S.evaluate(n.x[0], n.x[1], 1, a);
        T.evaluate(n.x[2], n.x[3], 1, b);
        if (distance(a[0], b[0]) > 1e-10 * scale)
            return false;
        const Vec3 c0 = a[Surface::derivativeIndex(1, 0, 1)], c1 = a[Surface::derivativeIndex(0, 1, 1)];
        const Vec3 c2 = -b[Surface::derivativeIndex(1, 0, 1)], c3 = -b[Surface::derivativeIndex(0, 1, 1)];
        double J[4][4] = {
            {c0.x(), c1.x(), c2.x(), c3.x()}, {c0.y(), c1.y(), c2.y(), c3.y()}, {c0.z(), c1.z(), c2.z(), c3.z()}, {0, 1, 0, 1}};
        double r[4] = {0, 0, 0, 1};
        if (!solveLinear<4>(J, r))
            return false;
        n.g = g;
        std::copy(r, r + 4, n.dx);
        n.p = 0.5 * (a[0] + b[0]);
        n.dp = c0 * r[0] + c1 * r[1];
        return true;
    };
    // Nodo in g da quello vicino (predittore lineare), a passi dimezzati se Newton non converge.
    const auto nodeAt = [&](double g, const Node &from, Node &out) {
        Node current = from;
        double step = g - from.g;
        while (true) {
            cancellationPoint();
            const bool last = std::fabs(g - current.g) <= std::fabs(step) * (1.0 + 1e-12);
            const double target = last ? g : current.g + step;
            Node trial = current;
            for (int k = 0; k < 4; ++k)
                trial.x[k] = current.x[k] + current.dx[k] * (target - current.g);
            if (solve(target, trial)) {
                current = trial;
                if (last) {
                    out = current;
                    return true;
                }
                continue;
            }
            step *= 0.5;
            if (std::fabs(step) < 1e-12 * gX)
                return false;
        }
    };
    Node top;
    std::copy(X, X + 4, top.x);
    if (!solve(gX, top))
        throw std::domain_error("blendEdges: punto dei raccordi sui fianchi non trovato nell'angolo");
    double gMin = 1e-4 * gX;
    Node low;
    bool haveLow = false;
    // In prossimita' di A le due superfici di raccordo sono tangenti e il
    // sistema 4x4 diventa singolare. Sui loft guidati la zona numericamente
    // singolare puo' essere piu' ampia del valore fisso usato in precedenza;
    // arretra il primo nodo e lascia all'ultimo tratto tollerante la parte
    // tangente, gia' delimitata dai due punti esatti.
    for (double fraction : {1e-4, 3e-4, 1e-3, 3e-3, 1e-2, 3e-2, 1e-1}) {
        gMin = fraction * gX;
        Node direct;
        direct.g = gMin;
        const double zero[4] = {t0, 0., s0, 0.};
        for (int i = 0; i < 4; ++i)
            direct.x[i] = zero[i] + fraction * (X[i] - zero[i]);
        const bool nearSolved = solve(gMin, direct);
        const bool farSolved = nodeAt(gMin, top, low);
        const Vec3 origin = .5 * (S.point(t0, 0) + T.point(s0, 0));
        if (nearSolved && (!farSolved || distance(direct.p, origin) < distance(low.p, origin)))
            low = direct;
        if (nearSolved || farSolved) {
            haveLow = true;
            break;
        }
    }
    if (!haveLow)
        throw std::domain_error("blendEdges: curva dei raccordi nell'angolo non tracciata vicino alla faccia comune");
    std::map<double, Node> nodes{{gX, top}};
    nodes[gMin] = low;
    const auto hermite = [](const Node &a, const Node &b, double f) {
        const double h = b.g - a.g;
        const double h00 = (1 + 2 * f) * (1 - f) * (1 - f), h10 = f * (1 - f) * (1 - f), h01 = f * f * (3 - 2 * f), h11 = f * f * (f - 1);
        return h00 * a.p + (h10 * h) * a.dp + h01 * b.p + (h11 * h) * b.dp;
    };
    // Tratti tra gMin e gX: suddivisi finche' la cubica sta sulla curva vera.
    std::vector<std::pair<double, double>> pending{{gMin, gX}};
    std::vector<double> breaks;
    double approximationGap = 0.0;
    int guard = 0;
    while (!pending.empty()) {
        cancellationPoint();
        // La soglia minima 1e-4 consente fino a 2^14 foglie: il limite
        // deve contenere anche tutti i nodi interni dell'albero di raffinamento.
        if (++guard > 32767)
            throw std::domain_error("blendEdges: curva dei raccordi nell'angolo troppo complessa");
        const auto [ga, gb] = pending.back();
        pending.pop_back();
        const Node &a = nodes.at(ga), &b = nodes.at(gb);
        double error = 0.0;
        bool ok = true;
        for (double f : {0.25, 0.5, 0.75}) {
            Node exact;
            if (!nodeAt(ga + f * (gb - ga), a, exact)) {
                ok = false;
                break;
            }
            error = std::max(error, distance(hermite(a, b, f), exact.p));
        }
        // Vicino alle singolarita' dei fianchi di un loft Newton puo' non
        // risolvere uno dei campioni pur avendo estremi validi. Non suddividere
        // indefinitamente un intervallo ormai trascurabile.
        if ((!ok || error > fitTolerance) && gb - ga > 1e-4 * gX) {
            const double gm = 0.5 * (ga + gb);
            Node mid;
            // La continuazione puo' attraversare una parametrizzazione quasi
            // singolare in un solo verso. Il nodo superiore e' altrettanto
            // valido come predittore e spesso resta meglio condizionato.
            if (!nodeAt(gm, a, mid) && !nodeAt(gm, b, mid))
                throw std::domain_error("blendEdges: curva dei raccordi nell'angolo non tracciata nel tratto interno");
            nodes[gm] = mid;
            pending.push_back({gm, gb});
            pending.push_back({ga, gm});
            continue;
        }
        if (!ok || error > fitTolerance) {
            for (double f : {0.25, 0.5, 0.75}) {
                const Vec3 p = hermite(a, b, f);
                approximationGap = std::max({approximationGap, projectPoint(S, p).distance, projectPoint(T, p).distance});
            }
        }
    }
    // L'ultimo tratto fino al punto in A: stessa tangente del nodo gMin (la curva vi e' liscia).
    Node start;
    start.g = 0.0;
    start.x[0] = t0;
    start.x[2] = s0;
    start.p = 0.5 * (S.point(t0, 0.0) + T.point(s0, 0.0));
    start.dp = low.dp;
    double gap = 0.0;
    for (double f : {0.5, 0.9}) {
        Node exact;
        if (nodeAt(f * gMin, low, exact))
            gap = std::max(gap, distance(hermite(start, low, f), exact.p));
    }
    nodes[0.0] = start;
    // B-spline cubica: nodi tripli nei punti di rottura.
    std::vector<double> knots;
    std::vector<Vec3> poles;
    std::vector<double> gs;
    for (const auto &[g, node] : nodes)
        gs.push_back(g);
    for (int k = 0; k < 4; ++k)
        knots.push_back(gs.front());
    poles.push_back(nodes.at(gs.front()).p);
    for (std::size_t k = 0; k + 1 < gs.size(); ++k) {
        const Node &a = nodes.at(gs[k]), &b = nodes.at(gs[k + 1]);
        const double h = b.g - a.g;
        poles.push_back(a.p + (h / 3.0) * a.dp);
        poles.push_back(b.p - (h / 3.0) * b.dp);
        poles.push_back(b.p);
        for (int j = 0; j < (k + 2 == gs.size() ? 4 : 3); ++j)
            knots.push_back(b.g);
    }
    BlendCut cut;
    cut.curve = std::make_shared<BSplineCurve<3>>(3, knots, poles);
    cut.range = {0.0, gX};
    cut.forward = true;
    cut.gap = std::max(gap, approximationGap);
    return cut;
}

} // namespace ForgeCad::Kernel::detail
