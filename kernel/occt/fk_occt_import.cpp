#include "fk_occt_import.h"

#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GeomConvert.hxx>
#include <GeomConvert_ApproxSurface.hxx>
#include <Geom2dConvert.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_BezierCurve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <Geom2d_Line.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierCurve.hxx>
#include <Geom_BezierSurface.hxx>
#include <Geom_Circle.hxx>
#include <Geom_ConicalSurface.hxx>
#include <Geom_CylindricalSurface.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Line.hxx>
#include <Geom_OffsetSurface.hxx>
#include <Geom_Plane.hxx>
#include <Geom_RectangularTrimmedSurface.hxx>
#include <Geom_SphericalSurface.hxx>
#include <Geom_SurfaceOfLinearExtrusion.hxx>
#include <Geom_SurfaceOfRevolution.hxx>
#include <Geom_ToroidalSurface.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_Array2OfReal.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <TColgp_Array2OfPnt.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf2d.hxx>

#include <cmath>
#include <map>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_curve.h"
#include "fk_pcurve.h"
#include "fk_surface.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

Vec3 v3(const gp_Pnt &p) { return Vec3(p.X(), p.Y(), p.Z()); }
Vec3 v3(const gp_Dir &d) { return Vec3(d.X(), d.Y(), d.Z()); }
Vec2 v2(const gp_Pnt2d &p) { return Vec2(p.X(), p.Y()); }
Vec2 v2(const gp_Dir2d &d) { return Vec2(d.X(), d.Y()); }

std::vector<double> knotVector(const TColStd_Array1OfReal &knots, const TColStd_Array1OfInteger &multiplicities) {
    std::vector<double> distinct;
    std::vector<int> mults;
    for (int i = knots.Lower(); i <= knots.Upper(); ++i) distinct.push_back(knots(i));
    for (int i = multiplicities.Lower(); i <= multiplicities.Upper(); ++i) mults.push_back(multiplicities(i));
    return expandKnots(distinct, mults);
}

CurvePtr<3> bsplineCurve(const Handle(Geom_BSplineCurve) &c) {
    std::vector<Vec3> poles;
    std::vector<double> weights;
    for (int i = 1; i <= c->NbPoles(); ++i) {
        poles.push_back(v3(c->Pole(i)));
        if (c->IsRational()) weights.push_back(c->Weight(i));
    }
    return std::make_shared<BSplineCurve<3>>(c->Degree(), knotVector(c->Knots(), c->Multiplicities()), std::move(poles), std::move(weights));
}

CurvePtr<2> bsplineCurve2d(const Handle(Geom2d_BSplineCurve) &c) {
    std::vector<Vec2> poles;
    std::vector<double> weights;
    for (int i = 1; i <= c->NbPoles(); ++i) {
        poles.push_back(v2(c->Pole(i)));
        if (c->IsRational()) weights.push_back(c->Weight(i));
    }
    return std::make_shared<BSplineCurve<2>>(c->Degree(), knotVector(c->Knots(), c->Multiplicities()), std::move(poles), std::move(weights));
}

