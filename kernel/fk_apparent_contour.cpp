#include "fk_apparent_contour.h"
#include "fk_classify.h"
#include "fk_intersect.h"
#include "fk_cancel.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ForgeCad::Kernel {
namespace {
Vec3 canonicalDirection(Vec3 direction) {
    direction = normalized(direction);
    // Invertire soltanto il verso della normale non cambia la proiezione
    // ne' l'identita' dei rami. Nessun ordinamento basato sulla mesh.
    int major = 0;
    for (int k = 1; k < 3; ++k) if (std::abs(direction[k]) > std::abs(direction[major])) major = k;
    return direction[major] < 0 ? -direction : direction;
}
std::vector<Vec3> boundaryCuts(const Body &body, FaceId face, const Plane &plane, double tolerance) {
    std::vector<Vec3> points;
    for (auto loop : body.face(face).loops) for (auto fin : body.loopFins(loop)) {
        cancellationPoint();
        const auto &edge = body.edge(body.fin(fin).edge);
        if (!edge.curve) continue;
        const auto hits = intersectCurveSurface(*edge.curve, edge.range, plane, tolerance);
        for (double t : hits.parameters) points.push_back(edge.curve->point(t));
        for (const auto &range : hits.coincident)
            for (double t : {range.lo, range.hi}) points.push_back(edge.curve->point(t));
    }
    return points;
}
void circleContours(const Body &body, FaceId face, const std::shared_ptr<const Circle<3>> &circle,
                    const Plane &plane, int side, double tolerance, std::vector<ApparentContour> &out) {
    const auto points = boundaryCuts(body, face, plane, tolerance);
    std::vector<double> cuts{0, kTwoPi};
    const double angular = tolerance / std::max(circle->radius(), tolerance);
    for (const auto &point : points) {
        const auto d = point - circle->center();
        double angle = std::atan2(dot(d, circle->yAxis()), dot(d, circle->xAxis()));
        if (angle < 0) angle += kTwoPi;
        if (angle < angular || kTwoPi - angle < angular) angle = 0;
        if (distance(circle->point(angle), point) <= 4 * tolerance) cuts.push_back(angle);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [=](double a, double b) { return b-a <= angular; }), cuts.end());
    std::vector<Interval> retained;
    const FaceClassifier classifier(body, face, tolerance);
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        const Interval range{cuts[i-1], cuts[i]};
        if (range.length() <= angular || classifier.classify(circle->point(.5*(range.lo+range.hi))) == PointLocation::Outside) continue;
        if (!retained.empty() && retained.back().hi == range.lo) retained.back().hi = range.hi;
        else retained.push_back(range);
    }
    if (retained.size() > 1 && retained.front().lo == 0 && retained.back().hi == kTwoPi) {
        const Interval wrap{retained.back().lo, retained.front().hi + kTwoPi};
        retained.erase(retained.begin()); retained.pop_back(); retained.push_back(wrap);
    }
    for (const auto range : retained) out.push_back({circle, range, side});
}
}
std::vector<ApparentContour> faceApparentContours(const Body &body, FaceId faceId,
                                                const Vec3 &viewDirection, double tolerance) {
    if (!(tolerance > 0) || !std::isfinite(tolerance) || !isFinite(viewDirection) || norm(viewDirection) == 0)
        throw std::invalid_argument("Direzione o precisione del contorno apparente non valida.");
    cancellationPoint();
    std::vector<ApparentContour> out;
    const auto &face = body.face(faceId);
    if (!face.surface) return out;
    const auto direction = canonicalDirection(viewDirection);
    const auto type = face.surface->type();
    if (type == SurfaceType::Cylinder || type == SurfaceType::Cone) {
        const bool cone = type == SurfaceType::Cone;
        const auto frame = cone ? static_cast<const ConicalSurface &>(*face.surface).frame()
                                : static_cast<const CylindricalSurface &>(*face.surface).frame();
        const double radius = cone ? static_cast<const ConicalSurface &>(*face.surface).referenceRadius()
                                   : static_cast<const CylindricalSurface &>(*face.surface).radius();
        const double slope = cone ? std::tan(static_cast<const ConicalSurface &>(*face.surface).semiAngle()) : 0;
        const auto axis = frame.zDir(), origin = frame.origin();
        const double axial = dot(axis, direction);
        const auto transverse = direction - axial * axis;
        const double length = norm(transverse);
        if (length <= 1e-12) return out;
        const double cosine = slope * axial / length;
        if (std::abs(cosine) >= 1) return out; // vista tangente degenere o dentro il cono
        const auto along = transverse / length, across = normalized(cross(axis, direction));
        const auto bounds = faceBox(body, faceId);
        double low = 1e100, high = -1e100;
        for (int mask = 0; mask < 8; ++mask) {
            const double h = dot(Vec3(mask&1 ? bounds.hi.x() : bounds.lo.x(), mask&2 ? bounds.hi.y() : bounds.lo.y(),
                                      mask&4 ? bounds.hi.z() : bounds.lo.z()) - origin, axis);
            low = std::min(low, h); high = std::max(high, h);
        }
        const double factor = std::sqrt(1+slope*slope);
        const FaceClassifier classifier(body, faceId, tolerance);
        for (int side : {-1, 1}) {
            const auto radial = cosine*along + double(side)*std::sqrt(1-cosine*cosine)*across;
            auto line = std::make_shared<Line<3>>(origin + radius*radial, axis + slope*radial);
            const Plane meridian(Frame3(origin, cross(axis, radial), axis));
            std::vector<double> cuts{factor*low, factor*high};
            for (const auto &point : boundaryCuts(body, faceId, meridian, tolerance)) cuts.push_back(factor*dot(point-origin, axis));
            std::sort(cuts.begin(), cuts.end());
            cuts.erase(std::unique(cuts.begin(), cuts.end(), [=](double a, double b) { return b-a <= tolerance; }), cuts.end());
            std::vector<Interval> retained;
            for (std::size_t i = 1; i < cuts.size(); ++i) {
                const Interval range{cuts[i-1], cuts[i]};
                if (range.length() <= tolerance || classifier.classify(line->point(.5*(range.lo+range.hi))) == PointLocation::Outside) continue;
                if (!retained.empty() && std::abs(retained.back().hi-range.lo) <= tolerance) retained.back().hi = range.hi;
                else retained.push_back(range);
            }
            for (const auto range : retained) out.push_back({line, range, side});
        }
    } else if (type == SurfaceType::Sphere) {
        const auto &sphere = static_cast<const SphericalSurface &>(*face.surface);
        const Frame3 plane(sphere.frame().origin(), direction,
            std::abs(dot(direction, sphere.frame().xDir())) < .9 ? sphere.frame().xDir() : sphere.frame().yDir());
        auto circle = std::make_shared<const Circle<3>>(plane.origin(), plane.xDir(), plane.yDir(), sphere.radius());
        circleContours(body, faceId, circle, Plane(plane), 0, tolerance, out);
    } else if (type == SurfaceType::Torus) {
        const auto &torus = static_cast<const ToroidalSurface &>(*face.surface);
        const auto axis = torus.frame().zDir(), origin = torus.frame().origin();
        if (std::abs(dot(axis, direction)) > 1e-10) return out;
        const auto radial = normalized(cross(axis, direction));
        const Plane plane(Frame3(origin, cross(axis, radial), axis));
        for (int side : {-1, 1}) {
            const auto x = double(side)*radial;
            auto circle = std::make_shared<const Circle<3>>(origin + torus.majorRadius()*x, x, axis, torus.minorRadius());
            circleContours(body, faceId, circle, plane, side, tolerance, out);
        }
    }
    return out;
}
}
