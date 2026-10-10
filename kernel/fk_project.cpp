#include "fk_project.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>

#include "fk_boolean.h"
#include "fk_classify.h"
#include "fk_curve_algo.h"
#include "fk_curve_ops.h"
#include "fk_offset.h"
#include "fk_parallel.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

// Soluzione di un sistema 3 x 3 a colonne a, b, c (regola di Cramer).
bool solve3(const Vec3 &a, const Vec3 &b, const Vec3 &c, const Vec3 &r, double x[3]) {
    const double det = dot(a, cross(b, c));
    const double size = norm(a) * norm(b) * norm(c);
    if (!(std::fabs(det) > 1e-14 * size) || !std::isfinite(det)) return false;
    x[0] = dot(r, cross(b, c)) / det;
    x[1] = dot(a, cross(r, c)) / det;
    x[2] = dot(a, cross(b, r)) / det;
    return std::isfinite(x[0]) && std::isfinite(x[1]) && std::isfinite(x[2]);
}

// Punto della semiretta o + s d sulla superficie: Newton su S(u, v) = o + s d.
bool rayOnSurface(const Surface &surface, const Vec3 &o, const Vec3 &d, Vec2 &uv, double &s, double scale) {
    const int iu = Surface::derivativeIndex(1, 0, 1), iv = Surface::derivativeIndex(0, 1, 1);
    for (int iteration = 0; iteration < 40; ++iteration) {
        Vec3 D[4];
        surface.evaluate(uv[0], uv[1], 1, D);
        const Vec3 r = D[0] - (o + s * d);
        if (norm(r) <= 1e-13 * scale) return true;
        double x[3];
        if (!solve3(D[iu], D[iv], -1.0 * d, -1.0 * r, x)) return false;
        uv = Vec2(uv[0] + x[0], uv[1] + x[1]);
        s += x[2];
        if (iteration > 3 && norm(r) <= 1e-11 * scale) return true;  // ultimo bit
    }
    Vec3 D[4];
    surface.evaluate(uv[0], uv[1], 1, D);
    return norm(D[0] - (o + s * d)) <= 1e-10 * scale;
}

// Primo punto colpito da una semiretta.
struct Hit {
    bool hit = false;
    FaceId face;
    double s = 0.0;
    Vec2 uv;
    double gap = 0.0;  // distanza dal bordo della faccia
};

// Edge della faccia piu' vicino al punto, con il piede.
struct EdgeFoot {
    EdgeId edge;
    double parameter = 0.0, distance = std::numeric_limits<double>::infinity();
    Vec3 point;
};

EdgeFoot nearestFaceEdge(const Body &body, FaceId face, const Vec3 &p) {
    EdgeFoot best;
    for (LoopId l : body.face(face).loops)
        for (FinId f : body.loopFins(l)) {
            const EdgeId e = body.fin(f).edge;
            const Edge &edge = body.edge(e);
            const CurveProjection<3> projection = projectPoint(*edge.curve, p, edge.range);
            if (projection.distance < best.distance) {
                best.edge = e;
                best.parameter = projection.parameter;
                best.distance = projection.distance;
                best.point = projection.point;
            }
        }
    return best;
}

class Projector {
public:
    Projector(const Body &body, const Curve<3> &curve, const Interval &range, const Vec3 &direction, const ProjectionOptions &options)
        : body_(body), curve_(curve), range_(range), d_(normalized(direction)), options_(options), index_(body) {
        scale_ = std::max(1.0, index_.bounds.diagonal());
    }

