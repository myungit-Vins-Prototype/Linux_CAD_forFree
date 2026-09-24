#include "fk_mass.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fk_precision.h"
#include "fk_quadrature.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

using detail::Values;

// Integrandi di una faccia: |N| (area), x.N / 3 (volume), x_i (x.N) / 4,
// x_i x_j (x.N) / 5 con ij = xx, yy, zz, xy, xz, yz. N = Su x Sv, x relativo
// al punto di riferimento (vicino al body, per non perdere cifre).
constexpr std::size_t kFaceValues = 11;
// Lungo i loop, oltre a -G du: du, v du e dv (avvolgimenti e quota media).
constexpr std::size_t kLoopValues = 14;
using FaceValues = Values<kFaceValues>;
using LoopValues = Values<kLoopValues>;
using FaceEvaluation = detail::Evaluation<kFaceValues>;
using LoopEvaluation = detail::Evaluation<kLoopValues>;

// Arrotondamento ammesso, relativo alle grandezze: sulla quadratura interna
// quello dei double; lungo i loop anche il rumore dell'inversione (u, v).
constexpr double kInnerRoundoff = 50.0 * 2.220446049250313e-16;
constexpr double kLoopRoundoff = 1000.0 * 2.220446049250313e-16;
// SP-curve approssimate usate come bordo del dominio (u, v) al posto
// dell'inversione: lo scarto e' sotto quello che conta nelle proprieta' di massa.
constexpr double kMassPCurveTolerance = 1e-8;

struct Sample {
    double t;
    Vec2 uv;
};

// Una fin con la sua immagine nello spazio (u, v), con i parametri periodici
// "srotolati" in modo continuo lungo il loop: l'SP-curve esatta della fin
// traslata di `shift` (periodi interi) o, se manca, campioni ottenuti
// invertendo la superficie.
struct FinTrack {
    CurvePtr<3> curve;
    Interval range;
    bool sense = true;
    CurvePtr<2> pcurve;
    Vec2 shift;
    double acceptance = 0.0;      // distanza ammessa tra edge e superficie
    std::vector<Sample> samples;  // t crescente
};

class FaceIntegrator {
public:
    FaceIntegrator(const Body &body, FaceId face, const Vec3 &reference, double scale, double tolerance);

    // Integrali nello spazio dei parametri, sul dominio della faccia con
    // l'orientamento di (u, v): \iint_D f du dv per gli 11 integrandi.
    FaceValues domainIntegrals();

private:
    FaceEvaluation integrand(double u, double v) const;
    FaceEvaluation inner(double u, double v) const;  // G(u, v)
    FaceValues band(double vEnd) const;          // \int_{periodo} G(u, vEnd) du
    LoopValues finIntegral(const FinTrack &track) const;

    Vec2 invert(const Vec3 &p, const Vec2 &guess, double acceptance) const;
    Vec2 invertAt(const FinTrack &track, double t, const Vec3 &p) const;
    void unwrap(Vec2 &uv, const Vec2 &guess) const;
    bool bigJump(const Vec2 &a, const Vec2 &b) const;
    void refine(const FinTrack &track, const Sample &a, const Sample &b, int depth, std::vector<Sample> &out) const;
    bool degenerateAt(double v) const;

    const Surface *surface_;
    bool sense_;
    Vec3 reference_;
    double scale_, tolerance_;
    double v0_ = 0.0;
    std::vector<std::vector<FinTrack>> loops_;
};

