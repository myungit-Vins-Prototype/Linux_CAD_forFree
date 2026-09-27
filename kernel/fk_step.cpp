#include "fk_step.h"

#include "fk_precision.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <set>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <sstream>
#include <stdexcept>

#include "fk_bspline.h"
#include "fk_bspline_surface.h"
#include "fk_hermite.h"
#include "fk_intersect.h"
#include "fk_nurbs.h"
#include "fk_surface.h"
#include "fk_tessellate.h"
#include "fk_exchange.h"
#include "fk_surface_algo.h"
#include "fk_transform.h"
#include "fk_curve_algo.h"

namespace ForgeCad::Kernel {
namespace {

// --- Scrittura -----------------------------------------------------------------

// Numero reale nel formato STEP: sempre con il punto decimale, 17 cifre (il
// double si rilegge uguale).
std::string real(double x) {
    if (!std::isfinite(x)) throw std::domain_error("STEP: numero non finito");
    return detail::formatReal(x);
}

// Stringa STEP: gli apici raddoppiati, i caratteri non ASCII come \X2\hhhh\X0\ (UTF-16).
std::string text(const std::string &utf8) {
    std::string out = "'";
    std::vector<unsigned> wide;
    const auto flush = [&] {
        if (wide.empty()) return;
        out += "\\X2\\";
        char hex[8];
        for (unsigned c : wide) {
            std::snprintf(hex, sizeof hex, "%04X", c);
            out += hex;
        }
        out += "\\X0\\";
        wide.clear();
    };
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        unsigned code = c;
        int extra = 0;
        if (c >= 0xF0) code = c & 0x07, extra = 3;
        else if (c >= 0xE0) code = c & 0x0F, extra = 2;
        else if (c >= 0xC0) code = c & 0x1F, extra = 1;
        for (int k = 1; k <= extra && i + std::size_t(k) < utf8.size(); ++k) code = (code << 6) | (static_cast<unsigned char>(utf8[i + std::size_t(k)]) & 0x3F);
        i += std::size_t(extra) + 1;
        if (code >= 32 && code < 127 && code != '\\') {
            flush();
            if (code == '\'') out += "''";
            else out += char(code);
        } else if (code == '\\') {
            flush();
            out += "\\\\";
        } else if (code >= 0x10000) {
            code -= 0x10000;
            wide.push_back(0xD800 + (code >> 10));
            wide.push_back(0xDC00 + (code & 0x3FF));
        } else {
            wide.push_back(code);
        }
    }
    flush();
    return out + "'";
}

class StepWriter {
public:
    int add(const std::string &entity) {
        const int id = ++next_;
        data_ << '#' << id << '=' << entity << ";\n";
        return id;
    }
    static std::string ref(int id) { return "#" + std::to_string(id); }
    static std::string list(const std::vector<int> &ids) {
        std::string s = "(";
        for (std::size_t k = 0; k < ids.size(); ++k) s += (k ? "," : "") + ref(ids[k]);
        return s + ")";
    }
    int point(const Vec3 &p) { return add("CARTESIAN_POINT(''," + triple(p) + ")"); }
    int direction(const Vec3 &d) { return add("DIRECTION(''," + triple(normalized(d)) + ")"); }
    int placement(const Vec3 &origin, const Vec3 &z, const Vec3 &x) {
        const int o = point(origin), a = direction(z), r = direction(x - dot(x, normalized(z)) * normalized(z));
        return add("AXIS2_PLACEMENT_3D(''," + ref(o) + "," + ref(a) + "," + ref(r) + ")");
    }
    int placement(const Frame3 &f) { return placement(f.origin(), f.zDir(), f.xDir()); }
    std::string content() const { return data_.str(); }