    std::vector<ProjectedPiece> run();
    // Le fasi di run(), per distribuire i raggi di piu' curve sugli stessi thread
    // (le chiamate di computeAnchor e poi di computePair sono indipendenti).
    void prepare();
    std::size_t anchorCount() const { return anchors_.size(); }
    void computeAnchor(std::size_t k) { hits_[anchors_[k]] = hitAt(samples_[anchors_[k]]); }
    std::size_t pairCount() const { return anchors_.size() - 1; }
    void computePair(std::size_t k);
    std::vector<ProjectedPiece> finish();

private:
    Hit hitAt(double t) const {
        Hit h;
        const Vec3 o = curve_.point(t);
        double s = 0.0;
        FaceId f;
        if (!firstRayHit(body_, o, d_, 1e-7, s, &f, &index_)) return h;
        const Surface &surface = *body_.face(f).surface;
        const Vec3 p = o + s * d_;
        const SurfaceProjection projection = projectPoint(surface, p);
        h.hit = true;
        h.face = f;
        h.s = s;
        h.uv = Vec2(projection.u, projection.v);
        rayOnSurface(surface, o, d_, h.uv, h.s, scale_);
        h.gap = -1.0;  // calcolata quando serve (gapAt)
        return h;
    }
    // Continuazione: il punto al parametro t sulla faccia del campione `from`
    // (Newton dal suo punto), se sta ancora nella faccia.
    bool follow(const Hit &from, double t, Hit &out) const {
        Hit h = from;
        const Vec3 o = curve_.point(t);
        if (!rayOnSurface(*body_.face(from.face).surface, o, d_, h.uv, h.s, scale_) || h.s < 0.0) return false;
        if (classifyPointOnFace(body_, from.face, o + h.s * d_, 1e-7) == PointLocation::Outside) return false;
        h.gap = -1.0;
        out = h;
        return true;
    }
    // Distanza del punto del campione dal bordo della sua faccia (in cache).
    double gapAt(std::size_t i) {
        Hit &h = hits_[i];
        if (h.gap < 0.0) {
            h.gap = distanceToFaceBoundary(body_, h.face, curve_.point(samples_[i]) + h.s * d_);
        }
        return h.gap;
    }
    // Faccia colpita (o nessuna): il criterio della bisezione.
    int faceAt(double t) const {
        const Vec3 o = curve_.point(t);
        double s = 0.0;
        FaceId f;
        if (!firstRayHit(body_, o, d_, 1e-7, s, &f, &index_)) return -1;
        return f.index;
    }
    // Punto proiettato sulla faccia, da un campione vicino.
    bool pointOn(FaceId face, double t, std::size_t first, std::size_t last, Vec3 &p) const {
        const Surface &surface = *body_.face(face).surface;
        // Campione di partenza: il piu' vicino in t tra quelli del tratto.
        const auto found = std::lower_bound(samples_.begin() + std::ptrdiff_t(first), samples_.begin() + std::ptrdiff_t(last) + 1, t);
        std::size_t best = std::min(std::size_t(found - samples_.begin()), last);
        if (best > first && std::fabs(samples_[best - 1] - t) < std::fabs(samples_[best] - t)) --best;
        const Vec3 o = curve_.point(t);
        for (std::size_t k = 0; k < 3; ++k) {
            const std::size_t i = k == 0 ? best : (k == 1 ? (best > first ? best - 1 : best) : std::min(best + 1, last));
            if (!hits_[i].hit || hits_[i].face != face) continue;
            Vec2 uv = hits_[i].uv;
            double s = hits_[i].s;
            if (rayOnSurface(surface, o, d_, uv, s, scale_)) {
                p = o + s * d_;
                return true;
            }
        }
        return false;
    }
    // Passaggio tra il campione i (faccia A) e i + 1: bisezione sulla faccia
    // colpita, poi Newton sull'edge del bordo di A (o di B) vicino.
    struct Transition {
        double t = 0.0;
        bool onEdge = false;
        Vec3 point;
    };
    Transition transition(std::size_t i) const;

    const Body &body_;
    const Curve<3> &curve_;
    Interval range_;
    Vec3 d_;
    ProjectionOptions options_;
    RayFaceIndex index_;
    double scale_ = 1.0;
    std::vector<double> breaks_, samples_;
    std::vector<std::size_t> anchors_;
    std::vector<Hit> hits_;
};