// Curva 3D dell'edge sull'intervallo [first, last]. `sameParameter` resta
// vero se il parametro e' quello della curva OCCT (le SP-curve di OCCT valgono).
CurvePtr<3> convertCurve(Handle(Geom_Curve) curve, double &first, double &last, bool &sameParameter, OcctImportReport *report) {
    while (curve->IsKind(STANDARD_TYPE(Geom_TrimmedCurve))) curve = Handle(Geom_TrimmedCurve)::DownCast(curve)->BasisCurve();
    if (auto line = Handle(Geom_Line)::DownCast(curve)) return std::make_shared<Line<3>>(v3(line->Position().Location()), v3(line->Position().Direction()));
    if (auto circle = Handle(Geom_Circle)::DownCast(curve)) {
        const gp_Ax2 &a = circle->Position();
        return std::make_shared<Circle<3>>(v3(a.Location()), v3(a.XDirection()), v3(a.YDirection()), circle->Radius());
    }
    if (auto ellipse = Handle(Geom_Ellipse)::DownCast(curve)) {
        const gp_Ax2 &a = ellipse->Position();
        return std::make_shared<Ellipse<3>>(v3(a.Location()), v3(a.XDirection()), v3(a.YDirection()), ellipse->MajorRadius(), ellipse->MinorRadius());
    }
    if (auto spline = Handle(Geom_BSplineCurve)::DownCast(curve)) {
        Handle(Geom_BSplineCurve) copy = Handle(Geom_BSplineCurve)::DownCast(spline->Copy());
        if (copy->IsPeriodic()) {
            const double eps = 1e-12 * std::max(1.0, copy->LastParameter() - copy->FirstParameter());
            // Un edge che passa per l'inizio del periodo: la curva ripartita da li'.
            if (first < copy->FirstParameter() - eps || last > copy->LastParameter() + eps) copy->Segment(first, last);
            copy->SetNotPeriodic();
        }
        return bsplineCurve(copy);
    }
    if (auto bezier = Handle(Geom_BezierCurve)::DownCast(curve)) return bsplineCurve(GeomConvert::CurveToBSplineCurve(bezier));
    // Parabole, iperboli, offset e altre: B-spline sull'intervallo dell'edge
    // (esatta per le coniche, approssimata per gli offset); cambia il parametro.
    Handle(Geom_BSplineCurve) converted = GeomConvert::CurveToBSplineCurve(new Geom_TrimmedCurve(curve, first, last));
    if (converted.IsNull()) throw std::domain_error(std::string("curva non convertibile: ") + curve->DynamicType()->Name());
    first = converted->FirstParameter();
    last = converted->LastParameter();
    sameParameter = false;
    if (report) report->notes.push_back(std::string("curva ") + curve->DynamicType()->Name() + " convertita in B-spline");
    return bsplineCurve(converted);
}

CurvePtr<2> convertCurve2d(Handle(Geom2d_Curve) curve, double first, double last) {
    while (curve->IsKind(STANDARD_TYPE(Geom2d_TrimmedCurve))) curve = Handle(Geom2d_TrimmedCurve)::DownCast(curve)->BasisCurve();
    if (auto line = Handle(Geom2d_Line)::DownCast(curve)) return std::make_shared<Line<2>>(v2(line->Position().Location()), v2(line->Position().Direction()));
    if (auto circle = Handle(Geom2d_Circle)::DownCast(curve)) {
        const gp_Ax22d &a = circle->Position();
        return std::make_shared<Circle<2>>(v2(a.Location()), v2(a.XDirection()), v2(a.YDirection()), circle->Radius());
    }
    if (auto ellipse = Handle(Geom2d_Ellipse)::DownCast(curve)) {
        const gp_Ax22d a = ellipse->Position();
        return std::make_shared<Ellipse<2>>(v2(a.Location()), v2(a.XDirection()), v2(a.YDirection()), ellipse->MajorRadius(), ellipse->MinorRadius());
    }
    if (auto spline = Handle(Geom2d_BSplineCurve)::DownCast(curve)) {
        Handle(Geom2d_BSplineCurve) copy = Handle(Geom2d_BSplineCurve)::DownCast(spline->Copy());
        if (copy->IsPeriodic()) {
            const double eps = 1e-12 * std::max(1.0, copy->LastParameter() - copy->FirstParameter());
            if (first < copy->FirstParameter() - eps || last > copy->LastParameter() + eps) copy->Segment(first, last);
            copy->SetNotPeriodic();
        }
        return bsplineCurve2d(copy);
    }
    if (auto bezier = Handle(Geom2d_BezierCurve)::DownCast(curve)) return bsplineCurve2d(Geom2dConvert::CurveToBSplineCurve(bezier));
    return nullptr;  // le altre: SP-curve calcolata dal kernel
}

struct ConvertedSurface {
    SurfacePtr surface;
    bool flipped = false;        // normale opposta a quella della superficie OCCT
    bool sameParameter = true;   // stesso (u, v) della superficie OCCT
    bool occtPeriodicU = false;  // B-spline periodica in OCCT (resa non periodica)
    bool occtPeriodicV = false;
    bool spline = false;
};

