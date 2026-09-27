#include "fk_transform.h"

#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

#include "fk_body_check.h"
#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_pcurve.h"
#include "fk_surface_algo.h"

namespace ForgeCad::Kernel {
namespace {

class Transformer {
public:
    explicit Transformer(const Transform3 &t) : t_(t) {
        if (!t.isSimilarity(&scale_) || !(scale_ > 0.0)) throw std::domain_error("transformBody: serve una similitudine (rotazione, traslazione, simmetria, scala uniforme)");
        const Vec3 x = t.applyToVector(Vec3(1, 0, 0)), y = t.applyToVector(Vec3(0, 1, 0)), z = t.applyToVector(Vec3(0, 0, 1));
        improper_ = dot(cross(x, y), z) < 0.0;
    }
    double scale() const { return scale_; }
    // Determinante negativo (simmetria): i sistemi delle superfici analitiche
    // restano destrorsi (il parametro u gira), i loop cambiano verso.
    bool improper() const { return improper_; }
    Vec3 point(const Vec3 &p) const { return t_.applyToPoint(p); }
    Vec3 unit(const Vec3 &v) const { return normalized(t_.applyToVector(v)); }
    Frame3 frame(const Frame3 &f) const { return Frame3(point(f.origin()), unit(f.zDir()), unit(f.xDir())); }

    // Curva trasformata e fattore del suo parametro (t' = k t: le rette hanno la direzione unitaria).
    std::pair<CurvePtr<3>, double> curve(const CurvePtr<3> &c) {
        const auto found = curves_.find(c.get());
        if (found != curves_.end()) return found->second;
        std::pair<CurvePtr<3>, double> result{nullptr, 1.0};
        switch (c->type()) {
        case CurveType::Line: {
            const auto &line = static_cast<const Line<3> &>(*c);
            result = {std::make_shared<Line<3>>(point(line.origin()), unit(line.direction())), scale_};
            break;
        }
        case CurveType::Circle: {
            const auto &circle = static_cast<const Circle<3> &>(*c);
            result.first = std::make_shared<Circle<3>>(point(circle.center()), unit(circle.xAxis()), unit(circle.yAxis()), scale_ * circle.radius());
            break;
        }
        case CurveType::Ellipse: {
            const auto &ellipse = static_cast<const Ellipse<3> &>(*c);
            result.first = std::make_shared<Ellipse<3>>(point(ellipse.center()), unit(ellipse.xAxis()), unit(ellipse.yAxis()),
                                                        scale_ * ellipse.xRadius(), scale_ * ellipse.yRadius());
            break;
        }
        case CurveType::BSpline: {
            const auto &spline = static_cast<const BSplineCurve<3> &>(*c);
            std::vector<Vec3> poles;
            for (const Vec3 &p : spline.poles()) poles.push_back(point(p));
            result.first = std::make_shared<BSplineCurve<3>>(spline.degree(), spline.knots(), poles, spline.weights());
            break;
        }
        case CurveType::Trimmed: {
            const auto &trimmed = static_cast<const TrimmedCurve<3> &>(*c);
            const auto [basis, k] = curve(trimmed.basis());
            const Interval domain = trimmed.domain();
            result = {std::make_shared<TrimmedCurve<3>>(basis, k * domain.lo, k * domain.hi), k};
            break;
        }
        case CurveType::Transformed: {
            const auto &transformed = static_cast<const TransformedCurve &>(*c);
            result.first = std::make_shared<TransformedCurve>(transformed.basis(), t_ * transformed.transform());
            break;
        }
        default:
            throw std::domain_error("transformBody: tipo di curva non gestito");
        }
        curves_[c.get()] = result;
        return result;
    }