Projector::Transition Projector::transition(std::size_t i) const {
    const int a = hits_[i].hit ? int(hits_[i].face.index) : -1;
    double lo = samples_[i], hi = samples_[i + 1];
    // Bisezione fino a una frazione piccola dell'intervallo; il punto esatto lo da' Newton sull'edge.
    for (int k = 0; k < 40 && hi - lo > 1e-11 * std::max(1.0, range_.length()); ++k) {
        const double mid = 0.5 * (lo + hi);
        if (faceAt(mid) == a) lo = mid;
        else hi = mid;
    }
    Transition result;
    result.t = 0.5 * (lo + hi);
    // Edge vicino al punto di passaggio, delle due facce.
    std::vector<FaceId> faces;
    if (hits_[i].hit) faces.push_back(hits_[i].face);
    if (hits_[i + 1].hit) faces.push_back(hits_[i + 1].face);
    double bestGap = std::numeric_limits<double>::infinity();
    for (FaceId face : faces) {
        Vec3 p;
        const Hit &h = hits_[i].hit && hits_[i].face == face ? hits_[i] : hits_[i + 1];
        Vec2 uv = h.uv;
        double s = h.s;
        const Vec3 o = curve_.point(result.t);
        if (!rayOnSurface(*body_.face(face).surface, o, d_, uv, s, scale_)) continue;
        p = o + s * d_;
        const EdgeFoot foot = nearestFaceEdge(body_, face, p);
        if (foot.distance > 1e-3 * scale_) continue;
        // Newton su C(t) + s d = E(tau).
        const Edge &edge = body_.edge(foot.edge);
        double t = result.t, tau = foot.parameter;
        bool converged = false;
        for (int iteration = 0; iteration < 40; ++iteration) {
            Vec3 c[2], e[2];
            curve_.evaluate(t, 1, c);
            edge.curve->evaluate(tau, 1, e);
            const Vec3 r = c[0] + s * d_ - e[0];
            if (norm(r) <= 1e-12 * scale_) {
                converged = true;
                break;
            }
            double x[3];
            if (!solve3(c[1], d_, -1.0 * e[1], -1.0 * r, x)) break;
            t += x[0];
            s += x[1];
            tau += x[2];
        }
        const double margin = 0.1 * (samples_[i + 1] - samples_[i]);
        const double edgeMargin = 1e-9 * std::max(1.0, edge.range.length());
        if (!converged || t < samples_[i] - margin || t > samples_[i + 1] + margin || tau < edge.range.lo - edgeMargin
            || tau > edge.range.hi + edgeMargin)
            continue;
        const double gap = std::fabs(t - result.t);
        if (gap < bestGap) {
            bestGap = gap;
            result.onEdge = true;
            result.point = edge.curve->point(edge.range.clamp(tau));
            result.t = std::clamp(t, samples_[i], samples_[i + 1]);
        }
    }
    return result;
}

void Projector::prepare() {
    // Campioni: almeno 4 per tratto polinomiale della curva, 128 in tutto.
    breaks_ = curve_.breakpoints(range_);
    if (breaks_.size() < 2) breaks_ = {range_.lo, range_.hi};
    const std::vector<double> &breaks = breaks_;
    const std::size_t spans = breaks.size() - 1;
    const int perSpan = int(std::max<std::size_t>(4, (128 + spans - 1) / spans));
    for (std::size_t k = 0; k < spans; ++k)
        for (int j = 0; j < perSpan; ++j) samples_.push_back(breaks[k] + (breaks[k + 1] - breaks[k]) * j / perSpan);
    samples_.push_back(range_.hi);
    hits_.resize(samples_.size());
    // Raggi esatti (firstRayHit) su un campione ogni kStride; in mezzo, se i
    // due estremi stanno sulla stessa faccia, Newton sulla faccia dal campione
    // precedente con il controllo che il punto vi resti dentro. Se il
    // controllo fallisce, raggi esatti su tutti i campioni dell'intervallo.
    constexpr std::size_t kStride = 8;
    for (std::size_t i = 0; i < samples_.size(); i += kStride) anchors_.push_back(i);
    if (anchors_.back() != samples_.size() - 1) anchors_.push_back(samples_.size() - 1);
}