    static std::string triple(const Vec3 &p) { return "(" + real(p.x()) + "," + real(p.y()) + "," + real(p.z()) + ")"; }

private:
    int next_ = 0;
    std::ostringstream data_;
};

// Nodi distinti e molteplicita' di un vettore espanso.
void compress(const std::vector<double> &knots, std::vector<double> &distinct, std::vector<int> &multiplicities) {
    distinct.clear();
    multiplicities.clear();
    for (double k : knots) {
        if (!distinct.empty() && k == distinct.back()) ++multiplicities.back();
        else distinct.push_back(k), multiplicities.push_back(1);
    }
}

std::string realList(const std::vector<double> &values) {
    std::string s = "(";
    for (std::size_t k = 0; k < values.size(); ++k) s += (k ? "," : "") + real(values[k]);
    return s + ")";
}
std::string intList(const std::vector<int> &values) {
    std::string s = "(";
    for (std::size_t k = 0; k < values.size(); ++k) s += (k ? "," : "") + std::to_string(values[k]);
    return s + ")";
}

int writeBSplineCurve(StepWriter &w, const BSplineCurve<3> &c) {
    std::vector<int> poles;
    for (const Vec3 &p : c.poles()) poles.push_back(w.point(p));
    std::vector<double> knots;
    std::vector<int> mults;
    compress(c.knots(), knots, mults);
    const std::string common = std::to_string(c.degree()) + "," + StepWriter::list(poles) + ",.UNSPECIFIED.,.F.,.U.";
    if (!c.isRational())
        return w.add("B_SPLINE_CURVE_WITH_KNOTS(''," + common + "," + intList(mults) + "," + realList(knots) + ",.UNSPECIFIED.)");
    return w.add("(BOUNDED_CURVE() B_SPLINE_CURVE(" + common + ") B_SPLINE_CURVE_WITH_KNOTS(" + intList(mults) + "," + realList(knots) +
                 ",.UNSPECIFIED.) CURVE() GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_CURVE(" + realList(c.weights()) + ") REPRESENTATION_ITEM(''))");
}

// Curva di un edge (il tratto serve solo alle curve senza forma STEP propria).
int writeCurve(StepWriter &w, const CurvePtr<3> &input, const Interval &range) {
    const Curve<3> *curve = input.get();
    while (curve->type() == CurveType::Trimmed) curve = static_cast<const TrimmedCurve<3> *>(curve)->basis().get();
    // Trasformate: le coniche restano coniche nelle similitudini, le B-spline si trasformano nei poli.
    Transform3 transform;
    bool transformed = false;
    while (curve->type() == CurveType::Transformed) {
        const auto *t = static_cast<const TransformedCurve *>(curve);
        transform = transformed ? transform * t->transform() : t->transform();
        transformed = true;
        curve = t->basis().get();
        while (curve->type() == CurveType::Trimmed) curve = static_cast<const TrimmedCurve<3> *>(curve)->basis().get();
    }
    double scale = 1.0;
    const bool similar = !transformed || transform.isSimilarity(&scale);
    const auto P = [&](const Vec3 &p) { return transformed ? transform.applyToPoint(p) : p; };
    const auto V = [&](const Vec3 &v) { return transformed ? transform.applyToVector(v) : v; };
    switch (curve->type()) {
    case CurveType::Line:
        if (similar) {
            const auto &line = static_cast<const Line<3> &>(*curve);
            const int p = w.point(P(line.point(range.isFinite() ? range.lo : 0.0))), d = w.direction(V(line.derivative(0.0)));
            const int v = w.add("VECTOR(''," + StepWriter::ref(d) + ",1.)");
            return w.add("LINE(''," + StepWriter::ref(p) + "," + StepWriter::ref(v) + ")");
        }
        break;
    case CurveType::Circle:
        if (similar) {
            const auto &c = static_cast<const Circle<3> &>(*curve);
            const Vec3 x = V(c.xAxis()), y = V(c.yAxis());
            return w.add("CIRCLE(''," + StepWriter::ref(w.placement(P(c.center()), cross(x, y), x)) + "," + real(c.radius() * scale) + ")");
        }
        break;
    case CurveType::Ellipse:
        if (similar) {
            const auto &e = static_cast<const Ellipse<3> &>(*curve);
            const Vec3 x = V(e.xAxis()), y = V(e.yAxis());
            return w.add("ELLIPSE(''," + StepWriter::ref(w.placement(P(e.center()), cross(x, y), x)) + "," + real(e.xRadius() * scale) + "," +
                         real(e.yRadius() * scale) + ")");
        }
        break;
    case CurveType::BSpline: {
        const auto &b = static_cast<const BSplineCurve<3> &>(*curve);
        if (!transformed) return writeBSplineCurve(w, b);
        std::vector<Vec3> poles;
        for (const Vec3 &p : b.poles()) poles.push_back(P(p));
        return writeBSplineCurve(w, BSplineCurve<3>(b.degree(), b.knots(), poles, b.weights()));
    }
    default: break;
    }
    // Il resto (eliche, trasformate non simili): B-spline sul tratto dell'edge,
    // esatta dalle forme razionali di Bezier dove ci sono, altrimenti entro 1e-9.
    if (!range.isFinite()) throw std::domain_error("STEP: curva illimitata senza forma STEP");
    try {
        const std::vector<BSplineCurve<3>> pieces = rationalBezierPieces(*input, range);
        if (!pieces.empty()) return writeBSplineCurve(w, joinBezierPieces(pieces));
    } catch (const std::exception &) {
    }
    const detail::RowSpline spline = detail::fitQuinticRows([&](double t, bool left, Vec3 *out) {
        if (left) input->evaluateLeft(t, 2, out);
        else input->evaluate(t, 2, out);
    }, 1, input->breakpoints(range), 1e-9);
    return writeBSplineCurve(w, detail::rowCurve(spline, 0));
}

// Superficie di una faccia; `flipped`: la normale STEP e' opposta a quella del kernel.
int writeSurface(StepWriter &w, const Surface &surface, const Interval &uRange, const Interval &vRange, bool &flipped) {
    flipped = false;
    switch (surface.type()) {
    case SurfaceType::Plane:
        return w.add("PLANE(''," + StepWriter::ref(w.placement(static_cast<const Plane &>(surface).frame())) + ")");
    case SurfaceType::Cylinder: {
        const auto &c = static_cast<const CylindricalSurface &>(surface);
        return w.add("CYLINDRICAL_SURFACE(''," + StepWriter::ref(w.placement(c.frame())) + "," + real(c.radius()) + ")");
    }
    case SurfaceType::Cone: {
        const auto &c = static_cast<const ConicalSurface &>(surface);
        const Frame3 &f = c.frame();
        if (c.semiAngle() > 0.0)
            return w.add("CONICAL_SURFACE(''," + StepWriter::ref(w.placement(f)) + "," + real(c.referenceRadius()) + "," + real(c.semiAngle()) + ")");
        // STEP vuole il semiangolo positivo: l'asse al contrario (e Y al contrario: la normale resta).
        return w.add("CONICAL_SURFACE(''," + StepWriter::ref(w.placement(f.origin(), -f.zDir(), f.xDir())) + "," + real(c.referenceRadius()) + "," +
                     real(-c.semiAngle()) + ")");
    }
    case SurfaceType::Sphere: {
        const auto &s = static_cast<const SphericalSurface &>(surface);
        return w.add("SPHERICAL_SURFACE(''," + StepWriter::ref(w.placement(s.frame())) + "," + real(s.radius()) + ")");
    }
    case SurfaceType::Torus: {
        const auto &t = static_cast<const ToroidalSurface &>(surface);
        return w.add("TOROIDAL_SURFACE(''," + StepWriter::ref(w.placement(t.frame())) + "," + real(t.majorRadius()) + "," + real(t.minorRadius()) + ")");
    }
    case SurfaceType::Extrusion: {
        const auto &e = static_cast<const ExtrusionSurface &>(surface);
        const int curve = writeCurve(w, e.curve(), uRange);
        const int d = w.direction(e.direction());
        const int v = w.add("VECTOR(''," + StepWriter::ref(d) + ",1.)");
        return w.add("SURFACE_OF_LINEAR_EXTRUSION(''," + StepWriter::ref(curve) + "," + StepWriter::ref(v) + ")");
    }
    case SurfaceType::Revolution: {
        const auto &r = static_cast<const RevolutionSurface &>(surface);
        const int curve = writeCurve(w, r.meridian(), vRange);
        const int axis = w.add("AXIS1_PLACEMENT(''," + StepWriter::ref(w.point(r.axisPoint())) + "," + StepWriter::ref(w.direction(r.axisDirection())) + ")");
        return w.add("SURFACE_OF_REVOLUTION(''," + StepWriter::ref(curve) + "," + StepWriter::ref(axis) + ")");
    }
    case SurfaceType::BSpline: {
        const auto &b = static_cast<const BSplineSurface &>(surface);
        std::string rows = "(";
        for (int i = 0; i < b.uPoleCount(); ++i) {
            std::vector<int> row;
            for (int j = 0; j < b.vPoleCount(); ++j) row.push_back(w.point(b.pole(i, j)));
            rows += (i ? "," : "") + StepWriter::list(row);
        }
        rows += ")";
        std::vector<double> uk, vk;
        std::vector<int> um, vm;
        compress(b.uKnots(), uk, um);
        compress(b.vKnots(), vk, vm);
        const std::string common = std::to_string(b.uDegree()) + "," + std::to_string(b.vDegree()) + "," + rows + ",.UNSPECIFIED.,.F.,.F.,.U.";
        const std::string knots = intList(um) + "," + intList(vm) + "," + realList(uk) + "," + realList(vk) + ",.UNSPECIFIED.";
        if (!b.isRational()) return w.add("B_SPLINE_SURFACE_WITH_KNOTS(''," + common + "," + knots + ")");
        std::string weights = "(";
        for (int i = 0; i < b.uPoleCount(); ++i) {
            std::vector<double> row;
            for (int j = 0; j < b.vPoleCount(); ++j) row.push_back(b.weight(i, j));
            weights += (i ? "," : "") + realList(row);
        }
        weights += ")";
        return w.add("(BOUNDED_SURFACE() B_SPLINE_SURFACE(" + common + ") B_SPLINE_SURFACE_WITH_KNOTS(" + knots +
                     ") GEOMETRIC_REPRESENTATION_ITEM() RATIONAL_B_SPLINE_SURFACE(" + weights + ") REPRESENTATION_ITEM('') SURFACE())");
    }
    }
    throw std::domain_error("STEP: tipo di superficie non scrivibile");
}

// Segno del volume racchiuso da ciascuna shell (dalla tassellazione: serve
// solo a distinguere le shell esterne da quelle dei vuoti).
std::map<int, double> shellVolumes(const Body &body) {
    Box box;
    for (VertexId v : body.vertices()) box.add(body.vertex(v).point);
    std::map<int, double> volume;
    TessellationOptions options;
    options.deflection = 1e-3 * std::max(box.diagonal(), 1e-3);
    const Tessellation t = tessellate(body, options);
    for (const FaceMesh &mesh : t.faces) {
        const int shell = body.face(mesh.face).shell.index;
        double v = 0.0;
        for (const std::array<int, 3> &tri : mesh.triangles) {
            const Vec3 &a = mesh.points[std::size_t(tri[0])], &b = mesh.points[std::size_t(tri[1])], &c = mesh.points[std::size_t(tri[2])];
            v += dot(a, cross(b, c)) / 6.0;
        }
        volume[shell] += v;
    }
    return volume;
}

}  // namespace