    SurfacePtr surface(const SurfacePtr &s) {
        const auto found = surfaces_.find(s.get());
        if (found != surfaces_.end()) return found->second;
        SurfacePtr result;
        switch (s->type()) {
        case SurfaceType::Plane: result = std::make_shared<Plane>(frame(static_cast<const Plane &>(*s).frame())); break;
        case SurfaceType::Cylinder: {
            const auto &cylinder = static_cast<const CylindricalSurface &>(*s);
            result = std::make_shared<CylindricalSurface>(frame(cylinder.frame()), scale_ * cylinder.radius());
            break;
        }
        case SurfaceType::Cone: {
            const auto &cone = static_cast<const ConicalSurface &>(*s);
            result = std::make_shared<ConicalSurface>(frame(cone.frame()), cone.semiAngle(), scale_ * cone.referenceRadius());
            break;
        }
        case SurfaceType::Sphere: {
            const auto &sphere = static_cast<const SphericalSurface &>(*s);
            result = std::make_shared<SphericalSurface>(frame(sphere.frame()), scale_ * sphere.radius());
            break;
        }
        case SurfaceType::Torus: {
            const auto &torus = static_cast<const ToroidalSurface &>(*s);
            result = std::make_shared<ToroidalSurface>(frame(torus.frame()), scale_ * torus.majorRadius(), scale_ * torus.minorRadius());
            break;
        }
        case SurfaceType::Extrusion: {
            const auto &extrusion = static_cast<const ExtrusionSurface &>(*s);
            result = std::make_shared<ExtrusionSurface>(curve(extrusion.curve()).first, unit(extrusion.direction()));
            break;
        }
        case SurfaceType::Revolution: {
            const auto &revolution = static_cast<const RevolutionSurface &>(*s);
            result = std::make_shared<RevolutionSurface>(curve(revolution.meridian()).first, point(revolution.axisPoint()), unit(revolution.axisDirection()));
            break;
        }
        case SurfaceType::BSpline: {
            const auto &spline = static_cast<const BSplineSurface &>(*s);
            std::vector<Vec3> poles;
            std::vector<double> weights;
            for (int i = 0; i < spline.uPoleCount(); ++i)
                for (int j = 0; j < spline.vPoleCount(); ++j) {
                    poles.push_back(point(spline.pole(i, j)));
                    if (spline.isRational()) weights.push_back(spline.weight(i, j));
                }
            result = std::make_shared<BSplineSurface>(spline.uDegree(), spline.vDegree(), spline.uKnots(), spline.vKnots(), spline.uPoleCount(),
                                                      spline.vPoleCount(), poles, weights);
            break;
        }
        }
        if (!result) throw std::domain_error("transformBody: tipo di superficie non gestito");
        surfaces_[s.get()] = result;
        return result;
    }

private:
    Transform3 t_;
    double scale_ = 1.0;
    bool improper_ = false;
    std::map<const Curve<3> *, std::pair<CurvePtr<3>, double>> curves_;
    std::map<const Surface *, SurfacePtr> surfaces_;
};

}

namespace {

// Normale uscente della faccia in un punto del suo bordo (o del dominio se non ne ha).
bool faceNormalSample(const Body &body, FaceId f, Vec3 &point, Vec3 &normal) {
    const Face &face = body.face(f);
    const Surface &surface = *face.surface;
    for (LoopId l : face.loops)
        for (FinId fin : body.loopFins(l)) {
            const Edge &edge = body.edge(body.fin(fin).edge);
            if (!edge.curve) continue;
            for (double s : {0.5, 0.3, 0.7}) {
                point = edge.curve->point(edge.range.lo + s * edge.range.length());
                const SurfaceProjection p = projectPoint(surface, point);
                const Vec3 n = surface.normal(p.u, p.v);
                if (!isFinite(n) || !(norm(n) > 0.5)) continue;
                normal = face.sense ? n : -n;
                return true;
            }
        }
    const Interval u = surface.uDomain(), v = surface.vDomain();
    const double uu = u.isFinite() ? 0.5 * (u.lo + u.hi) : 0.0, vv = v.isFinite() ? 0.5 * (v.lo + v.hi) : 0.0;
    point = surface.point(uu, vv);
    const Vec3 n = surface.normal(uu, vv);
    if (!isFinite(n)) return false;
    normal = face.sense ? n : -n;
    return true;
}

// Simmetria: la stessa topologia con i loop percorsi al contrario (la
// faccia resta a sinistra delle fin nell'immagine speculare) e il verso di
// ogni faccia dalla normale trasformata.
Body mirroredBody(const Body &input, Transformer &t) {
    std::map<int, int> vertexIndex;
    std::vector<Vec3> points;
    std::vector<double> tolerances;
    // Solo i vertici degli edge: Body::build da' da se' il vertice delle facce senza bordo (sfera, toro interi).
    std::set<int> used;
    for (EdgeId e : input.edges()) used.insert({input.edgeStart(e).index, input.edgeEnd(e).index});
    for (VertexId v : input.vertices()) {
        if (!used.count(v.index)) continue;
        vertexIndex[v.index] = int(points.size());
        points.push_back(t.point(input.vertex(v).point));
        tolerances.push_back(input.vertex(v).tolerance * t.scale());
    }
    std::map<int, int> edgeIndex;
    std::vector<Body::BuildEdge> edges;
    for (EdgeId e : input.edges()) {
        const Edge &edge = input.edge(e);
        Body::BuildEdge b;
        b.start = vertexIndex.at(input.edgeStart(e).index);
        b.end = vertexIndex.at(input.edgeEnd(e).index);
        const auto [curve, k] = t.curve(edge.curve);
        b.curve = curve;
        b.range = {k * edge.range.lo, k * edge.range.hi};
        b.tolerance = edge.tolerance * t.scale();
        edgeIndex[e.index] = int(edges.size());
        edges.push_back(b);
    }
    std::vector<Body::BuildFace> faces;
    for (FaceId f : input.faces()) {
        const Face &face = input.face(f);
        Body::BuildFace b;
        b.surface = t.surface(face.surface);
        Vec3 p, n;
        if (!faceNormalSample(input, f, p, n)) throw std::domain_error("transformBody: normale di una faccia non definita");
        const Vec3 q = t.point(p), expected = t.unit(n);
        const SurfaceProjection image = projectPoint(*b.surface, q);
        b.sense = dot(b.surface->normal(image.u, image.v), expected) > 0.0;
        for (LoopId l : face.loops) {
            const std::vector<FinId> fins = input.loopFins(l);
            if (fins.empty()) continue;
            std::vector<Body::BuildFin> loop;
            for (auto it = fins.rbegin(); it != fins.rend(); ++it) {
                Body::BuildFin fin;
                fin.edge = edgeIndex.at(input.fin(*it).edge.index);
                fin.sense = !input.fin(*it).sense;
                loop.push_back(fin);
            }
            b.loops.push_back(std::move(loop));
        }
        faces.push_back(std::move(b));
    }
    Body body;
    try {
        body = input.isSheet() ? Body::buildSheet(points, edges, faces) : Body::build(points, edges, faces);
    } catch (const std::invalid_argument &failure) {
        throw std::domain_error(std::string("transformBody: simmetria non riuscita (") + failure.what() + ")");
    }
    // Body::build crea i vertici nell'ordine dei punti.
    std::size_t k = 0;
    for (VertexId v : body.vertices())
        if (k < tolerances.size()) body.vertex(v).tolerance = tolerances[k++];
    return body;
}

}

Body transformBody(const Body &input, const Transform3 &transform) {
    Transformer t(transform);
    if (t.improper()) {
        Body body = mirroredBody(input, t);
        computePCurves(body);
        const std::vector<CheckIssue> issues = checkBody(body);
        if (!issues.empty()) throw std::domain_error("transformBody: risultato non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
        return body;
    }
    Body body = input;
    for (VertexId v : body.vertices()) {
        Vertex &vertex = body.vertex(v);
        vertex.point = t.point(vertex.point);
        vertex.tolerance *= t.scale();
    }
    for (EdgeId e : body.edges()) {
        Edge &edge = body.edge(e);
        edge.tolerance *= t.scale();
        if (!edge.curve) continue;
        const auto [curve, k] = t.curve(edge.curve);
        edge.curve = curve;
        edge.range = {k * edge.range.lo, k * edge.range.hi};
    }
    for (FinId f : body.fins()) {
        body.fin(f).pcurve = nullptr;
        body.fin(f).pcurveTolerance = 0.0;
    }
    for (FaceId f : body.faces())
        if (body.face(f).surface) body.face(f).surface = t.surface(body.face(f).surface);
    computePCurves(body);
    const std::vector<CheckIssue> issues = checkBody(body);
    if (!issues.empty()) throw std::domain_error("transformBody: risultato non valido (" + describe(issues.front().code) + ": " + issues.front().message + ")");
    return body;
}

CurvePtr<3> transformCurve(const CurvePtr<3> &curve, const Transform3 &transform, double *parameterScale) {
    Transformer t(transform);
    const auto [result, k] = t.curve(curve);
    if (parameterScale) *parameterScale = k;
    return result;
}

SurfacePtr transformSurface(const SurfacePtr &surface, const Transform3 &transform) {
    Transformer t(transform);
    return t.surface(surface);
}

Body mirrorBody(const Body &body, const Vec3 &point, const Vec3 &normal) { return transformBody(body, Transform3::reflection(point, normal)); }

Body scaleBody(const Body &body, const Vec3 &center, double factor) {
    if (!(factor > 0.0) || !std::isfinite(factor)) throw std::domain_error("scaleBody: il fattore di scala deve essere positivo");
    return transformBody(body, Transform3::scaling(center, factor));
}

}