// Sistema diretto dal gp_Ax3: con un gp_Ax3 indiretto si tiene la direzione
// (l'asse delle superfici di rivoluzione) e Y cambia verso: u -> -u.
Frame3 frameOf(const gp_Ax3 &a, bool &flipped) {
    flipped = !a.Direct();
    return Frame3(v3(a.Location()), v3(a.Direction()), v3(a.XDirection()));
}

// Curva base di un'estrusione o di una rivoluzione: se e' illimitata (una
// retta) resta limitata dal Geom_TrimmedCurve che la contiene o, senza, dai
// parametri della faccia (`lo`, `hi`) con un margine.
CurvePtr<3> boundedBasis(const Handle(Geom_Curve) &curve, double lo, double hi, bool &same, OcctImportReport *report) {
    double first = curve->FirstParameter(), last = curve->LastParameter();
    Handle(Geom_Curve) basis = curve;
    while (basis->IsKind(STANDARD_TYPE(Geom_TrimmedCurve))) basis = Handle(Geom_TrimmedCurve)::DownCast(basis)->BasisCurve();
    const bool infinite = !std::isfinite(basis->FirstParameter()) || !std::isfinite(basis->LastParameter()) || std::fabs(basis->FirstParameter()) > 1e100
                       || std::fabs(basis->LastParameter()) > 1e100;
    if (infinite && (!std::isfinite(first) || !std::isfinite(last) || std::fabs(first) > 1e100 || std::fabs(last) > 1e100)) {
        const double margin = 0.1 * std::max(hi - lo, 1e-6);
        first = lo - margin;
        last = hi + margin;
    }
    CurvePtr<3> converted = convertCurve(curve, first, last, same, report);
    if (infinite && same) converted = std::make_shared<TrimmedCurve<3>>(converted, first, last);
    return converted;
}

// Verso della superficie convertita rispetto a quella OCCT nel punto (u, v) di questa.
bool oppositeNormal(const Handle(Geom_Surface) &occt, const Surface &converted, double u, double v) {
    gp_Pnt p;
    gp_Vec du, dv;
    occt->D1(u, v, p, du, dv);
    const gp_Vec n = du.Crossed(dv);
    const Vec3 q(p.X(), p.Y(), p.Z());
    const SurfaceProjection projection = projectPoint(converted, q);
    const Vec3 m = converted.normal(projection.u, projection.v);
    return n.X() * m.x() + n.Y() * m.y() + n.Z() * m.z() < 0.0;
}

