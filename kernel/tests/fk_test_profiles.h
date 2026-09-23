#ifndef FORGECAD_FK_TEST_PROFILES_H
#define FORGECAD_FK_TEST_PROFILES_H

// Profili di prova (segmenti, archi, spline chiuse) e loro equivalenti OCCT,
// condivisi dai test dell'estrusione e delle proprieta' di massa.

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pln.hxx>

#include "fk_curve_ops.h"
#include "fk_profile.h"
#include "fk_test_util.h"

namespace fktest {

inline ProfileSegment lineSegment(const Vec2 &a, const Vec2 &b) {
    return {std::make_shared<Line<2>>(a, b - a), {0.0, distance(a, b)}};
}

inline ProfileSegment arcSegment(const Vec2 &center, double radius, double from, double to) {
    return {std::make_shared<Circle<2>>(makeCircle(center, radius)), {from, to}};
}

// Rettangolo con gli spigoli arrotondati: 4 segmenti e 4 quarti di cerchio.
inline std::vector<ProfileSegment> roundedRectangle(const Vec2 &origin, double width, double height, double radius) {
    const double x0 = origin.x(), y0 = origin.y(), x1 = x0 + width, y1 = y0 + height, r = radius;
    return {lineSegment(Vec2(x0 + r, y0), Vec2(x1 - r, y0)), arcSegment(Vec2(x1 - r, y0 + r), r, -kHalfPi, 0.0),
            lineSegment(Vec2(x1, y0 + r), Vec2(x1, y1 - r)), arcSegment(Vec2(x1 - r, y1 - r), r, 0.0, kHalfPi),
            lineSegment(Vec2(x1 - r, y1), Vec2(x0 + r, y1)), arcSegment(Vec2(x0 + r, y1 - r), r, kHalfPi, kPi),
            lineSegment(Vec2(x0, y1 - r), Vec2(x0, y0 + r)), arcSegment(Vec2(x0 + r, y0 + r), r, kPi, 1.5 * kPi)};
}

// Spline chiusa (B-spline cubica clamped con primo e ultimo polo uguali).
inline ProfileSegment closedSpline(const Vec2 &center, double size, bool rational) {
    std::vector<Vec2> poles;
    for (int i = 0; i < 7; ++i) {
        const double angle = kTwoPi * i / 7.0, radius = size * (i % 2 ? 0.7 : 1.0);
        poles.push_back(center + Vec2(radius * std::cos(angle), radius * std::sin(angle)));
    }
    poles.push_back(poles.front());
    std::vector<double> weights;
    if (rational) weights = {1.0, 2.0, 0.7, 1.5, 1.0, 0.8, 1.7, 1.0};
    auto spline = std::make_shared<BSplineCurve<2>>(3, std::vector<double>{0, 0, 0, 0, 0.2, 0.4, 0.6, 0.8, 1, 1, 1, 1}, poles, weights);
    return {spline, spline->domain()};
}

// --- Riferimento OCCT: stessi tratti, stessa faccia, BRepPrimAPI_MakePrism ---

inline TopoDS_Edge occtEdge(const CurvePtr<3> &curve, const Interval &range) {
    switch (curve->type()) {
    case CurveType::Line:
        return BRepBuilderAPI_MakeEdge(toPnt(curve->point(range.lo)), toPnt(curve->point(range.hi))).Edge();
    case CurveType::Circle: {
        const auto &c = static_cast<const Circle<3> &>(*curve);
        const Handle(Geom_Circle) circle = new Geom_Circle(
            gp_Ax2(toPnt(c.center()), toDir(cross(c.xAxis(), c.yAxis())), toDir(c.xAxis())), c.radius());
        return BRepBuilderAPI_MakeEdge(circle, range.lo, range.hi).Edge();
    }
    case CurveType::Ellipse: {
        const auto &e = static_cast<const Ellipse<3> &>(*curve);
        const Handle(Geom_Ellipse) ellipse = new Geom_Ellipse(
            gp_Ax2(toPnt(e.center()), toDir(cross(e.xAxis(), e.yAxis())), toDir(e.xAxis())), e.xRadius(), e.yRadius());
        return BRepBuilderAPI_MakeEdge(ellipse, range.lo, range.hi).Edge();
    }
    case CurveType::BSpline:
        return BRepBuilderAPI_MakeEdge(toOcct(static_cast<const BSplineCurve<3> &>(*curve)), range.lo, range.hi).Edge();
    default:
        throw std::logic_error("occtEdge: tipo non gestito");
    }
}

inline TopoDS_Wire occtWire(const ProfileLoop &loop, const Frame3 &frame) {
    BRepBuilderAPI_MakeWire wire;
    for (const ProfileSegment &segment : loop.segments) wire.Add(occtEdge(embedCurve(segment.curve, frame), segment.range));
    return wire.Wire();
}

inline TopoDS_Face occtFace(const ProfileRegion &region, const Frame3 &frame) {
    BRepBuilderAPI_MakeFace face(gp_Pln(toAx3(frame)), occtWire(region.outer, frame), true);
    for (const ProfileLoop &hole : region.holes) face.Add(occtWire(hole, frame));
    return face.Face();
}

}

#endif
