#include "fk_history_tools.h"
#include "fk_shell.h"

#include "fk_body_check.h"
#include "fk_bspline_surface.h"
#include "fk_nurbs.h"
#include "fk_sheet.h"
#include "fk_classify.h"
#include "fk_mass.h"
#include "fk_primitives.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

#include "fk_boolean.h"
#include "fk_offset.h"
#include "fk_precision.h"
#include "fk_sew.h"

namespace ForgeCad::Kernel {

namespace {

// Certificato sul dominio UV che contiene tutti i contorni. Un box globale
// del supporto puo' comprendere pezzi estranei alla faccia rifilata.
bool splineAbovePlane(const Body &body, FaceId id, const Vec3 &origin, const Vec3 &normal, double budget) {
    const auto &face = body.face(id);
    const auto &source = static_cast<const BSplineSurface &>(*face.surface);
    Interval u = source.uDomain(), v = source.vDomain();
    const auto include = [&](const Vec2 &uv) {
        u.lo = std::min(u.lo, uv.x()); u.hi = std::max(u.hi, uv.x());
        v.lo = std::min(v.lo, uv.y()); v.hi = std::max(v.hi, uv.y());
    };
    for (LoopId l : face.loops) for (FinId f : body.loopFins(l)) {
        const auto &fin = body.fin(f);
        if (!fin.pcurve) return false;
        const Curve<2> *curve = fin.pcurve.get();
        while (curve->type() == CurveType::Trimmed) curve = static_cast<const TrimmedCurve<2> *>(curve)->basis().get();
        if (curve->type() == CurveType::BSpline) {
            for (const Vec2 &pole : static_cast<const BSplineCurve<2> &>(*curve).poles()) include(pole);
        } else if (curve->type() == CurveType::Line) {
            const auto range = body.edge(fin.edge).range;
            include(curve->point(range.lo)); include(curve->point(range.hi));
        } else return false;
    }
    // Anche piccoli prolungamenti delle SP-curve sono coperti dal certificato.
    const auto surface = extendBSplineSurface(source, u, v);
    bool above = true;
    bool positive[2] = {true, true}, negative[2] = {true, true};
    for (int i = 0; i < surface.uPoleCount(); ++i) for (int j = 0; j < surface.vPoleCount(); ++j) {
        above = above && dot(surface.pole(i, j) - origin, normal) >= -budget;
        for (int direction = 0; direction < 2; ++direction) {
            const int a = i + (direction == 0), b = j + (direction == 1);
            if (a >= surface.uPoleCount() || b >= surface.vPoleCount()) continue;
            const double delta = dot(surface.pole(a, b) - surface.pole(i, j), normal);
            positive[direction] = positive[direction] && delta >= 0.0;
            negative[direction] = negative[direction] && delta <= 0.0;
        }
    }
    if (above) return true;
    // Per un polinomio, quota monotona in una direzione: il minimo sulla
    // faccia cade sul contorno, verificato separatamente dal chiamante.
    return !surface.isRational() && (positive[0] || negative[0] || positive[1] || negative[1]);
}

// Un piano di appoggio delimita tutto il corpo da una parte. Un piano
// interno di un pezzo concavo non deve tagliare altre parti della cavita'.
bool supportingPlane(const Body &body, FaceId id) {
    const auto &face = body.face(id);
    if (face.surface->type() != SurfaceType::Plane || face.loops.size() != 1) return false;
    const auto &plane = static_cast<const Plane &>(*face.surface);
    const Vec3 origin = plane.frame().origin();
    const Vec3 inward = plane.frame().zDir() * (face.sense ? -1.0 : 1.0);
    double budget = kLinearResolution;
    for (LoopId l : face.loops) for (FinId f : body.loopFins(l))
        budget = std::max(budget, body.edge(body.fin(f).edge).tolerance);
    const auto above = [&](const Box &bounds) {
        for (int k = 0; k < 8; ++k) {
            const Vec3 corner(k & 1 ? bounds.hi.x() : bounds.lo.x(), k & 2 ? bounds.hi.y() : bounds.lo.y(),
                              k & 4 ? bounds.hi.z() : bounds.lo.z());
            if (dot(corner - origin, inward) < -budget) return false;
        }
        return true;
    };
    for (FaceId f : body.faces()) {
        if (above(faceBox(body, f))) continue;
        const auto type = body.face(f).surface->type();
        if (type == SurfaceType::BSpline) {
            if (!splineAbovePlane(body, f, origin, inward, budget)) return false;
        } else if (type != SurfaceType::Plane && type != SurfaceType::Extrusion && type != SurfaceType::Cylinder) return false;
        // Piani, cilindri ed estrusioni hanno quota affine lungo ogni
        // generatrice: il minimo sulla faccia limitata e' sul suo contorno.
        // I poli Bezier danno il limite nella direzione del piano, senza
        // dipendere dall'orientamento degli assi o da soli campioni.
        for (LoopId l : body.face(f).loops) for (FinId fin : body.loopFins(l)) {
            const auto &edge = body.edge(body.fin(fin).edge);
            for (const auto &piece : rationalBezierPieces(*edge.curve, edge.range))
                for (const Vec3 &pole : piece.poles())
                    if (dot(pole - origin, inward) < -budget) return false;
        }
    }
    return true;
}

// La corrispondenza delle facce dell'offset permette di costruire le due
// pelli senza intersecare di nuovo superfici libere quasi coincidenti.
// Per aperture planari disgiunte si aggiungono i contorni interni come fori;
// annidamento, chiusura, geometria e genealogia vengono verificati qui.
std::optional<Body> nestedShell(const Body &outer, const Body &inner, const std::set<int> &openings, const BodyHistory &inside,
                                BodyHistory *history) {
    if (outer.shells().size() != 1 || inner.shells().size() != 1) return {};
    std::map<int, FaceId> caps;
    for (auto f : inner.faces()) {
        const auto &origins = inside.origins(EntityKind::Face, f.index);
        if (origins.size() != 1 || origins.front().kind != EntityKind::Face) return {};
        if (openings.count(origins.front().index)) {
            if (!caps.emplace(origins.front().index, f).second) return {};
        }
    }
    for (int index : openings) {
        if (!caps.count(index) || outer.face(FaceId{index}).surface->type() != SurfaceType::Plane ||
            outer.face(FaceId{index}).loops.size() != 1)
            return {};
        const auto f = caps.at(index);
        if (inner.face(f).loops.size() != 1 || inner.face(f).surface != outer.face(FaceId{index}).surface) return {};
        FaceClassifier classify(outer, FaceId{index}, kBooleanConnectionTolerance);
        for (auto l : inner.face(f).loops)
            for (auto fid : inner.loopFins(l)) {
                const auto &e = inner.edge(inner.fin(fid).edge);
                for (int k = 0; k <= 16; ++k)
                    if (classify.classify(e.curve->point(e.range.lo + k * e.range.length() / 16.)) != PointLocation::Inside)
                        return {};
            }
    }
    SolidClassifier classify(outer, kBooleanConnectionTolerance, true);
    for (auto e : inner.edges()) {
        const auto &edge = inner.edge(e);
        for (int k = 0; k <= 8; ++k)
            if (classify.classify(edge.curve->point(edge.range.lo + k * edge.range.length() / 8.)) == PointLocation::Outside)
                return {};
    }
    for (auto f : inner.faces()) {
        Vec3 p;
        if (!faceInteriorPoint(inner, f, p) || classify.classify(p) == PointLocation::Outside) return {};
    }
    // Qui serve un controllo di contenimento/segno, non il tensore alla
    // massima precisione. I gusci quasi degeneri usano la quadratura completa.
    double innerVolume = massProperties(inner, 1e-7).volume;
    double outerVolume = massProperties(outer, 1e-7).volume;
    if (std::fabs(outerVolume - innerVolume) < 1e-5 * std::fabs(outerVolume)) {
        innerVolume = massProperties(inner).volume;
        outerVolume = massProperties(outer).volume;
    }
    if (!std::isfinite(innerVolume) || !std::isfinite(outerVolume) || !(innerVolume > 0.0 && innerVolume < outerVolume))
        return {};
    std::vector<Vec3> points;
    std::vector<double> vertexTolerances;
    std::vector<Body::BuildEdge> edges;
    std::vector<Body::BuildFace> faces;
    std::vector<std::vector<HistoryLink>> origins;
    std::map<int, int> pointOf[2], edgeOf[2];
    const Body *sources[2] = {&outer, &inner};
    for (int b = 0; b < 2; ++b) {
        const auto &body = *sources[b];
        for (auto v : body.vertices()) {
            pointOf[b][v.index] = int(points.size());
            points.push_back(body.vertex(v).point);
            vertexTolerances.push_back(body.vertex(v).tolerance);
        }
        for (auto e : body.edges()) {
            const auto &x = body.edge(e);
            edgeOf[b][e.index] = int(edges.size());
            edges.push_back(
                {pointOf[b].at(body.edgeStart(e).index), pointOf[b].at(body.edgeEnd(e).index), x.curve, x.range, x.tolerance});
        }
    }
    const auto loopOf = [&](int b, LoopId l, bool reverse) {
        const auto &body = *sources[b];
        std::vector<Body::BuildFin> loop;
        for (auto fid : body.loopFins(l)) {
            const auto &fin = body.fin(fid);
            loop.push_back({edgeOf[b].at(fin.edge.index), fin.sense, fin.pcurve, fin.pcurveTolerance});
        }
        if (reverse) {
            std::reverse(loop.begin(), loop.end());
            for (auto &x : loop)
                x.sense = !x.sense;
        }
        return loop;
    };
    for (auto f : outer.faces()) {
        const auto &x = outer.face(f);
        Body::BuildFace face;
        face.surface = x.surface;
        face.sense = x.sense;
        for (auto l : x.loops)
            face.loops.push_back(loopOf(0, l, false));
        if (openings.count(f.index))
            for (auto l : inner.face(caps.at(f.index)).loops)
                face.loops.push_back(loopOf(1, l, true));
        faces.push_back(std::move(face));
        origins.push_back(
            {{0, EntityKind::Face, f.index, openings.count(f.index) ? HistoryRelation::Modified : HistoryRelation::Same}});
    }
    for (auto f : inner.faces()) {
        const auto &sourceOrigins = inside.origins(EntityKind::Face, f.index);
        if (openings.count(sourceOrigins.front().index)) continue;
        const auto &x = inner.face(f);
        Body::BuildFace face;
        face.surface = x.surface;
        face.sense = !x.sense;
        for (auto l : x.loops)
            face.loops.push_back(loopOf(1, l, true));
        faces.push_back(std::move(face));
        origins.push_back(sourceOrigins);
    }
    auto result = Body::build(points, edges, faces);
    for (auto v : result.vertices())
        result.vertex(v).tolerance = vertexTolerances.at(v.index);
    CheckOptions options;
    options.loopCrossings = true;
    auto issues = checkBody(result, options);
    if (!issues.empty()) return {};
    if (history) {
        *history = makeHistory({&outer}, result);
        auto fs = result.faces();
        for (std::size_t i = 0; i < fs.size(); ++i)
            history->declareOrigins(EntityKind::Face, fs[i].index, origins[i]);
        for (auto e : outer.edges())
            history->declareOrigins(EntityKind::Edge, edgeOf[0].at(e.index),
                                    {{0, EntityKind::Edge, e.index, HistoryRelation::Same}});
        for (auto v : outer.vertices())
            history->declareOrigins(EntityKind::Vertex, pointOf[0].at(v.index),
                                    {{0, EntityKind::Vertex, v.index, HistoryRelation::Same}});
    }
    return result;
}
} // namespace

Body shellBody(const Body &solid, const std::vector<FaceId> &removed, double thickness, BodyHistory *history) {
    if (history) *history = {};
    if (solid.isSheet() || solid.faces().empty()) throw std::domain_error("guscio: serve un solido");
    if (!(thickness > kLinearResolution) || !std::isfinite(thickness))
        throw std::domain_error("guscio: lo spessore deve essere positivo e finito");
    std::set<int> openings;
    for (FaceId f : removed) {
        if (!solid.contains(f)) throw std::invalid_argument("guscio: faccia dell'apertura assente");
        openings.insert(f.index);
    }
    if (openings.size() == solid.faces().size()) throw std::domain_error("guscio: non resta nessuna faccia");

    // La cavita' conserva i supporti dell'esterno. Le facce dell'apertura
    // restano alla quota originale; le altre si spostano verso l'interno.
    // I raccordi analitici il cui raggio residuo e' nullo entro la risoluzione
    // si contraggono: i supporti adiacenti si prolungano fino a intersecarsi.
    std::vector<FaceOffset> offsets;
    for (FaceId f : solid.faces()) {
        const auto &face = solid.face(f);
        const auto &surface = *face.surface;
        const bool opening = openings.count(f.index) != 0;
        double radius = 0.0;
        if (surface.type() == SurfaceType::Cylinder) radius = static_cast<const CylindricalSurface &>(surface).radius();
        else if (surface.type() == SurfaceType::Sphere) radius = static_cast<const SphericalSurface &>(surface).radius();
        else if (surface.type() == SurfaceType::Torus) radius = static_cast<const ToroidalSurface &>(surface).minorRadius();
        const bool collapse = !opening && face.sense && radius > 0.0 && radius - thickness <= kLinearResolution;
        offsets.push_back({f, opening ? 0.0 : -thickness, collapse});
    }
    bool freeform = false, tolerant = false;
    for (FaceId f : solid.faces())
        freeform = freeform || solid.face(f).surface->type() == SurfaceType::BSpline;
    for (EdgeId e : solid.edges())
        tolerant = tolerant || solid.edge(e).tolerance > kLinearResolution;
    const bool tolerantFreeform = freeform && tolerant && solid.shells().size() == 1;
    // Nel guscio chiuso il vincolo del fondo si applica dopo le giunzioni
    // delle pareti, con un taglio piano: evita una rifilatura mal condizionata
    // simultanea di tre supporti quasi tangenti.
    FaceId temporaryCap;
    if (tolerantFreeform && openings.empty())
        for (FaceId f : solid.faces())
            if (supportingPlane(solid, f)) {
                temporaryCap = f;
                break;
            }
    if (temporaryCap.valid())
        for (auto &spec : offsets)
            if (spec.face == temporaryCap) spec.distance = 0;
    const auto offset = offsetJoinedFaces(solid, offsets, kLinearResolution);
    BodyHistory sewn;
    auto inner = sewSheets({&offset.body}, kBooleanConnectionTolerance, true, &sewn);
    if (!inner.solid) throw std::domain_error("guscio: la superficie interna non si chiude");
    auto insideDirect = detail::transportHistory(sewn, {&offset.history});
    if (temporaryCap.valid()) {
        const auto &face = solid.face(temporaryCap);
        const auto &plane = static_cast<const Plane &>(*face.surface);
        const Vec3 inward = plane.frame().zDir() * (face.sense ? -1.0 : 1.0);
        const Frame3 frame(plane.frame().origin() + thickness * inward, inward, plane.frame().xDir());
        Box bounds;
        for (auto f : inner.body.faces())
            bounds.add(faceBox(inner.body, f));
        Box local;
        for (int i = 0; i < 8; ++i)
            local.add(frame.toLocal(Vec3(i & 1 ? bounds.hi.x() : bounds.lo.x(), i & 2 ? bounds.hi.y() : bounds.lo.y(),
                                         i & 4 ? bounds.hi.z() : bounds.lo.z())));
        // Gli altri cinque lati del parallelepipedo restano oltre i box
        // delle facce. Solo il piano a quota spessore puo' rifilare la cavita'.
        const double margin = std::max(1., .01 * bounds.diagonal());
        const Frame3 boxFrame(frame.toGlobal(Vec3(local.lo.x() - margin, local.lo.y() - margin, 0)), frame.zDir(), frame.xDir());
        const auto tool = makeBox(boxFrame, local.hi.x() - local.lo.x() + 2 * margin, local.hi.y() - local.lo.y() + 2 * margin,
                                  local.hi.z() + margin);
        BodyHistory cutHistory;
        // Il taglio deve rispettare la precisione dell'offset (1e-7 mm),
        // distinta dal budget predefinito di connessione delle booleane.
        BooleanOptions clipOptions;
        clipOptions.tolerance = kLinearResolution;
        auto clipped = booleanOperation(inner.body, tool, BooleanOperation::Intersect, clipOptions, &cutHistory);
        auto toolHistory =
            detail::generatedFaces(solid, tool, {{0, EntityKind::Face, temporaryCap.index, HistoryRelation::Modified}});
        insideDirect = detail::transportHistory(cutHistory, {&insideDirect, &toolHistory});
        inner.body = std::move(clipped);
    }
    if (tolerantFreeform)
        if (auto result = nestedShell(solid, inner.body, openings, insideDirect, history)) return *result;
    BodyHistory cut;
    auto result = booleanOperation(solid, inner.body, BooleanOperation::Subtract, {}, history ? &cut : nullptr);
    if (history) {
        auto original = detail::indexHistory(solid, solid, HistoryRelation::Same);
        *history = detail::transportHistory(cut, {&original, &insideDirect});
    }
    return result;
}

} // namespace ForgeCad::Kernel