std::string writeStep(const std::vector<ExchangeBody> &bodies, const StepWriteOptions &options) {
    StepWriter w;
    const char *contextName = options.schema == StepSchema::AP203 ? "configuration controlled 3d designs of mechanical parts and assemblies"
                            : options.schema == StepSchema::AP214 ? "automotive design"
                                                                  : "managed model based 3d engineering";
    const int application = w.add(std::string("APPLICATION_CONTEXT('") + contextName + "')");
    if (options.schema == StepSchema::AP203)
        w.add("APPLICATION_PROTOCOL_DEFINITION('international standard','config_control_design',1994," + StepWriter::ref(application) + ")");
    else if (options.schema == StepSchema::AP214)
        w.add("APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000," + StepWriter::ref(application) + ")");
    else
        w.add("APPLICATION_PROTOCOL_DEFINITION('international standard','ap242_managed_model_based_3d_engineering',2011," + StepWriter::ref(application) + ")");
    const int productContext = w.add("PRODUCT_CONTEXT(''," + StepWriter::ref(application) + ",'mechanical')");
    const int definitionContext = w.add("PRODUCT_DEFINITION_CONTEXT('part definition'," + StepWriter::ref(application) + ",'design')");
    const int mm = w.add("(LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.))");
    const int rad = w.add("(NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.))");
    const int sr = w.add("(NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT())");
    const int uncertainty = w.add("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07)," + StepWriter::ref(mm) + ",'distance_accuracy_value','confusion accuracy')");
    const int context = w.add("(GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((" + StepWriter::ref(uncertainty) +
                              ")) GLOBAL_UNIT_ASSIGNED_CONTEXT((" + StepWriter::ref(mm) + "," + StepWriter::ref(rad) + "," + StepWriter::ref(sr) +
                              ")) REPRESENTATION_CONTEXT('3D',''))");
    std::vector<int> styled;
    int index = 0;
    for (const ExchangeBody &exchange : bodies) {
        ++index;
        const Body &body = exchange.body;
        const std::string name = exchange.name.empty() ? "body " + std::to_string(index) : exchange.name;
        std::vector<int> items;
        int representation = 0;
        if (exchange.curve) {
            // Curva: limitata dai suoi estremi (punti), intera se chiusa.
            const Interval range = exchange.curveRange;
            if (!range.isFinite() || !(range.hi > range.lo)) throw std::domain_error("STEP: curva senza tratto");
            const int basis = writeCurve(w, exchange.curve, range);
            const Vec3 a = exchange.curve->point(range.lo), b = exchange.curve->point(range.hi);
            int item = basis;
            if (distance(a, b) > kLinearResolution)
                item = w.add("TRIMMED_CURVE(" + text(name) + "," + StepWriter::ref(basis) + ",(" + StepWriter::ref(w.point(a)) + "),(" +
                             StepWriter::ref(w.point(b)) + "),.T.,.CARTESIAN.)");
            items.push_back(w.add("GEOMETRIC_CURVE_SET(" + text(name) + ",(" + StepWriter::ref(item) + "))"));
            std::vector<int> all = items;
            all.push_back(w.placement(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0)));
            representation = w.add("GEOMETRICALLY_BOUNDED_WIREFRAME_SHAPE_REPRESENTATION(" + text(name) + "," + StepWriter::list(all) + "," +
                                   StepWriter::ref(context) + ")");
        } else {
            // Vertici e edge.
            std::map<int, int> vertexOf, edgeOf;
            for (VertexId v : body.vertices()) {
                const int p = w.point(body.vertex(v).point);
                vertexOf[v.index] = w.add("VERTEX_POINT(''," + StepWriter::ref(p) + ")");
            }
            for (EdgeId e : body.edges()) {
                const Edge &edge = body.edge(e);
                if (!edge.curve) throw std::domain_error("STEP: edge senza curva");
                const int curve = writeCurve(w, edge.curve, edge.range);
                edgeOf[e.index] = w.add("EDGE_CURVE(''," + StepWriter::ref(vertexOf.at(body.edgeStart(e).index)) + "," +
                                        StepWriter::ref(vertexOf.at(body.edgeEnd(e).index)) + "," + StepWriter::ref(curve) + ",.T.)");
            }
            // Facce per shell.
            std::map<int, std::vector<int>> facesOf;
            for (FaceId f : body.faces()) {
                const Face &face = body.face(f);
                // Il tratto (u, v) della faccia per le curve base di estrusioni e rivoluzioni: dal box dei parametri dei suoi edge.
                Interval uRange{1e300, -1e300}, vRange{1e300, -1e300};
                for (LoopId l : face.loops)
                    for (FinId fin : body.loopFins(l)) {
                        const Fin &data = body.fin(fin);
                        const Edge &edge = body.edge(data.edge);
                        for (int k = 0; k <= 8; ++k) {
                            const double t = edge.range.lo + edge.range.length() * k / 8.0;
                            Vec2 uv;
                            if (data.pcurve) uv = data.pcurve->point(t);
                            else continue;
                            uRange.lo = std::min(uRange.lo, uv.x()), uRange.hi = std::max(uRange.hi, uv.x());
                            vRange.lo = std::min(vRange.lo, uv.y()), vRange.hi = std::max(vRange.hi, uv.y());
                        }
                    }
                if (!(uRange.lo <= uRange.hi)) uRange = face.surface->uDomain();
                if (!(vRange.lo <= vRange.hi)) vRange = face.surface->vDomain();
                bool flipped = false;
                const int surface = writeSurface(w, *face.surface, uRange, vRange, flipped);
                std::vector<int> bounds;
                for (LoopId l : face.loops) {
                    const Loop &loop = body.loop(l);
                    if (loop.isolatedVertex.valid()) {
                        const int vl = w.add("VERTEX_LOOP(''," + StepWriter::ref(vertexOf.at(loop.isolatedVertex.index)) + ")");
                        bounds.push_back(w.add("FACE_BOUND(''," + StepWriter::ref(vl) + ",.T.)"));
                        continue;
                    }
                    std::vector<int> oriented;
                    for (FinId fin : body.loopFins(l))
                        oriented.push_back(w.add("ORIENTED_EDGE('',*,*," + StepWriter::ref(edgeOf.at(body.fin(fin).edge.index)) + "," +
                                                 (body.fin(fin).sense ? ".T." : ".F.") + ")"));
                    const int el = w.add("EDGE_LOOP(''," + StepWriter::list(oriented) + ")");
                    bounds.push_back(w.add("FACE_BOUND(''," + StepWriter::ref(el) + ",.T.)"));
                }
                const bool sameSense = face.sense != flipped;
                facesOf[face.shell.index].push_back(w.add("ADVANCED_FACE(''," + StepWriter::list(bounds) + "," + StepWriter::ref(surface) + "," +
                                                          (sameSense ? ".T." : ".F.") + ")"));
            }
            const int origin = w.placement(Vec3(0, 0, 0), Vec3(0, 0, 1), Vec3(1, 0, 0));
            if (body.isSheet()) {
                std::vector<int> shells;
                for (const auto &[shell, faces] : facesOf) shells.push_back(w.add("OPEN_SHELL(''," + StepWriter::list(faces) + ")"));
                items.push_back(w.add("SHELL_BASED_SURFACE_MODEL(''," + StepWriter::list(shells) + ")"));
                std::vector<int> all = items;
                all.push_back(origin);
                representation = w.add("MANIFOLD_SURFACE_SHAPE_REPRESENTATION(" + text(name) + "," + StepWriter::list(all) + "," + StepWriter::ref(context) + ")");
            } else {
                // Shell esterne (volume positivo) con i vuoti (negativo) che stanno nel loro box.
                const std::map<int, double> volumes = shellVolumes(body);
                std::map<int, Box> boxes;
                for (FaceId f : body.faces())
                    for (LoopId l : body.face(f).loops)
                        for (FinId fin : body.loopFins(l)) boxes[body.face(f).shell.index].add(body.vertex(body.fin(fin).vertex).point);
                std::vector<int> outer, voids;
                for (const auto &[shell, faces] : facesOf) (volumes.count(shell) && volumes.at(shell) < 0.0 ? voids : outer).push_back(shell);
                std::map<int, std::vector<int>> voidsOf;
                for (int v : voids) {
                    int best = outer.empty() ? -1 : outer.front();
                    double smallest = 1e300;
                    for (int o : outer) {
                        const Box &b = boxes[o], &inner = boxes[v];
                        const bool contains = b.lo.x() <= inner.lo.x() && b.lo.y() <= inner.lo.y() && b.lo.z() <= inner.lo.z() && b.hi.x() >= inner.hi.x()
                                           && b.hi.y() >= inner.hi.y() && b.hi.z() >= inner.hi.z();
                        if (contains && b.diagonal() < smallest) smallest = b.diagonal(), best = o;
                    }
                    if (best >= 0) voidsOf[best].push_back(v);
                }
                for (int o : outer) {
                    const int shell = w.add("CLOSED_SHELL(''," + StepWriter::list(facesOf.at(o)) + ")");
                    if (!voidsOf.count(o)) {
                        items.push_back(w.add("MANIFOLD_SOLID_BREP(" + text(name) + "," + StepWriter::ref(shell) + ")"));
                        continue;
                    }
                    std::vector<int> holes;
                    for (int v : voidsOf.at(o)) {
                        const int voidShell = w.add("CLOSED_SHELL(''," + StepWriter::list(facesOf.at(v)) + ")");
                        holes.push_back(w.add("ORIENTED_CLOSED_SHELL('',*," + StepWriter::ref(voidShell) + ",.F.)"));
                    }
                    items.push_back(w.add("BREP_WITH_VOIDS(" + text(name) + "," + StepWriter::ref(shell) + "," + StepWriter::list(holes) + ")"));
                }
                std::vector<int> all = items;
                all.push_back(origin);
                representation = w.add("ADVANCED_BREP_SHAPE_REPRESENTATION(" + text(name) + "," + StepWriter::list(all) + "," + StepWriter::ref(context) + ")");
            }
        }
        // Prodotto.
        const int product = w.add("PRODUCT(" + text(name) + "," + text(name) + ",''," + StepWriter::list({productContext}) + ")");
        w.add("PRODUCT_RELATED_PRODUCT_CATEGORY('part',$," + StepWriter::list({product}) + ")");
        const int formation = w.add("PRODUCT_DEFINITION_FORMATION('',''," + StepWriter::ref(product) + ")");
        const int definition = w.add("PRODUCT_DEFINITION('design',''," + StepWriter::ref(formation) + "," + StepWriter::ref(definitionContext) + ")");
        const int shape = w.add("PRODUCT_DEFINITION_SHAPE('',''," + StepWriter::ref(definition) + ")");
        w.add("SHAPE_DEFINITION_REPRESENTATION(" + StepWriter::ref(shape) + "," + StepWriter::ref(representation) + ")");
        // Colore (non in AP203).
        if (exchange.hasColor && options.schema != StepSchema::AP203 && !exchange.curve) {
            const int colour = w.add("COLOUR_RGB(''," + real(exchange.color[0]) + "," + real(exchange.color[1]) + "," + real(exchange.color[2]) + ")");
            const int fill = w.add("FILL_AREA_STYLE_COLOUR(''," + StepWriter::ref(colour) + ")");
            const int area = w.add("FILL_AREA_STYLE(''," + StepWriter::list({fill}) + ")");
            const int surfaceFill = w.add("SURFACE_STYLE_FILL_AREA(" + StepWriter::ref(area) + ")");
            const int side = w.add("SURFACE_SIDE_STYLE(''," + StepWriter::list({surfaceFill}) + ")");
            const int usage = w.add("SURFACE_STYLE_USAGE(.BOTH.," + StepWriter::ref(side) + ")");
            const int assignment = w.add("PRESENTATION_STYLE_ASSIGNMENT(" + StepWriter::list({usage}) + ")");
            for (int item : items) styled.push_back(w.add("STYLED_ITEM('color'," + StepWriter::list({assignment}) + "," + StepWriter::ref(item) + ")"));
        }
    }
    if (!styled.empty()) w.add("MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION(''," + StepWriter::list(styled) + "," + StepWriter::ref(context) + ")");

    std::string stamp = options.timeStamp;
    if (stamp.empty()) {
        char buffer[32];
        const std::time_t now = std::time(nullptr);
        std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%S", std::localtime(&now));
        stamp = buffer;
    }
    const char *schema = options.schema == StepSchema::AP203 ? "CONFIG_CONTROL_DESIGN"
                       : options.schema == StepSchema::AP214 ? "AUTOMOTIVE_DESIGN { 1 0 10303 214 1 1 1 1 }"
                                                             : "AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF { 1 0 10303 442 1 1 4 }";
    std::ostringstream out;
    out << "ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('ForgeCAD model'),'2;1');\n";
    out << "FILE_NAME(" << text(options.fileName) << ",'" << stamp << "',(" << text(options.author) << "),('ForgeCAD'),'ForgeCAD','ForgeCAD','');\n";
    out << "FILE_SCHEMA(('" << schema << "'));\nENDSEC;\nDATA;\n" << w.content() << "ENDSEC;\nEND-ISO-10303-21;\n";
    return out.str();
}


