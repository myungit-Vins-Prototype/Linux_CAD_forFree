#include "cad_features.h"

#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <exception>

#include "cad_forge.h"
#include "cad_kernel.h"
#include "fk_curve_ops.h"
#include "fk_helix.h"
#include "fk_profile.h"

namespace ForgeCad {
namespace {

using namespace Kernel;

void setError(QString *error, const QString &message) {
    if (error) *error = message;
}

const Curve<3> &basisOf(const Curve<3> &curve) {
    const Curve<3> *c = &curve;
    while (c->type() == CurveType::Trimmed) c = static_cast<const TrimmedCurve<3> *>(c)->basis().get();
    return *c;
}

}

Vec3 helixReference(const Vec3 &axis) {
    const Vec3 a = normalized(axis);
    const Vec3 x = std::fabs(a.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    return normalized(x - dot(x, a) * a);
}

bool helixBaseFromSketch(const SketchObject &sketch, int curve, HelixBase &base, QString *error) {
    int chosen = -1;
    for (int index = 0; index < sketch.curves.size(); ++index) {
        const CurveObject &c = sketch.curves.at(index);
        const bool circular = (c.tool == DrawingTool::Circle && c.controlPoints.size() >= 2) || (c.tool == DrawingTool::Arc && c.controlPoints.size() >= 3);
        if (!circular) continue;
        if (curve < 0 || index == curve) {
            chosen = index;
            break;
        }
    }
    if (chosen < 0) {
        setError(error, QStringLiteral("Lo schizzo non ha un cerchio o un arco per la base dell'elica."));
        return false;
    }
    const CurveObject &c = sketch.curves.at(chosen);
    const Vec3 center = sketchToWorld(c.controlPoints.at(0), sketch), point = sketchToWorld(c.controlPoints.at(1), sketch);
    base = HelixBase();
    base.origin = center;
    base.axis = sketchAxes(sketch).zDir();
    base.radius = distance(center, point);
    if (!(base.radius > 1e-9)) {
        setError(error, QStringLiteral("Il cerchio della base ha raggio nullo."));
        return false;
    }
    base.xRef = (point - center) / base.radius;
    return true;
}

void helixDimensions(const HelixParameters &p, double &pitch, double &turns, double &height) {
    pitch = p.pitch;
    turns = p.turns;
    height = p.height;
    if (p.spiral) {
        height = 0.0;
        return;
    }
    switch (p.mode) {
    case 1: pitch = turns > 0.0 ? height / turns : 0.0; break;
    case 2: turns = pitch > 0.0 ? height / pitch : 0.0; break;
    default: height = pitch * turns; break;
    }
}

ForgeCurve helixCurve(const HelixParameters &parameters, const HelixBase &input, QString *error) {
    HelixBase base = input;
    double taper = base.hasTaper ? base.taper : parameters.taper * kPi / 180.0;
    if (parameters.reverse) {
        // Dalla faccia si parte dall'altro estremo; altrimenti l'asse si gira nello stesso punto.
        if (parameters.source == 2 && base.length > 0.0) {
            base.origin = base.origin + base.length * base.axis;
            base.radius += base.length * std::tan(taper);
        }
        base.axis = -base.axis;
        if (base.hasTaper) taper = -taper;
    }
    HelixSpec spec;
    double height = 0.0;
    helixDimensions(parameters, spec.pitch, spec.turns, height);
    spec.frame = Frame3(base.origin, base.axis, base.xRef);
    spec.radius = base.radius;
    spec.taper = parameters.spiral ? 0.0 : taper;
    spec.spiral = parameters.spiral;
    spec.leftHanded = parameters.leftHanded;
    spec.startAngle = parameters.startAngle * kPi / 180.0;
    try {
        return std::make_shared<const HelixCurve>(spec);
    } catch (const std::exception &failure) {
        setError(error, QString::fromUtf8(failure.what()));
        return nullptr;
    }
}

void curveDisplay(const Curve<3> &curve, int quality, BodyDisplay &display) {
    display = {};
    display.quality = quality;
    const Interval range = curve.domain();
    int count = 200 * (quality + 1);
    if (const auto *helix = dynamic_cast<const HelixCurve *>(&curve))
        count = std::max(16, int(std::ceil(helix->spec().turns * (quality <= 0 ? 36 : quality == 1 ? 72 : 144))));
    QVector<QVector3D> polyline;
    for (int i = 0; i <= count; ++i) {
        const Vec3 p = curve.point(range.lo + range.length() * i / count);
        polyline.append(QVector3D(float(p.x()), float(p.y()), float(p.z())));
    }
    display.edges.append(polyline);
}

bool sketchPath(const SketchObject &sketch, std::vector<PathSegment> &path, QString *error) {
    path.clear();
    try {
        const std::vector<ProfileSegment> segments = forgeSketchSegments(sketch);
        if (segments.empty()) {
            setError(error, QStringLiteral("Lo schizzo del percorso non contiene geometria."));
            return false;
        }
        const Profile profile = buildProfile(segments, kSketchConnectionTolerance);
        const ProfileLoop *loop = nullptr;
        if (profile.regions.size() == 1 && profile.regions.front().holes.empty() && profile.chains.empty()) loop = &profile.regions.front().outer;
        else if (profile.regions.empty() && profile.chains.size() == 1) loop = &profile.chains.front();
        if (!loop) {
            setError(error, QStringLiteral("Il percorso deve essere una sola catena di entita' o un solo contorno chiuso."));
            return false;
        }
        Frame3 frame;
        double height;
        forgeSketchFrame(sketch, 0.0, frame, height);
        for (const ProfileSegment &s : loop->segments) path.push_back({embedCurve(s.curve, frame), s.range});
        return true;
    } catch (const std::exception &failure) {
        setError(error, QString::fromUtf8(failure.what()));
        return false;
    }
}

std::vector<PathSegment> curvePath(const ForgeCurve &curve) {
    if (!curve) return {};
    return {{curve, curve->domain()}};
}

std::vector<PathSegment> alignPath(const std::vector<PathSegment> &path, const SketchObject &profile) {
    if (path.empty()) return path;
    // Baricentro (approssimato) del profilo: serve solo a scegliere l'inizio.
    Frame3 frame;
    double height;
    forgeSketchFrame(profile, 0.0, frame, height);
    Vec3 center;
    int count = 0;
    for (const ProfileSegment &s : forgeSketchSegments(profile))
        for (int i = 0; i < 16; ++i) {
            const Vec2 p = s.curve->point(s.range.lo + s.range.length() * i / 16.0);
            center += frame.toGlobal(Vec3(p.x(), p.y(), 0.0));
            ++count;
        }
    if (count == 0) return path;
    center /= double(count);
    const Vec3 start = path.front().curve->point(path.front().range.lo), end = path.back().curve->point(path.back().range.hi);
    const bool closed = distance(start, end) <= 1e-6 * std::max(1.0, norm(start));
    const auto reversible = [&] {
        for (const PathSegment &s : path)
            if (basisOf(*s.curve).type() == CurveType::Other) return false;
        return true;
    };
    if (!closed) {
        if (distance(end, center) >= distance(start, center) || !reversible()) return path;
        std::vector<PathSegment> result;
        for (auto it = path.rbegin(); it != path.rend(); ++it) result.push_back({reversedCurve(it->curve), {-it->range.hi, -it->range.lo}});
        return result;
    }
    // Contorno chiuso: parte dal punto piu' vicino al profilo.
    std::size_t bestSegment = 0;
    double bestT = path.front().range.lo, best = 1e300;
    for (std::size_t k = 0; k < path.size(); ++k) {
        const PathSegment &s = path[k];
        for (int i = 0; i <= 64; ++i) {
            const double t = s.range.lo + s.range.length() * i / 64.0;
            const double d = distance(s.curve->point(t), center);
            if (d < best) best = d, bestSegment = k, bestT = t;
        }
    }
    const PathSegment &s = path[bestSegment];
    double a = std::max(s.range.lo, bestT - s.range.length() / 64.0), b = std::min(s.range.hi, bestT + s.range.length() / 64.0);
    for (int i = 0; i < 80; ++i) {
        const double m1 = a + 0.381966 * (b - a), m2 = a + 0.618034 * (b - a);
        if (distance(s.curve->point(m1), center) < distance(s.curve->point(m2), center)) b = m2;
        else a = m1;
    }
    const double t = 0.5 * (a + b), tiny = 1e-9 * s.range.length();
    std::vector<PathSegment> result;
    if (s.range.hi - t > tiny) result.push_back({s.curve, {t, s.range.hi}});
    for (std::size_t k = 1; k < path.size(); ++k) result.push_back(path[(bestSegment + k) % path.size()]);
    if (t - s.range.lo > tiny) result.push_back({s.curve, {s.range.lo, t}});
    if (result.empty()) return path;
    return result;
}

}