void Projector::computePair(std::size_t k) {
    const std::size_t a = anchors_[k], b = anchors_[k + 1];
    // Facce diverse ai due estremi: si segue la prima, se arriva nello
    // stesso punto colpito dal raggio in b (un edge comune alle due).
    bool followed = hits_[a].hit && hits_[b].hit;
    for (std::size_t i = a + 1; followed && i < b; ++i) followed = follow(hits_[i - 1], samples_[i], hits_[i]);
    if (followed && hits_[a].face != hits_[b].face) {
        Hit end;
        followed = follow(hits_[b - 1], samples_[b], end) && std::fabs(end.s - hits_[b].s) <= 1e-7 * scale_;
    }
    if (!followed)
        for (std::size_t i = a + 1; i < b; ++i) hits_[i] = hitAt(samples_[i]);
}

std::vector<ProjectedPiece> Projector::run() {
    prepare();
    std::vector<std::exception_ptr> errors(anchorCount());
    const auto guarded = [&](const std::function<void(std::size_t)> &task) {
        return [&errors, task](std::size_t k) {
            try {
                task(k);
            } catch (...) {
                errors[k] = std::current_exception();
            }
        };
    };
    parallelFor(anchorCount(), threadCount(options_.threads), guarded([&](std::size_t k) { computeAnchor(k); }));
    for (const std::exception_ptr &error : errors)
        if (error) std::rethrow_exception(error);
    parallelFor(pairCount(), threadCount(options_.threads), guarded([&](std::size_t k) { computePair(k); }));
    for (const std::exception_ptr &error : errors)
        if (error) std::rethrow_exception(error);
    return finish();
}