// --- Lettura ---------------------------------------------------------------------

namespace {

// Parametro di un'istanza STEP.
struct Param {
    enum Kind { Null, Derived, Number, String, Enum, Ref, List, Typed } kind = Null;
    double number = 0.0;
    std::string text;  // stringa, enumerazione, nome del tipo
    int ref = 0;
    std::vector<Param> items;  // lista, o l'argomento del parametro tipizzato
};

struct Entity {
    // Istanza semplice: un tipo; complessa: piu' parti (tipo, argomenti), in ordine alfabetico.
    std::vector<std::pair<std::string, std::vector<Param>>> parts;
    const std::string &type() const { return parts.front().first; }
    const std::vector<Param> &args() const { return parts.front().second; }
    const std::vector<Param> *part(const std::string &name) const {
        for (const auto &p : parts)
            if (p.first == name) return &p.second;
        return nullptr;
    }
    bool is(const std::string &name) const { return part(name) != nullptr; }
};

class StepFile {
public:
    explicit StepFile(const std::string &content) : s_(content) { parse(); }
    const Entity *find(int id) const {
        const auto it = index_.find(id);
        return it == index_.end() ? nullptr : &entities_[it->second];
    }
    const Entity &at(int id) const {
        const Entity *e = find(id);
        if (!e) throw std::domain_error("STEP: riferimento a un'istanza inesistente #" + std::to_string(id));
        return *e;
    }
    // Istanze che hanno una parte del tipo dato.
    std::vector<int> ofType(const std::string &name) const {
        std::vector<int> ids;
        for (const auto &[id, k] : index_)
            if (entities_[k].is(name)) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        return ids;
    }

private:
    void skipSpace() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '*') {
                const std::size_t end = s_.find("*/", pos_ + 2);
                pos_ = end == std::string::npos ? s_.size() : end + 2;
            } else {
                break;
            }
        }
    }
    [[noreturn]] void fail(const std::string &what) const {
        std::size_t line = 1;
        for (std::size_t k = 0; k < pos_ && k < s_.size(); ++k) line += s_[k] == '\n';
        throw std::domain_error("STEP: " + what + " (riga " + std::to_string(line) + ")");
    }
    std::string keyword() {
        const std::size_t start = pos_;
        while (pos_ < s_.size() && (std::isalnum(static_cast<unsigned char>(s_[pos_])) || s_[pos_] == '_' || s_[pos_] == '-')) ++pos_;
        std::string word = s_.substr(start, pos_ - start);
        for (char &c : word) c = char(std::toupper(static_cast<unsigned char>(c)));
        return word;
    }
    static void appendUtf8(std::string &out, unsigned code) {
        if (code < 0x80) out += char(code);
        else if (code < 0x800) out += char(0xC0 | (code >> 6)), out += char(0x80 | (code & 0x3F));
        else if (code < 0x10000) out += char(0xE0 | (code >> 12)), out += char(0x80 | ((code >> 6) & 0x3F)), out += char(0x80 | (code & 0x3F));
        else out += char(0xF0 | (code >> 18)), out += char(0x80 | ((code >> 12) & 0x3F)), out += char(0x80 | ((code >> 6) & 0x3F)), out += char(0x80 | (code & 0x3F));
    }
    std::string string() {
        ++pos_;  // apice
        std::string raw;
        while (true) {
            if (pos_ >= s_.size()) fail("stringa non chiusa");
            const char c = s_[pos_++];
            if (c == '\'') {
                if (pos_ < s_.size() && s_[pos_] == '\'') {
                    raw += '\'';
                    ++pos_;
                    continue;
                }
                break;
            }
            raw += c;
        }
        // Codifiche: \X2\hhhh...\X0\ (UTF-16), \X4\...\X0\ (UTF-32), \X\hh (latin-1), \S\c, \\.
        std::string out;
        for (std::size_t i = 0; i < raw.size();) {
            if (raw.compare(i, 4, "\\X2\\") == 0 || raw.compare(i, 4, "\\X4\\") == 0) {
                const int width = raw[i + 2] == '2' ? 4 : 8;
                i += 4;
                std::vector<unsigned> units;
                while (i + std::size_t(width) <= raw.size() && raw.compare(i, 4, "\\X0\\") != 0) {
                    units.push_back(unsigned(std::stoul(raw.substr(i, std::size_t(width)), nullptr, 16)));
                    i += std::size_t(width);
                }
                i += 4;
                for (std::size_t k = 0; k < units.size(); ++k) {
                    unsigned code = units[k];
                    if (width == 4 && code >= 0xD800 && code < 0xDC00 && k + 1 < units.size()) code = 0x10000 + ((code - 0xD800) << 10) + (units[++k] - 0xDC00);
                    appendUtf8(out, code);
                }
            } else if (raw.compare(i, 3, "\\X\\") == 0 && i + 5 <= raw.size()) {
                appendUtf8(out, unsigned(std::stoul(raw.substr(i + 3, 2), nullptr, 16)));
                i += 5;
            } else if (raw.compare(i, 3, "\\S\\") == 0 && i + 4 <= raw.size()) {
                appendUtf8(out, unsigned(static_cast<unsigned char>(raw[i + 3])) + 128);
                i += 4;
            } else if (raw.compare(i, 2, "\\\\") == 0) {
                out += '\\';
                i += 2;
            } else {
                out += raw[i++];
            }
        }
        return out;
    }
    Param value() {
        skipSpace();
        if (pos_ >= s_.size()) fail("fine inattesa");
        Param p;
        const char c = s_[pos_];
        if (c == '$') {
            ++pos_;
        } else if (c == '*') {
            ++pos_;
            p.kind = Param::Derived;
        } else if (c == '#') {
            ++pos_;
            p.kind = Param::Ref;
            p.ref = int(std::strtol(s_.c_str() + pos_, nullptr, 10));
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        } else if (c == '\'') {
            p.kind = Param::String;
            p.text = string();
        } else if (c == '.') {
            ++pos_;
            p.kind = Param::Enum;
            const std::size_t end = s_.find('.', pos_);
            if (end == std::string::npos) fail("enumerazione non chiusa");
            p.text = s_.substr(pos_, end - pos_);
            pos_ = end + 1;
        } else if (c == '"') {
            ++pos_;
            p.kind = Param::String;
            const std::size_t end = s_.find('"', pos_);
            if (end == std::string::npos) fail("binario non chiuso");
            p.text = s_.substr(pos_, end - pos_);
            pos_ = end + 1;
        } else if (c == '(') {
            p.kind = Param::List;
            p.items = list();
        } else if (c == '-' || c == '+' || std::isdigit(static_cast<unsigned char>(c))) {
            p.kind = Param::Number;
            const std::size_t used = detail::parseReal(s_.data() + pos_, s_.data() + s_.size(), p.number);
            if (used == 0) fail("numero non valido");
            pos_ += used;
        } else if (std::isalpha(static_cast<unsigned char>(c))) {
            p.kind = Param::Typed;
            p.text = keyword();
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '(') fail("parametro tipizzato senza argomento");
            p.items = list();
        } else {
            fail(std::string("carattere inatteso '") + c + "'");
        }
        return p;
    }
    std::vector<Param> list() {
        std::vector<Param> items;
        ++pos_;  // (
        skipSpace();
        if (pos_ < s_.size() && s_[pos_] == ')') {
            ++pos_;
            return items;
        }
        while (true) {
            items.push_back(value());
            skipSpace();
            if (pos_ >= s_.size()) fail("lista non chiusa");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == ')') {
                ++pos_;
                return items;
            }
            fail("separatore atteso nella lista");
        }
    }
    void parse() {
        const std::size_t data = s_.find("DATA;");
        if (s_.compare(0, 13, "ISO-10303-21;") != 0 && s_.find("ISO-10303-21;") == std::string::npos) fail("non e' un file STEP (ISO-10303-21)");
        if (data == std::string::npos) fail("sezione DATA mancante");
        pos_ = data + 5;
        while (true) {
            skipSpace();
            if (pos_ >= s_.size()) break;
            if (s_.compare(pos_, 7, "ENDSEC;") == 0) break;
            if (s_[pos_] != '#') fail("istanza attesa");
            ++pos_;
            const int id = int(std::strtol(s_.c_str() + pos_, nullptr, 10));
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != '=') fail("'=' atteso");
            ++pos_;
            skipSpace();
            Entity entity;
            if (s_[pos_] == '(') {
                ++pos_;
                while (true) {
                    skipSpace();
                    if (s_[pos_] == ')') {
                        ++pos_;
                        break;
                    }
                    std::string name = keyword();
                    skipSpace();
                    entity.parts.push_back({name, list()});
                }
            } else {
                std::string name = keyword();
                skipSpace();
                entity.parts.push_back({name, list()});
            }
            skipSpace();
            if (pos_ >= s_.size() || s_[pos_] != ';') fail("';' atteso");
            ++pos_;
            index_[id] = entities_.size();
            entities_.push_back(std::move(entity));
        }
    }

    const std::string &s_;
    std::size_t pos_ = 0;
    std::vector<Entity> entities_;
    std::map<int, std::size_t> index_;
};

