#include "cad_curve_solver.h"

#include <GCE2d_MakeSegment.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom2d_Circle.hxx>
#include <Geom2d_Ellipse.hxx>
#include <gp_Ax22d.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Geom2dAdaptor_Curve.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColgp_Array1OfPnt2d.hxx>
#include <gp_Ax2d.hxx>

#include <cmath>

namespace ForgeCad {
namespace {

gp_Pnt2d toPnt(const QPointF &point) { return gp_Pnt2d(point.x(), point.y()); }

double distance(const QPointF &a, const QPointF &b) {
    return std::hypot(b.x() - a.x(), b.y() - a.y());
}

// Spline: sequenza di Bezier cubiche (P_i, H_i+, H_{i+1}-, P_{i+1}) espressa
// esattamente come B-spline di grado 3 con nodi interni di molteplicita' 3.
Handle(Geom2d_Curve) makeSpline(const CurveObject &curve) {
    const int count = curve.controlPoints.size();
    if (count < 2 || curve.tangentHandles.size() != count) return {};
    TColgp_Array1OfPnt2d poles(1, 3 * (count - 1) + 1);
    int pole = 1;
    for (int index = 0; index < count; ++index) {
        if (index > 0) poles.SetValue(pole++, toPnt(curve.tangentHandles.at(index).first));
        poles.SetValue(pole++, toPnt(curve.controlPoints.at(index)));
        if (index + 1 < count) poles.SetValue(pole++, toPnt(curve.tangentHandles.at(index).second));
    }
    TColStd_Array1OfReal knots(1, count);
    TColStd_Array1OfInteger multiplicities(1, count);
    for (int index = 0; index < count; ++index) {
        knots.SetValue(index + 1, double(index));
        multiplicities.SetValue(index + 1, index == 0 || index == count - 1 ? 4 : 3);
    }
    return new Geom2d_BSplineCurve(poles, knots, multiplicities, 3);
}

// NURBS: poli e pesi dell'utente, grado min(3, n-1), nodi uniformi "clamped".
Handle(Geom2d_Curve) makeNurbs(const CurveObject &curve) {
    const int count = curve.controlPoints.size();
    if (count < 2) return {};
    const int degree = qMin(3, count - 1);
    TColgp_Array1OfPnt2d poles(1, count);
    TColStd_Array1OfReal weights(1, count);
    for (int index = 0; index < count; ++index) {
        poles.SetValue(index + 1, toPnt(curve.controlPoints.at(index)));
        const double weight = curve.weights.size() == count ? curve.weights.at(index) : 1.0;
        if (weight <= 0.0) return {};
        weights.SetValue(index + 1, weight);
    }
    const int spans = count - degree;
    TColStd_Array1OfReal knots(1, spans + 1);
    TColStd_Array1OfInteger multiplicities(1, spans + 1);
    for (int index = 0; index <= spans; ++index) {
        knots.SetValue(index + 1, double(index) / double(spans));
        multiplicities.SetValue(index + 1, index == 0 || index == spans ? degree + 1 : 1);
    }
    return new Geom2d_BSplineCurve(poles, weights, knots, multiplicities, degree);
}

}

QVector<Handle(Geom2d_Curve)> curveGeometry(const CurveObject &curve) {
    QVector<Handle(Geom2d_Curve)> result;
    try {
        switch (curve.tool) {
        case DrawingTool::Spline: {
            const Handle(Geom2d_Curve) spline = makeSpline(curve);
            if (!spline.IsNull()) result.append(spline);
            break;
        }
        case DrawingTool::Nurbs: {
            const Handle(Geom2d_Curve) nurbs = makeNurbs(curve);
            if (!nurbs.IsNull()) result.append(nurbs);
            break;
        }
        case DrawingTool::Circle: {
            if (curve.controlPoints.size() < 2) break;
            const double radius = distance(curve.controlPoints.at(0), curve.controlPoints.at(1));
            if (radius <= Precision::Confusion()) break;
            result.append(new Geom2d_Circle(gp_Ax2d(toPnt(curve.controlPoints.at(0)), gp_Dir2d(1.0, 0.0)), radius));
            break;
        }
        case DrawingTool::Arc: {
            if (curve.controlPoints.size() < 3) break;
            const QPointF center = curve.controlPoints.at(0);
            const QPointF start = curve.controlPoints.at(1);
            const QPointF end = curve.controlPoints.at(2);
            const double radius = distance(center, start);
            if (radius <= Precision::Confusion() || distance(center, end) <= Precision::Confusion()) break;
            const double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
            double endAngle = std::atan2(end.y() - center.y(), end.x() - center.x());
            while (endAngle <= startAngle + Precision::Angular()) endAngle += 2.0 * M_PI;
            const Handle(Geom2d_Circle) circle = new Geom2d_Circle(gp_Ax2d(toPnt(center), gp_Dir2d(1.0, 0.0)), radius);
            result.append(new Geom2d_TrimmedCurve(circle, startAngle, endAngle));
            break;
        }
        case DrawingTool::Ellipse: {
            if (curve.controlPoints.size() < 3) break;
            const QPointF center = curve.controlPoints.at(0);
            const double a = distance(center, curve.controlPoints.at(1)), b = distance(center, curve.controlPoints.at(2));
            if (a <= Precision::Confusion() || b <= Precision::Confusion()) break;
            const QPointF u = (curve.controlPoints.at(1) - center) / a;
            // OCCT vuole il semiasse maggiore lungo X del sistema dell'ellisse.
            const gp_Dir2d x = a >= b ? gp_Dir2d(u.x(), u.y()) : gp_Dir2d(-u.y(), u.x());
            const gp_Dir2d y(-x.Y(), x.X());
            result.append(new Geom2d_Ellipse(gp_Ax22d(toPnt(center), x, y), std::max(a, b), std::min(a, b)));
            break;
        }
        case DrawingTool::Rectangle:
        case DrawingTool::CenterRectangle: {
            if (curve.controlPoints.size() < 2) break;
            const QPointF p = curve.controlPoints.at(0), q = curve.controlPoints.at(1);
            const QPointF a = curve.tool == DrawingTool::Rectangle ? p : 2.0 * p - q;
            const gp_Pnt2d corners[4] = {toPnt(a), gp_Pnt2d(q.x(), a.y()), toPnt(q), gp_Pnt2d(a.x(), q.y())};
            if (std::abs(q.x() - a.x()) <= Precision::Confusion() || std::abs(q.y() - a.y()) <= Precision::Confusion()) break;
            for (int side = 0; side < 4; ++side) result.append(GCE2d_MakeSegment(corners[side], corners[(side + 1) % 4]).Value());
            break;
        }
        case DrawingTool::Polygon: {
            if (curve.controlPoints.size() < 2 || curve.sides < 3) break;
            const QPointF center = curve.controlPoints.at(0);
            const QPointF vertex = curve.controlPoints.at(1);
            const double radius = distance(center, vertex);
            if (radius <= Precision::Confusion()) break;
            const double startAngle = std::atan2(vertex.y() - center.y(), vertex.x() - center.x());
            QVector<gp_Pnt2d> corners;
            for (int side = 0; side < curve.sides; ++side) {
                const double angle = startAngle + 2.0 * M_PI * double(side) / double(curve.sides);
                corners.append(side == 0 ? toPnt(vertex)
                               : gp_Pnt2d(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle)));
            }
            for (int side = 0; side < curve.sides; ++side)
                result.append(GCE2d_MakeSegment(corners.at(side), corners.at((side + 1) % curve.sides)).Value());
            break;
        }
        default:
            break;
        }
    } catch (const Standard_Failure &) {
        result.clear();
    }
    return result;
}