std::vector<ProjectedPiece> Projector::finish() {
    // Tratti di campioni consecutivi sulla stessa faccia.
    struct Run {
        std::size_t first, last;
        FaceId face;
    };
    std::vector<Run> runs;
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        if (!hits_[i].hit) continue;
        if (!runs.empty() && runs.back().last + 1 == i && runs.back().face == hits_[i].face) runs.back().last = i;
        else runs.push_back({i, i, hits_[i].face});
    }
    // Un campione isolato su un edge (gap ~ 0) tra due tratti della stessa
    // faccia, o all'estremo, assegnato all'altra faccia dell'edge: si unisce.
    for (bool merged = true; merged;) {
        merged = false;
        for (std::size_t r = 0; r < runs.size(); ++r) {
            const Run &run = runs[r];
            if (run.first != run.last || gapAt(run.first) > options_.edgeTolerance) continue;
            const bool prev = r > 0 && runs[r - 1].last + 1 == run.first;
            const bool next = r + 1 < runs.size() && runs[r + 1].first == run.last + 1;
            if (prev && next && runs[r - 1].face == runs[r + 1].face) {
                runs[r - 1].last = runs[r + 1].last;
                runs.erase(runs.begin() + std::ptrdiff_t(r), runs.begin() + std::ptrdiff_t(r) + 2);
            } else if (prev && !next) {
                runs[r - 1].last = run.last;
                runs.erase(runs.begin() + std::ptrdiff_t(r));
            } else if (next && !prev) {
                runs[r + 1].first = run.first;
                runs.erase(runs.begin() + std::ptrdiff_t(r));
            } else {
                continue;
            }
            merged = true;
            break;
        }
    }

    // Tratti sugli edge esistenti: si guardano al piu' 33 campioni
    // distribuiti sul tratto, estremi compresi.
    struct EdgeInfo {
        bool onEdge = false, partial = false;
        double widest = 0.0;
    };
    std::vector<EdgeInfo> info(runs.size());
    for (std::size_t r = 0; r < runs.size(); ++r) {
        const Run &run = runs[r];
        // Gli estremi sul bordo sono normali (un taglio da bordo a bordo):
        // "in parte" vuol dire campioni interni sul bordo e altri lontani.
        std::size_t near = 0, total = 0, innerNear = 0;
        double widest = 0.0;
        const std::size_t count = run.last - run.first + 1, step = std::max<std::size_t>(1, (count + 31) / 32);
        for (std::size_t i = run.first;; i = std::min(i + step, run.last)) {
            ++total;
            if (gapAt(i) <= options_.edgeTolerance) {
                ++near;
                if (i != run.first && i != run.last) ++innerNear;
            }
            widest = std::max(widest, gapAt(i));
            if (i == run.last) break;
        }
        const bool endsOnEdge = gapAt(run.first) <= options_.edgeTolerance && gapAt(run.last) <= options_.edgeTolerance;
        info[r].onEdge = near == total || (endsOnEdge && widest <= options_.followTolerance);
        info[r].partial = !info[r].onEdge && innerNear >= 2;
        info[r].widest = widest;
    }
    // Tratti adiacenti tutti sugli edge (un bordo comune a due facce, che i
    // raggi colpiscono ora sull'una ora sull'altra) diventano un pezzo solo.
    struct Group {
        std::size_t firstRun, lastRun;
    };
    std::vector<Group> groups;
    for (std::size_t r = 0; r < runs.size(); ++r) {
        if (!groups.empty() && info[r].onEdge && info[groups.back().lastRun].onEdge && runs[groups.back().lastRun].last + 1 == runs[r].first)
            groups.back().lastRun = r;
        else groups.push_back({r, r});
    }
    // Passaggi agli estremi dei gruppi, in parallelo.
    std::vector<std::size_t> needed;
    for (const Group &group : groups) {
        if (runs[group.firstRun].first > 0) needed.push_back(runs[group.firstRun].first - 1);
        if (runs[group.lastRun].last + 1 < samples_.size()) needed.push_back(runs[group.lastRun].last);
    }
    std::sort(needed.begin(), needed.end());
    needed.erase(std::unique(needed.begin(), needed.end()), needed.end());
    std::vector<Transition> computed(needed.size());
    std::vector<std::exception_ptr> transitionErrors(needed.size());
    parallelFor(needed.size(), threadCount(options_.threads), [&](std::size_t k) {
        try {
            computed[k] = transition(needed[k]);
        } catch (...) {
            transitionErrors[k] = std::current_exception();
        }
    });
    for (const std::exception_ptr &error : transitionErrors)
        if (error) std::rethrow_exception(error);
    auto transitionAfter = [&](std::size_t i) -> const Transition & {
        return computed[std::size_t(std::lower_bound(needed.begin(), needed.end(), i) - needed.begin())];
    };

    std::vector<ProjectedPiece> result;
    for (const Group &group : groups) {
        const Run &firstRun = runs[group.firstRun], &lastRun = runs[group.lastRun];
        ProjectedPiece piece;
        piece.face = firstRun.face;
        double lo = samples_[firstRun.first], hi = samples_[lastRun.last];
        bool loEdge = false, hiEdge = false;
        Vec3 loPoint, hiPoint;
        if (firstRun.first > 0) {
            const Transition &t = transitionAfter(firstRun.first - 1);
            lo = t.t;
            loEdge = t.onEdge;
            loPoint = t.point;
        }
        if (lastRun.last + 1 < samples_.size()) {
            const Transition &t = transitionAfter(lastRun.last);
            hi = t.t;
            hiEdge = t.onEdge;
            hiPoint = t.point;
        }
        if (!(hi - lo > 1e-12 * std::max(1.0, range_.length()))) continue;
        piece.onEdge = info[group.firstRun].onEdge;
        for (std::size_t r = group.firstRun; r <= group.lastRun; ++r) {
            piece.partialEdge = piece.partialEdge || info[r].partial;
            if (piece.onEdge) piece.edgeGap = std::max(piece.edgeGap, info[r].widest);
        }
        // Il punto al parametro t: sulla faccia del tratto che contiene t (o
        // di uno vicino: nei gruppi sul bordo le facce condividono l'edge).
        const auto f = [&](double t) {
            Vec3 p;
            std::size_t best = group.firstRun;
            for (std::size_t r = group.firstRun; r <= group.lastRun; ++r)
                if (samples_[runs[r].first] <= t) best = r;
            for (std::size_t k = 0; k < 3; ++k) {
                const std::size_t r = k == 0 ? best : (k == 1 ? (best > group.firstRun ? best - 1 : best) : std::min(best + 1, group.lastRun));
                const std::size_t first = runs[r].first > 0 ? runs[r].first - 1 : runs[r].first;
                const std::size_t last = std::min(runs[r].last + 1, samples_.size() - 1);
                if (pointOn(runs[r].face, t, first, last, p)) return p;
            }
            throw std::domain_error("projectCurve: proiezione sulla faccia non riuscita");
        };
        std::vector<double> inner;
        for (double b : breaks_)
            if (b > lo && b < hi) inner.push_back(b);
        double deviation = 0.0;
        piece.curve = fitCurve(f, Interval{lo, hi}, inner, options_.tolerance, &deviation);
        piece.range = Interval{lo, hi};
        piece.deviation = deviation;
        // Estremi: sull'edge del passaggio, o sul bordo se la curva vi finisce.
        const auto endOnEdge = [&](double t, FaceId face, bool known, const Vec3 &edgePoint, bool &onEdge, Vec3 &point) {
            if (known) {
                onEdge = true;
                point = edgePoint;
                return;
            }
            const EdgeFoot foot = nearestFaceEdge(body_, face, piece.curve->point(t));
            if (foot.distance <= options_.edgeTolerance) {
                onEdge = true;
                point = foot.point;
            }
        };
        endOnEdge(lo, firstRun.face, loEdge, loPoint, piece.loOnEdge, piece.loPoint);
        endOnEdge(hi, lastRun.face, hiEdge, hiPoint, piece.hiOnEdge, piece.hiPoint);
        result.push_back(std::move(piece));
    }
    return result;
}