FaceIntegrator::FaceIntegrator(const Body &body, FaceId faceId, const Vec3 &reference, double scale, double tolerance)
    : reference_(reference), scale_(scale), tolerance_(tolerance) {
    const Face &face = body.face(faceId);
    if (!face.surface) throw std::invalid_argument("massProperties: faccia senza superficie");
    surface_ = face.surface.get();
    sense_ = face.sense;

    bool first = true;
    Vec2 previous;  // ultimo campione: i loop successivi si srotolano vicino a questo
    for (LoopId l : face.loops) {
        const std::vector<FinId> fins = body.loopFins(l);
        if (fins.empty()) continue;  // vertice isolato
        std::vector<FinTrack> tracks;
        for (FinId f : fins) {
            const Fin &fin = body.fin(f);
            const Edge &edge = body.edge(fin.edge);
            if (!edge.curve) throw std::invalid_argument("massProperties: edge senza curva");
            FinTrack track;
            track.curve = edge.curve;
            track.range = edge.range;
            track.sense = fin.sense;
            track.acceptance = 10.0 * std::max(edge.tolerance, kLinearResolution) + 1e-9 * scale_;

            if (fin.pcurve && fin.pcurveTolerance <= kMassPCurveTolerance) {
                // SP-curve esatta, o approssimata molto da vicino (curve
                // d'intersezione tracciate): niente inversione, solo lo
                // srotolamento. Un edge che sta sulla superficie solo entro la
                // sua approssimazione darebbe all'inversione un rumore di quel
                // livello, e la quadratura non convergerebbe.
                const double tStart = fin.sense ? edge.range.lo : edge.range.hi;
                const double tEnd = fin.sense ? edge.range.hi : edge.range.lo;
                const Vec2 startUV = fin.pcurve->point(tStart);
                track.pcurve = fin.pcurve;
                if (first) {
                    v0_ = startUV[1];
                    first = false;
                } else {
                    Vec2 shifted = startUV;
                    unwrap(shifted, previous);
                    track.shift = shifted - startUV;
                }
                previous = fin.pcurve->point(tEnd) + track.shift;
                tracks.push_back(std::move(track));
                continue;
            }

            // Campioni nel verso della fin: nodi della curva, 4 per tratto.
            std::vector<double> ts;
            const std::vector<double> breaks = edge.curve->breakpoints(edge.range);
            for (std::size_t i = 0; i + 1 < breaks.size(); ++i)
                for (int j = 0; j < 4; ++j) ts.push_back(breaks[i] + (breaks[i + 1] - breaks[i]) * j / 4.0);
            ts.push_back(breaks.back());
            if (!fin.sense) std::reverse(ts.begin(), ts.end());

            std::vector<Sample> samples;
            for (double t : ts) {
                const Vec3 p = edge.curve->point(t);
                Sample sample{t, Vec2()};
                if (first) {
                    const SurfaceProjection projection = projectPoint(*surface_, p);
                    sample.uv = Vec2(projection.u, projection.v);
                    v0_ = projection.v;
                    first = false;
                } else {
                    sample.uv = invert(p, previous, track.acceptance);
                }
                if (!samples.empty()) refine(track, samples.back(), sample, 0, samples);
                samples.push_back(sample);
                previous = sample.uv;
            }
            if (!fin.sense) std::reverse(samples.begin(), samples.end());
            track.samples = std::move(samples);
            tracks.push_back(std::move(track));
        }
        loops_.push_back(std::move(tracks));
    }
    if (first) {
        const Interval v = surface_->vDomain();
        v0_ = v.isFinite() ? v.lo : 0.0;
    }
}

FaceEvaluation FaceIntegrator::integrand(double u, double v) const {
    Vec3 d[4];
    surface_->evaluate(u, v, 1, d);
    const Vec3 &su = d[Surface::derivativeIndex(1, 0, 1)], &sv = d[Surface::derivativeIndex(0, 1, 1)];
    const Vec3 n = cross(su, sv);
    const Vec3 x = d[0] - reference_;
    const double w = dot(x, n);
    const double area = norm(n), r = norm(x), m1 = r * area, m2 = r * m1, m3 = r * m2;
    return {{area, w / 3.0,
             x[0] * w / 4.0, x[1] * w / 4.0, x[2] * w / 4.0,
             x[0] * x[0] * w / 5.0, x[1] * x[1] * w / 5.0, x[2] * x[2] * w / 5.0,
             x[0] * x[1] * w / 5.0, x[0] * x[2] * w / 5.0, x[1] * x[2] * w / 5.0},
            {area, m1, m2, m2, m2, m3, m3, m3, m3, m3, m3}};
}

FaceEvaluation FaceIntegrator::inner(double u, double v) const {
    FaceEvaluation total;
    if (v == v0_) return total;
    const Interval range{std::min(v0_, v), std::max(v0_, v)};
    const std::vector<double> breaks = surface_->vBreakpoints(range);
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i) {
        const FaceEvaluation piece = detail::integrateVector<kFaceValues>(
            [&](double s) { return integrand(u, s); }, breaks[i], breaks[i + 1], tolerance_, kInnerRoundoff);
        for (std::size_t k = 0; k < kFaceValues; ++k) {
            total.value[k] += piece.value[k];
            total.magnitude[k] += piece.magnitude[k];
        }
    }
    if (v < v0_)
        for (double &value : total.value) value = -value;
    return total;
}

FaceValues FaceIntegrator::band(double vEnd) const {
    const double u0 = surface_->uDomain().lo;
    const std::vector<double> breaks = surface_->uBreakpoints({u0, u0 + surface_->uPeriod()});
    FaceValues total{};
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i) {
        const FaceEvaluation piece = detail::integrateVector<kFaceValues>(
            [&](double u) { return inner(u, vEnd); }, breaks[i], breaks[i + 1], tolerance_, kInnerRoundoff);
        for (std::size_t k = 0; k < kFaceValues; ++k) total[k] += piece.value[k];
    }
    return total;
}

