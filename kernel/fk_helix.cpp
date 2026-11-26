#include "fk_helix.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "fk_curve_algo.h"
#include "fk_hermite.h"
#include "fk_cancel.h"
#include "fk_nurbs.h"
#include "fk_topology.h"

namespace ForgeCad::Kernel {

HelixCurve::HelixCurve(const HelixSpec &spec) : spec_(spec) {
    if (!(spec.turns > 0.0) || !std::isfinite(spec.turns)) throw std::invalid_argument("elica: il numero di giri deve essere positivo");
    if (!(spec.pitch > 0.0) || !std::isfinite(spec.pitch)) throw std::invalid_argument("elica: il passo deve essere positivo");
    if (!(spec.radius >= 0.0) || !std::isfinite(spec.radius)) throw std::invalid_argument("elica: raggio non valido");
    if (!(std::fabs(spec.taper) < kHalfPi - 1e-9)) throw std::invalid_argument("elica: angolo di conicita' non valido");
    sense_ = spec.leftHanded ? -1.0 : 1.0;
    if (spec.spiral) {
        slope_ = spec.pitch / kTwoPi;
    } else {
        rise_ = spec.pitch / kTwoPi;
        slope_ = rise_ * std::tan(spec.taper);
    }
    const double end = radiusAt(domain().hi);
    if (end < -1e-12 * std::max(1.0, spec.radius)) throw std::invalid_argument("elica: il raggio si annulla prima della fine (conicita' troppo forte)");
    if (spec.radius <= 0.0 && slope_ <= 0.0) throw std::invalid_argument("elica: raggio nullo");
}

void HelixCurve::evaluate(double t, int order, Vec3 *out) const {
    const Frame3 &f = spec_.frame;
    const double theta = spec_.startAngle + sense_ * t;
    const double r = radiusAt(t);
    // E_n = d^n/dt^n (cos theta X + sin theta Y) = s^n (cos(theta + n pi/2) X + sin(theta + n pi/2) Y).
    const auto radial = [&](int n) {
        const double angle = theta + n * kHalfPi;
        const double scale = (n % 2 == 1) ? sense_ : 1.0;
        return scale * (std::cos(angle) * f.xDir() + std::sin(angle) * f.yDir());
    };
    Vec3 previous = radial(0);
    out[0] = f.origin() + r * previous + (rise_ * t) * f.zDir();
    for (int n = 1; n <= order; ++n) {
        const Vec3 current = radial(n);
        out[n] = r * current + (n * slope_) * previous;
        if (n == 1) out[n] += rise_ * f.zDir();
        previous = current;
    }
}

double HelixCurve::length() const {
    if (slope_ == 0.0) return spec_.turns * std::sqrt(std::pow(kTwoPi * spec_.radius, 2) + spec_.pitch * spec_.pitch);
    return arcLength(*this, domain(), 1e-13);
}

BSplineCurve<3> helixBSpline(const HelixCurve &helix, double tolerance) {
    const Interval d = helix.domain();
    const detail::RowSpline spline = detail::fitQuinticRows(
        [&](double t, bool, Vec3 *out) { helix.evaluate(t, 2, out); }, 1, {d.lo, d.hi}, tolerance, kHalfPi);
    return detail::rowCurve(spline, 0);
}

namespace {

const RevolutionSurface &revolutionHelixCarrier(const Body &body, FaceId face) {
    if (!body.contains(face) || !body.face(face).surface || body.face(face).surface->type() != SurfaceType::Revolution)
        throw std::invalid_argument("Elica su faccia: scegli una faccia di rivoluzione.");
    return static_cast<const RevolutionSurface &>(*body.face(face).surface);
}

Interval ringBand(const Body &body, FaceId face, const RevolutionSurface &surface) {
    const auto invalid = [] {
        return std::invalid_argument("Elica su faccia: serve una fascia di rivoluzione intera delimitata da due anelli circolari.");
    };
    const auto &loops = body.face(face).loops;
    if (loops.size() != 2) throw invalid();
    const Vec3 &axis = surface.axisDirection();
    const Vec3 &origin = surface.axisPoint();
    double limits[2];
    for (int i = 0; i < 2; ++i) {
        const auto fins = body.loopFins(loops[i]);
        if (fins.empty()) throw invalid();
        double angle = 0.0;
        for (std::size_t j = 0; j < fins.size(); ++j) {
            const auto &fin = body.fin(fins[j]);
            const auto &edge = body.edge(fin.edge);
            const Curve<3> *basis = edge.curve.get();
            while (basis && basis->type() == CurveType::Trimmed)
                basis = static_cast<const TrimmedCurve<3> &>(*basis).basis().get();
            if (!basis || basis->type() != CurveType::Circle) throw invalid();
            const auto &circle = static_cast<const Circle<3> &>(*basis);
            const Vec3 fromAxis = circle.center() - origin;
            const double z = dot(fromAxis, axis);
            const Vec3 normal = cross(circle.xAxis(), circle.yAxis());
            const double tolerance = std::max(linearResolution(), edge.tolerance);
            if (norm(fromAxis - z * axis) > tolerance || norm(cross(normal, axis)) > angularResolution())
                throw invalid();
            if (j == 0) limits[i] = z;
            if (std::abs(z - limits[i]) > tolerance) throw invalid();
            angle += (fin.sense ? 1.0 : -1.0) * (dot(normal, axis) > 0.0 ? 1.0 : -1.0)
                * edge.range.length();
        }
        if (std::abs(std::abs(angle) - kTwoPi) > angularResolution() * double(fins.size())) throw invalid();
    }
    return {std::min(limits[0], limits[1]), std::max(limits[0], limits[1])};
}

}

double revolutionFaceHelixHeight(const Body &body, FaceId face) {
    return ringBand(body, face, revolutionHelixCarrier(body, face)).length();
}

BSplineCurve<3> revolutionFaceHelix(const Body &body, FaceId face, const SurfaceHelixSpec &spec, double tolerance) {
    const auto &surface = revolutionHelixCarrier(body, face);
    const Interval band = ringBand(body, face, surface);
    const double height = band.length();
    if (!(height > 0.0 && spec.pitchOrTurns > 0.0 && std::isfinite(spec.pitchOrTurns)
          && tolerance > 0.0 && std::isfinite(tolerance) && std::isfinite(spec.startAngle)))
        throw std::invalid_argument("Elica su faccia: altezza, passo, giri o angolo fuori intervallo.");
    const double turns = spec.byTurns ? spec.pitchOrTurns : height / spec.pitchOrTurns;
    const double end = kTwoPi * turns;
    if (!(end > 0.0 && end <= 200000 * kHalfPi))
        throw std::invalid_argument("Elica su faccia: numero di giri fuori intervallo.");

    const Vec3 &axis = surface.axisDirection();
    const Vec3 &origin = surface.axisPoint();
    const auto &meridian = *surface.meridian();
    const Interval domain = meridian.domain();
    const auto axial = [&](const Vec3 &p) { return dot(p - origin, axis); };
    const double firstZ = axial(meridian.point(domain.lo)), lastZ = axial(meridian.point(domain.hi));
    const double trend = lastZ > firstZ ? 1.0 : -1.0;
    if (band.lo < std::min(firstZ, lastZ) - linearResolution() ||
        band.hi > std::max(firstZ, lastZ) + linearResolution())
        throw std::domain_error("Elica su faccia: i bordi non rientrano nel meridiano.");
    // Con pesi positivi, i poli di Bezier strettamente ordinati in quota
    // garantiscono che a ogni quota corrisponda un solo punto del meridiano.
    const auto pieces = rationalBezierPieces(meridian, domain);
    if (pieces.empty()) throw std::domain_error("Elica su faccia: meridiano non disponibile.");
    for (const auto &piece : pieces) {
        const auto &poles = piece.poles();
        for (std::size_t j = 1; j < poles.size(); ++j)
            if (!(trend * dot(poles[j] - poles[j - 1], axis) > 0.0))
                throw std::domain_error("Elica su faccia: il meridiano deve avanzare lungo l'asse senza inversioni.");
    }
    const double axialSense = spec.reverse ? -1.0 : 1.0;
    const double startZ = spec.reverse ? band.hi : band.lo;
    const double rise = axialSense * height / end;
    const double sense = spec.leftHanded ? -1.0 : 1.0;
    const Vec3 reference = std::abs(axis.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    const Frame3 frame(origin, axialSense * axis, reference);

    // Conserva le derivate a un solo lato quando l'elica passa sopra un nodo
    // del meridiano. La parametrizzazione del risultato resta l'angolo in radianti.
    std::vector<std::pair<double, double>> transitions;
    for (double v : meridian.breakpoints(domain)) {
        if (v <= domain.lo || v >= domain.hi) continue;
        const double t = (axial(meridian.point(v)) - startZ) / rise;
        if (t > 0.0 && t < end) transitions.emplace_back(t, v);
    }
    std::sort(transitions.begin(), transitions.end());
    std::vector<double> breaks{0.0};
    for (const auto &transition : transitions) breaks.push_back(transition.first);
    breaks.push_back(end);
    const auto fit = detail::fitQuinticRows([&](double t, bool left, Vec3 *out) {
        cancellationPoint();
        const double z = t == end ? (spec.reverse ? band.lo : band.hi) : band.clamp(startZ + rise * t);
        const auto transition = std::lower_bound(transitions.begin(), transitions.end(), t,
            [](const auto &p, double angle) { return p.first < angle; });
        double v;
        if (transition != transitions.end() && transition->first == t) {
            v = transition->second;
        } else {
            double lo = domain.lo, hi = domain.hi;
            for (int iteration = 0; iteration < 52; ++iteration) {
                const double mid = lo + (hi - lo) * 0.5;
                if (mid == lo || mid == hi) break;
                if (trend * (axial(meridian.point(mid)) - z) < 0.0) lo = mid;
                else hi = mid;
            }
            v = lo + (hi - lo) * 0.5;
        }

        Vec3 d[3];
        if ((left || t == end) == (trend == axialSense)) meridian.evaluateLeft(v, 2, d);
        else meridian.evaluate(v, 2, d);
        const double dz = dot(d[1], axis), ddz = dot(d[2], axis);
        if (!(trend * dz > 0.0)) throw std::domain_error("Elica su faccia: derivata assiale nulla.");
        const double dv = rise / dz, ddv = -ddz * dv * dv / dz;
        const Vec3 radial = d[0] - origin - axial(d[0]) * axis;
        const double radius = norm(radial);
        if (!(radius > linearResolution())) throw std::domain_error("Elica su faccia: il profilo raggiunge l'asse.");
        const Vec3 radialV = d[1] - dz * axis, radialVV = d[2] - ddz * axis;
        const double drdv = dot(radial, radialV) / radius;
        const double ddrdvv = (dot(radial, radialVV) + squaredNorm(radialV) - drdv * drdv) / radius;
        const double dr = drdv * dv, ddr = ddrdvv * dv * dv + drdv * ddv;
        const double angle = spec.startAngle + sense * t;
        const Vec3 u = std::cos(angle) * frame.xDir() + std::sin(angle) * frame.yDir();
        const Vec3 du = sense * cross(frame.zDir(), u);
        out[0] = origin + z * axis + radius * u;
        out[1] = rise * axis + dr * u + radius * du;
        out[2] = (ddr - radius) * u + (2.0 * dr) * du;
    }, 1, breaks, tolerance * 0.5, kHalfPi);
    if (fit.error > tolerance) throw std::domain_error("Elica su faccia: approssimazione fuori tolleranza.");
    return detail::rowCurve(fit, 0);
}

}