// Cubiche (anche con nodi interni di molteplicita' qualsiasi) concatenate: il
// parametro della seconda continua dalla fine della prima, nodo triplo nel giunto.
std::shared_ptr<const BSplineCurve<3>> concatenate(const std::vector<std::shared_ptr<const BSplineCurve<3>>> &curves) {
    std::vector<double> knots;
    std::vector<Vec3> poles;
    double offset = 0.0;
    for (std::size_t k = 0; k < curves.size(); ++k) {
        const BSplineCurve<3> &c = *curves[k];
        if (c.degree() != 3 || c.isRational()) throw std::domain_error("joinProjectedPieces: tratto non cubico");
        const std::vector<double> &ck = c.knots();
        const double shift = k == 0 ? 0.0 : offset - ck.front();
        if (k == 0) {
            knots.insert(knots.end(), ck.begin(), ck.end() - 1);
            poles = c.poles();
        } else {
            // Giunto: l'ultimo polo della precedente e il primo di questa (uguali entro la tolleranza) diventano la loro media.
            poles.back() = 0.5 * (poles.back() + c.poles().front());
            for (std::size_t i = 4; i + 1 < ck.size(); ++i) knots.push_back(ck[i] + shift);
            poles.insert(poles.end(), c.poles().begin() + 1, c.poles().end());
        }
        offset = ck.back() + shift;
    }
    knots.push_back(offset);
    return std::make_shared<BSplineCurve<3>>(3, std::move(knots), std::move(poles));
}

}

std::vector<ProjectedPiece> projectCurve(const Body &body, const Curve<3> &curve, const Interval &range, const Vec3 &direction,
                                         const ProjectionOptions &options) {
    if (!(norm(direction) > 0.0)) throw std::invalid_argument("projectCurve: direzione nulla");
    if (!range.isFinite() || !(range.lo < range.hi)) throw std::invalid_argument("projectCurve: intervallo non valido");
    return Projector(body, curve, range, direction, options).run();
}