void FaceIntegrator::unwrap(Vec2 &uv, const Vec2 &guess) const {
    if (surface_->isUPeriodic()) {
        const double period = surface_->uPeriod();
        uv[0] += period * std::round((guess[0] - uv[0]) / period);
    }
    if (surface_->isVPeriodic()) {
        const double period = surface_->vPeriod();
        uv[1] += period * std::round((guess[1] - uv[1]) / period);
    }
}

Vec2 FaceIntegrator::invert(const Vec3 &p, const Vec2 &guess, double acceptance) const {
    Vec2 uv = guess;
    if (!invertPoint(*surface_, p, uv, std::max(acceptance, 1e-4 * scale_), scale_))
        throw std::domain_error("massProperties: un edge non sta sulla superficie della sua faccia");
    unwrap(uv, guess);
    return uv;
}

Vec2 FaceIntegrator::invertAt(const FinTrack &track, double t, const Vec3 &p) const {
    const std::vector<Sample> &s = track.samples;
    auto after = std::upper_bound(s.begin(), s.end(), t, [](double value, const Sample &sample) { return value < sample.t; });
    std::size_t j = std::size_t(after - s.begin());
    j = std::clamp<std::size_t>(j, 1, s.size() - 1);
    const Sample &a = s[j - 1], &b = s[j];
    const double w = b.t > a.t ? (t - a.t) / (b.t - a.t) : 0.0;
    return invert(p, a.uv + (b.uv - a.uv) * w, track.acceptance);
}

bool FaceIntegrator::bigJump(const Vec2 &a, const Vec2 &b) const {
    return (surface_->isUPeriodic() && std::fabs(a[0] - b[0]) > 0.1 * surface_->uPeriod())
        || (surface_->isVPeriodic() && std::fabs(a[1] - b[1]) > 0.1 * surface_->vPeriod());
}

// Infittisce i campioni finche' i parametri periodici avanzano poco tra uno e
// l'altro: cosi' lo srotolamento non salta di un periodo.
void FaceIntegrator::refine(const FinTrack &track, const Sample &a, const Sample &b, int depth, std::vector<Sample> &out) const {
    if (depth >= 12 || !bigJump(a.uv, b.uv)) return;
    Sample middle{0.5 * (a.t + b.t), Vec2()};
    middle.uv = invert(track.curve->point(middle.t), a.uv + (b.uv - a.uv) * 0.5, track.acceptance);
    refine(track, a, middle, depth + 1, out);
    out.push_back(middle);
    refine(track, middle, b, depth + 1, out);
}

LoopValues FaceIntegrator::finIntegral(const FinTrack &track) const {
    double spanWidth = 1.0;  // tratto liscio corrente (vedi sotto)
    auto f = [&](double t) {
        Vec3 c[2];
        track.curve->evaluate(t, 1, c);
        Vec2 uv;
        Vec2 p[2];
        if (track.pcurve) {
            track.pcurve->evaluate(t, 1, p);
            uv = p[0] + track.shift;
        } else {
            uv = invertAt(track, t, c[0]);
        }
        Vec3 d[4];
        surface_->evaluate(uv[0], uv[1], 1, d);
        const Vec3 &su = d[Surface::derivativeIndex(1, 0, 1)], &sv = d[Surface::derivativeIndex(0, 1, 1)];
        // C' = Su u' + Sv v' (minimi quadrati se l'edge e' solo vicino alla superficie).
        const double a = dot(su, su), b = dot(su, sv), cc = dot(sv, sv), det = a * cc - b * b;
        double du = 0.0, dv = 0.0, duMagnitude = 0.0;
        if (track.pcurve) {
            du = p[1][0];
            dv = p[1][1];
            // La derivata di una B-spline su un tratto corto ha l'arrotondamento
            // dei poli diviso per la lunghezza del tratto.
            duMagnitude = std::fabs(du) + norm(p[1]) + 0.03 * (norm(uv) + 1.0) / spanWidth;
        } else if (det > 1e-24 * a * cc && a > 0.0) {
            const double ru = dot(su, c[1]), rv = dot(sv, c[1]);
            du = (cc * ru - b * rv) / det;
            dv = (a * rv - b * ru) / det;
            // u' vale ~ |C'| / |Su| anche quando si annulla (edge lungo v).
            duMagnitude = std::fabs(du) + norm(c[1]) / std::sqrt(a);
        }
        const FaceEvaluation g = inner(uv[0], uv[1]);
        // v e' noto a meno dell'arrotondamento delle coordinate: G ha quindi
        // un'incertezza f(u, v) dv, con dv ~ (|P| + dimensione) eps / |Sv|.
        const FaceEvaluation local = integrand(uv[0], uv[1]);
        const double vExtent = (norm(c[0]) + scale_) / std::max(std::sqrt(cc), 1e-300);
        LoopEvaluation out;
        for (std::size_t k = 0; k < kFaceValues; ++k) {
            out.value[k] = -g.value[k] * du;
            out.magnitude[k] = (g.magnitude[k] + local.magnitude[k] * vExtent) * duMagnitude;
        }
        // Servono solo a contare gli avvolgimenti: non guidano la quadratura.
        out.value[11] = du;
        out.value[12] = uv[1] * du;
        out.value[13] = dv;
        for (std::size_t k = kFaceValues; k < kLoopValues; ++k) out.magnitude[k] = HUGE_VAL;
        return out;
    };
    LoopValues total{};
    const std::vector<double> breaks = track.curve->breakpoints(track.range);
    for (std::size_t i = 0; i + 1 < breaks.size(); ++i) {
        spanWidth = breaks[i + 1] - breaks[i];
        const LoopEvaluation piece = detail::integrateVector<kLoopValues>(f, breaks[i], breaks[i + 1], tolerance_, kLoopRoundoff);
        for (std::size_t k = 0; k < kLoopValues; ++k) total[k] += piece.value[k];
    }
    if (!track.sense)
        for (double &value : total) value = -value;
    return total;
}