ConvertedSurface convertSurface(Handle(Geom_Surface) surface, const double bounds[4], OcctImportReport *report) {
    ConvertedSurface r;
    while (surface->IsKind(STANDARD_TYPE(Geom_RectangularTrimmedSurface)))
        surface = Handle(Geom_RectangularTrimmedSurface)::DownCast(surface)->BasisSurface();
    if (auto offset = Handle(Geom_OffsetSurface)::DownCast(surface)) {
        const Handle(Geom_Surface) equivalent = offset->Surface();
        if (!equivalent.IsNull()) {
            ConvertedSurface e = convertSurface(equivalent, bounds, report);
            e.sameParameter = false;
            return e;
        }
        GeomConvert_ApproxSurface approx(surface, 1e-7, GeomAbs_C2, GeomAbs_C2, 11, 11, 200, 0);
        if (!approx.HasResult()) throw std::domain_error("superficie offset non convertibile");
        if (report) report->notes.push_back("superficie offset approssimata con una B-spline (scarto " + std::to_string(approx.MaxError()) + ")");
        ConvertedSurface e = convertSurface(approx.Surface(), bounds, report);
        e.sameParameter = false;
        return e;
    }
    bool flipped = false;
    if (auto plane = Handle(Geom_Plane)::DownCast(surface)) {
        r.surface = std::make_shared<Plane>(frameOf(plane->Position(), flipped));
        r.flipped = flipped;
        r.sameParameter = !flipped;
        return r;
    }
    if (auto s = Handle(Geom_CylindricalSurface)::DownCast(surface)) {
        r.surface = std::make_shared<CylindricalSurface>(frameOf(s->Position(), flipped), s->Radius());
    } else if (auto s = Handle(Geom_ConicalSurface)::DownCast(surface)) {
        r.surface = std::make_shared<ConicalSurface>(frameOf(s->Position(), flipped), s->SemiAngle(), s->RefRadius());
    } else if (auto s = Handle(Geom_SphericalSurface)::DownCast(surface)) {
        r.surface = std::make_shared<SphericalSurface>(frameOf(s->Position(), flipped), s->Radius());
    } else if (auto s = Handle(Geom_ToroidalSurface)::DownCast(surface)) {
        r.surface = std::make_shared<ToroidalSurface>(frameOf(s->Position(), flipped), s->MajorRadius(), s->MinorRadius());
    } else if (auto s = Handle(Geom_SurfaceOfLinearExtrusion)::DownCast(surface)) {
        Handle(Geom_Curve) basis = s->BasisCurve();
        Handle(Geom_Curve) inner = basis;
        while (inner->IsKind(STANDARD_TYPE(Geom_TrimmedCurve))) inner = Handle(Geom_TrimmedCurve)::DownCast(inner)->BasisCurve();
        if (auto line = Handle(Geom_Line)::DownCast(inner)) {
            // Retta estrusa: un piano (esatto; cambia il parametro in v).
            const Vec3 d = v3(line->Position().Direction()), e = v3(s->Direction());
            const Vec3 n = cross(d, e);
            if (norm(n) < 1e-12) throw std::domain_error("superficie estrusa degenere (retta lungo la direzione)");
            r.surface = std::make_shared<Plane>(Frame3(v3(line->Position().Location()), normalized(n), d));
            r.sameParameter = false;
            return r;
        }
        bool same = true;
        r.surface = std::make_shared<ExtrusionSurface>(boundedBasis(basis, bounds[0], bounds[1], same, report), v3(s->Direction()));
        r.sameParameter = same;
        return r;
    } else if (auto s = Handle(Geom_SurfaceOfRevolution)::DownCast(surface)) {
        Handle(Geom_Curve) basis = s->BasisCurve();
        Handle(Geom_Curve) inner = basis;
        while (inner->IsKind(STANDARD_TYPE(Geom_TrimmedCurve))) inner = Handle(Geom_TrimmedCurve)::DownCast(inner)->BasisCurve();
        const Vec3 A = v3(s->Axis().Location()), a = normalized(v3(s->Axis().Direction()));
        if (auto line = Handle(Geom_Line)::DownCast(inner)) {
            // Retta parallela all'asse: un cilindro; perpendicolare: un piano (esatti; il verso dalla normale).
            const Vec3 P = v3(line->Position().Location()), d = v3(line->Position().Direction());
            const Vec3 offset = (P - A) - dot(P - A, a) * a;
            const double mid = 0.5 * (bounds[2] + bounds[3]);
            if (norm(cross(d, a)) < 1e-12 && norm(offset) > 1e-9) {
                r.surface = std::make_shared<CylindricalSurface>(Frame3(A, a, offset), norm(offset));
            } else if (std::fabs(dot(d, a)) < 1e-12) {
                r.surface = std::make_shared<Plane>(Frame3(A + dot(P - A, a) * a, a, d));
            }
            if (r.surface) {
                r.flipped = oppositeNormal(surface, *r.surface, 0.5 * (bounds[0] + bounds[1]), mid);
                r.sameParameter = false;
                return r;
            }
        }
        bool same = true;
        r.surface = std::make_shared<RevolutionSurface>(boundedBasis(basis, bounds[2], bounds[3], same, report), A, a);
        r.sameParameter = same;
        return r;
    } else {
        Handle(Geom_BSplineSurface) spline = Handle(Geom_BSplineSurface)::DownCast(surface);
        if (spline.IsNull()) {
            if (auto bezier = Handle(Geom_BezierSurface)::DownCast(surface)) spline = GeomConvert::SurfaceToBSplineSurface(bezier);
        }
        if (spline.IsNull()) throw std::domain_error(std::string("superficie non gestita: ") + surface->DynamicType()->Name());
        Handle(Geom_BSplineSurface) copy = Handle(Geom_BSplineSurface)::DownCast(spline->Copy());
        r.occtPeriodicU = copy->IsUPeriodic();
        r.occtPeriodicV = copy->IsVPeriodic();
        if (r.occtPeriodicU) copy->SetUNotPeriodic();
        if (r.occtPeriodicV) copy->SetVNotPeriodic();
        std::vector<Vec3> poles;
        std::vector<double> weights;
        const bool rational = copy->IsURational() || copy->IsVRational();
        for (int i = 1; i <= copy->NbUPoles(); ++i)
            for (int j = 1; j <= copy->NbVPoles(); ++j) {
                poles.push_back(v3(copy->Pole(i, j)));
                if (rational) weights.push_back(copy->Weight(i, j));
            }
        TColStd_Array1OfReal uk(1, copy->NbUKnots()), vk(1, copy->NbVKnots());
        TColStd_Array1OfInteger um(1, copy->NbUKnots()), vm(1, copy->NbVKnots());
        copy->UKnots(uk);
        copy->VKnots(vk);
        copy->UMultiplicities(um);
        copy->VMultiplicities(vm);
        r.surface = std::make_shared<BSplineSurface>(copy->UDegree(), copy->VDegree(), knotVector(uk, um), knotVector(vk, vm), copy->NbUPoles(),
                                                     copy->NbVPoles(), std::move(poles), std::move(weights));
        r.spline = true;
        return r;
    }
    // Superfici di rivoluzione analitiche (cilindro, cono, sfera, toro).
    r.flipped = flipped;
    r.sameParameter = !flipped;
    return r;
}