std::vector<std::vector<ProjectedPiece>> projectCurves(const Body &body, const std::vector<CurveSpan> &curves, const Vec3 &direction,
                                                       const ProjectionOptions &options) {
    if (!(norm(direction) > 0.0)) throw std::invalid_argument("projectCurve: direzione nulla");
    std::vector<std::unique_ptr<Projector>> projectors;
    for (const CurveSpan &span : curves) {
        if (!span.curve || !span.range.isFinite() || !(span.range.lo < span.range.hi)) throw std::invalid_argument("projectCurve: intervallo non valido");
        projectors.push_back(std::make_unique<Projector>(body, *span.curve, span.range, direction, options));
        projectors.back()->prepare();
    }
    // Tutti i raggi di tutte le curve in un'unica coda, poi le continuazioni, poi i tratti.
    const auto runAll = [&](const std::function<std::size_t(const Projector &)> &count, const std::function<void(Projector &, std::size_t)> &task) {
        std::vector<std::pair<std::size_t, std::size_t>> jobs;
        for (std::size_t p = 0; p < projectors.size(); ++p)
            for (std::size_t k = 0; k < count(*projectors[p]); ++k) jobs.emplace_back(p, k);
        std::vector<std::exception_ptr> errors(jobs.size());
        parallelFor(jobs.size(), threadCount(options.threads), [&](std::size_t j) {
            try {
                task(*projectors[jobs[j].first], jobs[j].second);
            } catch (...) {
                errors[j] = std::current_exception();
            }
        });
        for (const std::exception_ptr &error : errors)
            if (error) std::rethrow_exception(error);
    };
    runAll([](const Projector &p) { return p.anchorCount(); }, [](Projector &p, std::size_t k) { p.computeAnchor(k); });
    runAll([](const Projector &p) { return p.pairCount(); }, [](Projector &p, std::size_t k) { p.computePair(k); });
    std::vector<std::vector<ProjectedPiece>> result(curves.size());
    runAll([](const Projector &) { return std::size_t(1); }, [&](Projector &p, std::size_t) {
        const std::size_t index = std::size_t(std::find_if(projectors.begin(), projectors.end(), [&](const auto &q) { return q.get() == &p; }) - projectors.begin());
        result[index] = p.finish();
    });
    return result;
}

std::vector<ProjectedChain> joinProjectedPieces(const std::vector<ProjectedPiece> &pieces, double tolerance) {
    std::vector<ProjectedChain> chains;
    std::vector<std::shared_ptr<const BSplineCurve<3>>> current;
    Vec3 currentEnd;
    auto flush = [&]() {
        if (current.empty()) return;
        ProjectedChain chain;
        chain.curve = current.size() == 1 ? current.front() : concatenate(current);
        const Interval domain = chain.curve->domain();
        chain.closed = distance(chain.curve->point(domain.lo), chain.curve->point(domain.hi)) <= tolerance;
        chains.push_back(chain);
        current.clear();
    };
    for (const ProjectedPiece &piece : pieces) {
        const Vec3 start = piece.curve->point(piece.range.lo);
        if (!current.empty() && distance(start, currentEnd) > tolerance) flush();
        current.push_back(piece.curve);
        currentEnd = piece.curve->point(piece.range.hi);
    }
    flush();
    // La prima e l'ultima catena si toccano: un loop proiettato che parte nel mezzo.
    if (chains.size() > 1) {
        const ProjectedChain &first = chains.front(), &last = chains.back();
        if (!first.closed && !last.closed
            && distance(last.curve->point(last.curve->domain().hi), first.curve->point(first.curve->domain().lo)) <= tolerance) {
            ProjectedChain joined;
            joined.curve = concatenate({last.curve, first.curve});
            const Interval domain = joined.curve->domain();
            joined.closed = distance(joined.curve->point(domain.lo), joined.curve->point(domain.hi)) <= tolerance;
            chains.front() = joined;
            chains.pop_back();
        }
    }
    return chains;
}