bool FaceIntegrator::degenerateAt(double v) const {
    const double period = surface_->uPeriod(), u0 = surface_->uDomain().lo;
    const Vec3 a = surface_->point(u0, v), b = surface_->point(u0 + period / 3.0, v), c = surface_->point(u0 + 2.0 * period / 3.0, v);
    const double limit = 1e-9 * scale_;
    return distance(a, b) <= limit && distance(a, c) <= limit;
}

FaceValues FaceIntegrator::domainIntegrals() {
    const double s = sense_ ? 1.0 : -1.0;
    std::vector<LoopValues> loopValues;
    FaceValues result{};
    for (const std::vector<FinTrack> &tracks : loops_) {
        LoopValues sum{};
        for (const FinTrack &track : tracks) {
            const LoopValues piece = finIntegral(track);
            for (std::size_t k = 0; k < kLoopValues; ++k) sum[k] += piece[k];
        }
        for (std::size_t k = 0; k < kFaceValues; ++k) result[k] += s * sum[k];
        loopValues.push_back(sum);
    }
    if (!surface_->isUPeriodic()) {
        if (loops_.empty()) throw std::domain_error("massProperties: faccia senza bordo su una superficie aperta");
        return result;
    }

    // Superficie periodica in u: avvolgimenti dei loop e fasce mancanti.
    // Nell'orientamento del dominio (s = verso della faccia) un loop che
    // avanza in u ha il dominio sopra di se' (v maggiori), uno che arretra sotto.
    const double uPeriod = surface_->uPeriod();
    struct Wrapping {
        int direction;  // +1: dominio sopra, -1: dominio sotto
        double meanV;
    };
    std::vector<Wrapping> wrapping;
    bool enclosing = false;  // un loop non avvolto che racchiude il dominio
    for (const LoopValues &values : loopValues) {
        if (surface_->isVPeriodic() && std::lround(values[13] / surface_->vPeriod()) != 0)
            throw std::domain_error("massProperties: loop avvolto nella direzione v (non gestito)");
        const long turns = std::lround(values[11] / uPeriod);
        if (turns == 0) {
            enclosing = enclosing || s * -values[12] > 0.0;  // area con segno in (u, v)
        } else {
            if (std::labs(turns) > 1) throw std::domain_error("massProperties: loop avvolto piu' volte");
            wrapping.push_back({int(s * double(turns)), values[12] / values[11]});
        }
    }
    auto add = [&](const FaceValues &values, double factor) {
        for (std::size_t k = 0; k < kFaceValues; ++k) result[k] += factor * values[k];
    };

    if (surface_->isVPeriodic()) {
        // Toro: il dominio vive sul rivestimento [u] x [v0, v0 + periodo].
        const double vPeriod = surface_->vPeriod();
        if (wrapping.empty()) {
            if (!enclosing) add(band(v0_ + vPeriod), 1.0);  // tutto il toro meno i fori
        } else if (wrapping.size() == 2 && wrapping[0].direction != wrapping[1].direction) {
            // Fascia: il loop "superiore" va portato sopra quello inferiore,
            // entro un periodo; spostarlo di k periodi aggiunge k tori interi.
            const Wrapping &lower = wrapping[0].direction > 0 ? wrapping[0] : wrapping[1];
            const Wrapping &upper = wrapping[0].direction > 0 ? wrapping[1] : wrapping[0];
            const double k = std::ceil((lower.meanV - upper.meanV) / vPeriod);
            if (k != 0.0) add(band(v0_ + vPeriod), k);
        } else {
            throw std::domain_error("massProperties: loop avvolti in modo non gestito su una superficie doppiamente periodica");
        }
        return result;
    }

    bool topIn, bottomIn;
    if (wrapping.empty()) {
        topIn = bottomIn = !enclosing;
    } else {
        std::sort(wrapping.begin(), wrapping.end(), [](const Wrapping &a, const Wrapping &b) { return a.meanV < b.meanV; });
        topIn = wrapping.back().direction > 0;
        bottomIn = wrapping.front().direction < 0;
    }
    if (!topIn && !bottomIn) return result;

    // Estremi in v: poli della sfera, vertice del cono, estremi del meridiano.
    double top = surface_->vDomain().hi, bottom = surface_->vDomain().lo;
    if (surface_->type() == SurfaceType::Cone) {
        const auto *cone = static_cast<const ConicalSurface *>(surface_);
        const double apex = -cone->referenceRadius() / std::sin(cone->semiAngle());
        if (v0_ > apex) bottom = apex;
        else top = apex;
    }
    auto checkPole = [&](double v) {
        if (!std::isfinite(v) || !degenerateAt(v)) throw std::domain_error("massProperties: faccia aperta o illimitata");
    };
    if (topIn) {
        checkPole(top);
        add(band(top), 1.0);
    }
    if (bottomIn) {
        checkPole(bottom);
        add(band(bottom), -1.0);
    }
    return result;
}