void initializeTangentHandles(CurveObject &curve) {
    curve.tangentHandles.clear();
    if (curve.tool != DrawingTool::Spline) return;
    for (int index = 0; index < curve.controlPoints.size(); ++index) {
        const QPointF previous = curve.controlPoints.at(qMax(0, index - 1));
        const QPointF next = curve.controlPoints.at(qMin(int(curve.controlPoints.size()) - 1, index + 1));
        const QPointF tangent = (next - previous) / 3.0;
        curve.tangentHandles.append(qMakePair(curve.controlPoints.at(index) - tangent,
                                               curve.controlPoints.at(index) + tangent));
    }
}

void recalculateCurve(CurveObject &curve, int quality) {
    if (curve.tool == DrawingTool::Spline && curve.tangentHandles.size() != curve.controlPoints.size())
        initializeTangentHandles(curve);
    curve.samples.clear();
    const QVector<Handle(Geom2d_Curve)> geometry = curveGeometry(curve);
    const double angular = quality <= 0 ? 0.2 : quality == 1 ? 0.08 : 0.03;
    const double deflection = quality <= 0 ? 1.0e-2 : quality == 1 ? 2.0e-3 : 4.0e-4;
    try {
        for (const Handle(Geom2d_Curve) &piece : geometry) {
            Geom2dAdaptor_Curve adaptor(piece);
            GCPnts_TangentialDeflection discretizer(adaptor, angular, deflection, 2);
            for (int index = 1; index <= discretizer.NbPoints(); ++index) {
                if (index == 1 && !curve.samples.isEmpty()) continue;
                const gp_Pnt point = discretizer.Value(index);
                curve.samples.append(QPointF(point.X(), point.Y()));
            }
        }
    } catch (const Standard_Failure &) {
        curve.samples.clear();
    }
    curve.numericallyValid = !geometry.isEmpty() && curve.samples.size() >= 2;
    for (const QPointF &sample : curve.samples) {
        curve.numericallyValid = curve.numericallyValid && std::isfinite(sample.x()) && std::isfinite(sample.y());
    }
}

}