Body projectedProfileCut(const Body &body, const Frame3 &frame, const std::vector<ProfileRegion> &regions,
                         const std::vector<ProfileLoop> &chains, const Vec3 &direction, ProjectedCutMode mode,
                         ProjectedCutReport *report, const ProjectionOptions &options) {
    if (!(norm(direction) > 0.0)) throw std::invalid_argument("projectedProfileCut: direzione nulla");
    if (mode != ProjectedCutMode::SplitOnly && regions.empty())
        throw std::domain_error("il profilo non ha contorni chiusi: si puo' solo dividere le facce");
    const Vec3 d = normalized(direction);
    if (std::fabs(dot(d, frame.zDir())) < 1e-6) throw std::domain_error("la direzione della proiezione e' parallela al piano del profilo");
    ProjectedCutReport local;
    ProjectedCutReport &out = report ? *report : local;
    out = ProjectedCutReport();

    std::vector<const ProfileLoop *> loops;
    for (const ProfileRegion &region : regions) {
        loops.push_back(&region.outer);
        for (const ProfileLoop &hole : region.holes) loops.push_back(&hole);
    }
    for (const ProfileLoop &chain : chains) loops.push_back(&chain);
    std::vector<CurveSpan> spans;
    for (const ProfileLoop *loop : loops)
        for (const ProfileSegment &segment : loop->segments) spans.push_back({embedCurve(segment.curve, frame), segment.range});
    const std::vector<std::vector<ProjectedPiece>> projected = projectCurves(body, spans, d, options);
    std::vector<ImprintCurve> curves;
    bool anyHit = false;
    for (const std::vector<ProjectedPiece> &pieces : projected)
            for (const ProjectedPiece &piece : pieces) {
                anyHit = true;
                if (piece.onEdge) {
                    ++out.followedEdges;
                    out.followedGap = std::max(out.followedGap, piece.edgeGap);
                    continue;
                }
                if (piece.partialEdge)
                    throw std::domain_error("un tratto del profilo segue un bordo esistente solo in parte: dividere il tratto dove lascia il bordo");
                ImprintCurve imprint;
                imprint.face = piece.face;
                imprint.curve = piece.curve;
                imprint.range = piece.range;
                imprint.tolerance = piece.deviation > 1e-9 ? 1.5 * piece.deviation : 0.0;
                imprint.loOnEdge = piece.loOnEdge;
                imprint.hiOnEdge = piece.hiOnEdge;
                imprint.loPoint = piece.loPoint;
                imprint.hiPoint = piece.hiPoint;
                curves.push_back(imprint);
                out.deviation = std::max(out.deviation, piece.deviation);
            }
    if (!anyHit) throw std::domain_error("la proiezione del profilo non incontra il corpo");
    out.cuts = int(curves.size());

    // Pezzi da togliere: dentro una regione (proiettati sul piano del profilo)
    // e sulla faccia colpita per prima dalla semiretta che parte dal piano.
    RayFaceIndex index(body);
    const double scale = std::max(1.0, index.bounds.diagonal());
    const auto insideProfile = [&](FaceId face, const Vec3 &z) {
        const Vec3 local = frame.toLocal(z);
        const Vec3 origin = z - (local.z() / dot(d, frame.zDir())) * d;  // sul piano del profilo, lungo d
        const Vec3 onPlane = frame.toLocal(origin);
        const Vec2 q(onPlane.x(), onPlane.y());
        bool inside = false;
        for (const ProfileRegion &region : regions) {
            if (windingNumber(region.outer, q) == 0) continue;
            bool inHole = false;
            for (const ProfileLoop &hole : region.holes) inHole = inHole || windingNumber(hole, q) != 0;
            if (!inHole) inside = true;
        }
        if (!inside) return false;
        double s = 0.0;
        FaceId first;
        if (!firstRayHit(body, origin, d, 1e-7, s, &first, &index)) return false;
        return first == face && std::fabs(s - dot(z - origin, d)) <= 1e-6 * scale;
    };
    std::function<bool(FaceId, const Vec3 &)> remove;
    if (mode == ProjectedCutMode::RemoveInside) remove = insideProfile;
    else if (mode == ProjectedCutMode::KeepInside) remove = [&](FaceId face, const Vec3 &z) { return !insideProfile(face, z); };
    BooleanOptions booleanOptions;
    booleanOptions.threads = options.threads;
    booleanOptions.unifySameDomain = mode != ProjectedCutMode::SplitOnly;
    if (curves.empty() && mode == ProjectedCutMode::SplitOnly) throw std::domain_error("la proiezione non divide nessuna faccia");
    Body result = imprintCurves(body, curves, remove, booleanOptions, &out.removed);
    if (mode != ProjectedCutMode::SplitOnly && out.removed == 0) throw std::domain_error("nessuna parte del corpo cade dentro il profilo proiettato");
    if (out.followedEdges > 0)
        out.notes.push_back(std::to_string(out.followedEdges) + " tratti del profilo seguono bordi esistenti (scarto massimo "
                            + std::to_string(out.followedGap) + " mm): delimitano la regione senza nuovi tagli");
    return result;
}

}