bool isFinite(double x) { return std::isfinite(x) && std::fabs(x) < 1e100; }

}  // namespace

Body bodyFromOcct(const TopoDS_Shape &shape, OcctImportReport *report) {
    if (shape.IsNull()) throw std::domain_error("forma vuota");
    OcctImportReport local;
    OcctImportReport &out = report ? *report : local;
    try {
        // Facce: dei solidi, o tutte se non ce ne sono (lamina).
        std::vector<TopoDS_Face> faces;
        bool solid = false;
        for (TopExp_Explorer s(shape, TopAbs_SOLID); s.More(); s.Next()) {
            solid = true;
            for (TopExp_Explorer f(s.Current(), TopAbs_FACE); f.More(); f.Next()) faces.push_back(TopoDS::Face(f.Current()));
        }
        if (!solid)
            for (TopExp_Explorer f(shape, TopAbs_FACE); f.More(); f.Next()) faces.push_back(TopoDS::Face(f.Current()));
        if (faces.empty()) throw std::domain_error("la forma non ha facce");

        std::vector<Vec3> points;
        std::vector<double> vertexTolerance;
        std::vector<Body::BuildEdge> edges;
        std::vector<Body::BuildFace> buildFaces;
        std::vector<bool> edgeSameParameter;
        TopTools_IndexedMapOfShape vertexMap, edgeMap;
        std::map<int, int> vertexIndex, edgeIndex;
        const auto vertexOf = [&](const TopoDS_Vertex &v) {
            const int key = vertexMap.Add(v);
            const auto found = vertexIndex.find(key);
            if (found != vertexIndex.end()) return found->second;
            points.push_back(v3(BRep_Tool::Pnt(v)));
            vertexTolerance.push_back(BRep_Tool::Tolerance(v));
            return vertexIndex[key] = int(points.size()) - 1;
        };
        const auto edgeOf = [&](const TopoDS_Edge &e) {
            const int key = edgeMap.Add(e);
            const auto found = edgeIndex.find(key);
            if (found != edgeIndex.end()) return found->second;
            const TopoDS_Edge forward = TopoDS::Edge(e.Oriented(TopAbs_FORWARD));
            double first = 0.0, last = 0.0;
            const Handle(Geom_Curve) raw = BRep_Tool::Curve(forward, first, last);  // con la Location dell'edge
            if (raw.IsNull()) throw std::domain_error("spigolo senza curva 3D");
            if (!isFinite(first) || !isFinite(last)) throw std::domain_error("spigolo illimitato");
            TopoDS_Vertex a, b;
            TopExp::Vertices(forward, a, b);
            if (a.IsNull() || b.IsNull()) throw std::domain_error("spigolo senza vertici");
            bool same = BRep_Tool::SameParameter(forward);
            Body::BuildEdge spec;
            spec.curve = convertCurve(raw, first, last, same, &out);
            spec.range = {first, last};
            spec.start = vertexOf(a);
            spec.end = vertexOf(b);
            spec.tolerance = BRep_Tool::Tolerance(forward);
            edges.push_back(spec);
            edgeSameParameter.push_back(same);
            return edgeIndex[key] = int(edges.size()) - 1;
        };

        for (const TopoDS_Face &face : faces) {
            if (face.Orientation() != TopAbs_FORWARD && face.Orientation() != TopAbs_REVERSED) continue;
            TopLoc_Location location;
            const Handle(Geom_Surface) raw = BRep_Tool::Surface(face, location);
            if (raw.IsNull()) throw std::domain_error("faccia senza superficie");
            const Handle(Geom_Surface) placed =
                location.IsIdentity() ? raw : Handle(Geom_Surface)::DownCast(raw->Transformed(location.Transformation()));
            double uv[4];
            BRepTools::UVBounds(face, uv[0], uv[1], uv[2], uv[3]);
            const ConvertedSurface converted = convertSurface(placed, uv, &out);
            Body::BuildFace spec;
            spec.surface = converted.surface;
            spec.sense = (face.Orientation() == TopAbs_FORWARD) != converted.flipped;

            // B-spline periodica resa non periodica: la faccia deve stare in un periodo (le SP-curve si spostano di periodi interi).
            double shiftU = 0.0, shiftV = 0.0;
            if (converted.occtPeriodicU || converted.occtPeriodicV) {
                double u1, u2, v1, v2;
                BRepTools::UVBounds(face, u1, u2, v1, v2);
                const Interval ud = converted.surface->uDomain(), vd = converted.surface->vDomain();
                const auto shiftFor = [](double lo, double hi, const Interval &domain, double &shift) {
                    const double period = domain.length(), eps = 1e-9 * std::max(1.0, period);
                    shift = -std::floor((lo - domain.lo + eps) / period) * period;
                    if (hi + shift > domain.hi + eps) throw std::domain_error("faccia a cavallo della cucitura di una B-spline periodica");
                };
                if (converted.occtPeriodicU) shiftFor(u1, u2, ud, shiftU);
                if (converted.occtPeriodicV) shiftFor(v1, v2, vd, shiftV);
            }

            for (TopExp_Explorer w(face, TopAbs_WIRE); w.More(); w.Next()) {
                const TopoDS_Wire wire = TopoDS::Wire(w.Current());
                std::vector<Body::BuildFin> kept;
                for (BRepTools_WireExplorer explorer(wire, face); explorer.More(); explorer.Next()) {
                    const TopoDS_Edge edge = explorer.Current();
                    if (edge.Orientation() != TopAbs_FORWARD && edge.Orientation() != TopAbs_REVERSED) continue;
                    if (BRep_Tool::Degenerated(edge)) {
                        // Poli di sfere, coni e rivoluzioni: il kernel cammina lungo la linea del polo.
                        if (converted.spline) throw std::domain_error("spigolo degenere (polo) su una superficie B-spline");
                        ++out.droppedDegenerated;
                        continue;
                    }
                    if (BRep_Tool::IsClosed(edge, face)) {
                        // Cucitura: le due SP-curve differiscono di un periodo in u o in v.
                        double f1, l1, f2, l2;
                        const Handle(Geom2d_Curve) c1 = BRep_Tool::CurveOnSurface(TopoDS::Edge(edge.Oriented(TopAbs_FORWARD)), face, f1, l1);
                        const Handle(Geom2d_Curve) c2 = BRep_Tool::CurveOnSurface(TopoDS::Edge(edge.Oriented(TopAbs_REVERSED)), face, f2, l2);
                        if (!c1.IsNull() && !c2.IsNull()) {
                            const gp_Pnt2d p1 = c1->Value(0.5 * (f1 + l1)), p2 = c2->Value(0.5 * (f2 + l2));
                            const bool alongU = std::fabs(p1.X() - p2.X()) > std::fabs(p1.Y() - p2.Y());
                            if (alongU ? converted.surface->isUPeriodic() : converted.surface->isVPeriodic()) {
                                ++out.droppedSeams;
                                continue;
                            }
                        }
                    }
                    Body::BuildFin fin;
                    fin.edge = edgeOf(edge);
                    fin.sense = edge.Orientation() == TopAbs_FORWARD;
                    // Sulle B-spline (stesso parametro) le SP-curve di OCCT: anche le due di una cucitura che resta.
                    const bool seam = BRep_Tool::IsClosed(edge, face);
                    if ((converted.spline || seam) && converted.sameParameter && edgeSameParameter[std::size_t(fin.edge)]) {
                        double f = 0.0, l = 0.0;
                        const Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(edge, face, f, l);
                        if (!pc.IsNull()) {
                            Handle(Geom2d_Curve) moved = pc;
                            if (shiftU != 0.0 || shiftV != 0.0) {
                                moved = Handle(Geom2d_Curve)::DownCast(pc->Copy());
                                moved->Translate(gp_Vec2d(shiftU, shiftV));
                            }
                            fin.pcurve = convertCurve2d(moved, f, l);
                            if (fin.pcurve) fin.pcurveTolerance = std::max(1e-7, BRep_Tool::Tolerance(edge));
                        }
                    } else if (seam) {
                        throw std::domain_error("cucitura su una superficie non periodica senza SP-curve utilizzabili");
                    }
                    kept.push_back(fin);
                }
                if (kept.empty()) continue;  // loop fatto solo di cuciture e poli (sfera, toro interi)
                // Loop: dove gli estremi non si seguono (cuciture tolte) il ciclo si divide.
                const auto startOf = [&](const Body::BuildFin &f) { return f.sense ? edges[std::size_t(f.edge)].start : edges[std::size_t(f.edge)].end; };
                const auto endOf = [&](const Body::BuildFin &f) { return f.sense ? edges[std::size_t(f.edge)].end : edges[std::size_t(f.edge)].start; };
                std::size_t begin = 0;
                const std::size_t n = kept.size();
                for (std::size_t i = 0; i < n; ++i)
                    if (endOf(kept[(i + n - 1) % n]) != startOf(kept[i])) {
                        begin = i;
                        break;
                    }
                std::vector<Body::BuildFin> current;
                for (std::size_t k = 0; k < n; ++k) {
                    const Body::BuildFin &fin = kept[(begin + k) % n];
                    if (!current.empty() && endOf(current.back()) != startOf(fin)) {
                        if (endOf(current.back()) != startOf(current.front())) throw std::domain_error("loop aperto dopo aver tolto le cuciture");
                        spec.loops.push_back(current);
                        current.clear();
                    }
                    current.push_back(fin);
                }
                if (endOf(current.back()) != startOf(current.front())) throw std::domain_error("loop aperto dopo aver tolto le cuciture");
                spec.loops.push_back(current);
            }
            buildFaces.push_back(std::move(spec));
        }
        std::vector<int> uses(edges.size(), 0);
        for (const Body::BuildFace &f : buildFaces)
            for (const auto &loop : f.loops)
                for (const Body::BuildFin &fin : loop) ++uses[std::size_t(fin.edge)];
        for (int u : uses)
            if (u > 2) throw std::domain_error("spigolo condiviso da piu' di due facce (forma non manifold)");
        Body body = solid ? Body::build(points, edges, buildFaces) : Body::buildSheet(points, edges, buildFaces);
        for (std::size_t i = 0; i < points.size(); ++i) body.vertex(VertexId(int(i))).tolerance = vertexTolerance[i];
        const int missing = computePCurves(body);
        if (missing > 0) throw std::domain_error(std::to_string(missing) + " SP-curve non calcolabili");
        const std::vector<CheckIssue> issues = checkBody(body);
        if (!issues.empty()) throw std::domain_error("il corpo convertito non e' valido: " + describe(issues.front().code) + " (" + issues.front().message + ")");
        out.faces = int(buildFaces.size());
        out.edges = int(edges.size());
        out.vertices = int(points.size());
        return body;
    } catch (const Standard_Failure &failure) {
        const char *message = failure.GetMessageString();
        throw std::domain_error(std::string("OpenCASCADE: ") + (message && *message ? message : failure.DynamicType()->Name()));
    } catch (const std::invalid_argument &failure) {
        throw std::domain_error(failure.what());
    }
}

}