double number(const Param &p) {
    if (p.kind == Param::Number) return p.number;
    if (p.kind == Param::Typed && !p.items.empty()) return number(p.items.front());
    throw std::domain_error("STEP: numero atteso");
}
int reference(const Param &p) {
    if (p.kind != Param::Ref) throw std::domain_error("STEP: riferimento atteso");
    return p.ref;
}
bool logical(const Param &p) { return p.kind == Param::Enum && (p.text == "T" || p.text == "TRUE"); }
const std::vector<Param> &items(const Param &p) {
    if (p.kind != Param::List) throw std::domain_error("STEP: lista attesa");
    return p.items;
}
std::string stringOf(const Param &p) { return p.kind == Param::String ? p.text : std::string(); }

// Conversione di geometria e topologia di una rappresentazione.
class StepGeometry {
public:
    StepGeometry(const StepFile &file, double lengthScale, double angleScale) : f_(file), length_(lengthScale), angle_(angleScale) {}

    Vec3 point(int id) const {
        const Entity &e = f_.at(id);
        if (e.type() != "CARTESIAN_POINT") throw std::domain_error("STEP: punto atteso, trovato " + e.type());
        const std::vector<Param> &c = items(e.args().at(1));
        return Vec3(number(c.at(0)) * length_, c.size() > 1 ? number(c[1]) * length_ : 0.0, c.size() > 2 ? number(c[2]) * length_ : 0.0);
    }
    Vec3 direction(int id) const {
        const Entity &e = f_.at(id);
        const std::vector<Param> &c = items(e.args().at(1));
        const Vec3 d(number(c.at(0)), c.size() > 1 ? number(c[1]) : 0.0, c.size() > 2 ? number(c[2]) : 0.0);
        if (!(norm(d) > 0.0)) throw std::domain_error("STEP: direzione nulla");
        return normalized(d);
    }
    Vec3 vector(int id, double *magnitude = nullptr) const {
        const Entity &e = f_.at(id);
        if (e.type() == "DIRECTION") return direction(id);
        const Vec3 d = direction(reference(e.args().at(1)));
        if (magnitude) *magnitude = number(e.args().at(2)) * length_;
        return d;
    }
    Frame3 placement(int id) const {
        const Entity &e = f_.at(id);
        const Vec3 origin = point(reference(e.args().at(1)));
        if (e.type() == "AXIS2_PLACEMENT_2D") return Frame3(origin, Vec3(0, 0, 1), Vec3(1, 0, 0));
        const Vec3 z = e.args().size() > 2 && e.args()[2].kind == Param::Ref ? direction(e.args()[2].ref) : Vec3(0, 0, 1);
        Vec3 x = e.args().size() > 3 && e.args()[3].kind == Param::Ref ? direction(e.args()[3].ref) : Vec3(1, 0, 0);
        if (norm(cross(z, x)) < 1e-12) x = std::fabs(z.x()) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        return Frame3(origin, z, x);
    }

    // Curva 3D (le curve di superficie: la curva 3D).
    CurvePtr<3> curve(int id) const {
        const auto found = curves_.find(id);
        if (found != curves_.end()) return found->second;
        CurvePtr<3> c = makeCurve(id);
        curves_[id] = c;
        return c;
    }
    SurfacePtr surface(int id, bool &flipped) const {
        const auto found = surfaces_.find(id);
        if (found != surfaces_.end()) {
            flipped = found->second.second;
            return found->second.first;
        }
        flipped = false;
        SurfacePtr s = makeSurface(id, flipped);
        surfaces_[id] = {s, flipped};
        return s;
    }

    double length() const { return length_; }

private:
    std::shared_ptr<BSplineCurve<3>> bsplineCurve(const Entity &e) const {
        const std::vector<Param> *curve = e.part("B_SPLINE_CURVE");
        const std::vector<Param> *knots = e.part("B_SPLINE_CURVE_WITH_KNOTS");
        const std::vector<Param> *rational = e.part("RATIONAL_B_SPLINE_CURVE");
        // Istanza semplice: (nome, grado, poli, forma, chiusa, autointersezione, molteplicita', nodi, tipo).
        int degree = 0;
        const std::vector<Param> *polesList = nullptr;
        std::vector<double> distinct;
        std::vector<int> mults;
        if (!curve) {
            const std::vector<Param> &a = e.args();
            degree = int(number(a.at(1)));
            polesList = &items(a.at(2));
            if (e.type() == "B_SPLINE_CURVE_WITH_KNOTS") {
                for (const Param &m : items(a.at(6))) mults.push_back(int(number(m)));
                for (const Param &k : items(a.at(7))) distinct.push_back(number(k));
            }
        } else {
            degree = int(number(curve->at(0)));
            polesList = &items(curve->at(1));
            if (knots) {
                for (const Param &m : items(knots->at(0))) mults.push_back(int(number(m)));
                for (const Param &k : items(knots->at(1))) distinct.push_back(number(k));
            }
        }
        std::vector<Vec3> poles;
        for (const Param &p : *polesList) poles.push_back(point(reference(p)));
        const int n = int(poles.size());
        if (distinct.empty()) {
            // Bezier, uniforme o quasi uniforme: nodi impliciti.
            const bool bezier = e.is("BEZIER_CURVE"), uniform = e.is("UNIFORM_CURVE");
            if (bezier || n == degree + 1) {
                distinct = {0.0, 1.0};
                mults = {degree + 1, degree + 1};
            } else if (uniform) {
                for (int k = 0; k < n + degree + 1; ++k) distinct.push_back(double(k - degree)), mults.push_back(1);
            } else {
                const int inner = n - degree - 1;
                distinct.push_back(0.0);
                mults.push_back(degree + 1);
                for (int k = 1; k <= inner; ++k) distinct.push_back(double(k)), mults.push_back(1);
                distinct.push_back(double(inner + 1));
                mults.push_back(degree + 1);
            }
        }
        std::vector<double> weights;
        if (rational)
            for (const Param &w : items(rational->at(0))) weights.push_back(number(w));
        return std::make_shared<BSplineCurve<3>>(degree, expandKnots(distinct, mults), std::move(poles), std::move(weights));
    }

    CurvePtr<3> makeCurve(int id) const {
        const Entity &e = f_.at(id);
        const std::string &t = e.type();
        const std::vector<Param> &a = e.args();
        if (e.parts.size() > 1 || t.rfind("B_SPLINE_CURVE", 0) == 0 || t == "BEZIER_CURVE" || t == "UNIFORM_CURVE" || t == "QUASI_UNIFORM_CURVE") {
            if (e.is("B_SPLINE_CURVE") || t.rfind("B_SPLINE_CURVE", 0) == 0 || t == "BEZIER_CURVE" || t == "UNIFORM_CURVE" || t == "QUASI_UNIFORM_CURVE")
                return bsplineCurve(e);
        }
        if (t == "LINE") return std::make_shared<Line<3>>(point(reference(a.at(1))), vector(reference(a.at(2))));
        if (t == "CIRCLE") {
            const Frame3 f = placement(reference(a.at(1)));
            return std::make_shared<Circle<3>>(f.origin(), f.xDir(), f.yDir(), number(a.at(2)) * length_);
        }
        if (t == "ELLIPSE") {
            const Frame3 f = placement(reference(a.at(1)));
            return std::make_shared<Ellipse<3>>(f.origin(), f.xDir(), f.yDir(), number(a.at(2)) * length_, number(a.at(3)) * length_);
        }
        if (t == "TRIMMED_CURVE") return curve(reference(a.at(1)));
        if (t == "SURFACE_CURVE" || t == "SEAM_CURVE" || t == "BOUNDED_SURFACE_CURVE" || t == "INTERSECTION_CURVE") return curve(reference(a.at(1)));
        if (t == "POLYLINE") {
            std::vector<Vec3> poles;
            for (const Param &p : items(a.at(1))) poles.push_back(point(reference(p)));
            std::vector<double> knots{0.0};
            for (std::size_t k = 0; k < poles.size(); ++k) knots.push_back(double(k));
            knots.push_back(double(poles.size() - 1));
            return std::make_shared<BSplineCurve<3>>(1, knots, std::move(poles));
        }
        throw std::domain_error("STEP: curva non gestita (" + t + ")");
    }