// Punto di riferimento (centro dei vertici e dei punti medi degli edge) e
// dimensione del body.
void referenceFrame(const Body &body, Vec3 &reference, double &scale) {
    std::vector<Vec3> points;
    for (VertexId v : body.vertices()) points.push_back(body.vertex(v).point);
    for (EdgeId e : body.edges()) {
        const Edge &edge = body.edge(e);
        if (edge.curve && edge.range.isFinite()) points.push_back(edge.curve->point(0.5 * (edge.range.lo + edge.range.hi)));
    }
    Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
    for (const Vec3 &p : points)
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], p[i]);
            hi[i] = std::max(hi[i], p[i]);
        }
    if (points.empty()) {
        reference = Vec3();
        scale = 1.0;
        return;
    }
    reference = 0.5 * (lo + hi);
    scale = std::max(distance(lo, hi), 1e-3);
}

}

MassProperties massProperties(const Body &body, double relativeTolerance) {
    Vec3 reference;
    double scale;
    referenceFrame(body, reference, scale);
    FaceValues flux{};
    double area = 0.0;
    for (FaceId f : body.faces()) {
        FaceIntegrator integrator(body, f, reference, scale, relativeTolerance);
        const FaceValues values = integrator.domainIntegrals();
        const double s = body.face(f).sense ? 1.0 : -1.0;
        area += values[0];
        for (std::size_t k = 1; k < kFaceValues; ++k) flux[k] += s * values[k];
    }

    MassProperties result;
    result.volume = flux[1];
    result.area = area;
    if (!(std::fabs(result.volume) > 0.0)) return result;
    const Vec3 d(flux[2] / result.volume, flux[3] / result.volume, flux[4] / result.volume);
    result.centroid = reference + d;
    // Momenti secondi rispetto al baricentro: M = S2 - V d d^T.
    const double m[3][3] = {{flux[5], flux[8], flux[9]}, {flux[8], flux[6], flux[10]}, {flux[9], flux[10], flux[7]}};
    double c[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c[i][j] = m[i][j] - result.volume * d[i] * d[j];
    const double trace = c[0][0] + c[1][1] + c[2][2];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) result.inertia[i][j] = (i == j ? trace : 0.0) - c[i][j];
    return result;
}

double faceArea(const Body &body, FaceId face, double relativeTolerance) {
    Vec3 reference;
    double scale;
    referenceFrame(body, reference, scale);
    return FaceIntegrator(body, face, reference, scale, relativeTolerance).domainIntegrals()[0];
}

}