    std::shared_ptr<BSplineSurface> bsplineSurface(const Entity &e) const {
        const std::vector<Param> *surface = e.part("B_SPLINE_SURFACE");
        const std::vector<Param> *knots = e.part("B_SPLINE_SURFACE_WITH_KNOTS");
        const std::vector<Param> *rational = e.part("RATIONAL_B_SPLINE_SURFACE");
        int ud = 0, vd = 0;
        const std::vector<Param> *rows = nullptr;
        std::vector<double> uk, vk;
        std::vector<int> um, vm;
        if (!surface) {
            const std::vector<Param> &a = e.args();
            ud = int(number(a.at(1)));
            vd = int(number(a.at(2)));
            rows = &items(a.at(3));
            if (e.type() == "B_SPLINE_SURFACE_WITH_KNOTS") {
                for (const Param &m : items(a.at(8))) um.push_back(int(number(m)));
                for (const Param &m : items(a.at(9))) vm.push_back(int(number(m)));
                for (const Param &k : items(a.at(10))) uk.push_back(number(k));
                for (const Param &k : items(a.at(11))) vk.push_back(number(k));
            }
        } else {
            ud = int(number(surface->at(0)));
            vd = int(number(surface->at(1)));
            rows = &items(surface->at(2));
            if (knots) {
                for (const Param &m : items(knots->at(0))) um.push_back(int(number(m)));
                for (const Param &m : items(knots->at(1))) vm.push_back(int(number(m)));
                for (const Param &k : items(knots->at(2))) uk.push_back(number(k));
                for (const Param &k : items(knots->at(3))) vk.push_back(number(k));
            }
        }
        const int nu = int(rows->size()), nv = nu ? int(items(rows->front()).size()) : 0;
        std::vector<Vec3> poles;
        for (const Param &row : *rows)
            for (const Param &p : items(row)) poles.push_back(point(reference(p)));
        const auto implicitKnots = [](int n, int degree, std::vector<double> &distinct, std::vector<int> &mults) {
            const int inner = n - degree - 1;
            distinct = {0.0};
            mults = {degree + 1};
            for (int k = 1; k <= inner; ++k) distinct.push_back(double(k)), mults.push_back(1);
            distinct.push_back(double(inner + 1));
            mults.push_back(degree + 1);
        };
        if (uk.empty()) implicitKnots(nu, ud, uk, um);
        if (vk.empty()) implicitKnots(nv, vd, vk, vm);
        std::vector<double> weights;
        if (rational)
            for (const Param &row : items(rational->at(0)))
                for (const Param &w : items(row)) weights.push_back(number(w));
        return std::make_shared<BSplineSurface>(ud, vd, expandKnots(uk, um), expandKnots(vk, vm), nu, nv, std::move(poles), std::move(weights));
    }

    SurfacePtr makeSurface(int id, bool &flipped) const {
        const Entity &e = f_.at(id);
        const std::string &t = e.type();
        const std::vector<Param> &a = e.args();
        if (e.is("B_SPLINE_SURFACE") || t.rfind("B_SPLINE_SURFACE", 0) == 0 || t == "BEZIER_SURFACE" || t == "UNIFORM_SURFACE" || t == "QUASI_UNIFORM_SURFACE")
            return bsplineSurface(e);
        if (t == "PLANE") return std::make_shared<Plane>(placement(reference(a.at(1))));
        if (t == "CYLINDRICAL_SURFACE") return std::make_shared<CylindricalSurface>(placement(reference(a.at(1))), number(a.at(2)) * length_);
        if (t == "CONICAL_SURFACE")
            return std::make_shared<ConicalSurface>(placement(reference(a.at(1))), number(a.at(3)) * angle_, number(a.at(2)) * length_);
        if (t == "SPHERICAL_SURFACE") return std::make_shared<SphericalSurface>(placement(reference(a.at(1))), number(a.at(2)) * length_);
        if (t == "TOROIDAL_SURFACE" || t == "DEGENERATE_TOROIDAL_SURFACE")
            return std::make_shared<ToroidalSurface>(placement(reference(a.at(1))), number(a.at(2)) * length_, number(a.at(3)) * length_);
        if (t == "SURFACE_OF_LINEAR_EXTRUSION") {
            const CurvePtr<3> basis = curve(reference(a.at(1)));
            const Vec3 d = vector(reference(a.at(2)));
            if (basis->type() == CurveType::Line) {
                // Retta estrusa: un piano (esatto).
                const Vec3 dir = basis->derivative(0.0);
                const Vec3 n = cross(dir, d);
                if (norm(n) < 1e-12) throw std::domain_error("STEP: estrusione degenere");
                return std::make_shared<Plane>(Frame3(basis->point(0.0), normalized(n), dir));
            }
            return std::make_shared<ExtrusionSurface>(basis, d);
        }
        if (t == "SURFACE_OF_REVOLUTION") {
            const CurvePtr<3> basis = curve(reference(a.at(1)));
            const Entity &axis = f_.at(reference(a.at(2)));
            const Vec3 origin = point(reference(axis.args().at(1)));
            const Vec3 dir = axis.args().size() > 2 && axis.args()[2].kind == Param::Ref ? direction(axis.args()[2].ref) : Vec3(0, 0, 1);
            if (basis->type() == CurveType::Line) {
                // Retta parallela all'asse: cilindro; perpendicolare: piano. Altrimenti limitata (iperboloide, cono).
                const Vec3 P = basis->point(0.0), l = normalized(basis->derivative(0.0));
                const Vec3 offset = (P - origin) - dot(P - origin, dir) * dir;
                SurfacePtr canonical;
                if (norm(cross(l, dir)) < 1e-12 && norm(offset) > 1e-12) canonical = std::make_shared<CylindricalSurface>(Frame3(origin, dir, offset), norm(offset));
                else if (std::fabs(dot(l, dir)) < 1e-12) canonical = std::make_shared<Plane>(Frame3(origin + dot(P - origin, dir) * dir, dir, l));
                if (canonical) {
                    // Verso: la normale della rivoluzione (a x (C - A)) x C' contro quella nuova.
                    const Vec3 q = P + l * (norm(offset) > 1e-12 ? 0.0 : 1.0);
                    const Vec3 n = cross(cross(dir, q - origin), l);
                    const SurfaceProjection pr = projectPoint(*canonical, q);
                    flipped = dot(n, canonical->normal(pr.u, pr.v)) < 0.0;
                    return canonical;
                }
                const double L = 1e7 * length_;
                return std::make_shared<RevolutionSurface>(std::make_shared<TrimmedCurve<3>>(basis, -L, L), origin, dir);
            }
            return std::make_shared<RevolutionSurface>(basis, origin, dir);
        }
        if (t == "RECTANGULAR_TRIMMED_SURFACE" || t == "CURVE_BOUNDED_SURFACE") return surface(reference(a.at(1)), flipped);
        if (t == "OFFSET_SURFACE") {
            bool inner = false;
            const SurfacePtr base = surface(reference(a.at(1)), inner);
            const double d = number(a.at(2)) * length_ * (inner ? -1.0 : 1.0);
            flipped = inner;
            switch (base->type()) {
            case SurfaceType::Plane: {
                const Frame3 &f = static_cast<const Plane &>(*base).frame();
                return std::make_shared<Plane>(Frame3(f.origin() + d * f.zDir(), f.zDir(), f.xDir()));
            }
            case SurfaceType::Cylinder: {
                const auto &c = static_cast<const CylindricalSurface &>(*base);
                return std::make_shared<CylindricalSurface>(c.frame(), c.radius() + d);
            }
            case SurfaceType::Sphere: {
                const auto &c = static_cast<const SphericalSurface &>(*base);
                return std::make_shared<SphericalSurface>(c.frame(), c.radius() + d);
            }
            case SurfaceType::Torus: {
                const auto &c = static_cast<const ToroidalSurface &>(*base);
                return std::make_shared<ToroidalSurface>(c.frame(), c.majorRadius(), c.minorRadius() + d);
            }
            default: throw std::domain_error("STEP: superficie offset di una superficie libera (non gestita)");
            }
        }
        throw std::domain_error("STEP: superficie non gestita (" + t + ")");
    }

    const StepFile &f_;
    double length_, angle_;
    mutable std::map<int, CurvePtr<3>> curves_;
    mutable std::map<int, std::pair<SurfacePtr, bool>> surfaces_;
};

// Fattori di conversione delle unita' di un contesto di rappresentazione (lunghezze in mm, angoli in radianti).
double unitFactor(const StepFile &file, int unitId, bool angle, int depth = 0) {
    if (depth > 8) return 1.0;
    const Entity &u = file.at(unitId);
    if (const std::vector<Param> *si = u.part("SI_UNIT")) {
        const std::string prefix = si->size() > 0 && si->at(0).kind == Param::Enum ? si->at(0).text : std::string();
        static const std::map<std::string, double> prefixes = {{"EXA", 1e18}, {"PETA", 1e15}, {"TERA", 1e12}, {"GIGA", 1e9}, {"MEGA", 1e6},
                                                               {"KILO", 1e3},  {"HECTO", 1e2}, {"DECA", 1e1}, {"DECI", 1e-1}, {"CENTI", 1e-2},
                                                               {"MILLI", 1e-3}, {"MICRO", 1e-6}, {"NANO", 1e-9}, {"PICO", 1e-12}};
        const double p = prefixes.count(prefix) ? prefixes.at(prefix) : 1.0;
        return angle ? p : p * 1000.0;  // metri -> mm; radianti
    }
    if (const std::vector<Param> *conversion = u.part("CONVERSION_BASED_UNIT")) {
        const Entity &measure = file.at(reference(conversion->at(1)));
        const double value = number(measure.args().at(0));
        return value * unitFactor(file, reference(measure.args().at(1)), angle, depth + 1);
    }
    return 1.0;
}

void contextUnits(const StepFile &file, int contextId, double &length, double &angle) {
    length = 1.0;
    angle = 1.0;
    const Entity *context = file.find(contextId);
    if (!context) return;
    const std::vector<Param> *units = context->part("GLOBAL_UNIT_ASSIGNED_CONTEXT");
    if (!units) return;
    for (const Param &p : items(units->at(0))) {
        const Entity &u = file.at(reference(p));
        if (u.is("LENGTH_UNIT")) length = unitFactor(file, p.ref, false);
        else if (u.is("PLANE_ANGLE_UNIT")) angle = unitFactor(file, p.ref, true);
    }
}

// Colore di un solido dagli STYLED_ITEM (il primo trovato).
bool styledColour(const StepFile &file, int psa, double rgb[3], int depth = 0) {
    if (depth > 12) return false;
    const Entity *e = file.find(psa);
    if (!e) return false;
    if (e->type() == "COLOUR_RGB") {
        for (int k = 0; k < 3; ++k) rgb[k] = number(e->args().at(std::size_t(k + 1)));
        return true;
    }
    if (e->type() == "DRAUGHTING_PRE_DEFINED_COLOUR") {
        static const std::map<std::string, std::array<double, 3>> named = {
            {"red", {1, 0, 0}}, {"green", {0, 1, 0}}, {"blue", {0, 0, 1}}, {"yellow", {1, 1, 0}}, {"magenta", {1, 0, 1}},
            {"cyan", {0, 1, 1}}, {"black", {0, 0, 0}}, {"white", {1, 1, 1}}};
        const auto it = named.find(stringOf(e->args().at(0)));
        if (it == named.end()) return false;
        for (int k = 0; k < 3; ++k) rgb[k] = it->second[std::size_t(k)];
        return true;
    }
    for (const auto &[type, args] : e->parts)
        for (const Param &p : args) {
            if (p.kind == Param::Ref && styledColour(file, p.ref, rgb, depth + 1)) return true;
            if (p.kind == Param::List)
                for (const Param &q : p.items)
                    if (q.kind == Param::Ref && styledColour(file, q.ref, rgb, depth + 1)) return true;
        }
    return false;
}

}  // namespace

StepReadResult readStep(const std::string &content) {
    using namespace detail;
    const StepFile file(content);
    StepReadResult result;

    // Colori dei solidi.
    std::map<int, std::array<double, 3>> colours;
    for (int id : file.ofType("STYLED_ITEM")) {
        const Entity &e = file.at(id);
        const std::vector<Param> &a = *e.part("STYLED_ITEM");
        if (a.size() < 3 || a[2].kind != Param::Ref) continue;
        double rgb[3];
        for (const Param &p : items(a.at(1)))
            if (p.kind == Param::Ref && styledColour(file, p.ref, rgb)) {
                colours[a[2].ref] = {rgb[0], rgb[1], rgb[2]};
                break;
            }
    }

    // Body di un solido (o di una shell) di una rappresentazione.
    const auto buildShells = [&](const StepGeometry &geometry, const std::vector<std::pair<int, bool>> &shells, bool solid) {
        RawModel model;
        std::map<int, int> vertexIndex, edgeIndex;
        const auto vertexOf = [&](int id) {
            const auto it = vertexIndex.find(id);
            if (it != vertexIndex.end()) return it->second;
            const Entity &v = file.at(id);
            model.points.push_back(geometry.point(reference(v.args().at(1))));
            return vertexIndex[id] = int(model.points.size()) - 1;
        };
        // Edge: (indice, verso della curva rispetto all'edge STEP).
        std::map<int, bool> sameSenseOf;
        const auto edgeOf = [&](int id) {
            const auto it = edgeIndex.find(id);
            if (it != edgeIndex.end()) return it->second;
            const Entity &e = file.at(id);
            if (e.type() != "EDGE_CURVE") throw std::domain_error("STEP: edge non gestito (" + e.type() + ")");
            const std::vector<Param> &a = e.args();
            RawEdge edge;
            const int start = vertexOf(reference(a.at(1))), end = vertexOf(reference(a.at(2)));
            edge.curve = geometry.curve(reference(a.at(3)));
            const bool same = a.size() < 5 || logical(a.at(4));
            // Nel kernel la curva va dall'inizio alla fine dell'edge: con same_sense falso gli estremi si scambiano.
            edge.start = same ? start : end;
            edge.end = same ? end : start;
            sameSenseOf[id] = same;
            model.edges.push_back(edge);
            return edgeIndex[id] = int(model.edges.size()) - 1;
        };
        for (const auto &[shellId, shellSense] : shells) {
            const Entity &shell = file.at(shellId);
            for (const Param &faceRef : items(shell.args().at(1))) {
                int faceId = reference(faceRef);
                bool faceSense = shellSense;
                const Entity *faceEntity = &file.at(faceId);
                if (faceEntity->type() == "ORIENTED_FACE") {
                    faceSense = faceSense == logical(faceEntity->args().at(3));
                    faceId = reference(faceEntity->args().at(2));
                    faceEntity = &file.at(faceId);
                }
                const std::vector<Param> &a = faceEntity->args();
                RawFace face;
                bool flipped = false;
                if (faceEntity->type() == "FACE_SURFACE" || faceEntity->type() == "ADVANCED_FACE") {
                    face.surface = geometry.surface(reference(a.at(2)), flipped);
                    face.sense = (logical(a.at(3)) != flipped) == faceSense;
                } else {
                    throw std::domain_error("STEP: faccia non gestita (" + faceEntity->type() + ")");
                }
                for (const Param &boundRef : items(a.at(1))) {
                    const Entity &bound = file.at(reference(boundRef));
                    const bool boundSense = logical(bound.args().at(2));
                    const Entity &loop = file.at(reference(bound.args().at(1)));
                    if (loop.type() == "VERTEX_LOOP") continue;  // polo o faccia intera
                    std::vector<RawFin> fins;
                    if (loop.type() == "EDGE_LOOP") {
                        for (const Param &orientedRef : items(loop.args().at(1))) {
                            int edgeId = reference(orientedRef);
                            bool sense = true;
                            // ORIENTED_EDGE, anche annidati.
                            while (file.at(edgeId).type() == "ORIENTED_EDGE") {
                                const Entity &oriented = file.at(edgeId);
                                sense = sense == logical(oriented.args().at(4));
                                edgeId = reference(oriented.args().at(3));
                            }
                            const int index = edgeOf(edgeId);
                            fins.push_back({index, sense == sameSenseOf.at(edgeId)});
                        }
                    } else if (loop.type() == "POLY_LOOP") {
                        // Poligono (FACETED_BREP): edge rettilinei tra punti consecutivi, condivisi per coppia di punti.
                        std::vector<int> ids;
                        for (const Param &p : items(loop.args().at(1))) ids.push_back(reference(p));
                        for (std::size_t k = 0; k < ids.size(); ++k) {
                            const int pa = ids[k], pb = ids[(k + 1) % ids.size()];
                            const auto key = std::make_pair(std::min(pa, pb), std::max(pa, pb));
                            const int keyId = -(key.first * 100003 + key.second) - 1;
                            int index;
                            const auto found = edgeIndex.find(keyId);
                            if (found != edgeIndex.end()) {
                                index = found->second;
                            } else {
                                const auto pointIndex = [&](int pointId) {
                                    const int vid = -pointId - 1;
                                    const auto it = vertexIndex.find(vid);
                                    if (it != vertexIndex.end()) return it->second;
                                    model.points.push_back(geometry.point(pointId));
                                    return vertexIndex[vid] = int(model.points.size()) - 1;
                                };
                                RawEdge edge;
                                edge.start = pointIndex(key.first);
                                edge.end = pointIndex(key.second);
                                const Vec3 p0 = model.points[std::size_t(edge.start)], p1 = model.points[std::size_t(edge.end)];
                                edge.curve = std::make_shared<Line<3>>(p0, normalized(p1 - p0));
                                edge.hasRange = true;
                                edge.range = {0.0, distance(p0, p1)};
                                model.edges.push_back(edge);
                                index = edgeIndex[keyId] = int(model.edges.size()) - 1;
                            }
                            fins.push_back({index, pa == key.first});
                        }
                    } else {
                        throw std::domain_error("STEP: loop non gestito (" + loop.type() + ")");
                    }
                    if (!boundSense) {
                        std::reverse(fins.begin(), fins.end());
                        for (RawFin &fin : fins) fin.sense = !fin.sense;
                    }
                    if (!faceSense) {
                        std::reverse(fins.begin(), fins.end());
                        for (RawFin &fin : fins) fin.sense = !fin.sense;
                    }
                    face.loops.push_back(std::move(fins));
                }
                model.faces.push_back(std::move(face));
            }
        }
        return assembleBody(model, solid, &result.notes);
    };

    // Solidi e superfici di una rappresentazione (con le rappresentazioni collegate e gli oggetti mappati).
    struct Placed {
        int item;
        Transform3 transform;
        int context;
    };
    std::function<void(int, const Transform3 &, std::vector<Placed> &, int)> collect = [&](int rep, const Transform3 &transform, std::vector<Placed> &out, int depth) {
        if (depth > 32) return;
        const Entity &r = file.at(rep);
        const std::vector<Param> *args = r.part(r.type());
        if (!args || args->size() < 3) return;
        const int context = args->at(2).kind == Param::Ref ? args->at(2).ref : 0;
        for (const Param &itemRef : items(args->at(1))) {
            if (itemRef.kind != Param::Ref) continue;
            const Entity &item = file.at(itemRef.ref);
            const std::string &t = item.type();
            if (t == "MANIFOLD_SOLID_BREP" || t == "BREP_WITH_VOIDS" || t == "FACETED_BREP" || t == "SHELL_BASED_SURFACE_MODEL"
                || t == "FACETED_BREP_AND_BREP_WITH_VOIDS") {
                out.push_back({itemRef.ref, transform, context});
            } else if (t == "MAPPED_ITEM") {
                const Entity &map = file.at(reference(item.args().at(1)));
                double length = 1.0, angle = 1.0;
                contextUnits(file, context, length, angle);
                const StepGeometry g(file, length, angle);
                const Frame3 origin = g.placement(reference(map.args().at(0)));
                const Frame3 target = g.placement(reference(item.args().at(2)));
                collect(reference(map.args().at(1)), transform * Transform3::fromFrame(target) * Transform3::fromFrame(origin).inverted(), out, depth + 1);
            }
        }
        // Rappresentazioni collegate senza trasformazione (la geometria sta spesso in un'altra rappresentazione).
        for (int id : file.ofType("SHAPE_REPRESENTATION_RELATIONSHIP")) {
            const Entity &rel = file.at(id);
            if (rel.is("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION")) continue;
            const std::vector<Param> &a = rel.part("REPRESENTATION_RELATIONSHIP") ? *rel.part("REPRESENTATION_RELATIONSHIP") : rel.args();
            if (a.size() < 4 || a[2].kind != Param::Ref || a[3].kind != Param::Ref) continue;
            if (a[2].ref == rep && a[3].ref != rep) collect(a[3].ref, transform, out, depth + 1);
            else if (a[3].ref == rep && a[2].ref != rep && file.at(a[2].ref).type() != "SHAPE_REPRESENTATION") collect(a[2].ref, transform, out, depth + 1);
        }
    };

    // Prodotti: nome di una definizione di prodotto.
    const auto productName = [&](int definition) {
        const Entity &pd = file.at(definition);
        const Entity &formation = file.at(reference(pd.args().at(2)));
        const Entity &product = file.at(reference(formation.args().at(2)));
        std::string name = stringOf(product.args().at(1));
        if (name.empty()) name = stringOf(product.args().at(0));
        return name;
    };
    // Rappresentazioni di forma di ogni definizione di prodotto.
    std::map<int, std::vector<int>> repsOf;  // definizione -> rappresentazioni
    std::map<int, int> definitionOfShape;    // PRODUCT_DEFINITION_SHAPE -> definizione (o NAUO)
    for (int id : file.ofType("PRODUCT_DEFINITION_SHAPE")) {
        const Entity &e = file.at(id);
        if (e.args().size() > 2 && e.args()[2].kind == Param::Ref) definitionOfShape[id] = e.args()[2].ref;
    }
    for (int id : file.ofType("SHAPE_DEFINITION_REPRESENTATION")) {
        const Entity &e = file.at(id);
        const auto it = definitionOfShape.find(reference(e.args().at(0)));
        if (it == definitionOfShape.end()) continue;
        repsOf[it->second].push_back(reference(e.args().at(1)));
    }
    // Assiemi: occorrenze (padre -> figli) con la trasformazione.
    struct Occurrence {
        int child;
        std::string name;
        Transform3 transform;
    };
    std::map<int, std::vector<Occurrence>> children;
    std::set<int> used;
    std::map<int, Transform3> occurrenceTransform;
    for (int id : file.ofType("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION")) {
        const Entity &e = file.at(id);
        const Entity &relation = file.at(reference(e.args().at(0)));
        const auto shape = definitionOfShape.find(reference(e.args().at(1)));
        if (shape == definitionOfShape.end()) continue;
        const std::vector<Param> *with = relation.part("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION");
        const std::vector<Param> *base = relation.part("REPRESENTATION_RELATIONSHIP");
        if (!with || !base) continue;
        const Entity &transformation = file.at(reference(with->at(0)));
        if (transformation.type() != "ITEM_DEFINED_TRANSFORMATION") continue;
        // Le posizioni sono nelle unita' dei contesti delle due rappresentazioni.
        const int rep1 = reference(base->at(2)), rep2 = reference(base->at(3));
        const auto contextOf = [&](int rep) {
            const Entity &r = file.at(rep);
            const std::vector<Param> *args = r.part(r.type());
            return args && args->size() > 2 && args->at(2).kind == Param::Ref ? args->at(2).ref : 0;
        };
        double l1, a1, l2, a2;
        contextUnits(file, contextOf(rep1), l1, a1);
        contextUnits(file, contextOf(rep2), l2, a2);
        const Frame3 from = StepGeometry(file, l1, a1).placement(reference(transformation.args().at(2)));
        const Frame3 to = StepGeometry(file, l2, a2).placement(reference(transformation.args().at(3)));
        occurrenceTransform[shape->second] = Transform3::fromFrame(to) * Transform3::fromFrame(from).inverted();
    }
    for (int id : file.ofType("NEXT_ASSEMBLY_USAGE_OCCURRENCE")) {
        const Entity &e = file.at(id);
        const int parent = reference(e.args().at(3)), child = reference(e.args().at(4));
        const auto t = occurrenceTransform.find(id);
        children[parent].push_back({child, stringOf(e.args().at(1)), t == occurrenceTransform.end() ? Transform3() : t->second});
        used.insert(child);
    }
    // Istanziazione dalle radici.
    std::vector<std::pair<std::string, Placed>> placed;
    std::function<void(int, const Transform3 &, int)> instantiate = [&](int definition, const Transform3 &transform, int depth) {
        if (depth > 32) return;
        std::vector<Placed> items;
        for (int rep : repsOf[definition]) collect(rep, transform, items, 0);
        const std::string name = productName(definition);
        for (const Placed &p : items) placed.push_back({name, p});
        for (const Occurrence &o : children[definition]) instantiate(o.child, transform * o.transform, depth + 1);
    };
    const std::vector<int> definitions = file.ofType("PRODUCT_DEFINITION");
    for (int d : definitions)
        if (!used.count(d)) instantiate(d, Transform3(), 0);
    // Senza prodotti: tutti i solidi del file.
    if (placed.empty())
        for (const char *type : {"MANIFOLD_SOLID_BREP", "BREP_WITH_VOIDS", "FACETED_BREP", "SHELL_BASED_SURFACE_MODEL"})
            for (int id : file.ofType(type)) placed.push_back({std::string(), {id, Transform3(), 0}});
    // Doppioni (stessa istanza trovata per due strade con la stessa trasformazione).
    std::set<std::pair<int, std::string>> seen;

    std::map<std::string, int> nameCount;
    for (const auto &[name, p] : placed) ++nameCount[name];
    std::map<std::string, int> nameIndex;
    for (const auto &[productNameValue, p] : placed) {
        std::string key;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) key += std::to_string(p.transform.matrix(r, c)) + ",";
        key += std::to_string(p.transform.translationPart().x()) + std::to_string(p.transform.translationPart().y()) + std::to_string(p.transform.translationPart().z());
        if (!seen.insert({p.item, key}).second) continue;
        double length = 1.0, angle = 1.0;
        contextUnits(file, p.context, length, angle);
        if (p.context == 0 || !file.find(p.context)) {
            // Contesto mancante: quello della prima rappresentazione con le unita'.
            for (int id : file.ofType("GLOBAL_UNIT_ASSIGNED_CONTEXT")) {
                contextUnits(file, id, length, angle);
                break;
            }
        }
        const StepGeometry geometry(file, length, angle);
        const Entity &item = file.at(p.item);
        std::string name = !productNameValue.empty() ? productNameValue : stringOf(item.args().at(0));
        if (name.empty()) name = "solido";
        if (nameCount[productNameValue] > 1) name += " (" + std::to_string(++nameIndex[productNameValue]) + ")";
        try {
            std::vector<std::pair<int, bool>> shells;
            bool solid = true;
            const std::string &t = item.type();
            if (t == "MANIFOLD_SOLID_BREP" || t == "FACETED_BREP") {
                shells.push_back({reference(item.args().at(1)), true});
            } else if (t == "BREP_WITH_VOIDS" || t == "FACETED_BREP_AND_BREP_WITH_VOIDS") {
                shells.push_back({reference(item.args().at(1)), true});
                for (const Param &v : items(item.args().at(2))) {
                    const Entity &oriented = file.at(reference(v));
                    if (oriented.type() == "ORIENTED_CLOSED_SHELL") shells.push_back({reference(oriented.args().at(2)), logical(oriented.args().at(3))});
                    else shells.push_back({v.ref, false});
                }
            } else {
                for (const Param &s : items(item.args().at(1))) {
                    const Entity &shell = file.at(reference(s));
                    if (shell.type() == "ORIENTED_CLOSED_SHELL" || shell.type() == "ORIENTED_OPEN_SHELL")
                        shells.push_back({reference(shell.args().at(2)), logical(shell.args().at(3))});
                    else shells.push_back({s.ref, true});
                    if (shell.type() != "CLOSED_SHELL") solid = false;
                }
            }
            Body body = buildShells(geometry, shells, solid);
            if (!p.transform.isSimilarity() || std::fabs(p.transform.matrix(0, 0) - 1.0) > 0.0 || std::fabs(p.transform.matrix(1, 1) - 1.0) > 0.0
                || std::fabs(p.transform.matrix(2, 2) - 1.0) > 0.0 || norm(p.transform.translationPart()) > 0.0)
                body = transformBody(body, p.transform);
            ExchangeBody exchange;
            exchange.name = name;
            exchange.body = std::move(body);
            const auto colour = colours.find(p.item);
            if (colour != colours.end()) {
                exchange.hasColor = true;
                for (int k = 0; k < 3; ++k) exchange.color[k] = colour->second[std::size_t(k)];
            }
            result.bodies.push_back(std::move(exchange));
        } catch (const std::exception &failure) {
            result.notes.push_back(name + ": non ricostruito (" + failure.what() + ")");
        }
    }
    return result;
}

}
